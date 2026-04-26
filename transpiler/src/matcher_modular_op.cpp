// matcher_modular_op.cpp — sturm-qzab.1 (Phase 5 beat 5.1) +
// sturm-qzab.2 (Phase 5 beat 5.2) implementation.
//
// AST matcher for the modular-arithmetic rewrite. Beat 5.1 landed the
// AddMod arm (`(qint + qint) % qint` as the initializer of a `qint_t<W>`
// VarDecl); beat 5.2 mirrors that for MulMod (`(qint * qint) % qint`).
// Beats 5.4-5.6 will reuse the same hits vector for PowMod by appending
// additional `register_one<...>` calls here.
//
// The matcher anchors on a `varDecl(hasInitializer(...))` shape. The
// initializer's outer node is a `CXXOperatorCallExpr` for the
// user-defined `qint_t<W>::operator%`; its first argument is a
// `CXXOperatorCallExpr` for `qint_t<W>::operator+` (AddMod) or
// `qint_t<W>::operator*` (MulMod). Both operator overloads are
// non-member templates in the actual `sturm/qtypes/*` headers, but
// Clang normalises both member and non-member operator overloads to
// `CXXOperatorCallExpr` so a single anchor handles both shapes.
//
// Why VarDecl, not the inner `%` expression? The PRD §2.1 rewrite
// replaces the entire `qint_t<W> r = (a OP b) % n;` declaration with
// `qint_t<W> r = ::sturm::OP_mod(a, b, n);`. Anchoring on the VarDecl
// gives the wiring layer in `transpile_consumer.cpp` the full source
// range it needs for the `QReplacement`.
//
// Hard layering rule (plan §1, PRD §4): the matcher only RECOGNISES
// the pattern. It does not synthesise emission text — that is the
// emitter's job. The matcher writes a non-owning `ModularOpHit` into
// the caller-owned vector and returns.
//
// Coexistence with existing matchers: the AST shape matched here
// (`VarDecl` whose init is `(qint OP qint) % qint`) is structurally
// disjoint from every per-op compound-assign matcher in the pool. No
// suppression bookkeeping is needed. The AddMod and MulMod arms are
// also disjoint with respect to each other — the inner-operator
// `hasOverloadedOperatorName("+")` vs `("*")` guard means a single
// site cannot fire both arms.

#include "matcher_modular_op.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Resolve `W` in `qint_t<W>` off a VarDecl's type. Mirrors the helper
// in `matcher_lossy_op.cpp::extract_qint_width_from_dre` but starting
// from the declared type of the result variable (the LHS of
// `qint_t<W> r = ...;`). Returns 0 when the type is dependent, not a
// `sturm::qint_t` instantiation, or the first template argument is
// not an integral constant — same posture as the lossy matcher.
int extract_qint_width_from_vd(const VarDecl& vd) {
    QualType qt = vd.getType().getCanonicalType();
    const auto* record = qt->getAsCXXRecordDecl();
    if (record == nullptr) return 0;
    const auto* spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(record);
    if (spec == nullptr) return 0;
    const auto* primary = spec->getSpecializedTemplate();
    if (primary == nullptr) return 0;
    if (primary->getQualifiedNameAsString() != "sturm::qint_t") return 0;
    const TemplateArgumentList& args = spec->getTemplateArgs();
    if (args.size() == 0) return 0;
    const TemplateArgument& arg0 = args.get(0);
    if (arg0.getKind() != TemplateArgument::Integral) return 0;
    const llvm::APSInt& apsint = arg0.getAsIntegral();
    const int64_t w64 = apsint.getExtValue();
    if (w64 <= 0) return 0;
    return static_cast<int>(w64);
}

// Walk up the parent chain to the nearest enclosing CompoundStmt for a
// VarDecl — varDecl is anchored as a Decl, not a Stmt, so a Decl-rooted
// walker is required for the modular hit's `enclosing_block` slot.
// Mirrors `matcher_lossy_op.cpp::nearest_compound_stmt` posture.
const CompoundStmt* nearest_compound_stmt_for_decl(const VarDecl& vd,
                                                   ASTContext& ctx) {
    DynTypedNode current = DynTypedNode::create(vd);
    for (int hops = 0; hops < 128; ++hops) {
        const auto parents = ctx.getParents(current);
        if (parents.empty()) return nullptr;
        current = parents[0];
        if (const auto* cs = current.get<CompoundStmt>()) {
            return cs;
        }
    }
    return nullptr;
}

// Shared per-hit population helper. AddMod and MulMod use identical
// node-extraction + hit-construction logic — only the kind discriminant
// differs — so factoring the body into one function keeps the per-arm
// callbacks under 30 LoC each and eliminates the risk of the two arms
// drifting (e.g. one of them forgetting to populate `enclosing_block`).
void populate_modular_hit(ModularOpKind kind,
                          const MatchFinder::MatchResult& r,
                          std::vector<ModularOpHit>* hits) {
    const auto* var   = r.Nodes.getNodeAs<VarDecl>("var");
    const auto* outer = r.Nodes.getNodeAs<CXXOperatorCallExpr>("outer");
    const auto* inner = r.Nodes.getNodeAs<CXXOperatorCallExpr>("inner");
    const auto* a     = r.Nodes.getNodeAs<DeclRefExpr>("a");
    const auto* b     = r.Nodes.getNodeAs<DeclRefExpr>("b");
    const auto* n     = r.Nodes.getNodeAs<DeclRefExpr>("n");
    if (!var || !outer || !inner || !a || !b || !n || !r.Context) return;

    // Defensive: the parent walk must succeed for the hit to be
    // useful to the consumer. Skip the hit otherwise — we never
    // push a half-populated entry.
    const CompoundStmt* block =
        nearest_compound_stmt_for_decl(*var, *r.Context);
    if (!block) return;

    ModularOpHit hit;
    hit.kind = kind;
    hit.result_name = var->getNameAsString();
    if (const NamedDecl* nd = a->getDecl()) {
        hit.a_name = nd->getNameAsString();
    }
    if (const NamedDecl* nd = b->getDecl()) {
        hit.b_name = nd->getNameAsString();
    }
    if (const NamedDecl* nd = n->getDecl()) {
        hit.n_name = nd->getNameAsString();
    }
    hit.result_width = extract_qint_width_from_vd(*var);
    hit.mod_expr = outer;
    hit.inner_op_expr = inner;
    hit.result_var = var;
    hit.enclosing_block = block;
    hits->push_back(std::move(hit));
}

