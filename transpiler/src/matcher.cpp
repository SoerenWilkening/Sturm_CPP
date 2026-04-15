// matcher.cpp — M7 AST matcher implementation.
//
// Registers exactly one Clang ASTMatcher that fires on any VarDecl of the
// form `qbool tmp = a | b;` and appends a QOperation{kind=OR, ...} to the
// matching QScope in the caller-supplied QUnit.
//
// The matcher's surface is narrow on purpose — see matcher.hpp for the
// rationale. Anything not covered by the single AST pattern below is
// silently skipped, which is the MVP contract.
//
// Scope bookkeeping
// -----------------
// We key QScopes by the raw encoding of the enclosing CompoundStmt's opening
// brace (`L_BRACE`). This has two advantages over keying by the CompoundStmt
// pointer:
//
//   - It matches what the M9 emitter will use later (SourceLocation is the
//     only identity the emitter has when inserting text).
//   - It is stable across AST traversal: the same CompoundStmt seen twice
//     (e.g. when multiple matches fire inside it) resolves to the same key.
//
// On the first match inside a compound statement we create a new QScope
// populated with the block's brace locations; subsequent matches in the
// same block reuse it. Scopes are stored in insertion order, which is
// source order because MatchFinder walks the AST depth-first top-to-bottom.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Walk up the parent chain of `node` until we find the immediately enclosing
// CompoundStmt. Returns nullptr if none exists (e.g. a declaration at
// namespace scope, which cannot be the MVP pattern anyway). Uses the dynamic
// ParentMapContext because Decl / Stmt do not carry parent pointers.
//
// Two overloads are provided: initializer-based matchers (PA-1/PA-2) enter
// via a VarDecl, while statement-based matchers (PA-3 onward) enter via an
// Expr/Stmt. Both funnel through the same DynTypedNode walk.
template <typename T>
const CompoundStmt* enclosing_compound_stmt_impl(const T& n, ASTContext& ctx) {
    DynTypedNode node = DynTypedNode::create(n);
    while (true) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return nullptr;
        // We deliberately follow only the first parent. Clang's
        // ParentMapContext occasionally yields multiple parents for template
        // instantiations, but the MVP matcher does not run inside templated
        // contexts (the class-name matcher does not bind dependent types).
        node = parents[0];
        if (const auto* cs = node.get<CompoundStmt>()) return cs;
    }
}

const CompoundStmt* enclosing_compound_stmt(const Decl& decl,
                                            ASTContext& ctx) {
    return enclosing_compound_stmt_impl(decl, ctx);
}

const CompoundStmt* enclosing_compound_stmt(const Stmt& stmt,
                                            ASTContext& ctx) {
    return enclosing_compound_stmt_impl(stmt, ctx);
}

// Locate the QScope in `unit` whose open_brace matches `cs`, creating one at
// the end of `unit.scopes` if none exists. The raw encoding of the opening
// brace is a stable scope identity within a single translation unit.
QScope& find_or_create_scope(QUnit& unit, const CompoundStmt& cs) {
    const auto key = cs.getLBracLoc().getRawEncoding();
    for (auto& scope : unit.scopes) {
        if (scope.open_brace.getRawEncoding() == key) return scope;
    }
    QScope fresh;
    fresh.open_brace  = cs.getLBracLoc();
    fresh.close_brace = cs.getRBracLoc();
    unit.scopes.push_back(std::move(fresh));
    return unit.scopes.back();
}

// Extract a QValueRef from a DeclRefExpr. The decl_loc is the referenced
// declaration's location (NOT the call-site DeclRefExpr's location), which
// is what QValueRef equality uses to discriminate shadowed locals.
QValueRef make_ref(const DeclRefExpr& dre) {
    QValueRef ref;
    const NamedDecl* nd = dre.getDecl();
    if (nd) {
        ref.name     = nd->getNameAsString();
        ref.decl_loc = nd->getLocation();
    }
    return ref;
}

// ── MatchCallback ────────────────────────────────────────────────────────────

class OrCallback : public MatchFinder::MatchCallback {
public:
    explicit OrCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* lhs = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!var || !lhs || !rhs || !r.Context) return;

        // Locate the enclosing compound statement. A VarDecl that is NOT
        // inside a CompoundStmt is not something the uncompute pass can
        // handle (no close-brace anchor point), so we drop it silently.
        const CompoundStmt* cs =
            enclosing_compound_stmt(*var, *r.Context);
        if (!cs) return;

        QScope& scope = find_or_create_scope(*unit_, *cs);

        // Debug-build guard (Risk R3, LP4 §"Optional hardening"): the widened
        // `anyOf(eager_init, lazy_init)` pattern is mutually exclusive by
        // overload resolution (eager returns qbool, lazy returns
        // OrExpr<qbool>), but a pathological reshuffle could still cause one
        // VarDecl to bind twice. Assert that we have not already emitted an
        // op for this var's declaration location in the current QScope.
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

