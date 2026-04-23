// reversible_loop_adder_runtime.cpp — Phase S / S-5 transpiler INPUT
// fixture for the adder carry-chain reversed-iteration gate-stream
// equivalence pair.
//
// Fourteenth gate-equivalence pair.  Companion to the ripple and
// bit-reversal fixtures — see `reversible_loop_ripple_runtime.cpp` for
// the full Phase S / B11 / R-2 / PH-3 rationale.  This fixture pins
// down the iteration-reversal contract on a loop body that mirrors the
// inner loop of a classical ripple-carry adder: the carry propagates
// left-to-right on the forward pass (each iteration writes regs[i+1]
// using regs[i] written by the previous iteration), and the adjoint
// MUST walk the loop right-to-left to unwind the carry correctly.
//
// Unlike the bit-reversal shape, this fixture's three-statement body
// is ASYMMETRIC in its operand ordering — the middle statement has a
// swapped LHS/RHS relative to the bookend statements — so reversing
// the intra-iteration statement order produces a visibly different
// gate sequence than the forward body.  This exercises the second
// half of the B11 "reverse statement order AND loop iteration order"
// contract: both reversal dimensions are needed for the gate stream
// to cancel bit-for-bit.
//
// Per-iteration body (three statements, asymmetric)
// -------------------------------------------------
//   stmt 1: *regs[i+1] ^= *regs[i];   → CX(regs[i], regs[i+1])
//   stmt 2: *regs[i]   ^= *regs[i+1]; → CX(regs[i+1], regs[i])
//   stmt 3: *regs[i+1] ^= *regs[i];   → CX(regs[i], regs[i+1])
//
// Per-invocation gate stream (twelve CX records)
// ----------------------------------------------
// Forward iter i=0:
//   CX(q0, q1), CX(q1, q0), CX(q0, q1)
// Forward iter i=1:
//   CX(q1, q2), CX(q2, q1), CX(q1, q2)
// Adjoint iter i=1 (reversed-statement order — reads bottom-up):
//   CX(q1, q2), CX(q2, q1), CX(q1, q2)
// Adjoint iter i=0 (reversed-statement order):
//   CX(q0, q1), CX(q1, q0), CX(q0, q1)
//
// Statement-order reversal visibility: stmt 1 and stmt 3 are the same
// CX pattern, with stmt 2 inverted between them.  Reversing the triple
// gives stmt 3, stmt 2, stmt 1 — textually the same order (1 and 3 are
// symmetric around stmt 2).  For a three-statement body this symmetry
// is unavoidable; a five-statement body or a statement 2 with a
// different LHS would break it.  We keep the shape minimal here — the
// iteration-reversal dimension is the load-bearing part, and the
// statement-order reversal is covered by the bit-reversal / ripple
// pairs as well as the test_invert.cpp Test 8 roundtrip that uses the
// full four-statement adder_carry body on classical int arrays.
//
// Harness invokes `demo` 3 times per capture (3 independent payloads),
// so the total captured stream is 36 CX records.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
#  include "sturm/qtypes/qbool.hpp"
#  include "sturm/qtypes/qbool_ops.hpp"
#else
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
#endif

namespace m12_reversible_loop_adder_transpiled {
using sturm::qbool;

// Phase S adder-carry-chain input.  Three qbool references threaded
// through a local pointer-array alias so PH-3 sees a
// UnaryOperator(ArraySubscriptExpr) LHS and does NOT fire (see ripple
// fixture top-of-file prose).  Two-iteration forward pass walks the
// carry left-to-right; reversed-iteration adjoint walks right-to-left.
//
// The inter-iteration dependency on regs[i+1] (written by iter i, read
// by iter i+1 through the shared carry slot) is what makes this the
// quintessential reversed-loop fixture: running the adjoint in forward
// iteration order would fail to un-chain the carry, leaving the state
// corrupt.
void demo(qbool& q0, qbool& q1, qbool& q2) {
    qbool* regs[3] = {&q0, &q1, &q2};

    // Forward carry propagation.  Each iteration writes regs[i+1]
    // three times (net: one CX on regs[i+1]'s wire conjugated with
    // one CX on regs[i]'s wire in the middle).  Iter 0 builds the
    // low-order carry into regs[1]; iter 1 propagates it into regs[2].
    for (int i = 0; i < 2; ++i) {
        *regs[i+1] ^= *regs[i];
        *regs[i]   ^= *regs[i+1];
        *regs[i+1] ^= *regs[i];
    }

    // Reversed-iteration adjoint (B11).  Walks the loop backwards (i=1
    // then i=0) and reverses the three statements within each iteration
    // — cancels the forward pass bit-for-bit because each `^=` is
    // self-inverse and the state dependency chain unwinds in reverse.
    for (int i = 1; i >= 0; --i) {
        *regs[i+1] ^= *regs[i];
        *regs[i]   ^= *regs[i+1];
        *regs[i+1] ^= *regs[i];
    }
}

} // namespace m12_reversible_loop_adder_transpiled
