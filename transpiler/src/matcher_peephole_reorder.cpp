// matcher_peephole_reorder.cpp — Phase M PM5-5 peephole gate-reordering
// matcher.
//
// Purpose
// -------
// The PJ-1d / PJ-1f zero-ancilla fusion peephole collapses the adjacent
// pair
//     qbool __t = a & b;   // QOpKind::AND with synthetic result __t
//     x ^= __t;            // QOpKind::XOR_ASSIGN consuming __t
// into a single `ccnot_inplace(x, a, b);` call. The fusion requires the
// two statements to be IMMEDIATELY ADJACENT in the enclosing
// CompoundStmt, because the fuse matcher walks the next sibling of the
// VarDecl to find the XOR_ASSIGN. If a THIRD statement B sits between
// the AND-decl and the XOR_ASSIGN, fusion is blocked — even when B's
// operands are bit-disjoint from A's result and C's operands, which
// means B and C structurally commute and B could legally be moved past
// C to restore adjacency.
//
// PM5 is the consumer that performs that commutation. It walks each
// scope's op list in source order, identifies every adjacent triple
// (A, B, C) that satisfies the pattern, proves disjointness via the
// PM5-1..PM5-3 alias extractor, and emits a QReplacement that rewrites
// `A; B; C;` into `A; C; B;` — leaving (A, C) adjacent in both the IR
// and the rewritten source text so the downstream fusion path can
// absorb them. The matcher does NOT invoke the fuse itself; that's
// PJ-1d's job on the next transpile invocation. PM5's contract is
// strictly "make (A, C) adjacent when safe, without altering any gate's
// semantics."
//
// Detection algorithm
// -------------------
// PM5 is a POST-PROCESSOR: it anchors on `translationUnitDecl()` only
// to capture the `ASTContext&` in `run()`, then does the real work
// inside `onEndOfTranslationUnit()` after every other matcher's
// callbacks have completed. This is the same idiom
// `matcher_hoist_invariant.cpp` uses (PJ-3d). It is load-bearing: a
// per-op anchor would observe scope ops as they were *being* populated,
// not as they are at the end of the pass, and many guards (fuse
// ranges, eliminated ranges, hoist overrides) are only settled after
// every matcher callback has fired.
//
// On entry the pass iterates every QScope in `unit.scopes`:
//
//   1. Skip scopes whose `classify_scope_kind` is NOT one of
//      `Function` / `LoopBody`. Branch / WHEN bodies present alignment
//      hazards (runtime-disjoint aliasing, early returns) the footprint-
//      only analysis cannot see; the matcher refuses those scopes
//      entirely. See §12 Sharp edge 2 in the PM5 plan.
//
//   2. Walk `QScope.ops` by index, considering every adjacent triple
//      (ops[i], ops[i+1], ops[i+2]) for i in [0, ops.size()-3]:
//
//      - Gate 1 — kind shape. ops[i].kind must be AND with a synthetic
//        result name (starts with `__stu_t`). ops[i+2].kind must be
//        XOR_ASSIGN, and its first operand must match ops[i].result
//        (by name AND decl_loc). ops[i+1].kind must NOT be PLUGIN or
//        USER_ROUTINE — those are opaque to the footprint extractor
//        (see §12 Sharp edge 3 / §12 Sharp edge 6).
//
//      - Gate 2 — hoist / fuse / eliminated guards. No op in the triple
//        may have `hoist_to_override` valid (commuting past a hoisted
//        anchor breaks PJ-3's pairing invariant). None of the three
//        ops' stmt_ranges may be covered by `unit.fused_stmt_ranges`
//        (PJ-1 has already fused that region) or
//        `unit.eliminated_stmt_ranges` (PJ-4 has already deleted that
//        decl). Uses `detail::is_range_covered_by_fused` for both
//        lists.
//
//      - Gate 3 — footprint disjointness. Extract footprints for A's
//        result, every operand of B, and every operand of C (except
//        the already-matched `__t` read). Call `may_overlap()`
//        pairwise. If B may overlap A's result OR any of C's other
//        operands, bail — the reorder is unsafe.
//
//      - Gate 4 — fuse precondition. Once B is moved past C, the
//        resulting (A, C) pair must match PJ-1d's fuse trigger:
//        __t has exactly one reader in the enclosing scope. Reuses
//        `detail::count_readers_in_scope`; non-1 reader counts mean
//        the reorder has no payoff and we bail.
//
//   3. On all four gates passing, emit ONE `QReplacement` whose source
//      range spans B's statement AND C's statement (inclusive, with
//      trailing `;`), whose replacement text is `C's source text` then
//      a separator then `B's source text`. The B text is prefixed with
//      a `#line` directive via `format_line_directive()` (PM2-4) so
//      any diagnostic originating inside the relocated B statement
//      cites the user's original B line, not the Rewriter's physical
//      position. After the replacement lands the IR's op list for
//      this scope is rewritten to reflect the new ordering; the
//      triple indexer advances past the consumed range so the same
//      triple is not re-evaluated.
//
// Design decisions
// ----------------
// Single-pass, not fixed-point. One pass over each scope's op list.
// If a reorder exposes a new triple (e.g. a previously-hidden (A', B',
// C') where B was between them), it does not land in the same run.
// This is deliberate: the PM5 plan (§5) explicitly refuses fixed-point
// iteration because (a) the test surface stays finite (no runtime
// divergence on pathological inputs), (b) the second-order opportunities
// are rare in practice, and (c) if a user's source produces a cascade
// that truly benefits, re-transpiling the emitted output would cover
// the follow-up case without any PM5 code change.
//
// Scope-kind guard. LoopBody / Function only. BranchBody (if/else) and
// WhenBody (WHEN macro expansions) are structurally harder — a `^=`
// that appears in BOTH arms of an if/else is not sequential with the
// AND-decl in the same way a linear scope is. The matcher refuses
// those scopes entirely rather than attempting a half-safe analysis
// that footprint-only reasoning cannot back up.
//
// No plugin-op commutation. PLUGIN-kind ops have no footprint hook in
// v1; every operand collapses to the universal sentinel, so
// may_overlap() returns true unconditionally against them. Rather than
// stage an analysis that would always reject, we short-circuit Gate 1.
// Same rationale for USER_ROUTINE ops, whose bodies are not re-analysed
// and whose operand footprints are not reliably disjoint from unrelated
// operands.
//
// Registration order (PM5-6 concern, NOT this issue)
// --------------------------------------------------
// PM5 is a post-processor that must run LAST in
// `transpile_consumer.cpp` — after every Phase A..I per-op matcher,
// after PJ-1d (ccnot fuse), PJ-4a (dead-ancilla), and PJ-3d (hoist
// invariant). Running last guarantees the matcher observes the FINAL
// op list after every optimization pass has settled. This wiring is
// PM5-6's deliverable and is NOT landed in PM5-5.
//
// Source-map attribution (PM2-5 contract)
// ---------------------------------------
// When the matcher rewrites `A; B; C;` into `A; C; B;`, the moved B
// statement carries a `#line` directive that points back at its
// ORIGINAL source line. The user's compile-error experience on any
// diagnostic originating inside the relocated B still cites the
// original line. The same contract PJ-1d / PJ-3d already preserve for
// their own rewrites.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/alias.hpp"
// PM2-4: pulls in `format_line_directive` so the moved B statement in
// the reorder replacement can be prefixed with a `#line` directive
// anchored at B's original begin loc — source-map emission for PM5-5
// peephole-reorder replacements.
#include "sturm/transpile/emitter.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
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
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Test-only counter. Bumped once per successfully reordered triple
// (i.e. per QReplacement the matcher pushes). The transpiler is
// single-threaded so a plain int is fine, same pattern as the
// PJ-3d / PH-3 / PF-2 test counters.
static int g_peephole_reorder_detection_count = 0;

