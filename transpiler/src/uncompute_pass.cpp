// uncompute_pass.cpp — implementation of the M8 uncompute synthesis pass.
//
// The logic is small on purpose: for each scope in the QUnit, iterate its
// ops *in reverse* and emit one UncomputeInsertion per handled op. LIFO is
// the invariant this file exists to enforce, so the reverse iteration is
// the most important line of code below.
//
// No Clang AST / Rewriter / SourceManager / I/O calls happen here. Only
// SourceLocation values (opaque 32-bit wrappers) flow through, which is
// why this file compiles in a fraction of a second and the tests run in
// microseconds.
//
// sturm-v0ur (LO-2 wiring): the per-op inverse renderer
// `render_uncompute()` was hoisted into `uncompute_render.cpp` so this
// file stays under the plan §9 600-LoC budget. The only API change is a
// new `register_external_cleanup()` hook (~20 LoC) that lets external
// emitters — currently `lossy_scope_exit_emitter` — inject one cleanup-
// text record keyed on the close-brace `SourceLocation` of an enclosing
// `CompoundStmt`. The hook accumulates records into a caller-owned
// vector; `synthesize()` accepts that vector and interleaves the
// external entries with the internal LIFO queue at the matching close-
// brace anchor. Pre-LO-2 callers (every existing non-LO consumer) pass
// nullptr / an empty vector and observe byte-identical output.

#include "sturm/transpile/uncompute_pass.hpp"

#include "sturm/transpile/qir.hpp"
// PM2-3: pulls in `format_line_directive` so each rendered uncompute call
// can be prefixed with a `#line` directive anchored at the user's forward
// (compute) expression — source-map emission for every uncompute kind.
#include "sturm/transpile/emitter.hpp"
// PM4-3: pulls in the `Registry` full definition so `render_uncompute`'s
// `case QOpKind::PLUGIN:` arm can call `find_render_fn(kind_id)` and
// invoke the returned `UncomputeRenderFn`. Header-only from the .hpp
// side uses a forward declaration, but the .cpp side needs the full
// class to dereference the pointer.
#include "sturm/transpile/plugin_api.hpp"

#include "clang/Basic/SourceManager.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

// sturm-v0ur (LO-2 wiring) — register one external cleanup record. See
// the header doc for the contract; the helper exists so external
// emitters do not need to spell out the `ExternalCleanup` struct
// shape at every call site. Discards records with an invalid
// close-brace loc or an empty text body — both are no-ops in the
// final rewrite, so we drop them at registration time to keep the
// downstream `synthesize()` loop linear in surviving entries.
void register_external_cleanup(std::vector<ExternalCleanup>& sink,
                               clang::SourceLocation close_brace,
                               std::string text) {
    if (close_brace.isInvalid() || text.empty()) return;
    sink.push_back(ExternalCleanup{close_brace, std::move(text)});
}

QSynthesisResult synthesize(const QUnit& unit,
                            const clang::SourceManager* sm,
                            const plugin::Registry* registry) {
    return synthesize(unit, {}, sm, registry);
}

