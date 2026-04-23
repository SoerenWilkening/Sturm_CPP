// reversible_loop_ripple_reference.cpp — Phase S / S-5 hand-written
// control for the ripple reversed-iteration gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the transpiler processes
// `reversible_loop_ripple_runtime.cpp`.  The demo body is a byte-for-byte
// twin of the runtime fixture — same forward for-loop, same reversed-
// iteration adjoint — because R-2 (auto_register_emitter) is not yet
// landed and the transpiler's pass for this fixture is currently a
// no-op (the transpiler emits nothing that reshapes the body).  When R-2
// + Phase S's `loop_reversal` module land, the runtime fixture will
// shrink to just the forward loop and the reversed adjoint will be
// machine-emitted — the reference here stays unchanged as the
// ground-truth byte-stream the transpiler's emission must match.
//
// Having a hand-written control checked in directly is what turns S-5
// into a meaningful capstone: the test is a cross-check between two
// independent realisations of the same circuit, and any drift in the
// transpiler's loop-reversal emission, the runtime `qbool::operator^=`
// decomposition, or the QubitPool non-owning index invariants would
// show up as a mismatch in the captured gate stream.
//
// Gate-stream witness
// -------------------
// Identical to the runtime fixture's per-invocation stream:
//   Forward i=1..3:   CX(q0,q1), CX(q1,q2), CX(q2,q3)   [3 CX]
//   Adjoint i=3..1:   CX(q2,q3), CX(q1,q2), CX(q0,q1)   [3 CX]
// Six CX records per invocation; the harness invokes `demo` three times
// per capture (3 independent payloads per pair), so the total captured
// stream is 18 CX records.
//
// ODR note
// --------
// The harness links this TU together with the runtime fixture.  Both
// define `demo(qbool&, qbool&, qbool&, qbool&)` inside their own
// namespace to avoid collisions.
//
// Preprocessor contract: this file is compiled directly (no transpile
// step), so it can include the real `sturm::qbool` / `qbool::operator^=`
// unconditionally — no `__has_include` guard is needed.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_reversible_loop_ripple_reference {

// Hand-written realisation of the Phase S ripple reversed-iteration
// adjoint.  Byte-for-byte twin of the runtime fixture: the pointer-
// array indirection, the forward i=1..3 sweep, and the reversed i=3..1
// adjoint sweep are all spelled the same way.  Using identical source
// keeps the captured gate streams byte-identical across the transpile
// vs. direct-compile boundary.
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3) {
    sturm::qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward sweep: each cell XORs its left neighbour.  Inter-
    // iteration data dependency (iter i reads regs[i-1] which was
    // written by iter i-1 two iters back) is the load-bearing
    // property that makes the reversed-iteration adjoint meaningful.
    for (int i = 1; i < 4; ++i) {
        *regs[i] ^= *regs[i-1];
    }

    // Reversed-iteration adjoint (B11).  Same body, reversed iter
    // order — cancels the forward pass bit-for-bit at the state level
    // and byte-for-byte at the gate-stream level.
    for (int i = 3; i >= 1; --i) {
        *regs[i] ^= *regs[i-1];
    }
}

} // namespace m12_reversible_loop_ripple_reference
