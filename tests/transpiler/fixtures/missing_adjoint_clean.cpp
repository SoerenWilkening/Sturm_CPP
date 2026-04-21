// PM3-3 negative-case (clean) input fixture for the
// `plugin_diagnostic_missing_adjoint_clean` CTest.
//
// Purpose
// -------
// Pin the NEGATIVE side of the PM3-3 triage: a CallExpr that targets
// a function with NO quantum OUTPUT parameters (every quantum-typed
// parameter is `const qbool&` / `const qint&`, i.e. INPUT only) MUST
// NOT raise the PM3-3 Error — even when the function is NOT
// registered via STURM_REGISTER_ADJOINT. The pre-PM3-3 silent
// early-return still applies to this case: an unregistered callee
// whose `outputs_mask == 0` is a pure classical side-effect call, and
// the transpiler should leave it alone.
//
// The companion positive fixture
// `missing_adjoint_diagnostic_input.cpp` takes the same `op(...)`
// callsite but declares its parameters as non-const `qbool&` so
// `outputs_mask != 0`; on that shape the PM3-3 matcher fires the
// diagnostic.
//
// Shape
// -----
// A minimal `sturm::qbool` stub plus a free function `op(const
// qbool&, const qbool&)` with TWO const qbool& parameters — both are
// INPUT slots by the PI-2 `is_output_param` rule, so the matcher's
// pre-registry-check outputs_mask scan yields 0. No
// STURM_REGISTER_ADJOINT is issued, so the PI-1 matcher does not add
// the function to the RoutineRegistry. The PI-2 triage then:
//
//   - registry miss (PI-1 never registered it)  → true
//   - outputs_mask != 0                          → FALSE
//
// The combined condition `miss && mask_nonzero` is false, so the
// matcher takes the silent early-return. No QOperation is pushed
// (the function's call is not part of the STURM uncompute protocol)
// AND no diagnostic is emitted.
//
// Acceptance (per PM3-3 issue):
//   - sturm-transpile exits zero (no error).
//   - stderr does NOT contain the substring `no adjoint is registered`
//     (if it did, the matcher fired on a shape it should have
//     rejected).
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};

} // namespace sturm
using sturm::qbool;

// Free function with TWO const qbool& parameters — both are INPUT
// slots by the PI-2 `is_output_param` rule, so the matcher's pre-
// registry-check outputs_mask scan yields 0. The triage takes the
// silent early-return, no diagnostic fires.
void op(const qbool& x, const qbool& y) {
    (void)x;
    (void)y;
}

void demo() {
    qbool a;
    qbool b;
    // This call reaches the PI-2 matcher just like the positive
    // fixture, but the callee's parameter types are INPUT-only, so
    // the matcher's PM3-3 branch is skipped — no diagnostic.
    op(a, b);
}
