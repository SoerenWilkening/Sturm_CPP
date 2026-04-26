// matcher_modular_compound_collapse.cpp — sturm-qzab.3 (Phase 5
// beat 5.3) compound peephole-collapsed AddMod arm of the modular-
// arithmetic matcher.
//
// Plan §7.1: when matcher_modular_op.cpp grows past the 280 LoC budget,
// split per-pattern. This sibling TU isolates the beat-5.3 arm so the
// in-initializer AddMod / MulMod arms (beats 5.1 / 5.2) keep their TU
// well under the cap. Both TUs export pattern-registration helpers into
// the same `register_modular_op_matcher` orchestrator over in
// matcher_modular_op.cpp; the shared hits vector and `ModularOpHit`
// struct mean the consumer drain in transpile_consumer.cpp does not
// need to know which TU produced any given hit.
//
// Pattern shape (split across two adjacent stmts in one CompoundStmt):
//
//   DeclStmt
//     VarDecl(type=qint_t<W>) → init →
//       CXXOperatorCallExpr('+') named "inner"
//         arg0: DeclRefExpr named "a" (qint_t)
//         arg1: DeclRefExpr named "b" (qint_t)
//   ExprStmt
//     CXXOperatorCallExpr('%=')
//       arg0: DeclRefExpr to the same VarDecl above
//       arg1: DeclRefExpr (qint_t)
//
// The matcher only anchors on the VarDecl (matchers cannot easily
// express "next sibling stmt is X" in pattern syntax); the per-pair
// validation (next-sibling adjacency, %= LHS pinning to the matched
// VarDecl, RHS qint-typed) lives in the callback. Falls back to the
// wide path (no hit emitted) when ANY sibling intervenes — even a
// no-op `(void)r;` — so the LO-2a `%=` desugar takes over verbatim.
// Walking IMMEDIATE next-sibling-only also subsumes the "intervening
// read of `r`" check the issue description / plan §7.2 #2 calls out:
// any read of `r` requires an intervening stmt, which already breaks
// adjacency.
//
// Hit-population shape mirrors the in-initializer AddMod arm except
// for two fields:
//   - `mod_expr` stays null (no outer `%` op-call exists in the
//     compound-collapsed AST shape).
//   - `mod_assign_call` is set to the `%=` op-call, telling the
//     consumer drain "this is a beat-5.3 hit; extend the QReplacement
//     range to absorb the `r %= n;` stmt and suppress the LO-2a hit
//     on this same op-call".

#include "matcher_modular_compound_collapse.hpp"

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
#include "clang/Basic/OperatorKinds.h"
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

// Resolve `W` in `qint_t<W>` off a VarDecl's type. Duplicated from
// matcher_modular_op.cpp's anonymous namespace because the original is
// TU-private; promoting it to a shared helper would expose Clang AST
// types in a public header (the canonical-type / template-argument walk
// is non-trivial). Keeping the duplicate is preferable to leaking AST
// internals into the matcher_modular_op.hpp surface.
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

// Walk up to nearest enclosing CompoundStmt for a VarDecl. Same
// posture as matcher_modular_op.cpp's helper of the same name.
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

// Walk from a VarDecl up to its DeclStmt parent, then to that
// DeclStmt's enclosing CompoundStmt; return the IMMEDIATE next sibling
// of the DeclStmt in that body, or nullptr when the VarDecl is not a
// direct child of any CompoundStmt body or is the last child.
//
// Mirrors `matcher_ccnot_fuse.cpp::next_sibling_in_compound` — same
// 8-hop bound on the parent walk (the VarDecl-to-DeclStmt distance is
// 1 in well-formed source; the bound is purely defensive against
// pathological dependent-context shapes).
const Stmt* next_sibling_of_var_decl(const VarDecl& var, ASTContext& ctx) {
    DynTypedNode current = DynTypedNode::create(var);
    for (int hops = 0; hops < 8; ++hops) {
        const auto parents = ctx.getParents(current);
        if (parents.empty()) return nullptr;
        current = parents[0];
        if (const auto* ds = current.get<DeclStmt>()) {
            const auto cs_parents = ctx.getParents(*ds);
            if (cs_parents.empty()) return nullptr;
            const auto* cs = cs_parents[0].get<CompoundStmt>();
            if (cs == nullptr) return nullptr;
            bool found = false;
            for (const Stmt* child : cs->body()) {
                if (found) return child;
                if (child == ds) found = true;
            }
            return nullptr;
        }
    }
    return nullptr;
}

// qint-typed DeclRefExpr binding helper. Same canonical-type guard
// matcher_modular_op.cpp uses for the in-initializer arms.
auto qint_dre(const char* binding) {
    return declRefExpr(hasType(hasCanonicalType(hasDeclaration(
        cxxRecordDecl(hasName("qint_t"))))))
        .bind(binding);
}

// Build the per-arm pattern. The VarDecl's init is `+` op-call on two
// qint DREs; we explicitly forbid the in-initializer `(a+b) % n` shape
// from absorbing this VarDecl by anchoring on the `+` directly (the
// in-init AddMod arm anchors on the OUTER `%`, so a VarDecl whose
// init is the `+` op-call alone falls through to this arm only).
auto compound_collapse_pattern() {
    auto inner = cxxOperatorCallExpr(
        hasOverloadedOperatorName("+"),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(qint_dre("a"))),
        hasArgument(1, ignoringImplicit(qint_dre("b")))
    ).bind("inner");

    return varDecl(
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint_t"))))),
        hasInitializer(ignoringImplicit(inner))
    ).bind("var");
}

