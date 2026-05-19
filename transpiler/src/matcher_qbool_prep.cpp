// matcher_qbool_prep.cpp — Phase N PN-5 qbool(p) prep diagnostic.
//
// Detects `qbool x(p);` VarDecls whose initializer is a probabilistic
// `double` argument AND whose lexically enclosing scope is an
// uncompute-eligible scope — a `WHEN(expr) { ... }` body or a
// compound-expression intermediate (a scope whose `unit.scopes[i].ops`
// already owns the VarDecl as a synthesized temp). Per docs/01_
// principles.md P5 item 1 + P9 ("routines are invertible by explicit
// adjoint"), preparation is a CP map with no adjoint; the transpiler
// cannot synthesize an inverse for a prep inside an uncompute-eligible
// scope without violating P9. Rather than add a `QOpKind::PREP` op
// whose inverse would be a discard / measurement, PN-5 routes the case
// through the PM3 diagnostics surface: a Warning is emitted via
// `DiagContext::report_prep_in_uncompute_scope(loc, name)`.
//
// Closest cousin: `matcher_outer_var_guard.cpp` (PM3-2 pathway). Both
// walk the parent chain to discover a scope property and emit a
// `DiagContext` call on match; neither writes into `unit.scopes.ops`.
//
// Detection shape (AST pattern)
// -----------------------------
//
//     varDecl(
//       hasType(qbool_guard()),
//       hasInitializer(cxxConstructExpr(
//         argumentCountIs(1),
//         hasArgument(0, expr().bind("init_arg")))))
//     .bind("prep_decl")
//
// `qbool_guard()` restricts the VarDecl type to a record whose
// qualified name matches `sturm::qbool` (mirrors the `qint_guard()`
// pattern used by `matcher_qint_const.cpp`).
//
// Classical-init guard
// --------------------
// Callers' classical init shapes MUST NOT trip the diagnostic:
//
//   - `qbool x(true);` / `qbool x(false);` — the peeled `init_arg` is
//     a `CXXBoolLiteralExpr`; early-return silently.
//   - `qbool x(b);` with `bool b` — `init_arg->getType()->isBooleanType()`
//     is true; early-return silently.
//   - `qbool x(y);` with another qbool `y` — the argument's post-impl-
//     cast type is `sturm::qbool` (neither bool nor double); the
//     matcher treats this as classical and silent. A qbool-to-qbool
//     copy has a trivial adjoint (also a copy) and doesn't contend
//     with P9.
//
// The matcher ONLY fires when `init_arg` is a `double`-typed
// expression (via `isFloatingType()` on the post-impl-cast type).
// This pins the PN-5 contract to the `qbool(double)` probabilistic
// constructor (include/sturm/qtypes/qbool.hpp:58) and leaves every
// other ctor overload silent.
//
// Scope classification
// --------------------
// Using `detail::enclosing_scope` to locate the VarDecl's enclosing
// scope anchor, then `detail::classify_scope_kind` to classify:
//
//   - Function       : top-level function body. Silent — prep here is
//                      a valid P5 use. Early-return.
//   - LoopBody       : inside a for/while body. Silent per §14 risk 2
//                      — loops are outside the v1 PN-5 scope (the
//                      plan explicitly mentions "WHEN body or
//                      compound-expression intermediate" and leaves
//                      loop-interior prep for a future v2).
//   - BranchBody     : inside a user `if` / `else` body. Silent — the
//                      user-if body is not uncompute-eligible (PH-3
//                      flags compound-assigns there instead), so PN-5
//                      does not fire.
//   - WhenBody       : inside a WHEN body. Fires.
//   - Other          : nested compound scopes that are not one of the
//                      above. Check the compound-intermediate path
//                      below; otherwise silent.
//
// Compound-intermediate probe
// ---------------------------
// Per the plan §5 step 2: "the VarDecl appears in a scope whose
// `unit.scopes[i].ops` owns an op that lists this VarDecl as a
// result". The transpiler's compound-expression matcher (Phase E
// PE-4) flattens nested bitwise expressions into a sequence of
// synthetic `__stu_t<N>` qbool decls; each of those is owned by the
// scope where the compound expression lives. PN-5 treats the
// presence of such a result-ownership relationship as an
// uncompute-eligible signal — the scope will be uncomputed at
// close, so a prep that lands inside it would fall under the
// uncompute pass's cleanup without a paired adjoint.
//
// The probe walks `unit.scopes` once and checks whether any op's
// `result.decl_loc` matches the candidate VarDecl's declaration loc.
// Scope counts are small in practice (one per lexical block) so the
// linear scan is O(scopes × ops_per_scope), well inside the budget.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "diag_context.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_qbool_prep_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;
using detail::classify_scope_kind;
using detail::enclosing_scope;
using detail::is_expansion_of_macro;
using detail::QScopeKind;
using detail::ScopeKind;

