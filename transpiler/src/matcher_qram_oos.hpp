// matcher_qram_oos.hpp — sturm-u9ge.16 (Beat E1) out-of-scope diagnostic
// matchers for the QRAM-via-array-subscript epic.
//
// Plan §8 / Beat E1; PRD §9 / M5. Detects each of the four shapes the v1
// rewrite (sturm-u9ge.12 / Beat C1) deliberately does NOT handle, and
// emits a hard compile error citing PRD §9 + the table row covering that
// shape. The intent is to ensure that a missed shape NEVER falls
// through to silent runtime measurement (PRD §10.1) — the user gets a
// loud diagnostic at transpile time instead.
//
// PRD §9 table (re-stated here for the diag bodies):
//
//     | Shape                       | Reason out of scope                    |
//     |-----------------------------|----------------------------------------|
//     | `b = a[i];` (existing `b`)  | needs uncompute of old `b` before QRAM |
//     |                             | writes                                 |
//     | `a[i] = b;`                 | QRAM-write is a different unitary from |
//     |                             | QRAM-read                              |
//     | `a[i] += b;`                | RMW: read + adjusted-write composition |
//     | `c = a[i] + d;`             | expression-position read: ancilla      |
//     |                             | extract + uncompute                    |
//
// One callback per shape, each raising a distinct diagnostic id:
//   - qram-oos-existing-target       (`b = a[i];`)
//   - qram-oos-write                 (`a[i] = b;`)
//   - qram-oos-rmw                   (`a[i] += b;` and friends)
//   - qram-oos-expression-position   (`c = a[i] + d;`)
//
// Discriminator (mirrors C1, sturm-u9ge.12): the index sub-expression
// contains an `ImplicitCastExpr` of `CK_UserDefinedConversion` whose
// conversion function is `sturm::frontend::qint::operator size_t`. This
// is the strong, unambiguous signal — it cannot be produced by any
// other type in the surface language because no other surface type
// carries an implicit `qint -> size_t` conversion.
//
// Coexistence with C1: this matcher runs in NEGATIVE contexts only —
// the C1 matcher (sturm-u9ge.12) anchors on `qint b = a[i];` (a fresh
// VarDecl whose initializer IS the subscript). The four shapes here
// are by construction structurally disjoint from that anchor:
//   - shape (1) `b = a[i];` is an assignment-shape op-call, not a
//     VarDecl init.
//   - shapes (2) and (3) are subscript-on-LHS shapes.
//   - shape (4) puts the subscript inside a binary operator at
//     non-init position.
// So a TU containing both an in-scope read (C1 fires + rewrites) and
// an out-of-scope shape (E1 fires + diagnoses) produces exactly one
// rewrite + one diagnostic, in either matcher-traversal order.
//
// LoC budget: <= 300 (plan §1 / §8 E1).

#ifndef STURM_TRANSPILE_MATCHER_QRAM_OOS_HPP
#define STURM_TRANSPILE_MATCHER_QRAM_OOS_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

namespace clang {
class DiagnosticsEngine;
} // namespace clang

namespace sturm::transpile {

/// Distinct diagnostic ids covering the four PRD §9 out-of-scope
/// shapes. Mirrored 1:1 by the four callbacks registered in
/// `register_qram_oos_matcher` and asserted by the test file
/// `test_matcher_qram_oos.cpp`. Stable string-ids — they appear in the
/// emitted diagnostic format string so test code can substring-match
/// rather than depending on Clang's per-engine diag-ID integers.
inline constexpr const char* kQramOosExistingTargetId =
    "qram-oos-existing-target";
inline constexpr const char* kQramOosWriteId =
    "qram-oos-write";
inline constexpr const char* kQramOosRmwId =
    "qram-oos-rmw";
inline constexpr const char* kQramOosExpressionPositionId =
    "qram-oos-expression-position";

/// Register the four out-of-scope-shape diagnostic matchers. One
/// callback per shape is appended to the finder; on match each
/// callback resolves a custom diag-ID via
/// `DiagnosticsEngine::getCustomDiagID` (lazy-cached on the callback
/// instance) and fires one Error-severity report whose format string
/// embeds the matching `kQramOos*Id` token.
///
/// `diag` must outlive the MatchFinder's run. The matcher is advisory
/// only — it does NOT mutate any QUnit. Threading the engine directly
/// (rather than going through `DiagContext`) mirrors
/// `register_dropped_quantum_return_matcher` and keeps the matcher
/// self-contained for the unit test, which constructs its own engine
/// without spinning up a full `DiagContext` graph.
///
/// Call at most once per finder; the callback pool is owned via
/// `std::unique_ptr` in a function-local static so the matcher
/// finder's raw-pointer storage remains valid across the run.
void register_qram_oos_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::DiagnosticsEngine& diag);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_QRAM_OOS_HPP
