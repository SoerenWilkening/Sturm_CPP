// matcher_hoist_invariant.cpp — Phase J PJ-3d uncompute-hoisting matcher.
//
// Purpose
// -------
// When a `qbool` intermediate is produced inside a loop and uncomputed at
// the end of the loop body, but the forward computation is loop-invariant
// (its operands do not change across iterations), the transpiler may hoist
// both the forward compute AND the uncompute out of the loop. The body
// then runs once (not N times), shrinking gate counts in proportion to the
// iteration count. See docs/roadmap_transpiler_post_mvp.md Phase J item 3.
//
// Detection algorithm
// -------------------
// This module is a POST-PROCESSOR: it runs once per translation unit,
// AFTER every Phase A–I per-op matcher has populated `unit.scopes`, and
// AFTER the PJ-1 CCNOT-fuse peephole and the PJ-4 dead-ancilla
// eliminator have settled. It hooks into `MatchFinder` via a
// `translationUnitDecl()` pattern to capture the ASTContext, then does
// the full pass inside `onEndOfTranslationUnit()`. This ordering is
// load-bearing: per-op matchers fire on VarDecls / CXXOperatorCallExprs
// that are visited AFTER their enclosing ForStmt/WhileStmt in the
// pre-order walk, so a ForStmt-anchored matcher would see empty scopes.
// Deferring to `onEndOfTranslationUnit` guarantees every matcher
// callback has finished before we iterate.
//
// On entry the pass iterates every QScope in `unit.scopes`:
//
//   1. Recover the scope anchor Stmt from the scope's `open_brace` raw
//      encoding. Braced scopes are keyed on the CompoundStmt's LBracLoc;
//      braceless (PH-1 synthetic) scopes are keyed on the body Stmt's
//      BeginLoc. A walker over the ASTContext resolves the raw encoding
//      back to the Stmt pointer.
//
//   2. Verify via `detail::classify_scope_kind` that the scope anchor is
//      a `LoopBody` (the body of a for/while whose parent is a
//      ForStmt/WhileStmt and whose anchor == parent->getBody()). If not
//      a LoopBody, skip.
//
//   3. Walk up from the scope anchor to find the enclosing ForStmt /
//      WhileStmt (the loop statement itself). The loop-begin location
//      is the pre-loop forward-compute anchor.
//
//   4. Walk further up from the loop statement to find the loop's
//      enclosing scope — a CompoundStmt (close brace = RBracLoc) or a
//      braceless body stmt (close brace = one-past-end-token). That
//      close brace is the post-loop uncompute anchor.
//
//   5. For each op in the loop body scope's ops vector:
//
//      - Skip if `op.skip_uncompute` is true. PH-3 sets this on compound-
//        assign mutations of outer vars; the hoist matcher must not
//        touch those ops. This guard is load-bearing: it preserves the
//        PH-3 disjointness invariant described in the roadmap.
//
//      - Skip if `op.kind` is NOT a decl-producing kind. Decl-producing
//        kinds are the shapes that introduce a named intermediate — OR,
//        AND, NOT, XOR, and the six compare kinds (EQ / NE / LT / LE /
//        GT / GE _QINT). Compound-assign kinds (XOR_ASSIGN, *_ASSIGN_*)
//        are NOT decl-producing; they mutate an existing variable and
//        hoisting them would change the program's semantics.
//
//      - Skip if ANY operand of the op is NOT loop-invariant per
//        `detail::expr_is_loop_invariant`. The probe returns true iff
//        every operand's decl_loc lives outside the loop body AND no
//        write to that decl is reachable inside the body. A single non-
//        invariant operand kills the hoist.
//
//      - Otherwise, set `op.hoist_to_override` to the loop-enclosing
//        scope's close_brace (post-loop uncompute anchor) AND
//        `op.insert_before_override` to the loop-begin location (pre-
//        loop forward-compute anchor). The M8 synthesis pass consumes
//        `hoist_to_override` at the uncompute-emission step (it takes
//        precedence over `insert_before_override` per uncompute_pass.cpp
//        PJ-3c documentation). The FORWARD compute is moved via a
//        matcher-owned text rewrite — but that rewrite is the PJ-3d+
//        downstream concern (PJ-3f fixtures land the full forward-move
//        cycle). Today PJ-3d only sets the override fields; the forward
//        text-move is scheduled as a future iteration.
//
// Registration order
// ------------------
// This matcher runs LAST — after every Phase A–I per-op matcher, after
// the PJ-1f CCX-fuse peephole, and after the PJ-4b dead-ancilla
// eliminator. Registration order for per-node callbacks matters only
// when they share AST nodes; this matcher post-processes via
// `onEndOfTranslationUnit`, which is called AFTER the entire traversal,
// so the LAST-registration convention is a diagnostic-grouping choice
// rather than a correctness requirement.
//
// PM2-5 Source-map attribution policy
// -----------------------------------
// Hoisting changes the INSERTION LOCATION of a synthesized uncompute
// (from the loop body's close brace to the loop-enclosing scope's
// close brace) but it does NOT change the op's SOURCE ATTRIBUTION.
// The `#line` directive the M8 synthesis pass prefixes onto the
// rendered uncompute text must point at `op.stmt_range.getBegin()`
// — the location of the user's ORIGINAL in-loop expression (e.g.
// the `qbool t = a | b;` on line N inside the loop body) — NOT at
// `op.hoist_to_override` (the post-loop close brace, which is
// merely the physical insertion point after hoisting).
//
// Rationale: when a hoisted uncompute raises a diagnostic or a
// debugger lands a step frame inside the synthesized text, the user
// expects to see their ORIGINAL in-loop expression's line — the
// thing they actually wrote — not the `}` that closes the loop's
// enclosing scope. The uncompute IS the structural dual of the
// in-loop expression even when its emitted text has been relocated.
//
// This matcher preserves the invariant by design: it ONLY writes to
// `op.hoist_to_override` and `op.insert_before_override`, and NEVER
// overwrites `op.stmt_range`. The `uncompute_pass.cpp` synthesis
// step then reads `op.stmt_range.getBegin()` for the `#line`
// derivation while independently selecting the `insert_before`
// anchor from `op.hoist_to_override`. Decoupling these two fields
// is what keeps the PM2-5 contract intact — any future refactor
// that tries to reuse one for both roles would collapse attribution
// onto the physical insertion point and break the user-facing
// diagnostic experience. Tests in `test_matcher_pm2_hoist_line.cpp`
// exercise this end-to-end.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_hoist_invariant_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// Test-only counter. Bumped once per successfully hoisted op (i.e. per
// op whose `hoist_to_override` / `insert_before_override` were set in
// this run). The transpiler is single-threaded so a plain int is fine.
static int g_hoist_invariant_detection_count = 0;

