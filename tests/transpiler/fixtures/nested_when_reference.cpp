// nested_when_reference.cpp — Phase G / PG-7 hand-written control for
// the nested-WHEN gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness will link against after the transpiler emits the Phase G
// nested-WHEN lowering into `nested_when_runtime.cpp`.  The body is
// deliberately byte-similar to the transpiler's expected output:
//
//     qbool __stu_ctrl0 = outer & inner;
//     WHEN(__stu_ctrl0) { target.flip(); }
//     sturm::uncompute_and(__stu_ctrl0, outer, inner);
//
// Having a hand-written control checked in directly is what turns
// PG-7 into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and any
// drift in either the transpiler's output or the runtime AND/WHEN
// decomposition shows up as a mismatch in the captured gate stream.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` / `sturm::uncompute_and` / `WHEN` so the `outer &
// inner` materialisation emits the CCX(outer, inner, __stu_ctrl0)
// forward AND, the lifted `target.flip()` under the active WHEN
// control emits CX(__stu_ctrl0, target), and `uncompute_and` emits
// the self-inverse CCX(outer, inner, __stu_ctrl0) adjoint.  The
// umbrella header is included directly — no `__has_include` guard is
// necessary here because this file never passes through the
// transpiler (CMake compiles it as-is) and the test target always
// sees `sturm`'s include directory.
//
// Namespace isolation: the harness links this TU together with the
// transpiled TU; both define a `demo(...)` function.  Wrapping each
// `demo` in a dedicated namespace avoids an ODR collision.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/control/when.hpp"

namespace m12_nested_reference {

// Hand-written realisation of the Phase G nested-WHEN lift.  The
// three gate emissions are:
//   1. `qbool __stu_ctrl0 = outer & inner;`
//        → AndExpr<qbool>::operator qbool() allocates an ancilla
//          and emits CCX(outer, inner, __stu_ctrl0).
//   2. `WHEN(__stu_ctrl0) { target.flip(); }`
//        → WhenGuard pushes __stu_ctrl0 onto ctx->control_stack,
//          emit_X_lifted sees depth==1 and emits CX(__stu_ctrl0,
//          target), WhenGuard pops on scope exit.
//   3. `sturm::uncompute_and(__stu_ctrl0, outer, inner);`
//        → self-inverse CCX(outer, inner, __stu_ctrl0).
//
// Total: three gates — the stream the transpiled companion must
// match byte-for-byte.  `const sturm::qbool&` arguments preserve the
// caller's qubit indices; see the rationale in
// test_gate_equivalence.cpp.
// Note: `outer` and `inner` are taken by non-const reference to match
// the companion runtime fixture's signature (WHEN(expr) requires a
// non-const lvalue, so the transpiled variant cannot take them as
// const-refs).  `target` likewise is taken by non-const reference so
// `target.flip()` emits its lifted X against the active control stack.
// The caller (`run_and_capture_nested`) retains ownership via
// `qbool::make_non_owning`, so no qbool destructor will double-release.
void demo(sturm::qbool& outer,
          sturm::qbool& inner,
          sturm::qbool& target) {
    sturm::qbool __stu_ctrl0 = outer & inner;
    WHEN(__stu_ctrl0) {
        target.flip();
    }
    sturm::uncompute_and(__stu_ctrl0, outer, inner);
}

} // namespace m12_nested_reference
