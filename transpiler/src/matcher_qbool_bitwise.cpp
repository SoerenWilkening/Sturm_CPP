// matcher_qbool_bitwise.cpp — qbool binary-bitwise matchers (M7 + PA-1/PA-2).
//
// Registers three matchers that fire on VarDecl initializers of shape
// `qbool tmp = <expr>;`:
//
//   - M7 : `qbool tmp = a | b;`   → QOpKind::OR
//   - PA-1 : `qbool tmp = ~a;`    → QOpKind::NOT
//   - PA-2 : `qbool tmp = a ^ b;` → QOpKind::XOR
//
// All three lean on the common helpers in matcher_common.hpp for scope
// bookkeeping (which is keyed off the enclosing CompoundStmt's opening
// brace — see the rationale in matcher.cpp's predecessor for why brace
// location rather than CompoundStmt pointer identity).

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
using detail::is_range_covered_by_fused;
using detail::make_ref;

// ── OrCallback (M7) ──────────────────────────────────────────────────────────

class OrCallback : public MatchFinder::MatchCallback {
public:
    explicit OrCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* lhs = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!var || !lhs || !rhs || !r.Context) return;

        // Phase J PJ-4a: early-return if the VarDecl has been flagged
        // for elimination by the dead-ancilla matcher. The elimination
        // pass deletes the decl verbatim; re-pushing an OR op here
        // would leave a stale `tmp = a | b;` uncompute at scope close
        // that the M8 synthesis pass would render against a no-longer-
        // existent decl.
        if (is_range_covered_by_fused(var->getSourceRange(),
                                      unit_->eliminated_stmt_ranges,
                                      r.Context->getSourceManager())) {
            return;
        }

        // Locate the enclosing scope. Phase H PH-1: `enclosing_scope`
        // returns either a braced CompoundStmt (legacy shape, byte-
        // identical to the old enclosing_compound_stmt walk) or a
        // BracelessBody (new: single-statement body of a for/while/if).
        const auto es = enclosing_scope(*var, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_ = r.Context->getSourceManager();
        const LangOptions& lang_ = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_, lang_);

        // Debug-build guard (Risk R3, LP4 §"Optional hardening"): the widened
        // `anyOf(eager_init, lazy_init)` pattern is mutually exclusive by
        // overload resolution (eager returns qbool directly, the conversion-
        // wrapped branch fires only when operator| returns an expression-
        // template wrapper that must go through a user-defined conversion),
        // but a pathological reshuffle could still cause one VarDecl to bind
        // twice. Assert that we have not already emitted an op for this
        // var's declaration location in the current QScope.
#ifndef NDEBUG
        const auto var_key = var->getLocation().getRawEncoding();
        for (const auto& existing : scope.ops) {
            assert(existing.result.decl_loc.getRawEncoding() != var_key &&
                   "OrCallback double-matched the same VarDecl "
                   "(anyOf eager/lazy branches are not mutually exclusive)");
        }
#endif

        QOperation op;
        op.kind = QOpKind::OR;
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

std::vector<std::unique_ptr<OrCallback>>& or_callback_pool() {
    static std::vector<std::unique_ptr<OrCallback>> pool;
    return pool;
}

// ── NotCallback (PA-1) ───────────────────────────────────────────────────────

class NotCallback : public MatchFinder::MatchCallback {
public:
    explicit NotCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var     = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* operand = r.Nodes.getNodeAs<DeclRefExpr>("operand");
        if (!var || !operand || !r.Context) return;

        // Phase J PJ-4a: same eliminated-range guard as OrCallback —
        // the dead-ancilla matcher may have deleted this NOT decl, and
        // we must not re-push a QOperation that would render a stale
        // uncompute.
        if (is_range_covered_by_fused(var->getSourceRange(),
                                      unit_->eliminated_stmt_ranges,
                                      r.Context->getSourceManager())) {
            return;
        }

        const auto es = enclosing_scope(*var, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_ = r.Context->getSourceManager();
        const LangOptions& lang_ = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_, lang_);

        // Same debug-build guard as OrCallback: the matcher pattern is
        // narrow enough that the same VarDecl cannot legitimately bind
        // twice, but if a future pattern widening accidentally produces a
        // duplicate the assert catches it before the uncompute list grows.
#ifndef NDEBUG
        const auto var_key = var->getLocation().getRawEncoding();
        for (const auto& existing : scope.ops) {
            assert(existing.result.decl_loc.getRawEncoding() != var_key &&
                   "NotCallback double-matched the same VarDecl");
        }
#endif

        QOperation op;
        op.kind = QOpKind::NOT;
        op.result.name     = var->getNameAsString();
        op.result.decl_loc = var->getLocation();
        op.operands.push_back(make_ref(*operand));
        op.stmt_range = var->getSourceRange();

        scope.ops.push_back(std::move(op));
    }

private:
    QUnit* unit_;
};

std::vector<std::unique_ptr<NotCallback>>& not_callback_pool() {
    static std::vector<std::unique_ptr<NotCallback>> pool;
    return pool;
}

// ── XorCallback (PA-2) ───────────────────────────────────────────────────────

class XorCallback : public MatchFinder::MatchCallback {
public:
    explicit XorCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* lhs = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!var || !lhs || !rhs || !r.Context) return;

        // Phase J PJ-4a: same eliminated-range guard as OrCallback /
        // NotCallback — bail if the dead-ancilla matcher deleted this
        // XOR decl.
        if (is_range_covered_by_fused(var->getSourceRange(),
                                      unit_->eliminated_stmt_ranges,
                                      r.Context->getSourceManager())) {
            return;
        }

        const auto es = enclosing_scope(*var, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_ = r.Context->getSourceManager();
        const LangOptions& lang_ = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_, lang_);

