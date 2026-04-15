// matcher_qbool_assign.cpp — qbool compound-assign matchers (PA-3 / PA-4).
//
// Registers two matchers that fire on `a ^= <rhs>;` statements (bare
// compound-assign expressions, not VarDecl initializers):
//
//   - PA-3 : `a ^= b;`      RHS is a DeclRefExpr (named variable)
//   - PA-4 : `a ^= <expr>;` RHS is any non-DeclRefExpr classical expression
//
// The two matchers are structurally disjoint by construction: PA-3 binds a
// DeclRefExpr RHS, PA-4 excludes it via `unless(declRefExpr())`, so no
// single CXXOperatorCallExpr can trigger both. Both share QOpKind::XOR_ASSIGN
// — the render switch embeds the RHS name verbatim regardless of whether it
// came from a DeclRefExpr or a Lexer::getSourceText extraction.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;
using detail::enclosing_compound_stmt;
using detail::find_or_create_scope;
using detail::make_ref;

// ── XorAssignCallback (PA-3) ─────────────────────────────────────────────────

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

// ── XorAssignClassicalCallback (PA-4) ────────────────────────────────────────

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
