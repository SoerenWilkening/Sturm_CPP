// matcher_user_routine.hpp — Phase I PI-2 routine-call matcher interface.
//
// This header is NOT part of the public transpiler API. It lives under
// transpiler/src/ (not include/) alongside routine_registry.hpp because
// the matcher needs a reference to the PI-1 `RoutineRegistry` — a type
// that reveals Clang AST types (FunctionDecl*) downstream consumers of
// the public matcher.hpp must not pick up.
//
// Purpose
// -------
// Phase I subphase 2. The PI-1 subphase populates a
// `RoutineRegistry` with every `STURM_REGISTER_ADJOINT(fwd, adj)` pair in
// the translation unit. This matcher consumes that registry: it fires on
// every `callExpr` whose callee's canonical `FunctionDecl*` appears as a
// key in the registry, and records one `QOperation{kind=USER_ROUTINE}`
// per call into the caller-supplied `QUnit`.
//
// Contract
// --------
// - `register_user_routine_matcher` must be called at most once per QUnit.
// - Both `unit` and `registry` must outlive the MatchFinder's run.
// - The registry MUST already be populated (or co-populated by a sibling
//   matcher whose callback fires before this one) for a call to match.
//   Registering this matcher after the PI-1 routine-registry matcher in
//   main.cpp — AND ordering both before other call-matcher-style
//   registrations — keeps the PI-4 adjoint dispatch deterministic.
//
// Scope (PI-2 only)
// -----------------
// This subphase produces the IR entries; the adjoint-dispatch rendering
// in the uncompute pass is PI-4's responsibility. PI-2 records only
// `QOperation{kind=USER_ROUTINE, routine_name=..., operands=..., outputs_mask=...}`;
// it does not emit any `UncomputeInsertion`.
//
// Argument classification
// -----------------------
// Each call argument is classified by the *parameter's* declared type on
// the callee's FunctionDecl — not the argument expression's static type.
// Three categories:
//   - non-const qbool&/qint& parameter → OUTPUT (bit set in outputs_mask)
//   - const    qbool&/qint& parameter → INPUT  (bit clear)
//   - classical scalar parameter      → INPUT  (bit clear, operand.name
//                                               is the verbatim source text)
//
// Operands are recorded in source order (positional). The classification
// of a slot is expressed via `outputs_mask` bit i. Classical scalars are
// rendered by Lexer::getSourceText so the adjoint dispatch in PI-4 can
// emit them verbatim (`foo(x, 42)` → `invert(foo)(x, 42)`).

#ifndef STURM_TRANSPILE_MATCHER_USER_ROUTINE_HPP
#define STURM_TRANSPILE_MATCHER_USER_ROUTINE_HPP

#include "sturm/transpile/qir.hpp"
#include "routine_registry.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"

namespace sturm::transpile {

// PM3-3: forward declaration so the PI-2 matcher's registration helper
// can accept a `DiagContext&` argument without dragging the full
// `transpiler/src/diag_context.hpp` into this header. The full
// definition lives in `transpiler/src/diag_context.hpp` and is
// included by `matcher_user_routine.cpp` proper.
struct DiagContext;

/// Register the Phase I PI-2 routine-call matcher against `finder`,
/// directing every match into `unit` and consulting `registry` for the
/// forward-function allowlist. Both `unit` and `registry` must outlive
/// the MatchFinder's run.
///
/// On match the callback:
///   1. Resolves the enclosing QScope via `detail::enclosing_scope()`
///      (the same helper every other matcher in the transpiler uses).
///   2. Classifies each argument by the callee's corresponding parameter
///      declared-type: non-const `qbool&`/`qint&` → OUTPUT, const
///      `qbool&`/`qint&` → INPUT, classical scalar → INPUT.
///   3. Builds one `QOperation{kind=USER_ROUTINE}` whose `routine_name`
///      is the callee's source-level identifier, whose `operands` list
///      the arguments in source order, and whose `outputs_mask` flags
///      the output-classified operands. The op's `result` is left
///      empty (USER_ROUTINE produces no single named result — each
///      output parameter is a separate mutation).
///
/// PM3-3: adds a `DiagContext&` parameter so the matcher can surface
/// an Error through the shared `DiagnosticsEngine` when a CallExpr
/// targets a function that is NOT in the registry AND has at least
/// one quantum output parameter (`outputs_mask != 0`). The pre-PM3-3
/// silent early-return still applies to the disjoint case (callee
/// not registered AND outputs_mask == 0 — pure classical
/// side-effect call), so ordinary classical helpers never trigger
/// a false-positive diagnostic.
///
/// Call at most once per QUnit.
void register_user_routine_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    const RoutineRegistry& registry,
    DiagContext& diag);

/// Test-only instrumentation (Phase I / PI-2). Returns the number of
/// successfully-matched routine calls since the last reset (one
/// increment per `QOperation{kind=USER_ROUTINE}` appended to the
/// QUnit). Production code must not touch either helper; the unit
/// tests use them to pin detection counts.
int user_routine_detection_count_for_test();
void reset_user_routine_detection_count_for_test();

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_USER_ROUTINE_HPP