// Prefix that identifies a synthetic temporary produced by the
// transpiler (Phase E compound-flatten, fresh_names.hpp). A user who
// writes their own `__stu_t` prefix is outside the PM5 contract —
// matching by prefix is the same convention the fuse peephole inherits
// from the compound-flatten matcher, and collisions would be a user
// error not a PM5 regression.
constexpr llvm::StringLiteral kSyntheticPrefix = "__stu_t";

// Return true iff `kind` is a kind the reorder matcher is willing to
// commute B past. Rejects:
//   - PLUGIN: the Registry has no footprint hook in v1; every operand
//     would collapse to the universal sentinel and the Gate 3 check
//     would refuse anyway. Short-circuiting here keeps the cost low.
//   - USER_ROUTINE: routine bodies are not re-analysed; a routine's
//     operand mask flags which operands are written, but we cannot
//     prove disjointness of the routine's internal footprint against
//     A.result / C operands without a deeper analysis. §12 Sharp
//     edge 6 pins the conservative refusal.
//
// Every other kind is acceptable — the Gate 3 footprint disjointness
// check on B's operands (and A.result) decides whether the actual
// triple is safe. A / C kinds themselves are filtered earlier by Gate
// 1; this helper exists to short-circuit the B slot.
bool b_kind_is_reorderable(QOpKind kind) {
    switch (kind) {
    case QOpKind::PLUGIN:
    case QOpKind::USER_ROUTINE:
        return false;
    default:
        return true;
    }
}

