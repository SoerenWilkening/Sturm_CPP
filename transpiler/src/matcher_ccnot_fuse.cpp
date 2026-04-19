// matcher_ccnot_fuse.cpp — Phase J PJ-1d zero-ancilla fusion peephole.
//
// Recognises the adjacent pair
//
//   qbool __t = a & b;   // VarDecl init = `&` op-call, bare DREs
//   x ^= __t;            // `^=` op-call on qbool target, RHS = __t
//
// when `__t` has exactly one reader within its enclosing scope (the
// `^=` RHS itself). On fusion the matcher collapses the pair into a
// single call
//
//   ccnot_inplace(x, a, b);
//
// emitted as a `QReplacement` spanning both statements. A matching
// `QOperation{kind=CCNOT_INPLACE, result=x, operands=[a, b]}` is
// appended to the enclosing QScope so the M8 uncompute pass emits a
// second `ccnot_inplace(x, a, b);` at scope close — CCX is self-
// adjoint, so the helper's forward and uncompute emissions share the
// same identifier. See `QOpKind::CCNOT_INPLACE` in qir.hpp and the
// PJ-1c render case in uncompute_pass.cpp.
//
// Rejection rules (every rejected pair stays on the runtime path; the
// Phase E compound-flatten matcher picks the unmodified VarDecl up):
//
//   - Nested init — `qbool __t = (a | b) & c;` — is compound-flatten
//     territory; this matcher requires bare DeclRefExpr operands on
//     the `&` op-call.
//   - Reader-count != 1 — zero readers are a dead-ancilla case
//     (PJ-4a), two-or-more readers would lose a use on fusion.
//   - Next stmt is not `x ^= __t` — the adjacent stmt must be a `^=`
//     CXXOperatorCallExpr whose DRE-peeled RHS is the temp's own decl.
//   - No next stmt — the VarDecl is the last stmt in its CompoundStmt;
//     nothing to fuse with.
//
// The matcher is ORDERING-SENSITIVE with the Phase E compound-flatten
// matcher: registering it BEFORE the compound matcher is the PJ-1f
// wiring rationale — on a successful fuse this matcher's
// `QReplacement` replaces the original VarDecl source text with the
// `ccnot_inplace(...)` call before the compound matcher would fire.
// The fused pair's `fused_stmt_ranges` bookkeeping (PJ-1e) prevents
// the downstream `matcher_qbool_assign.cpp` / `matcher_qbool_compound.cpp`
// from double-emitting over the replaced region.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Casting.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;
using detail::count_readers_in_scope;
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::make_ref;

// Peel through `IgnoreParenImpCasts` (matches the XorAssign matchers'
// peel depth — `a ^= __t;`'s AST shape does not require a heavier
// `peel_to_payload` walk here: PJ-1d only looks up a plain DeclRefExpr
// operand on a XorAssign, not through any wrapper/materialization
// layer.
const DeclRefExpr* peel_to_dre(const Expr* e) {
    if (!e) return nullptr;
    return dyn_cast_or_null<DeclRefExpr>(e->IgnoreParenImpCasts());
}

// Walk the enclosing CompoundStmt's body to locate the statement that
// immediately follows `anchor`. Returns nullptr if `anchor` is not a
// direct child of any CompoundStmt OR is the last child. The walk
// intentionally does NOT descend through braceless bodies — a
// `for (...) qbool __t = a & b;` shape has no "adjacent next stmt" at
// all (the entire `for` body IS the VarDecl). PJ-1d's peephole only
// fires on two-statement sequences inside a braced block; braceless
// cases fall through to the Phase E compound-flatten matcher.
const Stmt* next_sibling_in_compound(const VarDecl& var,
                                     ASTContext& ctx) {
    // Climb from the VarDecl to the owning DeclStmt (VarDecls sit
    // under a DeclStmt in a CompoundStmt body). Parents() on the
    // VarDecl gives us the DeclStmt directly.
    DynTypedNode current = DynTypedNode::create(var);
    for (int hops = 0; hops < 8; ++hops) {
        const auto parents = ctx.getParents(current);
        if (parents.empty()) return nullptr;
        current = parents[0];
        if (const auto* ds = current.get<DeclStmt>()) {
            // Now climb from the DeclStmt to the enclosing CompoundStmt.
            const auto cs_parents = ctx.getParents(*ds);
            if (cs_parents.empty()) return nullptr;
            const auto* cs = cs_parents[0].get<CompoundStmt>();
            if (!cs) return nullptr;
            // Scan the body for the matching DeclStmt; the immediate
            // next sibling is our anchor.
            bool found = false;
            for (const Stmt* child : cs->body()) {
                if (found) return child;
                if (child == ds) found = true;
            }
            return nullptr; // ds was the last stmt.
        }
    }
    return nullptr;
}

