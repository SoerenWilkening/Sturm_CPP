// matcher_qint_compare.cpp — Phase D (PD-1..PD-6) qint-qint comparisons.
//
// Six near-identical matchers for `qbool c = a OP b;` where a and b are
// `qint_t<W>` locals/parameters (bare DeclRefExprs) and OP is one of
// the six relational operators:
//
//   - PD-1 : `qbool c = a == b;`  → QOpKind::EQ_QINT
//   - PD-2 : `qbool c = a != b;`  → QOpKind::NE_QINT
//   - PD-3 : `qbool c = a <  b;`  → QOpKind::LT_QINT
//   - PD-4 : `qbool c = a <= b;`  → QOpKind::LE_QINT
//   - PD-5 : `qbool c = a >  b;`  → QOpKind::GT_QINT
//   - PD-6 : `qbool c = a >= b;`  → QOpKind::GE_QINT
//
// Structural difference from the Phase C compound-assign matchers: this is
// a *VarDecl initializer* match (shape `qbool c = <init>;`) rather than a
// bare compound-assign expression. The VarDecl is the anchor we use to name
// the produced qbool `c`; the CXXOperatorCallExpr nested inside the
// initializer carries the two qint_t operands.
//
// AST shape of the RHS
// --------------------
// `qint_t<W>::operator==` (and siblings) return `qbool` by value — see
// include/sturm/qtypes/qint_compare_v3.hpp:93-127 where
// detail::make_dsl_compare_result returns a qbool the compiler must
// copy/move into the VarDecl. Under C++17 mandatory elision the copy is
// elided, but Clang's AST still carries a CXXConstructExpr wrapping the
// call expression in many cases (elidable copy construction). The
// canonical peel via `ignoringImplicit` handles ImplicitCastExpr and
// ExprWithCleanups but does NOT peel CXXConstructExpr, so we wrap the
// initializer pattern in an
// `anyOf(ignoringImplicit(call), ignoringImplicit(cxxConstructExpr(has(ignoringImplicit(call)))))`
// — the same shape matcher_qbool_bitwise.cpp:200-228 uses for
// anyOf(eager_init, lazy_init). The two branches are mutually exclusive
// by overload resolution (the return type is always qbool, so either the
// copy elides cleanly via the bare call or it materializes through a
// CXXConstructExpr), so no op ever binds twice.
//
// Disjointness from other matchers
// --------------------------------
// - Phase C qint-qint compound-assigns (`a += b;`) are statement-shaped
//   (CXXOperatorCallExpr at statement scope, no enclosing VarDecl), so
//   they cannot collide with this VarDecl-initializer pattern.
// - The M7 OR matcher keys off `operator|` on qbool operands, not the
//   relational operators on qint_t. Non-overlapping.
// - A classical-typed compare (`bool c = i == j;` for plain ints) has
//   neither a qbool VarDecl type nor qint_t-typed arguments, so both
//   guards reject it.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::make_ref;

// Shared callback body. Parameterized by the target QOpKind so each of the
// six forms shares a single implementation — the only difference between
// them is the emitted QOpKind and the matched overloaded operator name.
template <QOpKind Kind>
class QIntCompareCallback : public MatchFinder::MatchCallback {
public:
    explicit QIntCompareCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* lhs = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!var || !lhs || !rhs || !r.Context) return;

        // Phase H PH-1: `enclosing_scope` transparently supports braced
        // CompoundStmt and braceless for/while/if/else body positions.
        const auto es = enclosing_scope(*var, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_ = r.Context->getSourceManager();
        const LangOptions& lang_ = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_, lang_);

        // Debug-build guard (mirror of matcher_qbool_bitwise.cpp's OR
        // callback): the widened `anyOf(direct_call, ctor_wrapped_call)`
        // branches are mutually exclusive by construction, but a future
        // pattern tweak could accidentally produce a duplicate. Assert
        // that we have not already recorded an op for this var's decl
        // location in the current QScope.
#ifndef NDEBUG
        const auto var_key = var->getLocation().getRawEncoding();
        for (const auto& existing : scope.ops) {
            assert(existing.result.decl_loc.getRawEncoding() != var_key &&
                   "QIntCompareCallback double-matched the same VarDecl "
                   "(anyOf direct/ctor-wrapped branches are not mutually "
                   "exclusive)");
        }
