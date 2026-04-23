// reversible_reject_io.cpp — Phase P / P-5 (sturm-z2e8.6) negative
// fixture pinning the P9d (ii) classical I/O reject class.
//
// Canonical PRD §5.2 / P9d (ii) shape
// -----------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body
// calls a function in the transpiler's known classical-I/O set
// (`printf`, `fprintf`, `std::printf`, stream operators, `abort`, ...)
// is rejected at the forward-function definition site. I/O is an
// observable classical side effect that cannot be undone by gate
// reversal.
//
// Under the P-C validator (`matcher_reversible_validate.cpp` line ~210
// `VisitCallExpr` + `name_is_classical_io`) the call fires one
// `DiagContext::report_reversible_io` at Error severity. The
// validator's `ReversibleValidationResult` reports
// `reason=ClassicalIO` with `diagnostics_fired=1` (pinned unit-test
// side by `test_matcher_reversible_validate.cpp` — see
// `test_reject_classical_io`).
//
// Expected diagnostic format
// --------------------------
// See the sibling `.expected.diag` golden for the locked-down
// substring the harness forbids today (MUST_NOT_CONTAIN — driver
// pass-through) and will require once P-C wires into the consumer
// (MUST_CONTAIN + EXPECTED_LINE at the `std::printf(...)` line).
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
// Stub qbool + `std::printf`
// --------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths; we inline the minimal stubs the parser
// needs. `std::printf` is declared (not defined) because the P-C
// classifier only inspects the callee's qualified name.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
namespace std {
// Declared-only I/O primitive; the P-C validator classifies callees
// by their qualified name (`std::printf`).
int printf(const char*, ...);
} // namespace std
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_io(qbool& r, qbool a) {
    // P-C MUST cite this line — the `CallExpr` on `std::printf` is
    // the I/O-classifier anchor. A preamble edit that shifts this
    // line number MUST bump the CMakeLists `EXPECTED_LINE` constant
    // once the MUST_NOT_CONTAIN CTest flips to MUST_CONTAIN mode.
    std::printf("reversible oracle running\n");
    r ^= a;
}

// Phase T T-2 (sturm-xrob.3): the PRD §9 Q2 error-emission gate fires
// only when the TU contains at least one `sturm::invert(&fd)` call
// site targeting this forward. We add the canonical `sturm::invert`
// stub + a call site below so condition (3) holds and the P-C I/O
// diagnostic is not swallowed by the silence guard.
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
} // namespace sturm

void invoke_bad_io_adjoint() {
    auto p = sturm::invert(&bad_io);
    (void)p;
}
