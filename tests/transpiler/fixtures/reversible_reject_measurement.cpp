// reversible_reject_measurement.cpp — Phase P / P-5 (sturm-z2e8.6)
// negative fixture pinning the P9d (i) measurement reject class.
//
// Canonical PRD §5.2 / P9d (i) shape
// ----------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body
// calls one of the transpiler's known measurement spellings
// (`sturm::measure_qubit`, `measure_qubit`, `sturm_measure`, ...) is
// rejected at the forward-function definition site. Measurement is
// a quantum -> classical collapse that has no inverse, so B11 / R-A
// cannot synthesise an adjoint.
//
// Under the P-C validator (`matcher_reversible_validate.cpp` line ~198
// `VisitCallExpr` + `name_is_measurement`) the call fires one
// `DiagContext::report_reversible_measurement` at Error severity. The
// validator's `ReversibleValidationResult` reports
// `reason=Measurement` with `diagnostics_fired=1` (pinned unit-test
// side by `test_matcher_reversible_validate.cpp`'s
// `test_reject_measurement_call`).
//
// Expected diagnostic format
// --------------------------
// See the sibling `.expected.diag` golden for the locked-down
// substring the harness forbids today (MUST_NOT_CONTAIN — driver
// pass-through) and will require once P-C wires into the consumer
// (MUST_CONTAIN + EXPECTED_LINE at the `int x = ...` line).
//
// Driver wiring — PASS-THROUGH
// ----------------------------
// As of sturm-z2e8.6, `validate_reversible_body` is not wired into
// `transpile_consumer.cpp`; the R-C `DriveOptions::body_validator`
// hook is null. Running `sturm-transpile` on this fixture therefore
// PASSES TODAY as a pure pass-through: the driver prepends the
// `AUTO-GENERATED` / `Source:` header, copies the body verbatim, exits
// zero, emits nothing on stderr. The companion CTest asserts
// MUST_NOT_CONTAIN against the `.expected.diag` substring. Once the
// validator wires in, that CTest flips to MUST_CONTAIN.
//
// Stub qbool + `sturm::measure_qubit`
// -----------------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths, so we inline minimal stubs. The
// measurement helper is declared (no definition needed — the AST
// walker only inspects the callee's qualified name).
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
// Declared-only measurement primitive; the P-C validator classifies
// callees by their qualified name (`sturm::measure_qubit`).
int measure_qubit(int);
} // namespace sturm
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_measurement(qbool& r, qbool a) {
    // P-C MUST cite this line — the `CallExpr` on `sturm::measure_qubit`
    // is the measurement-classifier anchor. A preamble edit that
    // shifts this line number MUST bump the CMakeLists
    // `EXPECTED_LINE` constant once the MUST_NOT_CONTAIN CTest flips
    // to MUST_CONTAIN mode.
    int x = sturm::measure_qubit(0);
    (void)x;
    r ^= a;
}
