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
#include <sstream>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

// Build the source-text snippet for a single forward op's inverse. The
// exact format is locked down by the M8 tests and the PRD's output
// contract: four-space indent, `uncompute_or(<result>, <op0>, <op1>);\n`
// (and analogous per-kind forms for later phases).
//
// Phase A adds NOT. Per the roadmap, NOT is its own inverse: applying the
// same `~` to the result qubit uncomputes it. The emitted form is
// `<result> = ~<result>;` rather than a separate `uncompute_not(...)`
// free function, because there is no meaningful out-of-place inverse for
// a single-qubit X gate — the in-place form composes to identity with
// zero ancilla cost.
//
// PM4-3: the `registry` parameter is consulted ONLY for
// `case QOpKind::PLUGIN:` — every in-tree kind ignores it. Passed by
// const pointer (not reference) so a hand-built test fixture without a
// Registry can pass `nullptr`; in that case, a `QOpKind::PLUGIN` op
// renders to an empty string (same defensive posture as every other
// render case on malformed input).
std::string render_uncompute(const QOperation& op,
                             const plugin::Registry* registry) {
    std::ostringstream os;
    switch (op.kind) {
    case QOpKind::OR: {
        // Defensive: the MVP matcher always produces exactly two operands
        // for OR. If a future IR consumer seeds a malformed op, emit
        // nothing so we do not inject invalid C++ into the user's file.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_or(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::AND: {
        // Phase E: exact analogue of the OR case. The Phase E compound
        // matcher (PE-4) records `qbool r = a & b;` with two operands
        // (the two qbool inputs); the inverse is a free-function call
        // `uncompute_and(r, a, b);` declared in
        // include/sturm/uncompute/uncompute_api.hpp and landed in PE-0
        // (sturm-oheo). Malformed seeds (operand count != 2) render
        // nothing, matching the OR guard.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_and(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::NOT: {
        // NOT is self-inverse: re-applying `~` to the result qubit
        // uncomputes it. The operand list is unused in the emission
        // (the inverse touches only the result) but must be non-empty —
        // an op with no operand would indicate a malformed IR seed.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " = ~" << op.result.name << ";\n";
        break;
    }
    case QOpKind::XOR: {
        // XOR is self-inverse with a two-step reduction: applying the
        // same two operands via `^=` to the result undoes it, because
        // (a^b)^a^b == 0. Emitted on two separate source lines for
        // readability; the order is lhs then rhs, matching the forward
        // source order of the original `a ^ b`.
        if (op.operands.size() != 2) return {};
        os << "    " << op.result.name << " ^= " << op.operands[0].name
           << ";\n"
           << "    " << op.result.name << " ^= " << op.operands[1].name
           << ";\n";
        break;
    }
    case QOpKind::XOR_ASSIGN: {
        // `a ^= b;` is self-adjoint: applying the same statement twice
        // returns a to its original state. The emitted inverse is a
        // verbatim re-emission of the forward call.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " ^= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::ADD_ASSIGN_CONST: {
        // Phase B: `a += C;` where C is a compile-time-readable classical
        // constant source fragment stored verbatim in operands[0].name.
        // The inverse is the dual operator over the same constant.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " -= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::SUB_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " += " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::MUL_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " /= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::DIV_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " *= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::ADD_ASSIGN_QINT: {
        // Phase C: `a += b;` where b is another qint named in source.
        // operands[0].name carries the verbatim RHS identifier. The
        // inverse is the `uncompute_add_qint` free function declared in
        // include/sturm/uncompute/uncompute_api.hpp, which delegates to
        // the forward `-=` compound-assign on the runtime side.
        if (op.operands.size() != 1) return {};
        os << "    uncompute_add_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::SUB_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_sub_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::MUL_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_mul_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::DIV_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_div_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::MOD_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_mod_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::EQ_QINT: {
        // Phase D: `qbool c = a == b;` — c is the produced qbool result,
        // operands[0] is the LHS qint identifier, operands[1] is the RHS
        // qint identifier. The inverse is the `uncompute_eq_qint` free
        // function declared in include/sturm/uncompute/uncompute_api.hpp,
        // which re-dispatches to the self-adjoint DSL `lib_eq_dsl` in
        // include/sturm/lib/compare_dsl.hpp. Two operands, not one, because
        // the comparator adjoint needs both inputs to flip the result bit
        // back to |0⟩.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_eq_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::NE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_ne_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::LT_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_lt_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::LE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_le_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::GT_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_gt_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::GE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_ge_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::USER_ROUTINE: {
        // Phase I PI-4: render the user-routine's adjoint dispatch.
        // The emitted form is
        //   "    invert(<routine_name>)(<op0>, <op1>, ...);\n"
        // with operands listed in source order. There is NO result-name
        // prefix — a USER_ROUTINE op mutates through its output
        // parameters (flagged by `outputs_mask`) and has no single
        // named result. The four-space leading indent matches every
        // other kind so the emitter injects uniform text into the
        // user's source.
        //
        // Defensive: if `routine_name` is empty (should never happen
        // in practice — the PI-2 matcher always populates it, and a
        // FunctionDecl with no name could not have been registered in
        // the PI-1 routine registry — but a hand-built fixture or a
        // future IR consumer could seed an empty one), emit nothing
        // rather than render `invert()(...);` which would not compile.
        if (op.routine_name.empty()) return {};
        os << "    invert(" << op.routine_name << ")(";
        for (std::size_t i = 0; i < op.operands.size(); ++i) {
            if (i != 0) os << ", ";
            os << op.operands[i].name;
        }
        os << ");\n";
        break;
    }
    case QOpKind::CCNOT_INPLACE: {
        // Phase J PJ-1c: zero-ancilla fusion seeded by the PJ-1d peephole
        // matcher from the adjacent pair
        //     qbool __t = a & b;
        //     x ^= __t;
        // when `__t` has exactly one reader. Operand shape mirrors OR /
        // AND: one result (the `x` target of the in-place flip) plus two
        // named operand QValueRefs (the two qbool controls).
        //
        // Self-adjoint: `ccnot_inplace(x, a, b)` is a single CCX(a, b, x)
        // that is its own inverse, so the forward emission (a verbatim
        // QReplacement text applied by the matcher over the fused pair)
        // and the uncompute emission (this render case) share a single
        // identifier — running the helper a second time at the uncompute
        // point undoes the forward flip. The four-space leading indent
        // matches every other kind so the emitter injects uniform text.
        //
        // Defensive: if the operand count is not exactly 2 — a malformed
        // hand-built fixture or a future IR consumer seeding a bad op —
        // emit nothing so we do not inject invalid C++ into the user's
        // file. Mirrors the identical guard on OR / AND.
        if (op.operands.size() != 2) return {};
        os << "    ccnot_inplace(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::PLUGIN: {
        // Phase M PM4-3: plugin-registered op. Consult the per-consumer
        // Registry via `find_render_fn(plugin_kind_id)` and invoke the
        // returned `UncomputeRenderFn`. The returned string is taken
        // verbatim — the plugin is responsible for the four-space
        // indent + trailing '\n' invariant every in-tree renderer
        // follows (documented in `plugin_api.hpp`'s
        // `UncomputeRenderFn` doc).
        //
        // Defensive guards:
        //
        //   1. `registry == nullptr` — a hand-built test fixture or a
        //      pre-PM4 caller that forgot to thread the Registry.
        //      Emitting nothing matches the posture other kinds take on
        //      malformed input, and the degenerate case is unreachable
        //      from the production consumer (which always passes a
        //      valid Registry).
        //
        //   2. `plugin_kind_id` empty — a hand-built op missing the
        //      key. Skipping keeps the malformed-input posture uniform
        //      with USER_ROUTINE's empty-routine-name guard.
        //
        //   3. `find_render_fn(kind_id)` returns null — the plugin's
        //      registration was dropped (collision rejected) or never
        //      ran. Emitting nothing lets the scope still close cleanly
        //      and the build keep going; a missing renderer for a
        //      claimed kind_id is a build-by-build mismatch the user
        //      should see via the host's own dlopen-side error, not a
        //      nested segfault inside a rewrite pass.
        //
        //   4. The renderer's `std::function<>` target is empty (moved
        //      from or default-constructed) — the UncomputeRenderFn
        //      registered was uninitialized. Same posture: emit
        //      nothing.
        if (registry == nullptr) return {};
        if (op.plugin_kind_id.empty()) return {};
        const plugin::UncomputeRenderFn* fn =
            registry->find_render_fn(op.plugin_kind_id);
        if (fn == nullptr) return {};
        if (!*fn) return {};
        return (*fn)(op);
    }
    // No `default:` — adding a new QOpKind should fail the build here
    // until every downstream consumer is updated. (Compilers warn on
    // missing enum cases when default is absent.)
    }
    return os.str();
}

} // namespace

QSynthesisResult synthesize(const QUnit& unit,
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

    return result;
}

} // namespace sturm::transpile
