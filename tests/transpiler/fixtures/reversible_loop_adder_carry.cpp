// reversible_loop_adder_carry.cpp — Phase S / S-3 (sturm-ha2k.4) positive
// loop fixture for the adder-carry reversed-iteration synthesis contract.
//
// Canonical PRD §5.2 reversible shape
// -----------------------------------
// The routine carries `[[clang::annotate("sturm::reversible")]]` and its
// body is a single canonical forward for-loop modelling the inner loop
// of a classical ripple-carry adder:
//
//     for (int i = 0; i < 2; ++i) {
//         *regs[i+1] ^= *regs[i];
//         *regs[i]   ^= *regs[i+1];
//         *regs[i+1] ^= *regs[i];
//     }
//
// This is the adder-carry shape pinned by PRD §5.2: iteration i writes
// `regs[i+1]` using `regs[i]` (itself written by iteration i-1), so the
// carry propagates left-to-right on the forward pass.  The
// reversed-iteration adjoint MUST walk the loop right-to-left to
// un-chain the carry correctly — both the outer iteration order AND the
// intra-iteration statement order are reversed per B11.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// See `reversible_loop_ripple.cpp` for the full rationale.  Routing the
// LHS through `*regs[i+1] ^= *regs[i]` makes it a
// UnaryOperator(ArraySubscriptExpr) rather than a DeclRefExpr, so PH-3
// does not match and no diagnostic fires under the current (pre-R-2)
// transpiler.  The snapshot golden is therefore a clean byte-for-byte
// pass-through of the input.
//
// R-2 status (auto_register_emitter)
// ----------------------------------
// Same pass-through posture as the ripple and bit-reversal fixtures.
// When R-2 lands and the Phase S driver wires `loop_reversal` into the
// emission path, the golden upgrades to also carry the synthesised
// `adder_carry_adj` companion: loop header reversed
// (`for (int i = 1; i >= 0; --i)`) and the three body statements in
// reversed source order.  The asymmetric operand ordering between
// statements 1 and 2 makes this fixture the sharpest test of the
// "reverse statement order" half of B11 — any future Phase S
// implementation that skipped intra-iteration reversal would fail the
// byte-compare here even though the symmetric shapes (ripple,
// bit-reversal) might tolerate it.
//
// Stub qbool — same minimal shape as the ripple fixture.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void adder_carry(qbool& q0, qbool& q1, qbool& q2) {
    qbool* regs[3] = {&q0, &q1, &q2};
    for (int i = 0; i < 2; ++i) {
        *regs[i+1] ^= *regs[i];
        *regs[i]   ^= *regs[i+1];
        *regs[i+1] ^= *regs[i];
    }
}
