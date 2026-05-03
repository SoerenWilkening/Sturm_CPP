// matcher_qint_implicit_warn.hpp — sturm-u9ge.14 (Beat F1) optional
// post-v1 warning matcher for the QRAM-via-array-subscript epic.
//
// Plan §9 / Beat F1; PRD §10.1 / M7. Detects every UserDefinedConversion
// site at which `sturm::frontend::qint` is implicitly converted to an
// integer type AND whose immediate enclosing expression is NOT a
// subscript. The warning is "optional follow-up" per PRD §10.1: it
// catches obvious silent-measurement mistakes (`int x = q;`,
// `std::vector<int> v(q);`, `for (size_t i = 0; i < q; ++i)`, …) without
// flagging the in-scope C1 shape `arr[q]` (which the matcher in
// `matcher_qram_subscript.{hpp,cpp}` rewrites into `QRAM_read(arr, q,
// b)`).
//
// Subscript discriminator
// -----------------------
// A UDC site is "in subscript context" iff its immediate enclosing
// expression (after walking through ParenExpr / ImplicitCastExpr layers)
// is an ArraySubscriptExpr's index slot, or a CXXOperatorCallExpr whose
// operator is `operator[]` and whose arg(1) hosts the conversion. Both
// the C-array / pointer subscript (`ArraySubscriptExpr`) and the
// std::array-style overloaded subscript (`CXXOperatorCallExpr` on
// `operator[]`) are recognised — symmetric with the C1 matcher's
// container-arm discrimination.
//
// We deliberately do NOT walk multiple levels up: a conversion buried
// inside `arr[q + 1]` lands as the LHS of a `+` expression, the
// enclosing of which is the subscript — but only one level above the
// `+` op, not the UDC site itself. That nested case is a §9 row 4
// "expression-position read" and is E1's responsibility (`qram-oos-
// expression-position` Error). F1 deliberately stays a one-hop check
// so it never overlaps E1.
//
// Severity & opt-in
// -----------------
// `DiagnosticsEngine::Warning` (NOT Error). Suppressed by default in v1
// — until the transpiler grows a `-W*` flag mechanism comparable to
// Clang's, the gate is a single static bool exposed by
// `set_qint_implicit_measure_warning_enabled`. The placeholder mirrors
// the mechanism the transpiler driver will eventually expose as
// `-Wsturm-qint-implicit-measure`; documenting the placeholder keeps the
// flag's spelling pinned even before a flag parser exists.
//
// Coexistence with C1 / E1
// ------------------------
// C1 (matcher_qram_subscript) anchors on the VarDecl init shape `qint
// b = a[i];` — the UDC there lives inside an ArraySubscriptExpr / op-
// call subscript, so F1's "not in subscript" gate skips it. E1
// (matcher_qram_oos) fires Error-severity for the four out-of-scope
// shapes; F1 may also fire on those (e.g. `qint c = a[i] + d;` where
// the UDC's enclosing is `+`, not `[]`), giving the user one Error
// from E1 and one Warning from F1 — both correct and complementary.
//
// LoC budget: <= 200 (plan §1, §9 / F1).

#ifndef STURM_TRANSPILE_MATCHER_QINT_IMPLICIT_WARN_HPP
#define STURM_TRANSPILE_MATCHER_QINT_IMPLICIT_WARN_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

namespace clang {
class DiagnosticsEngine;
} // namespace clang

namespace sturm::transpile {

/// Stable string id for the warning. Embedded in the format string so
/// tests / drivers can substring-match instead of relying on Clang's
/// per-engine integer ID.
inline constexpr const char* kQintImplicitMeasureId =
    "qint-implicit-measure";

/// Toggle the warning. Default state: false (suppressed). The transpiler
/// driver will flip this to true on `-Wsturm-qint-implicit-measure`
/// once a flag parser exists; until then library / test code calls
/// `set_qint_implicit_measure_warning_enabled(true)` directly.
///
/// Thread-safety: the gate is a single boolean; readers / writers race
/// at most by one instruction, and the warning is advisory anyway. No
/// synchronisation; mirrors the gating posture in
/// `transpiler/src/diag_context.cpp` for the project's other Warning-
/// severity diags.
void set_qint_implicit_measure_warning_enabled(bool on);

/// Read the current gate. Exposed for tests that want to verify the
/// default-off contract without poking at the underlying static.
bool qint_implicit_measure_warning_enabled();

/// Register the F1 warning matcher against `finder`. On match, fire
/// one `DiagnosticsEngine::Warning`-severity diagnostic per
/// non-subscript-context UDC site, citing PRD §10.1 in the body.
///
/// `diag` must outlive the MatchFinder's run. Call at most once per
/// finder; the callback pool is owned via a function-local static
/// `unique_ptr` vector so the finder's raw-pointer storage stays valid
/// across the run.
void register_qint_implicit_warn_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::DiagnosticsEngine& diag);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_QINT_IMPLICIT_WARN_HPP