// Compound-collapse callback. Anchored on the `qint r = a + b;`
// VarDecl; per-pair adjacency / pinning validation lives here so the
// matcher pattern stays declarative.
class CompoundCollapseAddModCallback : public MatchFinder::MatchCallback {
public:
    explicit CompoundCollapseAddModCallback(std::vector<ModularOpHit>* hits)
        : hits_(hits) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var   = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* inner = r.Nodes.getNodeAs<CXXOperatorCallExpr>("inner");
        const auto* a     = r.Nodes.getNodeAs<DeclRefExpr>("a");
        const auto* b     = r.Nodes.getNodeAs<DeclRefExpr>("b");
        if (!var || !inner || !a || !b || !r.Context) return;

        ASTContext& ctx = *r.Context;

        // Adjacency gate: the IMMEDIATE next sibling of the VarDecl's
        // DeclStmt must be the `r %= n;` op-call. Any intervening stmt
        // (including a reader of `r` like `r += c;` or even a no-op
        // `(void)r;`) breaks adjacency — the matcher bails and the
        // wide path takes over.
        const Stmt* next = next_sibling_of_var_decl(*var, ctx);
        if (next == nullptr) return;

        const auto* mod_assign = llvm::dyn_cast<CXXOperatorCallExpr>(next);
        if (mod_assign == nullptr) return;
        if (mod_assign->getOperator() != OO_PercentEqual) return;
        if (mod_assign->getNumArgs() != 2) return;

        // LHS pinning: the `%=` LHS must refer to the just-declared
        // VarDecl (decl_loc equality, not just name equality — guards
        // against shadowing). Same discriminator the PJ-1d ccnot-fuse
        // helper uses.
        const Expr* lhs_arg = mod_assign->getArg(0);
        if (lhs_arg == nullptr) return;
        const auto* lhs_dre = llvm::dyn_cast<DeclRefExpr>(
            lhs_arg->IgnoreParenImpCasts());
        if (lhs_dre == nullptr) return;
        const NamedDecl* lhs_nd = lhs_dre->getDecl();
        if (lhs_nd == nullptr) return;
        if (lhs_nd->getLocation() != var->getLocation()) return;

        // RHS extraction + qint-type guard. The `n` operand is the
        // second argument of the `%=` op-call; it must be a qint-typed
        // DRE for the rewrite to type-check.
        const Expr* rhs_arg = mod_assign->getArg(1);
        if (rhs_arg == nullptr) return;
        const auto* rhs_dre = llvm::dyn_cast<DeclRefExpr>(
            rhs_arg->IgnoreParenImpCasts());
        if (rhs_dre == nullptr) return;
        const QualType rhs_qt =
            rhs_dre->getType().getCanonicalType();
        const auto* rhs_record = rhs_qt->getAsCXXRecordDecl();
        if (rhs_record == nullptr) return;
        const auto* rhs_spec =
            llvm::dyn_cast<ClassTemplateSpecializationDecl>(rhs_record);
        if (rhs_spec == nullptr) return;
        const auto* rhs_primary = rhs_spec->getSpecializedTemplate();
        if (rhs_primary == nullptr) return;
        if (rhs_primary->getQualifiedNameAsString() != "sturm::qint_t") {
            return;
        }
        const NamedDecl* rhs_nd = rhs_dre->getDecl();
        if (rhs_nd == nullptr) return;

        // Enclosing-block resolution — needed for the hit's
        // `enclosing_block` slot and to keep the diagnostic shape
        // identical to the in-initializer AddMod arm.
        const CompoundStmt* block =
            nearest_compound_stmt_for_decl(*var, ctx);
        if (block == nullptr) return;

        ModularOpHit hit;
        hit.kind = ModularOpKind::AddMod;
        hit.result_name = var->getNameAsString();
        if (const NamedDecl* a_nd = a->getDecl()) {
            hit.a_name = a_nd->getNameAsString();
        }
        if (const NamedDecl* b_nd = b->getDecl()) {
            hit.b_name = b_nd->getNameAsString();
        }
        hit.n_name = rhs_nd->getNameAsString();
        hit.result_width = extract_qint_width_from_vd(*var);
        // mod_expr stays null on purpose — no outer `%` op-call exists
        // in the compound-collapsed shape. The mod_assign_call slot
        // tells the consumer drain "this is a beat-5.3 hit; extend the
        // QReplacement range to absorb the `r %= n;` stmt and suppress
        // the LO-2a hit on this same op-call".
        hit.inner_op_expr = inner;
        hit.result_var = var;
        hit.enclosing_block = block;
        hit.mod_assign_call = mod_assign;
        hits_->push_back(std::move(hit));
    }

private:
    std::vector<ModularOpHit>* hits_;
};

std::vector<std::unique_ptr<CompoundCollapseAddModCallback>>&
compound_collapse_pool() {
    static std::vector<std::unique_ptr<CompoundCollapseAddModCallback>> pool;
    return pool;
}

} // namespace

void register_compound_collapse_addmod_arm(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<ModularOpHit>& hits) {
    auto& pool = compound_collapse_pool();
    pool.push_back(
        std::make_unique<CompoundCollapseAddModCallback>(&hits));
    finder.addMatcher(compound_collapse_pattern(), pool.back().get());
}

} // namespace sturm::transpile
