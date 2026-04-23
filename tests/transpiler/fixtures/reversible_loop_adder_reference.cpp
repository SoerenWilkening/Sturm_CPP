// reversible_loop_adder_reference.cpp — Phase S / S-5 hand-written
// control for the adder carry-chain reversed-iteration gate-stream
// equivalence test.
//
// Byte-for-byte twin of `reversible_loop_adder_runtime.cpp` — see the
// runtime fixture's top-of-file prose for the full rationale (R-2
// status, PH-3 pass-through, gate-stream witness).  Both TUs emit
// identical streams because both spell the same forward + reversed
// adjoint.
//
// Preprocessor contract: compiled directly by the harness (no
// transpile step), so we include the real `sturm::qbool` unconditionally.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_reversible_loop_adder_reference {

// Hand-written adder carry-chain on three qbools.  Two forward
// iterations propagating the carry left-to-right, followed by the
// reversed-iteration adjoint — twelve CX records per `demo` invocation.
void demo(sturm::qbool& q0, sturm::qbool& q1, sturm::qbool& q2) {
    sturm::qbool* regs[3] = {&q0, &q1, &q2};

    // Forward carry propagation.  Iter 0 builds the low carry into
    // regs[1]; iter 1 propagates it into regs[2].
    for (int i = 0; i < 2; ++i) {
        *regs[i+1] ^= *regs[i];
        *regs[i]   ^= *regs[i+1];
        *regs[i+1] ^= *regs[i];
    }

    // Reversed-iteration adjoint.  Same three-statement body, iter
    // 1..0, statements 3..1 (implicit by the reversed traversal).
    for (int i = 1; i >= 0; --i) {
        *regs[i+1] ^= *regs[i];
        *regs[i]   ^= *regs[i+1];
        *regs[i+1] ^= *regs[i];
    }
}

} // namespace m12_reversible_loop_adder_reference
