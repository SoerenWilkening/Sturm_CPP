// for_loop_reference.cpp — Phase H / PH-6a hand-written control for
// the for-loop gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness will link against after the transpiler emits the OR matcher
// lowering into for_loop_runtime.cpp.  The loop body is deliberately
// byte-similar to the transpiler's expected output: a single
// `qbool tmp = a | b;` followed by the explicit
// `sturm::uncompute_or(tmp, a, b);` call that the transpiler injects.
//
// Having a hand-written control checked in directly is what turns
// PH-6a into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and any
// drift in either the transpiler's output or the runtime OR/uncompute
// decomposition shows up as a mismatch in the captured gate stream.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` / `sturm::uncompute_or` so that each iteration emits
// the forward three-gate OR (CX+CX+CCX) and its three-gate adjoint
// (CCX+CX+CX).  The umbrella header is included directly — no
// `__has_include` guard is necessary here because this file never
// passes through the transpiler (CMake compiles it as-is) and the
// test target always sees `sturm`'s include directory.
//
// Namespace isolation: the harness links this TU together with the
// transpiled TU; both define a `demo(...)` function.  Wrapping each
// `demo` in a dedicated namespace avoids an ODR collision.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_for_loop_reference {

// Hand-written realisation of the Phase H for-loop lowering.  Each
// iteration emits six gates (three forward + three inverse):
//   qbool tmp = a | b;                 → CX(a,tmp) + CX(b,tmp) + CCX(a,b,tmp)
//   sturm::uncompute_or(tmp, a, b);    → CCX(a,b,tmp) + CX(b,tmp) + CX(a,tmp)
// `tmp`'s RAII destructor releases the ancilla index at the loop
// body's `}`, and because `QubitPool` is a LIFO free-list the next
// iteration's `allocate()` hands the SAME index back out.  Three
// iterations × six gates = eighteen gates, with identical qubit
// indices across iterations — this is the stream the transpiled
// companion must match byte-for-byte.
//
// `const sturm::qbool&` arguments preserve the caller's qubit indices;
// see the rationale in test_gate_equivalence.cpp.
void demo(const sturm::qbool& a, const sturm::qbool& b) {
    for (int i = 0; i < 3; ++i) {
        sturm::qbool tmp = a | b;
        sturm::uncompute_or(tmp, a, b);
    }
}

} // namespace m12_for_loop_reference