// Walker that builds a raw-encoding → Stmt* lookup table for every
// Stmt whose begin loc OR LBracLoc is a candidate QScope open_brace
// key. Mirrors `ScopeAnchorIndex` in matcher_hoist_invariant.cpp
// verbatim (one local walker per TU), so the reorder matcher can
// recover the scope anchor Stmt from a QScope's `open_brace` raw
// encoding without re-walking the AST per scope.
//
// We could alternatively have promoted the walker into
// matcher_common.hpp, but the PJ-3d walker is already tightly coupled
// to its own iteration (it caches the Stmt* per raw encoding across
// the entire TU and the table is O(N) on size). Mirroring the shape
// locally keeps the PM5 TU self-contained and avoids a header
// refactor. A future consolidation issue could promote it.
class ScopeAnchorIndex
    : public clang::RecursiveASTVisitor<ScopeAnchorIndex> {
public:
    bool VisitCompoundStmt(clang::CompoundStmt* cs) {
        if (!cs) return true;
        const clang::SourceLocation lb = cs->getLBracLoc();
        if (lb.isValid()) {
            table_[lb.getRawEncoding()] = cs;
        }
        return true;
    }

    bool VisitStmt(clang::Stmt* s) {
        if (!s) return true;
        const clang::SourceLocation b = s->getBeginLoc();
        if (b.isValid()) {
            auto [it, inserted] = table_.insert({b.getRawEncoding(), s});
            (void)it;
            (void)inserted;
        }
        return true;
    }

    clang::Stmt* lookup(clang::SourceLocation loc) const {
        if (!loc.isValid()) return nullptr;
        auto it = table_.find(loc.getRawEncoding());
        if (it == table_.end()) return nullptr;
        return it->second;
    }

private:
    std::unordered_map<std::uint32_t, clang::Stmt*> table_;
};

// Walk the lookup table to classify `scope_anchor` as LoopBody /
// Function / other. Returns true iff the scope is one the reorder
// pass is willing to consider. Branch / WHEN / nested-block scopes
// are refused per §12 Sharp edge 2.
bool scope_is_reorderable(const clang::Stmt* scope_anchor,
                          clang::ASTContext& ctx) {
    if (!scope_anchor) return false;
    const detail::ScopeKind kind =
        detail::classify_scope_kind(scope_anchor, ctx);
    return kind == detail::ScopeKind::LoopBody ||
           kind == detail::ScopeKind::Function;
}