QSynthesisResult synthesize(const QUnit& unit,
                            const std::vector<ExternalCleanup>& external,
                            const clang::SourceManager* sm,
                            const plugin::Registry* registry) {
    QSynthesisResult result;
    std::vector<UncomputeInsertion>& out = result.insertions;

    // PE-2: pass replacements through unchanged. Pre-Phase-E the matcher
    // never populates this vector, so the loop is a no-op on existing
    // snapshot fixtures (byte-identical output guaranteed). When Phase E's
    // compound matcher lands it will append to `unit.replacements` and
    // the emitter will apply every entry ahead of the insertion pass.
    result.replacements = unit.replacements;

    // Rough capacity reservation to avoid mid-loop reallocations on the
    // common single-scope case. Worst-case each op yields one insertion,
    // plus one per pre-staged `raw_insertions` entry (Phase F PF-1).
    std::size_t upper_bound = unit.raw_insertions.size();
    for (const auto& scope : unit.scopes) upper_bound += scope.ops.size();
    out.reserve(upper_bound);

    // Scopes are processed in source order so that M9 inserts text into
    // the outermost / earliest block first. WITHIN each scope, ops are
    // iterated in REVERSE to realize the LIFO uncompute schedule the PRD
    // is built around. This reverse iteration is the single load-bearing
    // line in this module — do not replace it with a forward walk.
    //
    // Ops are first sorted by their statement's source-begin location so
    // the reverse walk produces true source-LIFO. The matcher populates
    // scope.ops in MatchFinder callback-firing order, which is per-matcher
    // (registration) order rather than source order: when multiple
    // Phase A matchers contribute to the same scope, a raw reverse walk
    // would flip the inter-matcher ordering. Sorting pins LIFO to the
    // user's code, not the matcher's dispatch schedule. Regression:
    // sturm-ny2.
    //
    // Phase F PF-4: use `std::stable_sort` instead of `std::sort`. All
    // Phase F ops lifted from a single `WHEN(expr)` invocation share the
    // same `stmt_range` (the expansion range of the macro argument), so
    // multiple ops compare equal under the begin-loc key. `std::stable_sort`
    // preserves their relative insertion order — which the Phase F matcher
    // controls to be innermost-first → outermost-last (post-order from the
    // compound flattener). The subsequent reverse walk then produces
    // outermost-first, innermost-last: true source LIFO. Pre-Phase-F
    // snapshots contain one op per stmt_range, so stable_sort is a no-op
    // tiebreaker on those inputs — byte-identical output is guaranteed.
    for (const auto& scope : unit.scopes) {
        std::vector<QOperation> sorted_ops = scope.ops;
        std::stable_sort(sorted_ops.begin(), sorted_ops.end(),
                  [](const QOperation& a, const QOperation& b) {
                      return a.stmt_range.getBegin().getRawEncoding() <
                             b.stmt_range.getBegin().getRawEncoding();
                  });

        // PM2-3 bookkeeping: track whether any insertion for this scope
        // lands at `scope.close_brace` (the default anchor used by every
        // Phase A..E matcher and all kinds without Phase F/J per-op
        // overrides). When `sm` is provided AND at least one such
        // insertion exists, we append a restoring `#line` directive
        // insertion anchored at the same `close_brace` so code AFTER the
        // scope stays line-accurate. The restoring entry is pushed LAST
        // for this scope, which (given `emit()`'s reverse iteration over
        // the insertion list) places it IMMEDIATELY before the user's
        // `}` in the final source text — i.e. after every uncompute call
        // that shares the anchor.
        bool scope_has_close_brace_insertion = false;

        for (auto it = sorted_ops.rbegin(); it != sorted_ops.rend(); ++it) {
            const QOperation& op = *it;
            // Phase H PH-3: honour the per-op skip flag the
            // outer-var-guard matcher sets on mutations whose automatic
            // uncomputation would require reverse-loop synthesis. The op
            // still exists in the IR (so `dump()` shows it and future
            // tooling remains aware it was recognised), but NO
            // UncomputeInsertion is emitted for it — the user is expected
            // to provide a manual adjoint per P9. Pre-Phase-H matchers
            // leave the flag false, so this branch is a no-op on every
            // prior fixture.
            if (op.skip_uncompute) {
                continue;
            }
            std::string code = render_uncompute(op, registry);
            if (code.empty()) {
                // Unsupported / malformed op — skip silently; see the
                // rationale in render_uncompute(). The MVP matcher never
                // produces such ops.
                continue;
            }

            // PM2-3: prepend a `#line` directive pointing at the forward
            // op's `stmt_range.getBegin()` — the uncompute is the
            // structural dual of that statement, so the user's compute
            // line is the correct anchor for any diagnostic or debugger
            // step landing inside the synthesized uncompute text. Empty
            // string on degenerate inputs (invalid loc, non-main-file
            // loc) — in that case the rendered code is emitted
            // unchanged, preserving the pre-PM2-3 shape byte-for-byte.
            //
            // Why the leading `\n`? The insertion lands right before its
            // anchor location (`close_brace` or an override). The
            // pre-existing text immediately before the anchor is
            // whatever the user's source held there — typically a `;`
            // from the last statement plus some trailing whitespace.
            // `#line` must be the first NON-WHITESPACE token on its own
            // line per the C/C++ standard, so we unconditionally prefix
            // a `\n` to guarantee the directive lands at column 0 of a
            // fresh line. A double newline (when the prior insertion
            // already ends in `\n`) is harmless — it just leaves a
            // blank line between uncompute blocks.
            if (sm != nullptr) {
                const std::string line_directive = format_line_directive(
                    *sm, op.stmt_range.getBegin());
                if (!line_directive.empty()) {
                    std::string prefixed;
                    prefixed.reserve(1 + line_directive.size() + code.size());
                    prefixed.push_back('\n');
                    prefixed.append(line_directive);
                    prefixed.append(code);
                    code = std::move(prefixed);
                }
            }

            UncomputeInsertion rec;
            // Anchor selection for the uncompute insertion, in priority
            // order:
            //   1. Phase J PJ-3c: `hoist_to_override` — set by the PJ-3d
            //      uncompute-hoisting matcher on a decl-producing op whose
            //      operands are all loop-invariant. When valid, the
            //      uncompute lands AFTER the loop end (at this location),
            //      while the forward computation is simultaneously moved
            //      BEFORE the loop begin by a matcher-owned QReplacement
            //      and/or raw_insertion pair (not synthesize()'s concern).
            //      Takes precedence over `insert_before_override` —
            //      without this precedence the uncompute would land at
            //      the loop-BEGIN location (where the FORWARD goes), not
            //      after the loop body, and the optimization's semantic
            //      would be broken.
            //   2. Phase F PF-1: `insert_before_override` — set by the
            //      WHEN-lift matcher to target the post-WHEN-body close
            //      brace instead of the enclosing scope's close brace.
            //      Unchanged by PJ-3c.
            //   3. Legacy `scope.close_brace` — the anchor every Phase
            //      A..E matcher relies on when no per-op override is set.
            //
            // Default-constructed (invalid) overrides at each layer fall
            // through to the next, so every pre-PJ-3 snapshot fixture
            // stays byte-identical (no op before PJ-3 sets
            // `hoist_to_override`).
            if (op.hoist_to_override.isValid()) {
                rec.insert_before = op.hoist_to_override;
            } else if (op.insert_before_override.isValid()) {
                rec.insert_before = op.insert_before_override;
            } else {
                rec.insert_before = scope.close_brace;
                scope_has_close_brace_insertion = true;
            }
            rec.code = std::move(code);
            out.push_back(std::move(rec));
        }

        // PM2-3: emit a restoring `#line` directive at the scope's
        // `close_brace` when at least one uncompute call landed there.
        // The restoring directive points at the close_brace's own line,
        // so the user's source lines AFTER the scope (outside `}`) stay
        // numbered correctly even though the synthesized uncompute
        // block's `#line` prefixes re-anchored the line counter to
        // earlier forward expressions. Pushed LAST in the scope's
        // insertions so `emit()`'s reverse-iteration order places its
        // text IMMEDIATELY before `}` — after every per-op uncompute
        // directive+call. Skipped when `sm` is null (backward-compat
        // path for hand-built tests) or when
        // `format_line_directive` degenerates to empty (invalid loc,
        // non-main-file loc — in which case no synthesized directives
        // were emitted above either, so no restore is needed).
        //
        // Override-anchored insertions (Phase F WHEN-lift, Phase J
        // PJ-3 hoist) deliberately do NOT trigger a restoring directive
        // here — their matchers (PM2-4, PM2-5, PM2-6) own the source-
        // map contract for those locations. Emitting a restoring
        // directive at `scope.close_brace` for an override-anchored
        // insertion would mis-attribute the user's line count to the
        // outer scope's close brace when the uncompute actually landed
        // elsewhere.
        if (sm != nullptr && scope_has_close_brace_insertion) {
            const std::string restore_directive = format_line_directive(
                *sm, scope.close_brace);
            if (!restore_directive.empty()) {
                UncomputeInsertion rec;
                rec.insert_before = scope.close_brace;
                // Leading `\n` mirrors the per-op prefix rationale: the
                // immediately preceding insertion ends in `\n`, but the
                // pre-existing user source at the close_brace is `}`
                // (no leading whitespace guarantee). A consistent `\n`
                // prefix keeps the directive at column 0 in every
                // possible context.
                std::string code;
                code.reserve(1 + restore_directive.size());
                code.push_back('\n');
                code.append(restore_directive);
                rec.code = std::move(code);
                out.push_back(std::move(rec));
            }
        }
    }

    // Phase F PF-1: append pre-staged matcher insertions verbatim.
    // Pre-Phase-F matchers leave `unit.raw_insertions` empty so this loop
    // is a no-op on every pre-existing snapshot fixture. Order is
    // preserved exactly — `synthesize()` does not sort, dedupe, or filter
    // these records; the matcher controls their ordering and their
    // anchor SourceLocations directly.
    for (const auto& rec : unit.raw_insertions) {
        out.push_back(rec);
    }

    // sturm-v0ur (LO-2 wiring): drain external cleanup records. Each
    // record is converted to a single `UncomputeInsertion` anchored at
    // the registered close-brace location. External cleanups are
    // appended to `out` AFTER per-op LIFO insertions and AFTER the
    // PM2-3 restoring `#line` directives, but BEFORE the
    // `unit.raw_insertions` block — actually, after, since they live
    // here at the tail. The Rewriter's reverse-iter at `emit()` will
    // therefore place each external cleanup BEFORE any internal
    // restoring `#line` for the same anchor (because reverse-iter
    // applies later vector elements first, and InsertTextBefore
    // prepends to the buffer at the location). This produces the
    // desired source order at close brace: per-op uncomputes (LIFO) →
    // external cleanup → restoring `#line` → `}`.
    //
    // For external cleanups whose close_brace anchor was NOT visited
    // by the scope loop above (the LO common case — a WHEN body or
    // inner CompoundStmt that no other matcher claimed as a QScope),
    // the scope loop never set `scope_has_close_brace_insertion` for
    // that anchor, so no restoring `#line` was emitted. We fix that
    // here by emitting one restoring `#line` per UNIQUE close_brace
    // anchor that has at least one external cleanup but whose anchor
    // does not already match any visited scope's `close_brace`.
    if (!external.empty()) {
        // Collect the set of close-brace locations the scope loop
        // already emitted a restoring `#line` for. Same raw-encoding
        // equality that `format_line_directive` works on; SourceLocation
        // is already a thin wrapper around uint32 so direct comparison
        // is canonical.
        std::vector<unsigned> covered;
        covered.reserve(unit.scopes.size());
        for (const auto& scope : unit.scopes) {
            covered.push_back(scope.close_brace.getRawEncoding());
        }
        const auto already_covered = [&](clang::SourceLocation loc) {
            const unsigned key = loc.getRawEncoding();
            for (unsigned c : covered) {
                if (c == key) return true;
            }
            return false;
        };

        std::vector<unsigned> seen;
        seen.reserve(external.size());
        for (const auto& cu : external) {
            if (cu.close_brace.isInvalid() || cu.text.empty()) continue;
            UncomputeInsertion rec;
            rec.insert_before = cu.close_brace;
            rec.code = cu.text;
            out.push_back(std::move(rec));

            // Restoring `#line` — emit at most once per unique anchor,
            // and only when `sm` is provided AND the scope loop did
            // not already cover this close_brace (avoiding a duplicate
            // restoring directive at the same loc).
            if (sm == nullptr) continue;
            const unsigned key = cu.close_brace.getRawEncoding();
            bool seen_here = false;
            for (unsigned s : seen) {
                if (s == key) { seen_here = true; break; }
            }
            if (seen_here) continue;
            seen.push_back(key);
            if (already_covered(cu.close_brace)) continue;
            const std::string restore_directive = format_line_directive(
                *sm, cu.close_brace);
            if (restore_directive.empty()) continue;
            UncomputeInsertion restore;
            restore.insert_before = cu.close_brace;
            std::string code;
            code.reserve(1 + restore_directive.size());
            code.push_back('\n');
            code.append(restore_directive);
            restore.code = std::move(code);
            out.push_back(std::move(restore));
        }
    }

    return result;
}

} // namespace sturm::transpile
