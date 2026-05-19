// matcher_modular_pow_mod.cpp — sturm-qzab.5 (Phase 5 beat 5.5) PowMod
// arm. Factored out of `matcher_modular_op.cpp` to keep the orchestrator
// TU under the plan §7.1 280-LoC budget — same posture as
// `matcher_modular_compound_collapse.{hpp,cpp}` for beat 5.3.
//
// The whole TU body is gated on `STURM_MODULAR_POW`. Under flag OFF
// the file compiles to an empty translation unit (no symbols emitted),
// which keeps the orchestrator's negative-path contract intact: under
// OFF the matcher recognises NO `pow(a, x) % n` site and the AST
// round-trips identically through the transpiler.
//
// The matched AST shape is:
//
//   VarDecl(type=qint_t<W>) → init →
//     CXXOperatorCallExpr('%') named "outer"
//       arg0: CallExpr → callee=functionDecl(hasName("pow"))     named "pow"
//         arg0: DeclRefExpr (qint_t)                            named "a"
//         arg1: DeclRefExpr (qint_t)                            named "b"
//       arg1: DeclRefExpr (qint_t)                              named "n"
//
// Plan §7.4 / PRD §3.4: rewrite target is
// `sturm::qint_t<W> r = ::sturm::pow_mod(a, x, n);` — emission lives
// in `modular_rewrite_emitter.cpp`'s PowMod arm; this file's job is
// purely AST recognition + hit population.

#include "matcher_modular_pow_mod.hpp"