// Extend a stmt_range to include the trailing `;` semicolon. The
// Clang AST's SourceRange for a CXXOperatorCallExpr or VarDecl does
// NOT include the terminating `;` (that token is part of the enclosing
// DeclStmt / ExprStmt wrapper). To compute a replacement range that
// covers the whole statement — so the Rewriter deletes / relocates
// exactly what the user wrote — we walk forward one token from the
// range's end via `Lexer::findNextToken` and check that the next
// token is indeed a `;`. Returns the extended range on success, or a
// default-constructed SourceRange on failure (missing terminator,
// unexpected token shape).
//
// Mirrors the range-extension idiom used in
// `matcher_ccnot_fuse.cpp` (fused_range) and
// `matcher_dead_ancilla.cpp` (decl_stmt_range). Kept local to avoid
// promoting another helper into matcher_common.hpp.
clang::SourceRange extend_range_to_semi(clang::SourceRange range,
                                        const clang::SourceManager& sm,
                                        const clang::LangOptions& lang) {
    if (!range.isValid()) return {};
    std::optional<clang::Token> semi = clang::Lexer::findNextToken(
        range.getEnd(), sm, lang);
    if (!semi || semi->getKind() != clang::tok::semi) {
        return {};
    }
    return clang::SourceRange(range.getBegin(), semi->getLocation());
}

// Extract the verbatim source text of a token-range SourceRange. The
// range endpoints must lie in the main file (not a macro expansion)
// or the Lexer returns an empty StringRef; the caller treats empty
// text as an extraction failure and refuses the reorder rather than
// producing a truncated replacement.
std::string get_source_text_of_range(clang::SourceRange range,
                                     const clang::SourceManager& sm,
                                     const clang::LangOptions& lang) {
    if (!range.isValid()) return {};
    llvm::StringRef text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(range),
        sm, lang);
    return text.str();
}

// Gate 3 helper. Compute the disjointness predicate between A's
// result footprint and every relevant operand of B and C. Returns
// true IFF the reorder is safe (no footprint overlap is possible).
//
// Inputs:
//   - `a_result_fp` : the footprint of A's result (the `__t`).
//   - `b_ops`       : B's operand QValueRefs.
//   - `c_ops_extra` : C's operand QValueRefs EXCEPT the already-
//                     matched `__t` read (which the caller filters).
//   - `ctx`         : AST context for the extractor.
//
// Overlap semantics: the reorder commutes B past C. Two disjointness
// constraints must hold simultaneously:
//   - B's operands must not overlap A's result. If they did, moving
//     B past C (which writes A's result via the XOR_ASSIGN) would
//     change what B reads / writes.
//   - B's operands must not overlap any of C's other operands. If
//     they did, the reorder would swap a conflicting read/write
//     ordering between B and C.
// Both constraints are a single pairwise sweep: for every pair
// (b_op, target_fp) where target_fp ∈ {a_result_fp} ∪ c_ops_extra,
// if may_overlap(footprint(b_op), target_fp) returns true we bail.
bool b_is_disjoint(const detail::QubitFootprint& a_result_fp,
                   const std::vector<QValueRef>& b_ops,
                   const std::vector<detail::QubitFootprint>& c_ops_extra,
                   const clang::ASTContext& ctx) {
    for (const QValueRef& b_op : b_ops) {
        const detail::QubitFootprint b_fp = detail::footprint(b_op, ctx);
        // Check against A's result.
        if (detail::may_overlap(b_fp, a_result_fp)) return false;
        // Check against every other C operand footprint.
        for (const detail::QubitFootprint& c_fp : c_ops_extra) {
            if (detail::may_overlap(b_fp, c_fp)) return false;
        }
    }
    return true;
}