// AddMod callback. Bound names follow the LHS / inner-LHS / inner-RHS
// / outer-RHS pattern: `var` is the result VarDecl, `inner` is the
// `+` expression, `outer` is the `%` expression, `a`/`b` are the
// operands of `+`, and `n` is the second operand of `%`.
class AddModCallback : public MatchFinder::MatchCallback {
public:
    explicit AddModCallback(std::vector<ModularOpHit>* hits) : hits_(hits) {}

    void run(const MatchFinder::MatchResult& r) override {
        populate_modular_hit(ModularOpKind::AddMod, r, hits_);
    }

private:
    std::vector<ModularOpHit>* hits_;
};

// MulMod callback. Mirrors AddModCallback — same bound-name shape, same
// hit-construction logic, only the kind discriminant differs.
class MulModCallback : public MatchFinder::MatchCallback {
public:
    explicit MulModCallback(std::vector<ModularOpHit>* hits) : hits_(hits) {}

    void run(const MatchFinder::MatchResult& r) override {
        populate_modular_hit(ModularOpKind::MulMod, r, hits_);
    }

private:
    std::vector<ModularOpHit>* hits_;
};

// Per-callback unique_ptr pool — outlives MatchFinder runs.
std::vector<std::unique_ptr<AddModCallback>>& add_mod_pool() {
    static std::vector<std::unique_ptr<AddModCallback>> pool;
    return pool;
}

std::vector<std::unique_ptr<MulModCallback>>& mul_mod_pool() {
    static std::vector<std::unique_ptr<MulModCallback>> pool;
    return pool;
}

// `(a OP b) % n` pattern shape:
//
// VarDecl(type=qint_t<W>) → init →
//   CXXOperatorCallExpr('%') named "outer"
//     arg0: CXXOperatorCallExpr(OP) named "inner"
//       arg0: DeclRefExpr named "a" (qint_t)
//       arg1: DeclRefExpr named "b" (qint_t)
//     arg1: DeclRefExpr named "n" (qint_t)
//
// `ignoringImplicit` peels MaterializeTemporaryExpr / ImplicitCastExpr
// layers Clang inserts around lvalue-to-rvalue conversions. The
// `ignoringParenImpCasts` peel on the inner slot handles the `(a OP b)`
// parens — the source `( ... )` becomes a ParenExpr in the AST that we
// need to look through.
//
// `hasName("qint_t")` rejects non-qint user types structurally, mirroring
// the LO-2a guard. The check applies to the result VarDecl, the inner
// operands, and the outer-`%` second operand — matching the PRD §2.1
// "all four operands are qint_t<W>" precondition.
auto qint_dre(const char* binding) {
    return declRefExpr(hasType(hasCanonicalType(hasDeclaration(
        cxxRecordDecl(hasName("qint_t"))))))
        .bind(binding);
}

// Factory for the `(a OP b) % n` AST pattern. `inner_op` is `"+"` for
// AddMod (beat 5.1) and `"*"` for MulMod (beat 5.2). The two patterns
// share every other constraint — only the inner-operator name varies.
auto binary_mod_pattern(const char* inner_op) {
    auto inner = cxxOperatorCallExpr(
        hasOverloadedOperatorName(inner_op),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(qint_dre("a"))),
        hasArgument(1, ignoringImplicit(qint_dre("b")))
    ).bind("inner");

    auto outer_mod = cxxOperatorCallExpr(
        hasOverloadedOperatorName("%"),
        argumentCountIs(2),
        hasArgument(0, ignoringParenImpCasts(inner)),
        hasArgument(1, ignoringImplicit(qint_dre("n")))
    ).bind("outer");

    return varDecl(
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint_t"))))),
        hasInitializer(ignoringImplicit(outer_mod))
    ).bind("var");
}

} // namespace

void register_modular_op_matcher(clang::ast_matchers::MatchFinder& finder,
                                 std::vector<ModularOpHit>& hits) {
    // Beat 5.1 (sturm-qzab.1): AddMod arm. Beat 5.2 (sturm-qzab.2):
    // MulMod arm. Beats 5.4-5.6 (PowMod variants) will extend this
    // body with their own register block; the hits vector is shared
    // across the arms because the enum discriminant on `ModularOpKind`
    // lets the emitter dispatch in O(1).
    {
        auto& pool = add_mod_pool();
        pool.push_back(std::make_unique<AddModCallback>(&hits));
        finder.addMatcher(binary_mod_pattern("+"), pool.back().get());
    }
    {
        auto& pool = mul_mod_pool();
        pool.push_back(std::make_unique<MulModCallback>(&hits));
        finder.addMatcher(binary_mod_pattern("*"), pool.back().get());
    }
}

} // namespace sturm::transpile