// Return true if `kind` is one of the decl-producing kinds the hoist
// matcher is allowed to hoist. Compound-assign kinds are explicitly
// excluded — they mutate an existing variable rather than producing a
// new intermediate, and hoisting them would change program semantics.
// USER_ROUTINE is also excluded (routines have side effects beyond
// their operand list that are not captured by the invariance probe),
// and CCNOT_INPLACE is the zero-ancilla fused in-place flip (not a
// decl-producing op in the hoist-eligible sense).
bool is_decl_producing_kind(QOpKind kind) {
    switch (kind) {
    case QOpKind::OR:
    case QOpKind::AND:
    case QOpKind::NOT:
    case QOpKind::XOR:
    case QOpKind::EQ_QINT:
    case QOpKind::NE_QINT:
    case QOpKind::LT_QINT:
    case QOpKind::LE_QINT:
    case QOpKind::GT_QINT:
    case QOpKind::GE_QINT:
        return true;
    // Compound-assigns mutate in place — hoisting them would change
    // program semantics. PH-3 already marks outer-var compound-assigns
    // with `skip_uncompute=true`, and the defensive skip-uncompute
    // guard in the main loop keeps those off the hoist path too.
    case QOpKind::XOR_ASSIGN:
    case QOpKind::ADD_ASSIGN_CONST:
    case QOpKind::SUB_ASSIGN_CONST:
    case QOpKind::MUL_ASSIGN_CONST:
    case QOpKind::DIV_ASSIGN_CONST:
    case QOpKind::ADD_ASSIGN_QINT:
    case QOpKind::SUB_ASSIGN_QINT:
    case QOpKind::MUL_ASSIGN_QINT:
    case QOpKind::DIV_ASSIGN_QINT:
    case QOpKind::MOD_ASSIGN_QINT:
        return false;
    // USER_ROUTINE has observable effects (outputs, side effects, ...)
    // that are not captured by the operand-only invariance probe.
    // Hoisting routine calls is a future-phase optimization.
    case QOpKind::USER_ROUTINE:
        return false;
    // CCNOT_INPLACE is the zero-ancilla fused flip — `x ^= __t;` where
    // `__t = a & b;` has been elided. The `x` operand is the target,
    // not a new intermediate, so the op is not decl-producing.
    case QOpKind::CCNOT_INPLACE:
        return false;
    }
    return false;
}