// Test-only counter. Incremented once per successfully flagged prep
// (i.e. per PN-5 diagnostic the callback fires). The transpiler is
// single-threaded so a plain int is fine.
static int g_qbool_prep_detection_count = 0;

// Classify `init_arg` as classical-init (bool literal OR bool-typed
// expression) or NOT. Returns true when the argument should NOT fire
// the PN-5 diagnostic (classical guard hit) and false otherwise.
// The check is:
//
//   - The expression itself, after `IgnoreParenImpCasts`, is a
//     `CXXBoolLiteralExpr` (e.g. `qbool x(true);`).
//   - OR the expression's static type (as seen at the init site, prior
//     to any implicit conversion to `double`) satisfies
//     `isBooleanType()` (e.g. `qbool x(b);` where `b` is `bool`).
//
// We use `IgnoreParenImpCasts` rather than `IgnoreImplicit` because
// the latter also peels through CXXConstructExpr and temporary
// materialisation — we want to see the raw argument the user wrote.
static bool is_classical_init(const Expr* init_arg) {
    if (!init_arg) return true;
    const Expr* peeled = init_arg->IgnoreParenImpCasts();
    if (!peeled) return true;
    if (isa<CXXBoolLiteralExpr>(peeled)) return true;
    // `getType()` on the raw (non-peeled) expression reports the
    // post-impl-cast type for the argument as handed to the ctor's
    // parameter. Checking BOTH the peeled and the unpeeled types
    // matches the "classical bool" semantics whether the user wrote
    // `qbool x(b)` with `b` a bool (unpeeled type is bool) or
    // `qbool x(static_cast<double>(b))` (peeled type is bool before
    // the cast). The PN-5 plan §5 risk 3 explicitly calls out
    // conservative silence on any bool-typed input — this helper
    // honours that contract.
    const QualType raw_ty = init_arg->getType();
    if (!raw_ty.isNull() && raw_ty->isBooleanType()) return true;
    const QualType peeled_ty = peeled->getType();
    if (!peeled_ty.isNull() && peeled_ty->isBooleanType()) return true;
    return false;
}

// Classify `init_arg` as a probabilistic-prep argument: its
// post-impl-cast type is a floating-point type (`double`, `float`, ...
// — the `qbool(double)` ctor in `include/sturm/qtypes/qbool.hpp:58` is
// the production entry point; the matcher accepts the broader
// `isFloatingType()` so `qbool x(float_var)` and `qbool x(0.0f)`
// shapes also fire). Returns true when the argument is probabilistic
// and the matcher should continue to the scope classification step.
//
// A non-floating, non-classical-bool argument (e.g. `qbool x(y)` with
// another qbool `y`) falls through both `is_classical_init` and
// `is_prep_arg` — the caller treats such a shape as "not in scope"
// and returns silently. This preserves the contract that PN-5 only
// flags the specific `qbool(double)` prep overload.
static bool is_prep_arg(const Expr* init_arg) {
    if (!init_arg) return false;
    const QualType raw_ty = init_arg->getType();
    if (!raw_ty.isNull() && raw_ty->isFloatingType()) return true;
    const Expr* peeled = init_arg->IgnoreParenImpCasts();
    if (!peeled) return false;
    const QualType peeled_ty = peeled->getType();
    if (!peeled_ty.isNull() && peeled_ty->isFloatingType()) return true;
    return false;
}

