// or_single_reference.cpp — M12 hand-written control for the
// gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness will link against after the transpiler emits
// `uncompute_or(tmp, a, b);` into `or_single_runtime.cpp`.  The body is
// deliberately byte-similar to the transpiler's expected output: a
// single `qbool tmp = a | b;` followed by the explicit
// `sturm::uncompute_or(tmp, a, b);` call that the transpiler injects.
//
// Having a hand-written control checked in directly is what turns M12
// into a meaningful capstone — the test is then a cross-check between
// two independent realizations of the same circuit, and any drift in
// either the transpiler's output or the runtime OR decomposition shows
// up as a mismatch in the captured gate stream.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` so that `a | b` emits the full three-gate OR circuit
// and `sturm::uncompute_or` emits its three-gate adjoint.  The
// umbrella header is therefore included directly — no `__has_include`
// guard is necessary here because this file never passes through the
// transpiler (CMake compiles it as-is) and the test target always
// sees `sturm`'s include directory.
//
// Namespace isolation: the harness links this TU together with the
// transpiled TU; both define a `demo(...)` function.  Wrapping each
// `demo` in a dedicated namespace avoids an ODR collision.

#include "sturm/sturm.hpp"
// The umbrella `sturm/sturm.hpp` only pulls in the uncompute free-
// function API.  The real quantum `operator|` and its lazy-expression
// wrapper live in `qbool_ops.hpp` / `lazy_expr.hpp`, so include them
// explicitly — without them the `a | b` below would hit the eager
// `qbool_logic.hpp` path (which does not emit gates) or fail to
// compile entirely.
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_reference {

// The body is the hand-written realization of what the transpiler must
// emit.  `qbool tmp = a | b;` exercises the forward OR circuit
// (see include/sturm/qtypes/qbool_ops.hpp `OrExpr<qbool>::operator
// qbool()`); `sturm::uncompute_or(tmp, a, b);` emits the three-gate
// adjoint (see src/sturm/uncompute/uncompute_api.cpp).  Together, the
// two calls yield the six-gate CX+CX+CCX+CCX+CX+CX stream that the
// test harness compares against the transpiler's output.
//
// `const sturm::qbool&` arguments preserve the caller's qubit
// indices; see the rationale in test_gate_equivalence.cpp.
void demo(const sturm::qbool& a, const sturm::qbool& b) {
    sturm::qbool tmp = a | b;
    sturm::uncompute_or(tmp, a, b);
}

// LP7: second reference that mirrors `examples/or_circuit.cpp`'s VarDecl
// (`qbool c = a | b;`) so the M12 harness can bind PRD acceptance #5 to
// the real example's pattern. The explicit `uncompute_or(c, a, b);` call
// is what the transpiler must inject into the matching runtime fixture.
void demo_or_circuit(const sturm::qbool& a, const sturm::qbool& b) {
    sturm::qbool c = a | b;
    sturm::uncompute_or(c, a, b);
}

} // namespace m12_reference
