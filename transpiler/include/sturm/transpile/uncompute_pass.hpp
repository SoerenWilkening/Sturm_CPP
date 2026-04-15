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

namespace sturm::transpile {

/// One record describing a single uncompute call to be inserted by M9.
///
/// - `insert_before` is the SourceLocation at which M9 inserts the text.
///   It is the `close_brace` of the originating QScope, so the resulting
///   `uncompute_or(...)` call lands inside the original `{ ... }` block
///   and immediately before its closing `}`.
/// - `code` is the complete source snippet to inject, terminated by '\n'
///   and indented with four spaces. The exact format is locked — M8 tests
///   golden-compare against it, and the M12 end-to-end snapshot depends on
///   it being stable.
struct UncomputeInsertion {
    clang::SourceLocation insert_before;
    std::string code;
};

// QReplacement is defined in qir.hpp (it is part of the QUnit data shape,
// so its storage must be complete at the IR boundary). It is visible here
// via qir.hpp's include above — this comment stands in place of a
// re-export so callers reading uncompute_pass.hpp know where to find the
// definition.

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
QSynthesisResult synthesize(const QUnit& unit);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_UNCOMPUTE_PASS_HPP