// The callback lifetime must match the MatchFinder's. We keep a single
// heap-allocated OrCallback per QUnit and leak it on program exit: the
// MatchFinder stores raw pointers to callbacks, and the caller cannot be
// expected to manage lifetime of an internal-only helper. The leak is
// bounded by the number of times `register_or_matcher` is called per
// process (usually exactly one).
//
// We store the callback in a side container whose lifetime matches the
// program's. Each call to register_or_matcher appends a new one; callbacks
// are never freed. For a one-shot transpiler tool this is the right
// trade-off.
std::vector<std::unique_ptr<OrCallback>>& callback_pool() {
    static std::vector<std::unique_ptr<OrCallback>> pool;
    return pool;
}

// ── NotCallback (Phase A / PA-1) ────────────────────────────────────────────

class NotCallback : public MatchFinder::MatchCallback {
public:
    explicit NotCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var     = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* operand = r.Nodes.getNodeAs<DeclRefExpr>("operand");
        if (!var || !operand || !r.Context) return;

        const CompoundStmt* cs =
            enclosing_compound_stmt(*var, *r.Context);
        if (!cs) return;

        QScope& scope = find_or_create_scope(*unit_, *cs);

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

// ── XorCallback (Phase A / PA-2) ────────────────────────────────────────────

class XorCallback : public MatchFinder::MatchCallback {
public:
    explicit XorCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* lhs = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!var || !lhs || !rhs || !r.Context) return;

        const CompoundStmt* cs =
            enclosing_compound_stmt(*var, *r.Context);
        if (!cs) return;

        QScope& scope = find_or_create_scope(*unit_, *cs);

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

// ── XorAssignCallback (Phase A / PA-3) ──────────────────────────────────────

class XorAssignCallback : public MatchFinder::MatchCallback {
public:
    explicit XorAssignCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs  = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!call || !lhs || !rhs || !r.Context) return;

        // XOR-assign is a statement, not an initializer, so we walk
        // up from the call expression rather than from a VarDecl.
        const CompoundStmt* cs =
            enclosing_compound_stmt(*call, *r.Context);
        if (!cs) return;

        QScope& scope = find_or_create_scope(*unit_, *cs);

        // No debug-build dedupe guard here: the same variable can be
        // legitimately `^=`'d multiple times in a scope, so keying off
        // result.decl_loc (as OrCallback does) would misfire. If we ever
        // need protection against the same statement matching twice, the
        // natural key is the call's source location, not the target's.

        QOperation op;
        op.kind   = QOpKind::XOR_ASSIGN;
        op.result = make_ref(*lhs);
        op.operands.push_back(make_ref(*rhs));
        op.stmt_range = call->getSourceRange();

        scope.ops.push_back(std::move(op));
    }

private:
    QUnit* unit_;
};

std::vector<std::unique_ptr<XorAssignCallback>>&
xor_assign_callback_pool() {
    static std::vector<std::unique_ptr<XorAssignCallback>> pool;
    return pool;
}

// ── XorAssignClassicalCallback (Phase A / PA-4) ─────────────────────────────

class XorAssignClassicalCallback : public MatchFinder::MatchCallback {
public:
    explicit XorAssignClassicalCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs  = r.Nodes.getNodeAs<Expr>("rhs_expr");
        if (!call || !lhs || !rhs || !r.Context) return;

        const CompoundStmt* cs =
            enclosing_compound_stmt(*call, *r.Context);
        if (!cs) return;

        QScope& scope = find_or_create_scope(*unit_, *cs);

        // Extract the verbatim source text of the classical RHS. The
        // Lexer token range covers the RHS's own tokens without picking
        // up surrounding punctuation; `1` becomes "1", `x & y` becomes
        // "x & y". The bound node has already been ignoringImplicit-
        // peeled, so implicit casts do not contaminate the text.
        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lo = r.Context->getLangOpts();
        auto text = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(rhs->getSourceRange()),
            sm, lo);
        if (text.empty()) return;

        QValueRef rhs_ref;
        rhs_ref.name = text.str();
        // rhs_ref.decl_loc intentionally left invalid — there is no
        // declaration for a literal / classical expression, and the
        // emitter only needs the name field.

        QOperation op;
        op.kind   = QOpKind::XOR_ASSIGN;
        op.result = make_ref(*lhs);
        op.operands.push_back(std::move(rhs_ref));
        op.stmt_range = call->getSourceRange();

        scope.ops.push_back(std::move(op));
    }