#ifndef NDEBUG
        const auto var_key = var->getLocation().getRawEncoding();
        for (const auto& existing : scope.ops) {
            assert(existing.result.decl_loc.getRawEncoding() != var_key &&
                   "XorCallback double-matched the same VarDecl");
        }
#endif

        QOperation op;
        op.kind = QOpKind::XOR;
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

std::vector<std::unique_ptr<XorCallback>>& xor_callback_pool() {
    static std::vector<std::unique_ptr<XorCallback>> pool;
    return pool;
}

} // namespace

void register_or_matcher(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit) {
    // Widened AST pattern (LP4) — see
    // `docs/archive/implementation_plan_transpiler_matcher_lazy_peel.md`
    // and the LP2 AST calibration notes on issue sturm-ea7. Fires on both
    // initializer shapes the matcher has to cover:
    //   (a) EAGER: `operator|(...) -> qbool`. VarDecl initializer is the
    //       CXXOperatorCallExpr itself (modulo implicit glue). Standard
    //       PK-2 shape — the qbool-level operators return owning qbool
    //       directly.
    //   (b) CONVERSION-WRAPPED: `operator|(...)` returns an expression-
    //       template wrapper with a user-defined `operator qbool()` that
    //       materialises to qbool. LP2 AST chain:
    //         VarDecl → ExprWithCleanups
    //                 → ImplicitCastExpr<UserDefinedConversion>
    //                 → CXXMemberCallExpr (operator qbool())
    //                 → ImplicitCastExpr<NoOp>
    //                 → MaterializeTemporaryExpr
    //                 → CXXOperatorCallExpr '|'
    //       `ignoringImplicit` peels all of the above (incl. the
    //       UserDefinedConversion cast and MaterializeTemporaryExpr).
    //       N.B. no CXXConstructExpr on this chain — PRD sketch was off.
    //       Phase K PK-2 retired the qbool-level wrappers, so today this
    //       branch fires only against fixtures that re-supply such a
    //       wrapper (see tests/transpiler/fixtures/or_single_backend.cpp).
    // Both branches bind the same `lhs`/`rhs`/`var` names; OrCallback::run
    // is unchanged. Branches are mutually exclusive by overload resolution
    // (eager returns qbool directly; the conversion-wrapped branch fires
    // only when operator| returns a distinct wrapper type).
    auto or_call = cxxOperatorCallExpr(
        hasOverloadedOperatorName("|"),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("lhs"))),
        hasArgument(1, ignoringImplicit(declRefExpr().bind("rhs"))));

    auto eager_init = ignoringImplicit(or_call);
    auto lazy_init  = ignoringImplicit(
        cxxMemberCallExpr(on(ignoringImplicit(or_call))));

    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(anyOf(eager_init, lazy_init))
    ).bind("var");

    auto& pool = or_callback_pool();
    pool.push_back(std::make_unique<OrCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

void register_not_matcher(clang::ast_matchers::MatchFinder& finder,
                          QUnit& unit) {
    // Phase A / PA-1: match `qbool tmp = ~a;`.
    //
    // `qbool::operator~` is a member function in the real header
    // (include/sturm/qtypes/qbool_ops.hpp:141), but Clang normalizes both
    // member and non-member operator overloads to CXXOperatorCallExpr, so
    // hasOverloadedOperatorName("~") + argumentCountIs(1) matches both
    // forms. For the member form, argument 0 is the `this` object; for the
    // non-member form, it is the single operand. Either way, the operand
    // we want to record is at index 0.
    //
    // Only the direct (non-conversion-wrapped) form is matched at PA-1:
    // `~` does not go through any expression-template wrapper — it is
    // always eager (the operator returns qbool directly, allocating an
    // ancilla and emitting X; see qbool_ops.hpp:141-153). No
    // UserDefinedConversion peeling is needed here, unlike
    // register_or_matcher's conversion-wrapped branch.
    auto not_call = cxxOperatorCallExpr(
        hasOverloadedOperatorName("~"),
        argumentCountIs(1),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("operand"))));

    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(ignoringImplicit(not_call))
    ).bind("var");

    auto& pool = not_callback_pool();
    pool.push_back(std::make_unique<NotCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

void register_xor_matcher(clang::ast_matchers::MatchFinder& finder,
                          QUnit& unit) {
    // Phase A / PA-2: match `qbool tmp = a ^ b;`.
    //
    // The binary XOR form is a straight mirror of the eager OR matcher:
    // two DeclRefExpr arguments, both bound and recorded as operands. The
    // inverse is two `^=` lines that together undo the forward op (XOR is
    // self-inverse: (a^b)^a^b = 0).
    //
    // Unlike OR, there is no `lazy` XOR branch to peel today — `operator^`
    // on qbool/qint returns the result type directly (qint_bitwise.hpp:87
    // and :108). If a future phase adds a lazy XorExpr wrapper, this
    // matcher will need the same anyOf(eager, lazy) peel the OR matcher
    // uses; until then, ignoringImplicit alone is sufficient.
    auto xor_call = cxxOperatorCallExpr(
        hasOverloadedOperatorName("^"),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("lhs"))),
        hasArgument(1, ignoringImplicit(declRefExpr().bind("rhs"))));

    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(ignoringImplicit(xor_call))
    ).bind("var");

    auto& pool = xor_callback_pool();
    pool.push_back(std::make_unique<XorCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
