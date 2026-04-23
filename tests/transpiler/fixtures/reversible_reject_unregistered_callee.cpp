// reversible_reject_unregistered_callee.cpp — Phase P / P-5
// (sturm-z2e8.6) negative fixture pinning the P9d (iii) unregistered-
// callee reject class.
//
// Canonical PRD §5.2 / P9d (iii) shape
// ------------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body
// calls a function that is neither `[[sturm::reversible]]` nor bound
// in the PI-1 `RoutineRegistry` is rejected at the forward-function
// definition site. The transpiler cannot synthesise an adjoint for
// callees whose reversibility contract is not declared.
//
// Under the P-C validator (`matcher_reversible_validate.cpp` line ~198
// `VisitCallExpr`) the call falls through the measurement / I/O
// classifiers, skips the primitive op-call / UDC-conversion escape
// hatches, and fires one `DiagContext::report_reversible_
// unregistered_callee` at Error severity. The validator's
// `ReversibleValidationResult` reports `reason=UnregisteredCallee`
// with `diagnostics_fired=1` (pinned unit-test side by
// `test_matcher_reversible_validate.cpp` — see
// `test_reject_unregistered_callee`).
//
// Expected diagnostic format
// --------------------------
// See the sibling `.expected.diag` golden for the locked-down
// substring the harness forbids today (MUST_NOT_CONTAIN — driver
// pass-through) and will require once P-C wires into the consumer
// (MUST_CONTAIN + EXPECTED_LINE at the `helper(...)` line). Note the
// diagnostic's `%0` is the CALLEE's qualified name (`helper`), NOT
// the enclosing reversible routine — the user's fix targets the
// callee (mark it `[[sturm::reversible]]` or register an adjoint).
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
// Stub qbool + plain `helper`
// ---------------------------
// `helper` is a plain non-quantum, non-reversible, non-registered
// function. The transpiler cannot synthesise its adjoint; P-C rejects
// the call.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
using sturm::qbool;

// Plain forward — NOT `[[sturm::reversible]]`, NOT in the PI-1
// `RoutineRegistry`. Declared with a `const qbool&` parameter so the
// pre-existing PI-2 / PM3-3 missing-adjoint matcher (which probes
// for NON-CONST `qbool&` output params) does NOT fire on this
// callsite — the pre-existing matcher's `is_output_param` rule
// yields `outputs_mask == 0`, so its early-return fires and no
// Error surfaces from it.
//
// The P-C walker we are pinning, however, classifies the call by
// callee-kind (not by output-param mask): `helper` is neither
// `[[sturm::reversible]]` nor in the PI-1 `RoutineRegistry`, so the
// P-C walker's `VisitCallExpr` fall-through path fires
// `report_reversible_unregistered_callee`. That is the diagnostic
// the sibling `.expected.diag` pins.
void helper(const qbool& /*x*/) {}

[[clang::annotate("sturm::reversible")]]
void bad_unregistered(qbool& r, qbool a) {
    // P-C MUST cite this line — the `CallExpr` on `helper(r)` is the
    // unregistered-callee classifier anchor. A preamble edit that
    // shifts this line number MUST bump the CMakeLists
    // `EXPECTED_LINE` constant once the MUST_NOT_CONTAIN CTest flips
    // to MUST_CONTAIN mode.
    helper(r);
    r ^= a;
}

// Phase T T-2 (sturm-xrob.3): the PRD §9 Q2 error-emission gate fires
// only when the TU contains at least one `sturm::invert(&fd)` call
// site targeting this forward. We add the canonical `sturm::invert`
// stub + a call site below so condition (3) holds and the P-C
// unregistered-callee diagnostic is not swallowed by the silence guard.
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
} // namespace sturm

void invoke_bad_unregistered_adjoint() {
    auto p = sturm::invert(&bad_unregistered);
    (void)p;
}
