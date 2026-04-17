// matcher_qbool_compound.cpp — Phase E / PE-4 compound qbool bitwise matcher.
//
// Recognizes compound-expression qbool VarDecl initializers — depth >= 2
// shapes like `qbool r = (b | c) & d;`, `qbool r = (b & c) | d;`, and
// `qbool r = (a | b) | (c | d);`. Every leaf operand must be a bare
// DeclRefExpr to a named qbool; every interior node is a
// CXXOperatorCallExpr on `|` or `&`. On match, the callback flattens the
// expression tree: each interior sub-expression becomes one QOperation
// with a fresh `__stu_t<N>` result, and the outermost node keeps the
// original VarDecl's name. The LIFO uncompute pass on QScope
// (uncompute_pass.cpp) emits the reverse-order `uncompute_{and,or}` calls
// at scope close for free.
//
// Disjointness from the MVP OR matcher: the MVP requires BOTH outer-call
// arguments to be bare DeclRefExprs; this matcher requires AT LEAST ONE
// argument (after paren+impl-cast peel) to itself be a CXXOperatorCallExpr
// on `|` or `&`. Mutually exclusive, so no VarDecl double-binds.
//
// Output contract
//   (1) One `QReplacement` over the original VarDecl's source range
//       containing the flat decl sequence (without trailing `;` on the
//       final line — the original source's semicolon lies just past the
//       VarDecl range and stays in place verbatim).
//   (2) One `QOperation` per flattened sub-expression, appended to the
//       enclosing QScope in source-post-order (deepest first). All ops
//       share the outer VarDecl's source range so `uncompute_pass.cpp`'s
//       LIFO sort keeps them adjacent at scope close.
//
// Implementation notes
//   - `ignoringImplicit` does not peel `ParenExpr`, so every argument peel
//     goes through `ignoringParenImpCasts`.
//   - One `FreshNameAllocator` per callback (per registration / per QUnit)
//     — `__stu_t<N>` names stay monotonic across every compound VarDecl
//     in one translation unit (fresh_names.hpp:17 "per-QUnit" rule).

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "fresh_names.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;
// The compound-flatten helpers (peel_to_payload, op_kind_for, flatten_arg,
// flatten_inner_call, render_decl_line) live in `detail` in
// matcher_common.hpp since Phase F PF-0, alongside the existing
// enclosing_scope / find_or_create_scope / make_ref helpers. We
// reach them via fully-qualified names below; no `using` for the flatten
// helpers, so it stays obvious which calls land in shared code.
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::is_range_covered_by_fused;
using detail::make_ref;

class CompoundQBoolCallback : public MatchFinder::MatchCallback {
public:
    explicit CompoundQBoolCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        if (!var || !r.Context) return;

        // Phase J PJ-1e: the PJ-1d ccnot-fuse peephole records the
        // second-stmt range of each fused `qbool __t = a & b; x ^= __t;`
        // pair in `unit_->fused_stmt_ranges`. The compound matcher's
        // own anchor shape — a VarDecl whose initializer is a nested
        // bitwise op-call — is structurally disjoint from PJ-1d's
        // bare-DRE operand requirement, so in practice a VarDecl
        // match never overlaps a fused second-stmt range. The guard
        // is still applied to keep the matcher family uniform and to
        // be defensive against future PJ-1 relaxations that widen the
        // fuse pattern (e.g. a hypothetical three-stmt fuse whose
        // first stmt is a compound VarDecl). The check is constant-
        // time when `fused_stmt_ranges` is empty — every
        // pre-Phase-J snapshot stays byte-identical.
        if (is_range_covered_by_fused(var->getSourceRange(),
                                      unit_->fused_stmt_ranges,
                                      r.Context->getSourceManager())) {
            return;
        }

        // Peel the VarDecl initializer to reach the outermost op-call.
        const Expr* init = var->getInit();
        const Expr* outer = detail::peel_to_payload(init);
        const auto* outer_call = dyn_cast_or_null<CXXOperatorCallExpr>(outer);
        auto outer_kind = detail::op_kind_for(outer_call);
        if (!outer_call || !outer_kind || outer_call->getNumArgs() != 2) {
            return;
        }

