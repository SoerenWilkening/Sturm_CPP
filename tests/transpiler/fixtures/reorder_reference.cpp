// reorder_reference.cpp — Phase M / PM5-8 hand-written control for
// the peephole-reorder gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the transpiler processes
// reorder_runtime.cpp.  The demo body is deliberately byte-similar
// to the transpiler's expected output: the PE-4 flattened pair
// `qbool __stu_t0 = a | b;  qbool r = __stu_t0 | c;` is spelled out
// verbatim (because the reorder does NOT fire on this shape — see
// the reorder_runtime.cpp top-of-file prose), followed by the two
// user-written `^=` statements and their LIFO self-adjoint
// counterparts at scope close.
//
// Having a hand-written control checked in directly is what turns
// the PM5 gate-equivalence pair into a meaningful capstone — the
// test is a cross-check between two independent realisations of
// the same circuit, and any drift in the transpiler's peephole-
// reorder decision logic, the PE-4 compound-flatten emission, the
// MVP OR uncompute decomposition, or the PA-3 self-adjoint
// rendering shows up as a mismatch in the captured gate stream.
//
// Gate-stream witness
// -------------------
// See reorder_runtime.cpp's "Gate-stream witness" section for the
// per-gate accounting.  Total gate count is structurally parity-
// equivalent between the runtime and reference fixtures; the
// m12 harness compares the streams gate-for-gate, operand-for-
// operand.
//
// ODR note
// --------
// The harness links this TU together with reorder_runtime.cpp.
// Both define `demo(const qbool&, const qbool&, const qbool&,
// qbool&, qbool&)` inside their own namespace to avoid collisions
// at the demo level.
//
// Preprocessor contract: we want this TU to compile against the
// real `sturm::qbool` / `sturm::uncompute_or` / `operator|` /
// `operator^=` so the captured gate stream actually reflects the
// runtime behaviour.  The umbrella header is included directly —
// no `__has_include` guard is necessary here because this file
// never passes through the transpiler (CMake compiles it as-is)
// and the test target always sees `sturm`'s include directory.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_reorder_reference {

// Hand-written realisation of the PM5 no-op path on this fixture.
// Shape mirrors the expected transpiler output verbatim:
//
//   PE-4 flatten decl         `qbool __stu_t0 = a | b; qbool r = __stu_t0 | c;`
//   user-written B            `y ^= x;`
//   user-written C'           `x ^= r;`
//   LIFO self-adjoint (x^=r)  `x ^= r;`
//   LIFO self-adjoint (y^=x)  `y ^= x;`
//   MVP OR uncompute (outer)  `sturm::uncompute_or(r, __stu_t0, c);`
//   MVP OR uncompute (inner)  `sturm::uncompute_or(__stu_t0, a, b);`
//
// `const sturm::qbool&` arguments preserve the caller's qubit
// indices; `sturm::qbool&` on x / y matches the runtime fixture's
// non-const `^=` targets.  All qubits are caller-owned; the
// reference body never allocates.
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          const sturm::qbool& c,
          sturm::qbool& x,
          sturm::qbool& y) {
    sturm::qbool __stu_t0 = a | b;
    sturm::qbool r = __stu_t0 | c;
    y ^= x;
    x ^= r;
    // LIFO self-adjoints — operator^= is self-adjoint so the adjoint
    // is a second `^=` with the same operands.
    x ^= r;
    y ^= x;
    // MVP OR uncomputes — outer first (LIFO), then inner.
    sturm::uncompute_or(r, __stu_t0, c);
    sturm::uncompute_or(__stu_t0, a, b);
    (void)r;
}

} // namespace m12_reorder_reference
