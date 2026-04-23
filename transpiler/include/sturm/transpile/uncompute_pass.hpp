// uncompute_pass.hpp — M8 uncompute synthesis pass.
//
// Purpose
// -------
// Given a QUnit produced by the M7 AST matcher, this pass produces a flat
// list of UncomputeInsertion records — one per forward quantum operation —
// describing WHAT source text to inject and WHERE. The pass does not touch
// the Clang AST, does not call Rewriter, and does not open the source file.
// Separation of concerns: M8 schedules, M9 rewrites.
//
// LIFO discipline
// ---------------
// The whole point of the transpiler PRD is reverse-order uncomputation. For
// a scope containing forward operations `op0, op1, ..., opN`, the generated
// insertions must call the inverses in the order `opN, ..., op1, op0`. This
// module is the single place where that invariant is enforced. Every test
// in test_uncompute_pass.cpp exercises it directly so a regression shows up
// the moment the reverse-iteration is replaced with a forward walk.
//
// Scope anchoring
// ---------------
// Every insertion carries the `close_brace` SourceLocation of the QScope
// that originated it. The M9 emitter uses `insert_before` as the anchor —
// the insertion text is written immediately *before* that location, which
// places the uncompute call inside the originating block and right before
// its closing `}`. Multiple insertions sharing the same `insert_before`
// stack in the order they appear in the returned vector (Clang's Rewriter
// preserves insertion order at a given location).
//
// MVP scope
// ---------
// Only `QOpKind::OR` is handled in the MVP. Post-MVP kinds (AND, XOR, NOT,
// arithmetic inverses) will extend the switch inside synthesize() — see the
// roadmap doc for the phase order. The header itself needs no change.
//
// Complexity & performance
// ------------------------
// One pass over every op in every scope, with a string build per op. No
// Clang AST traversal, no SourceManager access. The unit test for this
// module runs in microseconds because it constructs the QUnit by hand.

#ifndef STURM_TRANSPILE_UNCOMPUTE_PASS_HPP
#define STURM_TRANSPILE_UNCOMPUTE_PASS_HPP

#include "sturm/transpile/qir.hpp"

#include "clang/Basic/SourceLocation.h"

#include <string>
#include <vector>

// PM2-3: forward-declare `clang::SourceManager` so the `synthesize()` overload
// that emits `#line` directives can accept one by pointer without dragging the
// heavy `SourceManager.h` header into every TU that includes this file
// (test_uncompute_pass.cpp hand-builds a QUnit and has no SourceManager at
// all). The implementation in uncompute_pass.cpp includes the full header.
namespace clang {
class SourceManager;
} // namespace clang

// PM4-3: forward-declare the `Registry` type so the `synthesize()` overload
// that dispatches plugin renderers can accept one by pointer without dragging
// `plugin_api.hpp` into every TU that includes this header. Tests that
// hand-build a QUnit with only in-tree ops (no `QOpKind::PLUGIN` ops) can
// pass `nullptr` here and compile without linking against plugin_api.
namespace sturm::transpile::plugin {
class Registry;
} // namespace sturm::transpile::plugin

