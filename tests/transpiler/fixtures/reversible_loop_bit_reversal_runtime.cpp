// reversible_loop_bit_reversal_runtime.cpp — Phase S / S-5 transpiler
// INPUT fixture for the bit-reversal reversed-iteration gate-stream
// equivalence pair.
//
// Thirteenth gate-equivalence pair.  Companion to the ripple fixture
// (reversible_loop_ripple_runtime.cpp) — see that file's top-of-file
// prose for the full Phase S / B11 / R-2 / PH-3 rationale.  This
// fixture pins down the same iteration-reversal contract on a loop body
// that performs a 3-statement XOR-swap:
//
//   for (int i = 0; i < 2; ++i) {
//       int j = 3 - i;
//       *regs[i] ^= *regs[j];   // stmt 1: a ^= b
//       *regs[j] ^= *regs[i];   // stmt 2: b ^= a  (a' = a^b on prev line)
//       *regs[i] ^= *regs[j];   // stmt 3: a ^= b
//   }
//
// XOR-swap is the standard in-place swap identity (no temporary); each
// swap is self-inverse, and the two swaps in this loop (i=0↔j=3 and
// i=1↔j=2) are pair-wise disjoint in their qubit operands — so at the
// state level the adjoint's iteration order is not strictly load-bearing
// (either direction lands the same 4-qubit register state).  At the
// GATE-STREAM level, however, B11 mandates reversed iteration order AND
// reversed intra-iteration statement order — the byte-compare pins that
// contract so a future Phase S implementation cannot silently drift the
// emission order.
//
// Per-invocation gate stream (twelve CX records)
// ----------------------------------------------
// Forward iter i=0 (j=3):
//   CX(q3, q0)  // *regs[0] ^= *regs[3]
//   CX(q0, q3)  // *regs[3] ^= *regs[0]
//   CX(q3, q0)  // *regs[0] ^= *regs[3]
// Forward iter i=1 (j=2):
//   CX(q2, q1)  // *regs[1] ^= *regs[2]
//   CX(q1, q2)  // *regs[2] ^= *regs[1]
//   CX(q2, q1)  // *regs[1] ^= *regs[2]
// Adjoint iter i=1 (j=2) — reversed stmt order:
//   CX(q2, q1), CX(q1, q2), CX(q2, q1)   [same pattern; symmetric]
// Adjoint iter i=0 (j=3) — reversed stmt order:
//   CX(q3, q0), CX(q0, q3), CX(q3, q0)
//
// Note: because the three-statement body is symmetric around its middle
// statement, reversing the statement order gives the SAME three
// statements.  The byte-compare still runs correctly — the test
// exercises the iteration-reversal dimension regardless.  The adder
// fixture (reversible_loop_adder_runtime.cpp) carries an asymmetric
// 3-statement body that does visibly benefit from statement-order
// reversal.
//
// Harness invokes `demo` 3 times per capture (3 independent payloads),
// so the total captured stream is 36 CX records — the reference
// fixture's hand-written twin produces an identical stream.
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

namespace m12_reversible_loop_bit_reversal_transpiled {
using sturm::qbool;

// Phase S bit-reversal input.  Four qbool references + a local pointer
// array alias so the `*regs[i] ^= *regs[j]` shape on the LHS is a
// UnaryOperator(ArraySubscriptExpr) — PH-3 does NOT fire (see ripple
// fixture top-of-file prose for the full rationale).  Two forward
// iterations perform XOR-swaps on (q0, q3) and (q1, q2); the reversed-
// iteration adjoint walks the same two swaps in reverse source order.
void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
    qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward: for each i in [0, 2), swap regs[i] and regs[3-i] via
    // the standard XOR-triple identity.  Iter 0 swaps (q0, q3); iter 1
    // swaps (q1, q2).
    for (int i = 0; i < 2; ++i) {
        int j = 3 - i;
        *regs[i] ^= *regs[j];
        *regs[j] ^= *regs[i];
        *regs[i] ^= *regs[j];
    }

    // Reversed-iteration adjoint (B11).  Loop goes 1..0 in reverse,
    // and intra-iteration statement order is reversed too — although
    // for the XOR-swap shape the three statements are symmetric around
    // the middle statement so the reversed triple is textually the
    // same.  The byte-compare pins the iteration-order reversal.
    for (int i = 1; i >= 0; --i) {
        int j = 3 - i;
        *regs[i] ^= *regs[j];
        *regs[j] ^= *regs[i];
        *regs[i] ^= *regs[j];
    }
}

} // namespace m12_reversible_loop_bit_reversal_transpiled