        // Enclosing scope — required for scope / close_brace lookup.
        // Phase H PH-1: transparently supports both braced CompoundStmt
        // and braceless for/while/if/else body positions.
        const auto es = enclosing_scope(*var, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_ = r.Context->getSourceManager();
        const LangOptions& lang_ = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_, lang_);

#ifndef NDEBUG
        // Guard against the same VarDecl binding twice (the matcher pattern
        // uses anyOf across several peels; a future widening could
        // accidentally produce duplicates). If any existing op in this
        // scope already carries `var`'s decl location as its result, bail.
        const auto var_key = var->getLocation().getRawEncoding();
        for (const auto& existing : scope.ops) {
            assert(existing.result.decl_loc.getRawEncoding() != var_key &&
                   "CompoundQBoolCallback double-matched the same VarDecl");
        }
#endif

        // Flatten both arguments of the outer op-call. For each, emit
        // intermediate flat decls + QOperations into the scope and collect
        // the leaf/temp names.
        const SourceRange stmt_range = var->getSourceRange();
        std::vector<std::string> flat_lines;
        // Preserve the scope's pre-existing op count so we can unwind on a
        // reject and avoid polluting the IR with partial flattenings.
        const std::size_t ops_high_water = scope.ops.size();

        std::string lhs_name = detail::flatten_arg(
            outer_call->getArg(0), scope, names_, stmt_range, flat_lines);
        std::string rhs_name;
        if (!lhs_name.empty()) {
            rhs_name = detail::flatten_arg(
                outer_call->getArg(1), scope, names_, stmt_range, flat_lines);
        }
        if (lhs_name.empty() || rhs_name.empty()) {
            // Partial match — roll back any ops appended so far.
            if (scope.ops.size() > ops_high_water) {
                scope.ops.resize(ops_high_water);
            }
            return;
        }

        // Disjointness from the MVP OR matcher: when neither argument
        // produced an intermediate (flat_lines stays empty), the init
        // is a single, non-nested op-call like `qbool r = b | c;` —
        // which the MVP matcher_qbool_bitwise.cpp callback already
        // handles. Bail without emitting a flat decl so the two
        // matchers do not double-bind the same VarDecl.
        if (flat_lines.empty()) {
            if (scope.ops.size() > ops_high_water) {
                scope.ops.resize(ops_high_water);
            }
            return;
        }

        // Outermost op: result is the original VarDecl name, operands are
        // the two collected (possibly-temp) names.
        QOperation outer_op;
        outer_op.kind = *outer_kind;
        outer_op.result.name     = var->getNameAsString();
        outer_op.result.decl_loc = var->getLocation();

        // Rebuild operand QValueRefs with decl_loc where possible, mirroring
        // the logic in flatten_inner_call. This keeps the IR uniform with
        // what the MVP OR / AND matcher would have produced.
        auto build_ref = [&](const Expr* a, const std::string& name) {
            QValueRef ref;
            ref.name = name;
            const Expr* inner = detail::peel_to_payload(a);
            if (const auto* dre = dyn_cast_or_null<DeclRefExpr>(inner)) {
                if (const NamedDecl* nd = dre->getDecl()) {
                    ref.decl_loc = nd->getLocation();
                }
            }
            return ref;
        };
        outer_op.operands.push_back(
            build_ref(outer_call->getArg(0), lhs_name));
        outer_op.operands.push_back(
            build_ref(outer_call->getArg(1), rhs_name));
        outer_op.stmt_range = stmt_range;
        scope.ops.push_back(std::move(outer_op));

        // Build the flat replacement text. The intermediate decls each end
        // with a trailing `;` + newline + four-space indent; the outer
        // decl is emitted WITHOUT a trailing `;` because the original
        // source's semicolon lies just past `stmt_range`'s end — the
        // Rewriter preserves it verbatim and we would otherwise double it.
        std::ostringstream body;
        for (const auto& line : flat_lines) {
            body << line << "\n    ";
        }
        body << detail::render_decl_line(var->getNameAsString(), *outer_kind,
                                         lhs_name, rhs_name,
                                         /*include_semicolon=*/false);

        QReplacement rep;
        rep.range       = stmt_range;
        rep.replacement = body.str();
        unit_->replacements.push_back(std::move(rep));
    }