// Check whether `s` is `<qbool_lhs> ^= <dre_to_target_var>;`. Returns
// the matched (lhs DeclRefExpr, rhs DeclRefExpr) pair on success, or a
// `{nullptr, nullptr}` pair on reject. The classical-RHS `^=` overload
// produces a CXXOperatorCallExpr too, but we key off the RHS decl_loc
// matching `target_var`'s location to filter that branch out.
struct XorPair {
    const DeclRefExpr* lhs = nullptr;
    const DeclRefExpr* rhs = nullptr;
};

XorPair match_xor_assign_consumer(const Stmt* s, const VarDecl& target_var) {
    XorPair out;
    if (!s) return out;
    const auto* call = dyn_cast<CXXOperatorCallExpr>(s);
    if (!call) return out;
    if (call->getOperator() != OO_CaretEqual) return out;
    if (call->getNumArgs() != 2) return out;
    const DeclRefExpr* lhs_dre = peel_to_dre(call->getArg(0));
    if (!lhs_dre) return out;
    const DeclRefExpr* rhs_dre = peel_to_dre(call->getArg(1));
    if (!rhs_dre) return out;
    // RHS must refer to the target VarDecl (the `__t` we just saw). Key
    // on decl_loc equality — same discriminator the PJ-1a reader-count
    // helper uses to disambiguate shadowed locals.
    const NamedDecl* rhs_nd = rhs_dre->getDecl();
    if (!rhs_nd) return out;
    if (rhs_nd->getLocation() != target_var.getLocation()) return out;
    out.lhs = lhs_dre;
    out.rhs = rhs_dre;
    return out;
}

class CCNotFuseCallback : public MatchFinder::MatchCallback {
public:
    explicit CCNotFuseCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* lhs_dre = r.Nodes.getNodeAs<DeclRefExpr>("and_lhs");
        const auto* rhs_dre = r.Nodes.getNodeAs<DeclRefExpr>("and_rhs");
        if (!var || !lhs_dre || !rhs_dre || !r.Context) return;

        ASTContext& ctx = *r.Context;
        const SourceManager& sm = ctx.getSourceManager();
        const LangOptions& lang = ctx.getLangOpts();

        // Locate the adjacent next stmt in the enclosing CompoundStmt.
        // Braceless bodies have no "adjacent" stmt (the body IS the
        // VarDecl), so those shapes fall through to Phase E unchanged.
        const Stmt* next = next_sibling_in_compound(*var, ctx);
        if (!next) return;

        // Confirm the next stmt is `x ^= __t;` with RHS referring to
        // our VarDecl.
        const XorPair xp = match_xor_assign_consumer(next, *var);
        if (!xp.lhs || !xp.rhs) return;

        // Reader-count gate — exactly one reader of __t in the
        // enclosing scope. A reader-count of 1 means the `^=`'s RHS is
        // the SOLE use of __t; fusion preserves semantics. 0 readers
        // (dead ancilla) or ≥2 readers would lose a use.
        //
        // Anchor the count on the owning CompoundStmt so readers in
        // nested blocks / sibling braceless bodies still contribute;
        // the helper descends through every shape by design.
        const auto es = enclosing_scope(*var, ctx);
        if (!es.valid()) return;
        // The fuse peephole is structurally scoped to braced bodies —
        // braceless cases have no "next sibling" concept. Guard on
        // CompoundStmt so we bail cleanly if the scope-finder lands
        // on a braceless body (should not happen because
        // `next_sibling_in_compound` already rejected that path, but
        // the explicit check documents the invariant).
        if (es.kind != detail::QScopeKind::CompoundStmt) return;

        const int reader_count = count_readers_in_scope(
            llvm::StringRef(var->getName()),
            var->getLocation(),
            es.compound,
            ctx);
        if (reader_count != 1) return;

        // All guards passed — commit the fusion.

        // Build the target name (`x`) from the `^=` LHS DRE.
        const NamedDecl* target_nd = xp.lhs->getDecl();
        if (!target_nd) return;
        const std::string target_name = target_nd->getNameAsString();
        const std::string a_name = lhs_dre->getDecl() ?
            lhs_dre->getDecl()->getNameAsString() : std::string{};
        const std::string b_name = rhs_dre->getDecl() ?
            rhs_dre->getDecl()->getNameAsString() : std::string{};
        if (target_name.empty() || a_name.empty() || b_name.empty()) return;