// Walker that builds a raw-encoding → Stmt* lookup table for every Stmt
// whose begin loc OR LBracLoc is a candidate QScope open_brace key. We
// index both CompoundStmt LBracLocs (braced scopes) AND any Stmt's
// begin loc (for PH-1 braceless body scopes). The walker makes a single
// pass per TU, so the O(N) cost amortizes cleanly across all scopes.
class ScopeAnchorIndex
    : public clang::RecursiveASTVisitor<ScopeAnchorIndex> {
public:
    // Index a CompoundStmt by its LBracLoc — the braced-scope key. This
    // is the same key `find_or_create_scope(unit, cs)` uses when
    // recording QScopes from braced bodies.
    bool VisitCompoundStmt(clang::CompoundStmt* cs) {
        if (!cs) return true;
        const clang::SourceLocation lb = cs->getLBracLoc();
        if (lb.isValid()) {
            table_[lb.getRawEncoding()] = cs;
        }
        return true;
    }

    // Index every Stmt by its begin loc — the braceless-body scope key.
    // `find_or_create_scope(unit, es, sm, lang)` with a braceless
    // EnclosingScope uses `body->getBeginLoc()` as the QScope key,
    // regardless of the body Stmt's concrete type (DeclStmt, ExprStmt,
    // ...). Indexing ALL Stmts covers every possible braceless body
    // shape. CompoundStmt also gets indexed here, but the
    // VisitCompoundStmt override above takes precedence via insert-
    // without-overwrite semantics when the same location appears twice
    // (the CompoundStmt's begin loc equals its LBracLoc, so both
    // visits produce the same raw encoding → same pointer).
    bool VisitStmt(clang::Stmt* s) {
        if (!s) return true;
        const clang::SourceLocation b = s->getBeginLoc();
        if (b.isValid()) {
            // Only insert if not already present — the CompoundStmt
            // visit above preferentially binds LBracLoc=BeginLoc keys
            // to the CompoundStmt pointer. A later Stmt whose BeginLoc
            // happens to share that raw encoding should not displace it.
            auto [it, inserted] = table_.insert({b.getRawEncoding(), s});
            (void)it;
            (void)inserted;
        }
        return true;
    }

    // Return the Stmt (or nullptr) indexed at `loc`'s raw encoding.
    clang::Stmt* lookup(clang::SourceLocation loc) const {
        if (!loc.isValid()) return nullptr;
        auto it = table_.find(loc.getRawEncoding());
        if (it == table_.end()) return nullptr;
        return it->second;
    }

private:
    std::unordered_map<std::uint32_t, clang::Stmt*> table_;
};

// Compute the loop-enclosing scope's close brace — the post-loop
// uncompute anchor. Walk up the parent chain from `loop` until we hit
// an enclosing CompoundStmt (braced block) or an enclosing braceless
// body stmt (for/while/if whose body IS our loop). Returns an invalid
// SourceLocation if no enclosing scope is found (should not happen for
// a well-formed loop at function scope, but the defensive return keeps
// the caller clean).
//
// This mirrors `detail::enclosing_scope` but is specialized to the
// loop-statement case: we start from the loop stmt itself (not a decl
// or nested stmt), and `prev_stmt` tracks it so the braceless-body
// detection can compare against the parent's body pointer.
SourceLocation compute_loop_enclosing_close_brace(
    const Stmt* loop,
    ASTContext& ctx,
    const SourceManager& sm,
    const LangOptions& lang) {
    if (!loop) return {};
    const Stmt* prev_stmt = loop;
    DynTypedNode node = DynTypedNode::create(*loop);
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return {};
        node = parents[0];

        // Enclosing braced block — close brace is its RBracLoc.
        if (const auto* cs = node.get<CompoundStmt>()) {
            return cs->getRBracLoc();
        }

        // Enclosing braceless body of a for/while/if — we hit one when
        // the parent is a ForStmt/WhileStmt/IfStmt whose body/then/else
        // stmt IS our `prev_stmt` AND that body is not a CompoundStmt.
        // The close brace in this case is one-past the body's end token
        // (same convention PH-1's find_or_create_scope uses).
        if (const auto* fs = node.get<ForStmt>()) {
            if (fs->getBody() == prev_stmt &&
                detail::is_user_braceless_body(fs->getBody())) {
                return Lexer::getLocForEndOfToken(
                    fs->getBody()->getEndLoc(), /*Offset=*/0, sm, lang);
            }
        } else if (const auto* ws = node.get<WhileStmt>()) {
            if (ws->getBody() == prev_stmt &&
                detail::is_user_braceless_body(ws->getBody())) {
                return Lexer::getLocForEndOfToken(
                    ws->getBody()->getEndLoc(), /*Offset=*/0, sm, lang);
            }
        } else if (const auto* is = node.get<IfStmt>()) {
            const Stmt* then_b = is->getThen();
            const Stmt* else_b = is->getElse();
            if ((then_b == prev_stmt &&
                 detail::is_user_braceless_body(then_b)) ||
                (else_b == prev_stmt &&
                 detail::is_user_braceless_body(else_b))) {
                const Stmt* chosen =
                    (then_b == prev_stmt) ? then_b : else_b;
                return Lexer::getLocForEndOfToken(
                    chosen->getEndLoc(), /*Offset=*/0, sm, lang);
            }
        }

        if (const auto* as_stmt = node.get<Stmt>()) {
            prev_stmt = as_stmt;
        }
    }
    return {};
}