// Walk outward from `vd` and check whether the VarDecl is nested
// inside a `WHEN` macro expansion. We climb the parent chain looking
// for an `IfStmt` whose `getIfLoc()` is a macro-expansion of `WHEN`
// (via `detail::is_expansion_of_macro`). The walk stops at the
// function body or at the top of the TU — either way the answer is
// "not inside a WHEN" and the caller falls through to the
// compound-intermediate probe.
static bool is_inside_when_body(const VarDecl* vd, ASTContext& ctx) {
    if (!vd) return false;
    const SourceManager& sm = ctx.getSourceManager();
    const LangOptions& lang = ctx.getLangOpts();
    DynTypedNode node = DynTypedNode::create(*vd);
    // Cap the walk defensively; real source trees are nowhere near
    // this deep.
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return false;
        node = parents[0];
        if (const auto* is = node.get<IfStmt>()) {
            const SourceLocation if_loc = is->getIfLoc();
            if (if_loc.isValid() && if_loc.isMacroID() &&
                is_expansion_of_macro(if_loc, sm, lang, "WHEN")) {
                return true;
            }
            // User-written `if` with the VarDecl in its body is NOT a
            // WHEN — keep walking; the outer scope may still be a
            // WHEN body (e.g. `WHEN(c) { if (classical_flag) {
            // qbool x(p); } }`). The §5 step 2 contract is
            // "ANY enclosing WHEN macro expansion", so we do not
            // shortcut on the first user `if`.
        }
        if (node.get<FunctionDecl>() != nullptr) {
            // Reached the function body. No enclosing WHEN above.
            return false;
        }
    }
    return false;
}

// Check whether the VarDecl is owned by any `QScope::ops` entry as a
// synthesized compound-expression intermediate. The Phase E PE-4
// compound-flatten matcher pushes intermediate qbool decls with
// `result.decl_loc = {}` (invalid) for fresh `__stu_t<N>` temps AND
// with the user-written VarDecl's location for outermost scope binds.
// PN-5 is interested in the latter: a prep that coincides with a
// known op result would sit inside a scope whose close brace triggers
// the uncompute pass. We scan every scope.ops once and compare
// `op.result.decl_loc` (valid) against the VarDecl's `getLocation()`.
//
// For fresh `__stu_t<N>` temps the decl_loc is invalid and the
// comparison never matches — those synthesized decls are not user
// source, so they cannot be the target of a PN-5 `qbool x(p)` the
// user actually wrote. The probe therefore has no false-positive
// surface against synthetic temps; it only fires on user-written
// decls that the compound matcher has already claimed.
static bool is_owned_by_compound_scope(const QUnit& unit,
                                       const VarDecl* vd) {
    if (!vd) return false;
    const SourceLocation vd_loc = vd->getLocation();
    if (vd_loc.isInvalid()) return false;
    const auto vd_raw = vd_loc.getRawEncoding();
    for (const auto& scope : unit.scopes) {
        for (const auto& op : scope.ops) {
            if (!op.result.decl_loc.isValid()) continue;
            if (op.result.decl_loc.getRawEncoding() == vd_raw) {
                return true;
            }
        }
    }
    return false;
}

class QBoolPrepCallback : public MatchFinder::MatchCallback {
public:
    QBoolPrepCallback(QUnit* unit, DiagContext* diag)
        : unit_(unit), diag_(diag) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* vd = r.Nodes.getNodeAs<VarDecl>("prep_decl");
        const auto* init_arg = r.Nodes.getNodeAs<Expr>("init_arg");
        if (!vd || !init_arg || !r.Context || !diag_) return;

        // Filter 1: classical init. Silent early-return covers
        // `qbool x(true);`, `qbool x(false);`, and any bool-typed
        // argument handed through the converting constructor.
        if (is_classical_init(init_arg)) return;

        // Filter 2: argument must be a floating-point expression. This
        // pins the matcher to the `qbool(double)` probabilistic
        // constructor; copy-construct from another qbool or any
        // other non-floating argument type falls through here and
        // returns silently.
        if (!is_prep_arg(init_arg)) return;

        // Filter 3: scope classification. Use `enclosing_scope` to
        // locate the VarDecl's scope anchor and `classify_scope_kind`
        // to decide whether the scope is uncompute-eligible.
        ASTContext& ctx = *r.Context;
        const auto es = enclosing_scope(*vd, ctx);
        if (!es.valid()) return;