namespace sturm::transpile {

// UncomputeInsertion and QReplacement are defined in qir.hpp (both are part
// of the QUnit data shape — `QUnit::raw_insertions` and `QUnit::replacements`
// store complete objects of these types — so the definitions must be at the
// IR boundary). They are visible here via qir.hpp's include above. This
// comment stands in place of a re-export so callers reading
// uncompute_pass.hpp know where to find the definitions.
//
// `UncomputeInsertion` records:
//   - `insert_before` is the SourceLocation at which M9 inserts the text.
//     For ops produced by the M8 synthesis pass this defaults to the
//     `close_brace` of the originating QScope (so the resulting call
//     lands inside the originating `{ ... }` block, just before its
//     closing `}`). Phase F PF-1 introduces `QOperation::insert_before_
//     override` so individual ops can target a different location (e.g.
//     a WHEN body's closing brace inside an enclosing scope).
//   - `code` is the complete source snippet to inject, terminated by '\n'
//     and (for synthesised ops) indented with four spaces. The format is
//     locked — M8 tests golden-compare against it and the M12 end-to-end
//     snapshot depends on it being stable.

/// Output of the M8 synthesis pass: both the LIFO-ordered uncompute
/// insertion list AND any source-text replacements the matcher produced
/// for the emitter to apply ahead of the insertion pass.
///
/// Pre-Phase-E, `replacements` is always empty — the MVP matcher + Phases
/// A..D only ever schedule `close_brace` insertions. Phase E's compound
/// matcher is the first producer of `replacements`.
///
/// The struct exists so the return type of `synthesize()` stays a single
/// value (the PRD API shape is preserved) while carrying both vectors
/// through to `emit()` in one call.
struct QSynthesisResult {
    std::vector<UncomputeInsertion> insertions;
    std::vector<QReplacement>       replacements;
};

/// Synthesize uncompute insertions for every QOperation in `unit`.
///
/// Ordering contract:
///   - Scopes are processed in the order they appear in `unit.scopes`.
///   - Within a single scope, ops are iterated in REVERSE so the emitted
///     list is LIFO per the PRD: the most recently computed intermediate
///     is uncomputed first.
///   - Ops that are not yet handled by the MVP (anything other than OR)
///     are silently skipped — the matcher never produces them, but the
///     pass is defensive in case a future IR-level consumer seeds unit
///     with placeholders.
///
/// Returns a QSynthesisResult holding the LIFO-ordered insertion list and
/// the (possibly empty) list of replacements the matcher attached to
/// `unit`. The caller (M9 emitter) feeds both to clang::Rewriter with the
/// replacement pass run first.
///
/// PM2-3: the optional `sm` parameter enables `#line` directive emission
/// on every synthesized uncompute insertion. When non-null:
///   - Each rendered op's `code` is prefixed with a `#line` directive
///     built from `op.stmt_range.getBegin()` via
///     `format_line_directive(*sm, ...)`, so Clang diagnostics and
///     debug-line info for the synthesized uncompute call cite the user's
///     originating compute line (the dual of which this uncompute is).
///   - For every scope that has at least one uncompute insertion anchored
///     at its `close_brace` (the default anchor used by Phases A..E and
///     every pre-override matcher), one extra "restoring" `#line`
///     insertion is appended pointing at the `close_brace`'s own line,
///     so code after the scope stays line-accurate. The restoring
///     insertion is emitted at the same `close_brace` location and is the
///     LAST entry for that scope in the returned list, which (given the
///     reverse-iteration applied by `emit()`) places it immediately
///     before the user's `}` in the final source text.
///
/// When `sm` is null (the default, preserved for backward compatibility
/// with hand-built tests in `test_uncompute_pass.cpp` that construct
/// `SourceLocation`s via `getFromRawEncoding` and have no
/// `SourceManager`), no `#line` directives are emitted — the output is
/// byte-identical to the pre-PM2-3 shape.
///
/// PM4-3: the optional `registry` parameter supplies the per-consumer
/// plugin Registry whose `find_render_fn(kind_id)` backs the
/// `case QOpKind::PLUGIN:` dispatch in `render_uncompute`. When null
/// (the default, preserved for backward compatibility with the hand-
/// built tests in `test_uncompute_pass.cpp` that construct QUnits with
/// only in-tree QOpKinds), any `QOpKind::PLUGIN` op renders to an empty
/// string (same defensive posture as other render cases on malformed
/// input), so the insertion vector for in-tree-only fixtures is
/// byte-identical to the pre-PM4 shape. The parameter is a raw pointer
/// (not a reference) so existing call sites that do not carry a
/// Registry can pass `nullptr` explicitly or omit the argument — the
/// same ergonomics as `sm`.
///
/// Passing Registry& explicitly — not via a global — matches the
/// design-doc constraint (plan §5: a nested `CompilerInvocation` from
/// PM1-4 constructs a second consumer whose Registry is a different
/// object). A global singleton would fire the wrong Registry when the
/// outer and inner consumers are both alive; threading by pointer
/// keeps each synthesize() call bound to its own consumer's Registry.
QSynthesisResult synthesize(const QUnit& unit,
                            const clang::SourceManager* sm = nullptr,
                            const plugin::Registry* registry = nullptr);

/// Render a single op's inverse source-text snippet.
///
/// Exposed for Phase R (sturm-88d7.2) `adjoint_emitter`, which walks a
/// validated + normalized routine body in reverse statement order and
/// needs to call the per-kind inverse renderer without pulling in the
/// scope / insertion-anchor bookkeeping that `synthesize()` does.
///
/// Returns the same text `synthesize()` would place in an
/// `UncomputeInsertion.code`:
///   - Four-space leading indent, trailing '\n'.
///   - Free-function form (`uncompute_or(...)`, `uncompute_and(...)`,
///     `uncompute_*_qint(...)`) or inline self-dual form (`x = ~x;`,
///     `x ^= y;`, `q.theta() -= C;`) depending on the op kind.
///   - `USER_ROUTINE` renders `invert(<name>)(<args>);` — the audit-
///     trail form PI-4 fixed.
///   - `PLUGIN` dispatches through `registry->find_render_fn(kind_id)`.
/// Returns an empty string on malformed input (operand-count mismatch,
/// empty `routine_name` / `plugin_kind_id`, null registry for PLUGIN).
///
/// `registry` is only consulted for `QOpKind::PLUGIN`; every in-tree
/// kind ignores it, so tests that hand-build non-plugin ops can pass
/// `nullptr`.
std::string render_uncompute(const QOperation& op,
                             const plugin::Registry* registry = nullptr);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_UNCOMPUTE_PASS_HPP