// Walk up the parent chain from `scope_anchor` to find the enclosing
// ForStmt / WhileStmt. The scope anchor is the loop body Stmt; its
// immediate parent should be the loop stmt. Returns nullptr if the
// parent is not a for/while (defensive — classify_scope_kind should
// have already vouched for this).
const Stmt* find_enclosing_loop(const Stmt* scope_anchor,
                                ASTContext& ctx) {
    if (!scope_anchor) return nullptr;
    const auto parents = ctx.getParents(DynTypedNode::create(*scope_anchor));
    if (parents.empty()) return nullptr;
    const DynTypedNode parent = parents[0];
    if (const auto* fs = parent.get<ForStmt>()) {
        if (fs->getBody() == scope_anchor) return fs;
    }
    if (const auto* ws = parent.get<WhileStmt>()) {
        if (ws->getBody() == scope_anchor) return ws;
    }
    return nullptr;
}

// The matcher's main callback. Anchors on `translationUnitDecl()` only
// to capture the `ASTContext&` in `run()`. The real work happens in
// `onEndOfTranslationUnit()` after every other matcher callback has
// fired. Using `translationUnitDecl()` as the anchor is a clang-tidy-
// style post-processing idiom (see `clang-tidy/performance/
// UnnecessaryValueParamCheck` for a canonical example).
class HoistInvariantCallback : public MatchFinder::MatchCallback {
public:
    explicit HoistInvariantCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        // Cache the ASTContext pointer. MatchFinder guarantees `run()`
        // fires at least once per TU before `onEndOfTranslationUnit()`
        // (our anchor `translationUnitDecl()` always matches), so the
        // saved context is valid when `onEndOfTranslationUnit()`
        // executes.
        if (r.Context) ctx_ = r.Context;
    }

    void onEndOfTranslationUnit() override {
        if (!ctx_ || !unit_) return;
        ASTContext& ctx = *ctx_;
        const SourceManager& sm = ctx.getSourceManager();
        const LangOptions& lang = ctx.getLangOpts();

        // Build the scope-anchor lookup table once. Single AST walk —
        // avoids redoing the walk per QScope.
        ScopeAnchorIndex index;
        index.TraverseAST(ctx);

        // Iterate every QScope. For loop bodies whose operands are all
        // invariant, set the hoist anchors per-op. We MUST NOT hold a
        // pointer into `unit_->scopes` across the loop body — other
        // scopes may be added by this pass in future iterations (not
        // today, but the access pattern should be robust) — so we
        // index by position.
        for (std::size_t si = 0; si < unit_->scopes.size(); ++si) {
            QScope& scope = unit_->scopes[si];
            const Stmt* scope_anchor = index.lookup(scope.open_brace);
            if (!scope_anchor) continue;
            // LoopBody gate: classify_scope_kind walks one parent hop
            // and returns LoopBody only when the parent is a for/while
            // whose body == scope_anchor. Branch bodies (if/else),
            // WHEN bodies, function bodies, and bare nested blocks
            // fall into BranchBody / WhenBody / Function / Other and
            // are skipped here.
            if (detail::classify_scope_kind(scope_anchor, ctx) !=
                detail::ScopeKind::LoopBody) {
                continue;
            }

            // Recover the loop stmt itself from the scope anchor's
            // parent. The classify_scope_kind check above guarantees
            // the parent IS a for/while whose body is our scope
            // anchor; find_enclosing_loop replays the same hop and
            // returns the loop stmt pointer.
            const Stmt* loop = find_enclosing_loop(scope_anchor, ctx);
            if (!loop) continue;

            // Forward-compute anchor: loop begin loc (the `for` / `while`
            // keyword's spelling).
            const SourceLocation loop_begin_loc = loop->getBeginLoc();
            // Uncompute anchor: the close brace of the scope that
            // contains the LOOP itself (one level up from the loop body).
            const SourceLocation enclosing_close_brace =
                compute_loop_enclosing_close_brace(loop, ctx, sm, lang);
            if (!loop_begin_loc.isValid() ||
                !enclosing_close_brace.isValid()) {
                continue;
            }

            // Per-op gating. Mutate `unit_->scopes[si].ops[oi]` directly;
            // the hoist overrides are per-op, not per-scope, so mixing
            // hoist-eligible and non-hoist-eligible ops in the same
            // scope is explicitly supported.
            for (std::size_t oi = 0; oi < scope.ops.size(); ++oi) {
                QOperation& op = scope.ops[oi];

                // PH-3 disjointness guard — load-bearing. PH-3 marks
                // compound-assigns on outer vars with
                // `skip_uncompute=true`; the hoist matcher must NOT
                // touch those ops. Even though compound-assigns are
                // NOT decl-producing (the kind check below would also
                // reject them), the skip-uncompute guard is a per-op
                // flag that can in principle fire on any future kind,
                // so we check it first to stay future-proof.
                if (op.skip_uncompute) continue;

                // Kind guard: only decl-producing ops are hoistable.
                if (!is_decl_producing_kind(op.kind)) continue;

                // Operand guard: every operand must be loop-invariant.
                // A single non-invariant operand kills the hoist. Empty
                // operand lists (defensive, should not happen for the
                // kinds we accept) are considered trivially invariant —
                // but they would also fail the downstream uncompute
                // rendering, so the practical effect is nil.
                bool all_invariant = true;
                for (const QValueRef& operand : op.operands) {
                    if (!detail::expr_is_loop_invariant(
                            operand, scope_anchor, ctx)) {
                        all_invariant = false;
                        break;
                    }
                }
                if (!all_invariant) continue;

                // All guards passed — hoist. Set both anchor fields on
                // the op. The M8 synthesis pass consumes
                // `hoist_to_override` at uncompute-emission time; the
                // forward-compute text move anchored by
                // `insert_before_override` is a downstream PJ-3d+
                // concern (PJ-3f wires the forward rewrite via a
                // matcher-owned QReplacement — not landed in PJ-3d).
                op.hoist_to_override      = enclosing_close_brace;
                op.insert_before_override = loop_begin_loc;
                ++g_hoist_invariant_detection_count;
            }
        }
    }