#ifdef STURM_MODULAR_POW

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
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_modular_pow_mod_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// Resolve `W` in `qint_t<W>` off a VarDecl's type. Mirrors the helper
// in `matcher_modular_op.cpp::extract_qint_width_from_vd_pow_mod`. Local copy
// (rather than an exported helper) keeps the orchestrator TU's static
// `namespace { ... }` posture intact — the helper has the same
// "unique-per-TU; no ODR risk" shape used throughout the matcher pool.
int extract_qint_width_from_vd_pow_mod(const VarDecl& vd) {
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
// VarDecl. Mirrors the orchestrator TU's helper of the same name —
// duplicated here to avoid exporting an internal-linkage helper across
// TUs (which would force an `inline` function in a header just for
// this single use).
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

void populate_pow_mod_hit(const MatchFinder::MatchResult& r,
                          std::vector<ModularOpHit>* hits) {
    const auto* var   = r.Nodes.getNodeAs<VarDecl>("var");
    const auto* outer = r.Nodes.getNodeAs<CXXOperatorCallExpr>("outer");
    const auto* pow   = r.Nodes.getNodeAs<CallExpr>("pow");
    const auto* a     = r.Nodes.getNodeAs<DeclRefExpr>("a");
    const auto* b     = r.Nodes.getNodeAs<DeclRefExpr>("b");
    const auto* n     = r.Nodes.getNodeAs<DeclRefExpr>("n");
    if (!var || !outer || !pow || !a || !b || !n || !r.Context) return;

    const CompoundStmt* block =
        nearest_compound_stmt_for_decl(*var, *r.Context);
    if (!block) return;

    ModularOpHit hit;
    hit.kind = ModularOpKind::PowMod;
    hit.result_name = var->getNameAsString();
    if (const NamedDecl* nd = a->getDecl()) hit.a_name = nd->getNameAsString();
    if (const NamedDecl* nd = b->getDecl()) hit.b_name = nd->getNameAsString();
    if (const NamedDecl* nd = n->getDecl()) hit.n_name = nd->getNameAsString();
    hit.result_width = extract_qint_width_from_vd_pow_mod(*var);
    hit.mod_expr = outer;
    hit.pow_call = pow;
    // `inner_op_expr` stays null for PowMod — the inner node is a
    // CallExpr, not a CXXOperatorCallExpr; the consumer drain reads
    // the discriminant `kind` and ignores `inner_op_expr` on this arm.
    hit.result_var = var;
    hit.enclosing_block = block;
    hits->push_back(std::move(hit));
}

// sturm-qzab.6 (P5 beat 5.6): int-exponent PowMod arm callback.
//
// For the AST shape `pow(qint, <non-qint expr>) % qint` the second
// argument of `pow` is NOT a `DeclRefExpr` to a qint variable — it is
// an integer literal (`3LL`) or any other non-qint expression matching
// the `pow(qint_t<W>, long long)` overload. We bind it as a generic
// `Expr` (not a `DeclRefExpr`), then recover its verbatim source text
// via `Lexer::getSourceText` so the emitter can splice it back into
// the rewrite as `pow_mod(a, 3LL, n)`. This mirrors the
// `matcher_qint_const.cpp::QIntAssignConstCallback` pattern for
// constant-RHS extraction.
void populate_pow_mod_int_exp_hit(const MatchFinder::MatchResult& r,
                                  std::vector<ModularOpHit>* hits) {
    const auto* var    = r.Nodes.getNodeAs<VarDecl>("var");
    const auto* outer  = r.Nodes.getNodeAs<CXXOperatorCallExpr>("outer");
    const auto* pow    = r.Nodes.getNodeAs<CallExpr>("pow");
    const auto* a      = r.Nodes.getNodeAs<DeclRefExpr>("a");
    const auto* b_expr = r.Nodes.getNodeAs<Expr>("b_expr");
    const auto* n      = r.Nodes.getNodeAs<DeclRefExpr>("n");
    if (!var || !outer || !pow || !a || !b_expr || !n || !r.Context) return;

    const CompoundStmt* block =
        nearest_compound_stmt_for_decl(*var, *r.Context);
    if (!block) return;

    const SourceManager& sm = r.Context->getSourceManager();
    const LangOptions& lang = r.Context->getLangOpts();
    auto b_text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(b_expr->getSourceRange()),
        sm, lang);
    if (b_text.empty()) return;

    ModularOpHit hit;
    hit.kind = ModularOpKind::PowMod;
    hit.result_name = var->getNameAsString();
    if (const NamedDecl* nd = a->getDecl()) hit.a_name = nd->getNameAsString();
    hit.b_name = b_text.str();
    if (const NamedDecl* nd = n->getDecl()) hit.n_name = nd->getNameAsString();
    hit.result_width = extract_qint_width_from_vd_pow_mod(*var);
    hit.mod_expr = outer;
    hit.pow_call = pow;
    hit.result_var = var;
    hit.enclosing_block = block;
    hits->push_back(std::move(hit));
}

class PowModCallback : public MatchFinder::MatchCallback {
public:
    explicit PowModCallback(std::vector<ModularOpHit>* hits) : hits_(hits) {}

    void run(const MatchFinder::MatchResult& r) override {
        populate_pow_mod_hit(r, hits_);
    }

private:
    std::vector<ModularOpHit>* hits_;
};

class PowModIntExpCallback : public MatchFinder::MatchCallback {
public:
    explicit PowModIntExpCallback(std::vector<ModularOpHit>* hits)
        : hits_(hits) {}

    void run(const MatchFinder::MatchResult& r) override {
        populate_pow_mod_int_exp_hit(r, hits_);
    }

private:
    std::vector<ModularOpHit>* hits_;
};

std::vector<std::unique_ptr<PowModCallback>>& pow_mod_pool() {
    static std::vector<std::unique_ptr<PowModCallback>> pool;
    return pool;
}

std::vector<std::unique_ptr<PowModIntExpCallback>>& pow_mod_int_exp_pool() {
    static std::vector<std::unique_ptr<PowModIntExpCallback>> pool;
    return pool;
}