private:
    QUnit* unit_;
};

std::vector<std::unique_ptr<XorAssignClassicalCallback>>&
xor_assign_classical_callback_pool() {
    static std::vector<std::unique_ptr<XorAssignClassicalCallback>> pool;
    return pool;
}

} // namespace

void register_or_matcher(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit) {
    // Widened AST pattern (LP4) — see
    // `docs/archive/implementation_plan_transpiler_matcher_lazy_peel.md`
    // and the LP2 AST calibration notes on issue sturm-ea7. Fires on both
    // initializer shapes `examples/or_circuit.cpp` can take:
    //   (a) EAGER: `operator|(...) -> qbool`. VarDecl initializer is the
    //       CXXOperatorCallExpr itself (modulo implicit glue). MVP shape.
    //   (b) LAZY:  `operator|(...) -> OrExpr<qbool>`, materialized via
    //       `OrExpr<qbool>::operator qbool()`. LP2 AST chain:
    //         VarDecl → ExprWithCleanups
    //                 → ImplicitCastExpr<UserDefinedConversion>
    //                 → CXXMemberCallExpr (operator qbool())
    //                 → ImplicitCastExpr<NoOp>
    //                 → MaterializeTemporaryExpr
    //                 → CXXOperatorCallExpr '|'
    //       `ignoringImplicit` peels all of the above (incl. the
    //       UserDefinedConversion cast and MaterializeTemporaryExpr).
    //       N.B. no CXXConstructExpr on this chain — PRD sketch was off.
    // Both branches bind the same `lhs`/`rhs`/`var` names; OrCallback::run
    // is unchanged. Branches are mutually exclusive by overload resolution
    // (eager returns qbool; lazy returns OrExpr<qbool>).
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

    auto& pool = callback_pool();
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
    // Only the direct (non-lazy) form is matched at PA-1: `~` does not go
    // through OrExpr / AndExpr / lazy materialization — it is always eager
    // (the operator returns qbool directly, allocating an ancilla and
    // emitting X; see qbool_ops.hpp:141-153). No UserDefinedConversion
    // peeling is needed here, unlike register_or_matcher's lazy branch.
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

void register_xor_assign_matcher(clang::ast_matchers::MatchFinder& finder,
                                 QUnit& unit) {
    // Phase A / PA-3: match `a ^= b;` where both a and b are named
    // variables with overloaded `operator^=`. This form matches only when
    // RHS is a DeclRefExpr; classical literal RHS (e.g. `a ^= 1;`) is a
    // PA-4 responsibility with its own IR flavor and matcher.
    //
    // The statement-scope walk uses enclosing_compound_stmt(Stmt, ctx),
    // not the Decl overload — there is no VarDecl to anchor against
    // because `^=` mutates an existing variable rather than introducing
    // a new one.
    //
    // Note: the builtin `bool`/`int` `^=` does NOT produce a
    // CXXOperatorCallExpr (it lowers to CompoundAssignOperator), so the
    // cxxOperatorCallExpr matcher inherently rejects classical paths
    // without an explicit type guard.
    auto pattern = cxxOperatorCallExpr(
        hasOverloadedOperatorName("^="),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("lhs"))),
        hasArgument(1, ignoringImplicit(declRefExpr().bind("rhs")))
    ).bind("call");

    auto& pool = xor_assign_callback_pool();
    pool.push_back(std::make_unique<XorAssignCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

void register_xor_assign_classical_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    // Phase A / PA-4: match `a ^= <expr>;` where the RHS is any
    // classical expression (literal, compound expression) whose
    // post-implicit-cast form is NOT a DeclRefExpr. This is disjoint
    // from PA-3 by construction: PA-3 binds a DeclRefExpr RHS, PA-4
    // excludes it via `unless(declRefExpr())`. The same CXXOperatorCallExpr
    // never triggers both matchers.
    //
    // The bound node is the post-peel inner Expr (so an `ImplicitCast(1)`
    // binds to the IntegerLiteral `1` itself, and Lexer::getSourceText
    // returns "1" rather than a cast-wrapped rendering).
    auto pattern = cxxOperatorCallExpr(
        hasOverloadedOperatorName("^="),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("lhs"))),
        hasArgument(1, ignoringImplicit(
            expr(unless(declRefExpr())).bind("rhs_expr")))
    ).bind("call");

    auto& pool = xor_assign_classical_callback_pool();
    pool.push_back(std::make_unique<XorAssignClassicalCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
