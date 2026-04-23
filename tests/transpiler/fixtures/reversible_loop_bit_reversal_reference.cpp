// reversible_loop_bit_reversal_reference.cpp — Phase S / S-5 hand-
// written control for the bit-reversal reversed-iteration gate-stream
// equivalence test.
//
// Byte-for-byte twin of `reversible_loop_bit_reversal_runtime.cpp` —
// see the runtime fixture's top-of-file prose for the full rationale
// (R-2 status, PH-3 pass-through, gate-stream witness).  Both TUs emit
// identical streams because both spell the same forward + reversed
// adjoint.  When Phase S's `loop_reversal` module lands and R-2 wires
// up auto-registration, this reference stays unchanged; the runtime
// fixture shrinks to just the forward loop and the adjoint becomes
// machine-emitted.
//
// Preprocessor contract: compiled directly by the harness (no
// transpile step), so we include the real `sturm::qbool` unconditionally.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_reversible_loop_bit_reversal_reference {

// Hand-written bit-reversal circuit on four qbools.  Two iterations
// of the three-statement XOR-swap, followed by the reversed-iteration
// adjoint — twelve CX records per `demo` invocation.
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3) {
    sturm::qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward XOR-swap loop.  Iter 0 swaps (q0, q3); iter 1 swaps
    // (q1, q2).
    for (int i = 0; i < 2; ++i) {
        int j = 3 - i;
        *regs[i] ^= *regs[j];
        *regs[j] ^= *regs[i];
        *regs[i] ^= *regs[j];
    }

    // Reversed-iteration adjoint.  Same body, iter 1..0.
    for (int i = 1; i >= 0; --i) {
        int j = 3 - i;
        *regs[i] ^= *regs[j];
        *regs[j] ^= *regs[i];
        *regs[i] ^= *regs[j];
    }
}

} // namespace m12_reversible_loop_bit_reversal_reference