private:
    QUnit* unit_;
    FreshNameAllocator names_;
};

// Callback pool — same pattern as matcher_qint_compare.cpp:136-140.
std::vector<std::unique_ptr<CompoundQBoolCallback>>& compound_callback_pool() {
    static std::vector<std::unique_ptr<CompoundQBoolCallback>> pool;
    return pool;
}

} // namespace

void register_compound_qbool_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    // The inner op-call shape — a bitwise `|` or `&` overloaded-operator
    // call. Deliberately keep the arg guards loose (no DRE requirement):
    // the callback's flatten_arg walker recursively re-peels and will
    // reject anything non-structural.
    auto nested_bitwise_call = cxxOperatorCallExpr(
        hasAnyOverloadedOperatorName("|", "&"),
        argumentCountIs(2));

    // Helper: a bitwise op-call argument that may be wrapped in a
    // user-defined conversion `OrExpr<qbool>::operator qbool()` /
    // `AndExpr<qbool>::operator qbool()` (the lazy_expr.hpp shape used
    // under STURM_BACKEND_ENABLED). Matches either the bare op-call or
    // the conversion-wrapped form. Both shapes are valid nested-arg
    // candidates for the compound matcher. `ignoringImplicit` peels
    // ImplicitCastExpr, MaterializeTemporaryExpr and CXXBindTemporaryExpr
    // — the wrapper trio that wraps the `OrExpr<qbool>` lvalue between
    // its `operator qbool()` member call and its producing `|`/`&`
    // CXXOperatorCallExpr.
    auto nested_or_lazy_wrapped = anyOf(
        nested_bitwise_call,
        cxxMemberCallExpr(on(ignoringImplicit(nested_bitwise_call))));

    // Outer op-call: `|` or `&` with AT LEAST ONE argument that (after
    // implicit-node peel) is itself a nested bitwise op-call OR a
    // user-defined-conversion call wrapping a nested op-call. The anyOf
    // covers both "nested on the left" and "nested on the right" shapes;
    // the "nested on both" case is covered by either branch matching.
    auto outer_bitwise_call = cxxOperatorCallExpr(
        hasAnyOverloadedOperatorName("|", "&"),
        argumentCountIs(2),
        anyOf(
            hasArgument(0, ignoringImplicit(nested_or_lazy_wrapped)),
            hasArgument(1, ignoringImplicit(nested_or_lazy_wrapped))));

    // Three initializer shapes can reach a qbool VarDecl whose RHS is a
    // compound `(b|c) & d`-style expression:
    //   (a) direct  — the outer op-call sits under the VarDecl init,
    //                  modulo implicit wrappers (hermetic fixture
    //                  shape, where `operator&` returns `qbool`);
    //   (b) ctor-wrapped — same as (a) but with an elidable copy ctor
    //                       (mirrors Phase D comparator at
    //                       matcher_qint_compare.cpp:163-183);
    //   (c) lazy-wrapped — under STURM_BACKEND_ENABLED `operator&`
    //                       returns `AndExpr<qbool>`, materialized via
    //                       `AndExpr<qbool>::operator qbool()`. The init
    //                       chain is:
    //                         ExprWithCleanups
    //                         → ImplicitCastExpr<UserDefinedConversion>
    //                         → CXXMemberCallExpr (operator qbool())
    //                         → ImplicitCastExpr<NoOp>
    //                         → MaterializeTemporaryExpr
    //                         → CXXOperatorCallExpr `&`
    //                       — same shape the MVP OR matcher peels in
    //                       matcher_qbool_bitwise.cpp:223-224.
    auto direct_init  = ignoringImplicit(outer_bitwise_call);
    auto ctor_wrapped = ignoringImplicit(
        cxxConstructExpr(has(ignoringImplicit(outer_bitwise_call))));
    auto lazy_wrapped = ignoringImplicit(
        cxxMemberCallExpr(on(ignoringImplicit(outer_bitwise_call))));

    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(anyOf(direct_init, ctor_wrapped, lazy_wrapped))
    ).bind("var");

    auto& pool = compound_callback_pool();
    pool.push_back(std::make_unique<CompoundQBoolCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