#endif

        QOperation op;
        op.kind = Kind;
        op.result.name     = var->getNameAsString();
        op.result.decl_loc = var->getLocation();
        op.operands.push_back(make_ref(*lhs));
        op.operands.push_back(make_ref(*rhs));
        op.stmt_range = var->getSourceRange();

        scope.ops.push_back(std::move(op));
    }

private:
    QUnit* unit_;
};

using EqCompareCallback = QIntCompareCallback<QOpKind::EQ_QINT>;
using NeCompareCallback = QIntCompareCallback<QOpKind::NE_QINT>;
using LtCompareCallback = QIntCompareCallback<QOpKind::LT_QINT>;
using LeCompareCallback = QIntCompareCallback<QOpKind::LE_QINT>;
using GtCompareCallback = QIntCompareCallback<QOpKind::GT_QINT>;
using GeCompareCallback = QIntCompareCallback<QOpKind::GE_QINT>;

// Per-callback-type pool — keeps callbacks alive for the MatchFinder's
// lifetime (see the pattern note at the top of matcher_common.hpp).
template <typename Cb>
std::vector<std::unique_ptr<Cb>>& compare_callback_pool() {
    static std::vector<std::unique_ptr<Cb>> pool;
    return pool;
}

// Build the VarDecl-initializer pattern for a single comparison operator.
// The template parameter `op_name` flows into hasOverloadedOperatorName
// and is the only source of variation between the six registrations.
template <typename OperatorName>
auto make_compare_pattern(OperatorName op_name) {
    // Core binary-comparison call: both args must be qint_t-typed
    // DeclRefExprs (peel implicit casts, then require the canonical type
    // to name the qint_t record decl). `lhs` and `rhs` are the operand
    // identifier bindings the callback later lifts into QValueRefs.
    auto compare_call = cxxOperatorCallExpr(
        hasOverloadedOperatorName(op_name),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(
            declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))
                .bind("lhs"))),
        hasArgument(1, ignoringImplicit(
            declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))
                .bind("rhs"))));

    // Two AST shapes reach the VarDecl initializer:
    //   (a) direct: the call expression survives copy-elision and sits
    //       under the VarDecl through ImplicitCastExpr / ExprWithCleanups
    //       only. ignoringImplicit peels those.
    //   (b) ctor-wrapped: the call is wrapped in a CXXConstructExpr
    //       (elidable-copy / move construction). ignoringImplicit does
    //       NOT peel CXXConstructExpr, so we match it explicitly and
    //       descend via `has(ignoringImplicit(call))`.
    //
    // Mutually exclusive by construction — copy-elision either fires
    // cleanly (case a) or leaves the ctor in the AST (case b); neither
    // shape produces both simultaneously. The debug-build guard above
    // asserts this invariant for safety.
    auto direct_init      = ignoringImplicit(compare_call);
    auto ctor_wrapped     = ignoringImplicit(
        cxxConstructExpr(has(ignoringImplicit(compare_call))));

    return varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(anyOf(direct_init, ctor_wrapped))
    ).bind("var");
}

// Generic registration shim — creates a fresh callback instance, appends
// it to the per-type pool, and wires both into the MatchFinder.
template <typename Cb, typename OperatorName>
void register_compare(clang::ast_matchers::MatchFinder& finder,
                      QUnit& unit, OperatorName op_name) {
    auto& pool = compare_callback_pool<Cb>();
    pool.push_back(std::make_unique<Cb>(&unit));
    finder.addMatcher(make_compare_pattern(op_name), pool.back().get());
}

} // namespace

void register_eq_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_compare<EqCompareCallback>(finder, unit, "==");
}

void register_ne_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_compare<NeCompareCallback>(finder, unit, "!=");
}

void register_lt_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_compare<LtCompareCallback>(finder, unit, "<");
}

void register_le_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_compare<LeCompareCallback>(finder, unit, "<=");
}

void register_gt_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_compare<GtCompareCallback>(finder, unit, ">");
}

void register_ge_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_compare<GeCompareCallback>(finder, unit, ">=");
}

} // namespace sturm::transpile