        // Resolve the scope anchor Stmt* for classify_scope_kind.
        // A CompoundStmt-kind scope's anchor is the compound itself;
        // a BracelessBody scope's anchor is the body stmt.
        const Stmt* scope_anchor =
            (es.kind == QScopeKind::CompoundStmt)
                ? static_cast<const Stmt*>(es.compound)
                : es.braceless_body;
        const ScopeKind kind = classify_scope_kind(scope_anchor, ctx);

        // Function-body top-level prep is always silent — valid P5
        // use.
        if (kind == ScopeKind::Function) return;

        // Signal 1: WHEN body. Plan §5 step 2 bullet 1.
        const bool in_when = (kind == ScopeKind::WhenBody) ||
                             is_inside_when_body(vd, ctx);

        // Signal 2: compound-expression intermediate scope. Plan §5
        // step 2 bullet 2. Scan `unit.scopes` for any op whose
        // `result.decl_loc` matches this VarDecl's decl loc.
        const bool owned_by_compound =
            is_owned_by_compound_scope(*unit_, vd);

        if (!in_when && !owned_by_compound) return;

        // Emit the diagnostic. Funnel the loc through `getFileLoc`
        // so macro-expansion locations resolve to the user's
        // filename rather than the memory buffer (mirrors the PM3-2
        // pattern in `matcher_outer_var_guard.cpp`).
        const SourceManager& sm = ctx.getSourceManager();
        const SourceLocation file_loc = sm.getFileLoc(vd->getLocation());
        const std::string name = vd->getNameAsString();
        diag_->report_prep_in_uncompute_scope(
            file_loc, std::string_view(name));
        ++g_qbool_prep_detection_count;
    }

private:
    QUnit* unit_;
    DiagContext* diag_;
};

// One-callback-per-registration pool; same ownership discipline as
// the other matcher TUs.
std::vector<std::unique_ptr<QBoolPrepCallback>>&
qbool_prep_callback_pool() {
    static std::vector<std::unique_ptr<QBoolPrepCallback>> pool;
    return pool;
}

// Helper: restrict a VarDecl's type to `sturm::qbool`. Mirrors the
// `qint_guard()` pattern in `matcher_qint_const.cpp`. The canonical
// type + declaration drill-down peels typedef sugar; `hasName` alone
// is not enough because the DeclRefExpr's type is a sugared record
// type, not a bare `RecordType`. The plain `hasName("qbool")`
// (unqualified) mirrors what Phase E PE-4 and the dead-ancilla
// matcher use — anchoring on the class name alone is sufficient for
// the hermetic test stubs AND for production `sturm::qbool` because
// the record decl's short name is exactly `qbool` in both cases.
auto qbool_guard() {
    return hasCanonicalType(hasDeclaration(
        cxxRecordDecl(hasName("qbool"))));
}

} // namespace sturm_matcher_qbool_prep_anon_ns
using namespace sturm_matcher_qbool_prep_anon_ns;

void register_qbool_prep_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    DiagContext& diag) {
    auto& pool = qbool_prep_callback_pool();
    pool.push_back(std::make_unique<QBoolPrepCallback>(&unit, &diag));
    QBoolPrepCallback* cb = pool.back().get();

    // VarDecl-anchored pattern: a qbool-typed decl whose initializer
    // is a single-argument CXXConstructExpr. The `init_arg` binding
    // captures the raw argument expression (before any impl-cast /
    // paren peel); the callback applies `IgnoreParenImpCasts` so
    // both raw and peeled arguments are inspected.
    finder.addMatcher(
        varDecl(
            hasType(qbool_guard()),
            hasInitializer(cxxConstructExpr(
                argumentCountIs(1),
                hasArgument(0, expr().bind("init_arg")))))
            .bind("prep_decl"),
        cb);
}

int qbool_prep_detection_count_for_test() {
    return g_qbool_prep_detection_count;
}

void reset_qbool_prep_detection_count_for_test() {
    g_qbool_prep_detection_count = 0;
}

} // namespace sturm::transpile