        // Compute the fused source range: begin at the VarDecl's
        // source begin, end at the `^=` CXXOperatorCallExpr's source
        // end (the `;` of the `^=` stmt lies JUST past this range —
        // Lexer::getLocForEndOfToken would push past it, but the
        // CXXOperatorCallExpr's getSourceRange() does not include the
        // trailing `;`). We explicitly compute the end-of-token loc
        // so the replacement range covers the terminating `;` of the
        // second stmt as well; without that step the `;` would linger
        // in the output after the `ccnot_inplace(...)` text and
        // produce `ccnot_inplace(x, a, b);;`.
        const SourceRange var_range = var->getSourceRange();
        const SourceLocation xor_end = clang::Lexer::getLocForEndOfToken(
            next->getEndLoc(),
            /*Offset=*/0, sm, lang);
        // Lexer returns the loc one-past-the-end-token. For the
        // Rewriter's `ReplaceText(SourceRange, ...)` we want a
        // SourceRange whose end lands ON the last character we still
        // want to overwrite — which is the `;`. Walk back one token
        // location: the `;` sits just before `xor_end`. In practice
        // we can pass the `;` loc directly by letting the Rewriter
        // token-range semantics take over — we use
        // `CharSourceRange::getCharRange(begin, xor_end)` semantics
        // by specifying a range whose END is `xor_end` ... but
        // `QReplacement.range` is a SourceRange (token-range). To
        // keep the contract simple we grab the end loc of the last
        // token (including the `;`) via getLocForEndOfToken applied
        // to the `;` itself. The cleanest way: probe for the `;`
        // via `findNextToken` — but the simpler observation is that
        // `next->getEndLoc()` already points at the final token of
        // the `^=` op-call (the RHS DRE); the trailing `;` sits one
        // token further. `Lexer::findNextToken` gives us that `;`.
        //
        // However, since `QReplacement.range` uses token-range semantics
        // (ReplaceText on a SourceRange), we need the END to be the
        // SourceLocation of the LAST TOKEN we want to replace. We
        // therefore search forward for the `;` token and use its loc.
        std::optional<clang::Token> semi = clang::Lexer::findNextToken(
            next->getEndLoc(), sm, lang);
        if (!semi || semi->getKind() != clang::tok::semi) {
            // Missing or unexpected terminator — bail rather than
            // emit a partial replacement that would corrupt the
            // user's file.
            return;
        }
        SourceRange fused_range(var_range.getBegin(), semi->getLocation());

        // Stage the replacement. Replacement text is `ccnot_inplace(x,
        // a, b);` with no trailing newline — the original source's
        // own layout is preserved around the substituted region.
        QReplacement rep;
        rep.range = fused_range;
        rep.replacement =
            std::string("ccnot_inplace(") + target_name + ", "
            + a_name + ", " + b_name + ");";
        unit_->replacements.push_back(std::move(rep));

        // Stage the QOperation for the uncompute-pass LIFO schedule.
        QScope& scope = find_or_create_scope(*unit_, es, sm, lang);

        QOperation op;
        op.kind = QOpKind::CCNOT_INPLACE;
        op.result = make_ref(*xp.lhs);   // the `x` target (decl_loc pinned)
        op.operands.push_back(make_ref(*lhs_dre));
        op.operands.push_back(make_ref(*rhs_dre));
        // `stmt_range` drives the reverse walk's LIFO sort. Using the
        // fused range's begin loc keeps this op ordered in the same
        // source position the original `^=` stmt occupied.
        op.stmt_range = fused_range;
        scope.ops.push_back(std::move(op));

        // PJ-1e: record the SECOND stmt's range (the `x ^= __t;` op-call,
        // terminating-`;` included) so the downstream Phase A `^=` and
        // Phase E compound matchers can early-return on any match whose
        // own stmt-range lies inside this entry. `next->getSourceRange()`
        // stops at the RHS DRE (Clang does not include the trailing `;`
        // in a CXXOperatorCallExpr range), so we extend the end to the
        // `;` location already computed above. The Phase A xor-assign
        // matcher records its QOperation's stmt_range as
        // `call->getSourceRange()` — which is exactly the same opening
        // span `next->getBeginLoc()` covers — so the containment probe
        // catches it.
        clang::SourceRange second_range(next->getBeginLoc(),
                                        semi->getLocation());
        unit_->fused_stmt_ranges.push_back(second_range);
    }

private:
    QUnit* unit_;
};

std::vector<std::unique_ptr<CCNotFuseCallback>>& ccnot_fuse_callback_pool() {
    static std::vector<std::unique_ptr<CCNotFuseCallback>> pool;
    return pool;
}

} // namespace

