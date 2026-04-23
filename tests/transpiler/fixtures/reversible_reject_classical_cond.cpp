// reversible_reject_classical_cond.cpp — Phase P / P-5 (sturm-z2e8.6)
// negative fixture pinning the P9d (v) quantum-dependent classical
// condition reject class.
//
// Canonical PRD §5.2 / P9d (v) shape
// ----------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body
// branches on a classical condition derived from a quantum value is
// rejected at the forward-function definition site. The branch
// collapses the quantum state into a classical bit, which breaks the
// WHEN primitive's lexical-scope control semantics (P4).
//
// Under the P-C validator (`matcher_reversible_validate.cpp` line ~184
// `VisitIfStmt` -> `check_cond_is_quantum`) a cond whose static type
// (post-implicit-cast strip) is a quantum record fires one
// `DiagContext::report_reversible_classical_cond` at Error severity.
// The validator's `ReversibleValidationResult` reports
// `reason=ClassicalCond` with `diagnostics_fired=1` (pinned unit-test
// side by `test_matcher_reversible_validate.cpp`'s classical-cond
// tests).
//
// Why the cond is `qbool`, not `(bool)qbool`
// ------------------------------------------
// Two overlapping detection surfaces exist in the transpiler:
//
//   1. The PRE-EXISTING PM3-5 matcher
//      (`matcher_quantum_to_classical_cond.cpp`) already fires at
//      Error severity on `if ((bool)q)` shapes that reach the cond
//      via an EXPLICIT cast-to-bool, regardless of the enclosing
//      routine's `[[sturm::reversible]]` annotation.
//
//   2. The P-C validator's `check_cond_is_quantum` fires on a cond
//      whose static type (after `IgnoreParenImpCasts` strip) is a
//      quantum record — the `if (q)` implicit-conversion shape.
//
// If this fixture spelled the cond as `(bool)a`, PM3-5 would fire at
// Error severity TODAY — making the MUST_NOT_CONTAIN CTest
// contract (which requires rc=0) unsatisfiable independent of the
// P-C wiring state. Instead, the fixture uses `if (a)` against a
// `qbool` whose `operator bool()` is NON-explicit. The IMPLICIT
// conversion wraps the `DeclRefExpr(a)` in an
// `ImplicitCastExpr<UserDefinedConversion>`; `IgnoreParenImpCasts`
// strips it, leaving a DeclRefExpr of type `qbool`, which the P-C
// validator's `check_cond_is_quantum` classifies as quantum-dep.
// PM3-5 cannot fire because there is no EXPLICIT cast node in the
// cond — its `cxxStaticCastExpr` / `cStyleCastExpr` /
// `cxxFunctionalCastExpr` anchors are absent.
//
// Expected diagnostic format
// --------------------------
// See the sibling `.expected.diag` golden for the locked-down
// substring the harness forbids today (MUST_NOT_CONTAIN — driver
// pass-through) and will require once P-C wires into the consumer
// (MUST_CONTAIN + EXPECTED_LINE at the `if (a)` line).
//
// Driver wiring — PASS-THROUGH
// ----------------------------
// As of sturm-z2e8.6, `validate_reversible_body` is not wired into
// `transpile_consumer.cpp`; the R-C `DriveOptions::body_validator`
// hook is null. Running `sturm-transpile` on this fixture therefore
// PASSES TODAY as a pure pass-through — the driver prepends the
// `AUTO-GENERATED` / `Source:` header, copies the body verbatim,
// exits zero, emits nothing on stderr. The companion CTest asserts
// MUST_NOT_CONTAIN against the `.expected.diag` substring; once the
// validator wires in, that CTest flips to MUST_CONTAIN.
//
// Stub qbool with non-explicit `operator bool()`
// ----------------------------------------------
// Minimal stub; `operator bool()` is NOT `explicit` specifically to
// give the parser a valid `if (qbool)` implicit-conversion path.
// The production `sturm::qbool` declares its `operator bool()` as
// `explicit` — this stub diverges from that production shape to
// isolate the P-C `check_cond_is_quantum` classifier from the
// pre-existing PM3-5 cast-in-branch matcher. Tests that want to
// exercise the production `explicit` discipline continue to live in
// `test_matcher_quantum_to_classical_cond.cpp` and
// `tests/transpiler/fixtures/quantum_to_classical_cond_input.cpp`.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    operator bool() const noexcept { return false; }
};
} // namespace sturm
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_classical_cond(qbool& r, qbool a) {
    // P-C MUST cite this line — the `IfStmt` anchor's `getIfLoc()`
    // is what `check_cond_is_quantum` passes to
    // `report_reversible_classical_cond`. A preamble edit that
    // shifts this line number MUST bump the CMakeLists
    // `EXPECTED_LINE` constant once the MUST_NOT_CONTAIN CTest flips
    // to MUST_CONTAIN mode.
    if (a) {
        r ^= a;
    }
}

// Phase T T-2 (sturm-xrob.3): the PRD §9 Q2 error-emission gate fires
// only when the TU contains at least one `sturm::invert(&fd)` call
// site targeting this forward. We add the canonical `sturm::invert`
// stub + a call site below so condition (3) holds and the P-C
// classical-cond diagnostic is not swallowed by the silence guard.
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
} // namespace sturm

void invoke_bad_classical_cond_adjoint() {
    auto p = sturm::invert(&bad_classical_cond);
    (void)p;
}
