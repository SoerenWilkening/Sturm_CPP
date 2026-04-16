// if_branches_reference.cpp — Phase H / PH-6b hand-written control for
// the if/else gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness will link against after the transpiler emits the OR matcher
// lowerings into if_branches_runtime.cpp.  Each branch body is
// deliberately byte-similar to the transpiler's expected output: a
// single `qbool <var> = a | b;` followed by the explicit
// `sturm::uncompute_or(<var>, a, b);` call that the transpiler injects.
//
// Having a hand-written control checked in directly is what turns
// PH-6b into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and any
// drift in either the transpiler's output or the runtime OR/uncompute
// decomposition shows up as a mismatch in the captured gate stream.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` / `sturm::uncompute_or` so that each branch emits the
// forward three-gate OR (CX+CX+CCX) and its three-gate adjoint
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

namespace m12_if_branches_reference {

// Hand-written realisation of the Phase H if/else lowering.  Each
// branch emits six gates (three forward + three inverse):
//   then-arm:
//     qbool x = a | b;                 → CX(a,x) + CX(b,x) + CCX(a,b,x)
//     sturm::uncompute_or(x, a, b);    → CCX(a,b,x) + CX(b,x) + CX(a,x)
//   else-arm:
//     qbool y = a | b;                 → CX(a,y) + CX(b,y) + CCX(a,b,y)
//     sturm::uncompute_or(y, a, b);    → CCX(a,b,y) + CX(b,y) + CX(a,y)
//
// `const sturm::qbool&` arguments preserve the caller's qubit indices;
// see the rationale in test_gate_equivalence.cpp.  The `cond` parameter
// is a classical bool — only one branch fires per call.  The harness
// invokes this `demo` twice (cond=true, cond=false) so the captured
// stream exercises both branch lowerings.
void demo(const sturm::qbool& a, const sturm::qbool& b, bool cond) {
    if (cond) {
        sturm::qbool x = a | b;
        sturm::uncompute_or(x, a, b);
    } else {
        sturm::qbool y = a | b;
        sturm::uncompute_or(y, a, b);
    }
}

} // namespace m12_if_branches_reference