// We match `pow` by unqualified name (`hasName("pow")` is the matcher
// shorthand that ignores the `sturm::` qualifier on the AST node) and
// constrain the call's return type to `qint_t` so a same-named `pow`
// returning a non-qint type does NOT collapse into PowMod (mirrors
// the AddMod / MulMod arms' qint guard on the inner DeclRefExpr).
auto qint_dre_pow_mod(const char* binding) {
    return declRefExpr(hasType(hasCanonicalType(hasDeclaration(
        cxxRecordDecl(hasName("qint_t"))))))
        .bind(binding);
}

auto pow_mod_pattern() {
    auto pow_call = callExpr(
        callee(functionDecl(hasName("pow"))),
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint_t"))))),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(qint_dre_pow_mod("a"))),
        hasArgument(1, ignoringImplicit(qint_dre_pow_mod("b")))
    ).bind("pow");

    auto outer_mod = cxxOperatorCallExpr(
        hasOverloadedOperatorName("%"),
        argumentCountIs(2),
        hasArgument(0, ignoringParenImpCasts(pow_call)),
        hasArgument(1, ignoringImplicit(qint_dre_pow_mod("n")))
    ).bind("outer");

    return varDecl(
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint_t"))))),
        hasInitializer(ignoringImplicit(outer_mod))
    ).bind("var");
}

// sturm-qzab.6 (P5 beat 5.6): int-exponent variant pattern.
//
// Same outer shape as `pow_mod_pattern` (varDecl whose initializer is
// `pow(qint, ?) % qint`) but the second argument of `pow` is bound as a
// generic `Expr` rather than constrained to a qint `DeclRefExpr`. The
// `unless(ignoringImplicit(qint_dre_pow_mod(...)))` guard structurally disjoins
// this arm from the qint-exponent arm above so a single site cannot
// fire both — a `pow(qint, qint) % qint` site routes to the qint arm
// (its arg1 IS a qint DRE), and a `pow(qint, 3LL) % qint` site routes
// here (its arg1 is NOT a qint DRE). The exponent's verbatim source
// text is recovered by the callback via `Lexer::getSourceText` —
// matching the `matcher_qint_const.cpp` constant-RHS extraction shape.
auto pow_mod_int_exp_pattern() {
    auto pow_call = callExpr(
        callee(functionDecl(hasName("pow"))),
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint_t"))))),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(qint_dre_pow_mod("a"))),
        hasArgument(1, expr(unless(ignoringImplicit(declRefExpr(
            hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))))).bind("b_expr"))
    ).bind("pow");

    auto outer_mod = cxxOperatorCallExpr(
        hasOverloadedOperatorName("%"),
        argumentCountIs(2),
        hasArgument(0, ignoringParenImpCasts(pow_call)),
        hasArgument(1, ignoringImplicit(qint_dre_pow_mod("n")))
    ).bind("outer");

    return varDecl(
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint_t"))))),
        hasInitializer(ignoringImplicit(outer_mod))
    ).bind("var");
}

} // namespace sturm_matcher_modular_pow_mod_anon_ns
using namespace sturm_matcher_modular_pow_mod_anon_ns;

void register_pow_mod_arm(clang::ast_matchers::MatchFinder& finder,
                          std::vector<ModularOpHit>& hits) {
    {
        auto& pool = pow_mod_pool();
        pool.push_back(std::make_unique<PowModCallback>(&hits));
        finder.addMatcher(pow_mod_pattern(), pool.back().get());
    }
    // sturm-qzab.6 (P5 beat 5.6): register the int-exponent variant. Both
    // arms share the same hits vector — the consumer drain dispatches on
    // `ModularOpKind::PowMod` regardless of which variant fired.
    {
        auto& pool = pow_mod_int_exp_pool();
        pool.push_back(std::make_unique<PowModIntExpCallback>(&hits));
        finder.addMatcher(pow_mod_int_exp_pattern(), pool.back().get());
    }
}

} // namespace sturm::transpile

#endif // STURM_MODULAR_POW