// Gate 2 helper. Returns true iff `op`'s `stmt_range` is covered by
// any entry in `fused_ranges` OR `eliminated_ranges`. The probe is
// the same `detail::is_range_covered_by_fused` helper PJ-1e and
// PJ-4a's backstops consume — it accepts any `std::vector<SourceRange>`,
// not just fused ranges specifically.
bool op_is_already_processed(const QOperation& op,
                             const QUnit& unit,
                             const clang::SourceManager& sm) {
    if (op.hoist_to_override.isValid()) return true;
    if (detail::is_range_covered_by_fused(
            op.stmt_range, unit.fused_stmt_ranges, sm)) {
        return true;
    }
    if (detail::is_range_covered_by_fused(
            op.stmt_range, unit.eliminated_stmt_ranges, sm)) {
        return true;
    }
    return false;
}

// The matcher's main callback. Anchors on `translationUnitDecl()` only
// to capture the `ASTContext&` in `run()`. The real work happens in
// `onEndOfTranslationUnit()` after every other matcher callback has
// fired. Mirrors `HoistInvariantCallback` in
// `matcher_hoist_invariant.cpp` — same post-processor idiom.
class PeepholeReorderCallback : public MatchFinder::MatchCallback {
public:
    explicit PeepholeReorderCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        // Cache the ASTContext pointer. MatchFinder guarantees `run()`
        // fires at least once per TU before `onEndOfTranslationUnit()`
        // (our anchor `translationUnitDecl()` always matches), so the
        // saved context is valid when the terminal phase executes.
        if (r.Context) ctx_ = r.Context;
    }

    void onEndOfTranslationUnit() override {
        if (!ctx_ || !unit_) return;
        clang::ASTContext& ctx = *ctx_;
        const clang::SourceManager& sm = ctx.getSourceManager();
        const clang::LangOptions& lang = ctx.getLangOpts();

        // Build the scope-anchor lookup table once. Single AST walk —
        // avoids redoing the walk per QScope.
        ScopeAnchorIndex index;
        index.TraverseAST(ctx);

        // Iterate every QScope in source order. We index by position
        // because we do NOT hold a pointer into unit_->scopes across
        // the inner loop (some future iteration may add scopes, and
        // the access pattern should be robust to reallocation).
        for (std::size_t si = 0; si < unit_->scopes.size(); ++si) {
            QScope& scope = unit_->scopes[si];

            // Scope-kind gate. Only LoopBody / Function scopes are
            // reorderable. Branch / WHEN / nested-block scopes are
            // refused per §12 Sharp edge 2. A null anchor (e.g. a
            // synthetic scope that does not back any Stmt) is also
            // refused — defensive.
            const clang::Stmt* scope_anchor = index.lookup(scope.open_brace);
            if (!scope_is_reorderable(scope_anchor, ctx)) continue;

            // Walk adjacent triples by index. We need i+2 < size so
            // every triple (i, i+1, i+2) is complete. The bound check
            // uses subtraction carefully: a size < 3 means no triple
            // exists, and the loop condition below handles the empty
            // case cleanly.
            if (scope.ops.size() < 3) continue;

            const std::size_t last_i_plus_one = scope.ops.size() - 2;
            for (std::size_t i = 0; i + 2 < scope.ops.size(); ++i) {
                (void)last_i_plus_one;
                const QOperation& A = scope.ops[i];
                const QOperation& B = scope.ops[i + 1];
                const QOperation& C = scope.ops[i + 2];

                // ── Gate 1 — kind shape ────────────────────────────
                //
                // A must be an AND with a synthetic result name.
                // Non-AND kinds (OR / NOT / XOR / comparison / etc.)
                // are not fuse candidates for PJ-1d, so their
                // reorder has no payoff. The synthetic-name check
                // ensures we do not accidentally reorder a
                // user-named `qbool t = a & b;` that downstream
                // code might re-use — PJ-1d's reader-count gate
                // would reject the fuse anyway, but bailing early
                // keeps the matcher's attention on the intended
                // slice.
                if (A.kind != QOpKind::AND) continue;
                if (A.result.name.empty()) continue;
                if (!llvm::StringRef(A.result.name)
                         .startswith(kSyntheticPrefix)) {
                    continue;
                }

                // C must be XOR_ASSIGN with its first operand
                // naming A's result. The XOR_ASSIGN operation
                // records one operand (the RHS of `x ^= rhs`); the
                // LHS is stored in op.result. A matching triple is
                // one where `operands[0]` refers to the same VarDecl
                // A produced.
                if (C.kind != QOpKind::XOR_ASSIGN) continue;
                if (C.operands.empty()) continue;
                const QValueRef& c_first_op = C.operands[0];
                if (c_first_op.name != A.result.name) continue;
                // Decl-loc equality: belt-and-braces against shadowed
                // same-named locals. Two different `__t` temps (from
                // different invocations of Phase E flattening) would
                // have different decl_locs, and we refuse to reorder
                // across them.
                if (c_first_op.decl_loc.isValid() &&
                    A.result.decl_loc.isValid() &&
                    c_first_op.decl_loc.getRawEncoding() !=
                        A.result.decl_loc.getRawEncoding()) {
                    continue;
                }

                // B must be a reorderable kind (not PLUGIN / USER_
                // ROUTINE). PLUGIN ops are opaque in v1 (§12 Sharp
                // edge 3); USER_ROUTINE ops are refused (§12 Sharp
                // edge 6).
                if (!b_kind_is_reorderable(B.kind)) continue;

                // ── Gate 2 — hoist / fuse / eliminated guards ──────
                //
                // No op in the triple may have `hoist_to_override`
                // set (PJ-3 anchors forward/uncompute halves to
                // specific pre-loop/post-loop locations; commuting
                // past a hoisted op breaks the pairing). None of
                // the three ops' stmt_ranges may be covered by
                // `unit.fused_stmt_ranges` (PJ-1d has already fused
                // that region) or `unit.eliminated_stmt_ranges`
                // (PJ-4a has already deleted that decl).
                if (op_is_already_processed(A, *unit_, sm)) continue;
                if (op_is_already_processed(B, *unit_, sm)) continue;
                if (op_is_already_processed(C, *unit_, sm)) continue;

                // ── Gate 3 — footprint disjointness ────────────────
                //
                // Extract A's result footprint, every operand of B,
                // and every C operand except the already-matched
                // `__t` read. Overlap of any B operand with A's
                // result OR with any C operand refuses the reorder.
                const detail::QubitFootprint a_result_fp =
                    detail::footprint(A.result, ctx);
                // If the extractor failed on A.result we bail
                // conservatively — without a precise footprint for
                // the AND's result, we cannot prove disjointness
                // against any B operand. The universal sentinel
                // would make may_overlap return true against every
                // B operand anyway, so the reorder would never
                // accept; short-circuiting saves the work.
                if (a_result_fp.name.empty()) continue;

                // Build C's "extra operands" footprint list: every
                // operand of C except the first (the `__t` read).
                // An XOR_ASSIGN typically carries exactly the one
                // RHS operand, so c_ops_extra is commonly empty —
                // we still walk defensively so future XOR_ASSIGN
                // variants (classical RHS shape) are handled.
                std::vector<detail::QubitFootprint> c_ops_extra;
                c_ops_extra.reserve(C.operands.size());
                for (std::size_t oi = 1; oi < C.operands.size(); ++oi) {
                    c_ops_extra.push_back(
                        detail::footprint(C.operands[oi], ctx));
                }
                // Also add C's `result` as a target footprint — the
                // XOR_ASSIGN writes its `result` field (the `x`),
                // and if B touches `x` then the reorder would swap
                // a read/write ordering between B and C. C.result
                // is conceptually part of "what C writes," so
                // commuting B past that write is just as unsafe as
                // commuting past a read operand.
                {
                    const detail::QubitFootprint c_result_fp =
                        detail::footprint(C.result, ctx);
                    c_ops_extra.push_back(c_result_fp);
                }

                if (!b_is_disjoint(a_result_fp, B.operands,
                                   c_ops_extra, ctx)) {
                    continue;
                }
                // Also gate on B's `result` (for decl-producing
                // kinds). If B writes to a qubit that overlaps A's
                // result or any of C's operands, the reorder is
                // unsafe even if B's read operands are disjoint.
                if (!B.result.name.empty()) {
                    std::vector<QValueRef> b_result_as_ops{B.result};
                    if (!b_is_disjoint(a_result_fp, b_result_as_ops,
                                       c_ops_extra, ctx)) {
                        continue;
                    }
                }

                // ── Gate 4 — fuse precondition ─────────────────────
                //
                // Once B is moved past C, the resulting (A, C) pair
                // must match PJ-1d's fuse trigger: __t has exactly
                // one reader in the enclosing scope. The reader
                // count is computed over the WHOLE scope anchor,
                // including B's subtree — B may not read __t (Gate
                // 3 rules that out), but the reader count helper
                // does not know that; it counts every DeclRefExpr.
                // PJ-1d's own gate uses the same count, so we
                // mirror it verbatim here.
                //
                // A reader count != 1 means either __t has zero
                // readers (PJ-4a territory, not PJ-1d's) or ≥2
                // readers (not a fuse candidate at all). Either way
                // the reorder has no payoff and we bail.
                const int reader_count = detail::count_readers_in_scope(
                    llvm::StringRef(A.result.name),
                    A.result.decl_loc,
                    scope_anchor,
                    ctx);
                if (reader_count != 1) continue;

                // ── Emit the QReplacement ──────────────────────────
                //
                // Compute full statement ranges (with trailing `;`)
                // for B and C. The replacement range spans
                // [B.begin, C.end+`;`]; the replacement text is
                // C's verbatim source followed by B's verbatim
                // source, with a `#line` directive prefixed to B
                // so any diagnostic inside B still points at the
                // original line.
                const clang::SourceRange b_full_range =
                    extend_range_to_semi(B.stmt_range, sm, lang);
                const clang::SourceRange c_full_range =
                    extend_range_to_semi(C.stmt_range, sm, lang);
                if (!b_full_range.isValid() || !c_full_range.isValid()) {
                    continue;
                }

                // Extract the verbatim text of B and C. If either
                // extraction yields empty text (macro expansion,
                // invalid range) we bail rather than emit a
                // truncated replacement.
                const std::string b_text =
                    get_source_text_of_range(b_full_range, sm, lang);
                const std::string c_text =
                    get_source_text_of_range(c_full_range, sm, lang);
                if (b_text.empty() || c_text.empty()) continue;

                // Compose the replacement text: C first, then B,
                // with B prefixed by a `#line` directive anchored
                // at B's original begin loc. A single space
                // separator between C and B (which start on fresh
                // lines in well-formed source anyway) keeps the
                // Rewriter's output readable; the original
                // indentation is not reconstructed — that matches
                // PJ-1d / PJ-3d's existing behaviour on fused
                // replacements.
                const std::string b_line_directive =
                    format_line_directive(sm, b_full_range.getBegin());

                std::string replacement_text;
                replacement_text.reserve(
                    c_text.size() + b_text.size() +
                    b_line_directive.size() + 2);
                replacement_text.append(c_text);
                // Newline between C and the moved B preserves line
                // breaks the user already had between the two
                // statements; many fixtures rely on this layout.
                replacement_text.push_back('\n');
                if (!b_line_directive.empty()) {
                    replacement_text.append(b_line_directive);
                }
                replacement_text.append(b_text);

                // Stage the replacement. The range covers B and C
                // together; the replacement reorders them so (A,
                // C) land adjacent in the rewritten buffer.
                QReplacement rep;
                rep.range = clang::SourceRange(
                    b_full_range.getBegin(), c_full_range.getEnd());
                rep.replacement = std::move(replacement_text);
                unit_->replacements.push_back(std::move(rep));

                // Record the moved range in `fused_stmt_ranges` so
                // downstream matchers (should any re-fire) do not
                // re-visit the consumed region. Technically the
                // reorder does NOT "fuse" the pair — the fuse itself
                // happens on the next transpile invocation — but the
                // `fused_stmt_ranges` vector is the project's
                // conventional "this region has been mutated by the
                // optimizer, do not re-process" channel, and the
                // PJ-1e containment probe already works for our
                // purposes. A separate `reordered_stmt_ranges`
                // vector would be cleaner but is a wider change than
                // PM5-5 should attempt.
                //
                // Why both B and C? Because the matched range spans
                // both; either of them being touched again would
                // re-enter the same patch region.
                unit_->fused_stmt_ranges.push_back(rep.range);

                // Also rewrite the in-memory IR order: swap scope.
                // ops[i+1] and scope.ops[i+2] so the op list
                // reflects the new layout (A, C, B). Subsequent
                // passes that walk the op list — including PJ-3d's
                // hoist pass if wired to run after us — observe the
                // same order as the rewritten source.
                std::swap(scope.ops[i + 1], scope.ops[i + 2]);

                // Advance i past the consumed triple. After the
                // swap, scope.ops[i + 1] is the FORMER C (now the
                // new middle), and scope.ops[i + 2] is the FORMER
                // B. We do NOT want to re-evaluate (A, C, B) as a
                // new triple, because its shape (A=AND-synth, B'=
                // B=non-plugin, C'=XOR_ASSIGN on __t) would match
                // the very pattern we just processed, but the (A,
                // C) pair is now adjacent — so the fuse precondition
                // would pass and the extra reorder has no payoff.
                // Single-pass design (§5): never revisit a triple
                // post-swap. Advance i by 2 to skip past the
                // processed region (loop increments by 1 at the
                // bottom, so i+=2 effectively skips 3 slots).
                i += 2;

                ++g_peephole_reorder_detection_count;
            }
        }
    }