private:
    QUnit*               unit_;
    // MatchFinder::MatchResult::Context is an `ASTContext*` (non-const).
    // Cache the mutable pointer so the `onEndOfTranslationUnit()` pass
    // can invoke helpers (`classify_scope_kind`, `expr_is_loop_invariant`,
    // `getParents`) that take a non-const `ASTContext&`.
    clang::ASTContext*   ctx_ = nullptr;
};

std::vector<std::unique_ptr<HoistInvariantCallback>>&
hoist_invariant_callback_pool() {
    static std::vector<std::unique_ptr<HoistInvariantCallback>> pool;
    return pool;
}

} // namespace sturm_matcher_hoist_invariant_anon_ns
using namespace sturm_matcher_hoist_invariant_anon_ns;

void register_hoist_invariant_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    auto& pool = hoist_invariant_callback_pool();
    pool.push_back(std::make_unique<HoistInvariantCallback>(&unit));
    HoistInvariantCallback* cb = pool.back().get();

    // Anchor on `translationUnitDecl()` — matches exactly once per TU,
    // giving `run()` a chance to cache the ASTContext. The real work
    // happens in `onEndOfTranslationUnit()`. Using this anchor is the
    // clang-tidy idiom for post-processing passes that need per-TU
    // context (see performance/UnnecessaryValueParamCheck).
    finder.addMatcher(translationUnitDecl().bind("tu"), cb);
}

int hoist_invariant_detection_count_for_test() {
    return g_hoist_invariant_detection_count;
}

void reset_hoist_invariant_detection_count_for_test() {
    g_hoist_invariant_detection_count = 0;
}

} // namespace sturm::transpile