void register_ccnot_fuse_matcher(clang::ast_matchers::MatchFinder& finder,
                                 QUnit& unit) {
    // Match the canonical `qbool __t = a & b;` shape:
    //   - VarDecl of type qbool.
    //   - Initializer (after implicit-cast peel) is a CXXOperatorCallExpr
    //     on `&` with argumentCountIs(2).
    //   - Both arguments (after implicit-cast peel) are bare DeclRefExprs.
    //
    // Structurally disjoint from the Phase E compound-flatten matcher:
    // that matcher requires AT LEAST ONE argument (after peel) to itself
    // be a nested op-call, while this matcher requires BOTH arguments
    // to be bare DeclRefExprs. The two patterns are mutually exclusive
    // on any single VarDecl.
    auto and_call = cxxOperatorCallExpr(
        hasOverloadedOperatorName("&"),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("and_lhs"))),
        hasArgument(1, ignoringImplicit(declRefExpr().bind("and_rhs"))));

    // Two initializer shapes reach a `qbool __t = a & b;` VarDecl:
    //   (a) direct op-call — `operator&` on qbool returns an owning
    //       `qbool` directly (post-PK-2: the pre-PK lazy wrapper path
    //       was retired, and `operator&` in qbool_ops.hpp / qbool_logic.hpp
    //       now always hands back a `qbool` value; see also
    //       include/sturm/control/when.hpp:40-43 for the matching
    //       retirement note). The VarDecl's init is the
    //       CXXOperatorCallExpr itself modulo implicit glue. This is
    //       the shape produced by the snapshot fixtures under
    //       tests/transpiler/fixtures/fuse_xor_and*.cpp and by every
    //       live build profile (backend and non-backend alike).
    //   (b) wrapped-via-conversion — if some future fixture or header
    //       re-introduces a wrapper type whose `operator qbool()` feeds
    //       the VarDecl (AST chain:
    //         VarDecl → ExprWithCleanups
    //                 → ImplicitCastExpr<UserDefinedConversion>
    //                 → CXXMemberCallExpr (operator qbool())
    //                 → ImplicitCastExpr<NoOp>
    //                 → MaterializeTemporaryExpr
    //                 → CXXOperatorCallExpr '&'),
    //       we peel through the CXXMemberCallExpr the same way the MVP
    //       OR matcher does in matcher_qbool_bitwise.cpp:258-264. No
    //       in-tree build currently exercises this branch — it is kept
    //       as a defensive fallback so the PJ-1d peephole doesn't
    //       silently regress if a lazy shape re-enters the tree.
    auto eager_init = ignoringImplicit(and_call);
    auto lazy_init  = ignoringImplicit(
        cxxMemberCallExpr(on(ignoringImplicit(and_call))));

    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(anyOf(eager_init, lazy_init))
    ).bind("var");

    auto& pool = ccnot_fuse_callback_pool();
    pool.push_back(std::make_unique<CCNotFuseCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

// Phase J PJ-1e: post-matcher cleanup pass — removes every QOperation
// whose stmt_range lies inside some entry of `unit.fused_stmt_ranges`.
// See `matcher.hpp`'s `apply_fused_stmt_guards` docstring for the full
// rationale (MatchFinder's Decl/Stmt visit-pool interleaving means a
// Stmt-anchored PA-3 callback can fire before the Decl-anchored PJ-1d
// callback that populates `fused_stmt_ranges`; this pass restores the
// invariant after `matchAST` completes).
//
// The pass also scans `raw_insertions` entries, but none of the current
// matchers stage raw insertions whose anchor is inside a fused range, so
// that branch is a belt-and-braces guard only.
void apply_fused_stmt_guards(QUnit& unit, const clang::SourceManager& sm) {
    if (unit.fused_stmt_ranges.empty()) return;  // fast path

    for (auto& scope : unit.scopes) {
        auto& ops = scope.ops;
        // Walk in-place; erase any op whose stmt_range is covered AND
        // whose kind is NOT itself a fuse product (CCNOT_INPLACE's
        // own stmt_range is the fused pair's begin-to-`;` range, which
        // WOULD satisfy the containment check — but removing that op
        // is exactly the opposite of what PJ-1e wants). Keep
        // CCNOT_INPLACE; remove anything else that the fuse replacement
        // would shadow.
        ops.erase(
            std::remove_if(
                ops.begin(), ops.end(),
                [&](const QOperation& op) {
                    if (op.kind == QOpKind::CCNOT_INPLACE) return false;
                    return detail::is_range_covered_by_fused(
                        op.stmt_range, unit.fused_stmt_ranges, sm);
                }),
            ops.end());
    }
}

} // namespace sturm::transpile