private:
    QUnit* unit_;
    // MatchFinder::MatchResult::Context is an `ASTContext*` (non-const).
    // Cache the mutable pointer so the `onEndOfTranslationUnit()` pass
    // can invoke helpers (`classify_scope_kind`, footprint(),
    // getParents) that take a non-const `ASTContext&` OR a const
    // `ASTContext&`.
    clang::ASTContext* ctx_ = nullptr;
};

std::vector<std::unique_ptr<PeepholeReorderCallback>>&
peephole_reorder_callback_pool() {
    static std::vector<std::unique_ptr<PeepholeReorderCallback>> pool;
    return pool;
}

} // namespace

void register_peephole_reorder_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    auto& pool = peephole_reorder_callback_pool();
    pool.push_back(std::make_unique<PeepholeReorderCallback>(&unit));
    PeepholeReorderCallback* cb = pool.back().get();

    // Anchor on `translationUnitDecl()` — matches exactly once per TU,
    // giving `run()` a chance to cache the ASTContext. The real work
    // happens in `onEndOfTranslationUnit()`. Using this anchor is the
    // clang-tidy idiom for post-processing passes that need per-TU
    // context, mirroring the PJ-3d hoist matcher.
    finder.addMatcher(translationUnitDecl().bind("tu"), cb);
}

int peephole_reorder_detection_count_for_test() {
    return g_peephole_reorder_detection_count;
}

void reset_peephole_reorder_detection_count_for_test() {
    g_peephole_reorder_detection_count = 0;
}

} // namespace sturm::transpile
