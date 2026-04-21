// PM3-3 positive-case input fixture for the
// `plugin_diagnostic_missing_adjoint_fires` CTest.
//
// Purpose
// -------
// Pin the PM3-3 "Class 3 — missing adjoint registration" contract: a
// CallExpr that targets a function with at least one quantum OUTPUT
// parameter (non-const `qbool&` / `qint&`) AND that function is NOT
// registered via STURM_REGISTER_ADJOINT MUST raise the
// `DiagnosticsEngine::Error` configured by
// `DiagContext::report_missing_adjoint`. Error severity — compilation
// must abort non-zero, because the transpiler cannot plant an
// `invert(op)(...)` call without a registered adjoint, and silently
// skipping the uncompute would leak qubits (contradicts P9).
//
// Shape
// -----
// A minimal `sturm::qbool` stub plus a free function `op(qbool&,
// qbool&)` with TWO non-const qbool& output parameters — so the
// callsite's computed `outputs_mask` is `0b11` (non-zero). The
// function is NOT registered with STURM_REGISTER_ADJOINT, so the
// PI-1 routine-registry pass leaves it out of the registry. The PI-2
// matcher's PM3-3 triage path runs: registry miss + outputs_mask != 0
// → `diag.report_missing_adjoint(...)` → Error on stderr.
//
// The companion negative fixture `missing_adjoint_clean.cpp` takes
// the same `op(...)` callsite but declares its parameters as `const
// qbool&` so `outputs_mask == 0`; the matcher's triage then takes the
// silent early-return and no diagnostic fires.
//
// Expected diagnostic substring on stderr:
//     no adjoint is registered
// or
//     STURM_REGISTER_ADJOINT
//
// The callee qualified name interpolated into the Error message is
// `op` (free function, no enclosing namespace), so the locked-down
// text reads:
//
//     STURM: call to 'op' requires uncomputation (has quantum output
//     parameter), but no adjoint is registered. Use
//     STURM_REGISTER_ADJOINT(op, <adjoint_fn>) at TU scope.
//
// Acceptance (per PM3-3 issue):
//   - Error on stderr (substring `no adjoint is registered`).
//   - sturm-transpile exit code non-zero (Error promotes to hard fail).
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};

} // namespace sturm
using sturm::qbool;

// Free function with TWO non-const qbool& parameters — both are
// OUTPUT slots by the PI-2 `is_output_param` rule, so the PI-2
// matcher's pre-registry-check outputs_mask scan yields 0b11. No
// STURM_REGISTER_ADJOINT is issued on this function, so the PI-1
// matcher does not add it to the RoutineRegistry. The PI-2 triage
// then takes the missing-adjoint branch and fires the PM3-3
// diagnostic.
void op(qbool& x, qbool& y) {
    (void)x;
    (void)y;
}

void demo() {
    qbool a;
    qbool b;
    // The PM3-3 diagnostic MUST cite this line — the `op(a, b);`
    // CallExpr is the match anchor. Harness keeps the line number
    // stable by pinning it via grep so a reshuffle of the preamble
    // above keeps working.
    op(a, b);
}
