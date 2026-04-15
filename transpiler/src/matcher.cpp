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
#include "clang/Basic/SourceLocation.h"

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
const CompoundStmt* enclosing_compound_stmt(const Decl& decl,
                                            ASTContext& ctx) {
    DynTypedNode node = DynTypedNode::create(decl);
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

} // namespace

void register_or_matcher(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit) {
    // Widened AST pattern (LP4) — see
    // `docs/implementation_plan_transpiler_matcher_lazy_peel.md` and the
    // LP2 AST calibration notes on issue sturm-ea7. Fires on both
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

} // namespace sturm::transpile
