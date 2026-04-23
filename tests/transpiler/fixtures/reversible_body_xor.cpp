// reversible_body_xor.cpp — Phase R / R-4 (sturm-88d7.5) positive
// straight-line fixture pinning the XOR_ASSIGN adjoint-synthesis
// contract.
//
// Canonical PRD §5.1 / P9c reversible-body shape
// ----------------------------------------------
// The routine carries `[[clang::annotate("sturm::reversible")]]` and its
// body is a straight-line (NO loops) sequence of `qbool ^= qbool` XOR
// compound-assigns.  Phase R's `adjoint_emitter` (sturm-88d7.2) will
// machine-emit a sibling `__xor_body_adj` function whose body is the
// same sequence in REVERSED statement order — per B11, adjoint synthesis
// reverses statement order AND loop iteration order, but this fixture
// contains no loops, so only the first half of B11 applies here.
//
// Per PRD §5.2 XOR is self-inverse at the bit level: applying the same
// `a ^= b` statement twice returns `a` to its pre-forward state.  The
// adjoint body is therefore textually the same three statements as the
// forward, in reversed source order.  The reversal is the load-bearing
// invariant the R-4 byte-compare pins — not the per-statement textual
// difference.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// `qbool::operator^=` takes `const qbool&` RHS and returns `qbool&`, so
// a bare `q1 ^= q0` would be a CXXOperatorCallExpr whose arg0 is a
// DeclRefExpr on the parameter `q1`.  Phase A's PA-3 XorAssignCallback
// (matcher_qbool_assign.cpp) anchors on exactly that shape and would
// fire, landing an uncompute line in the body.  Routing the LHS
// through `*regs[i] ^= *regs[j]` gives it a UnaryOperator(ArraySubscript
// Expr) shape that PA-3 does not match at all — the transpiler passes
// the body through unchanged.  This is the same portable workaround the
// S-3 loop fixtures use (see `reversible_loop_ripple.cpp` for the
// fuller PH-3 story; the PA-3 story is structurally identical because
// the LHS binding pattern is shared).
//
// R-3 status (matcher_reversible_drive)
// -------------------------------------
// Per the R-4 issue (sturm-88d7.5) and the plan's §2.3 R-3/R-4 handoff
// contract, R-3 (matcher_reversible_drive) has landed but is not yet
// wired into `transpile_consumer.cpp`.  The transpile step for this
// fixture is therefore a PASS-THROUGH: sturm-transpile prepends the
// AUTO-GENERATED/Source header and copies the body verbatim.  The
// `.expected.cpp` golden differs from this input only by the two
// header lines.  When R-3 lands in the consumer, this fixture upgrades
// in place — the golden gains the machine-emitted
// `__xor_body_adj` companion + `STURM_REGISTER_ADJOINT(xor_body,
// __xor_body_adj);` line, and the fixture source itself does not
// change.
//
// Primitive coverage
// ------------------
// Exercises `QOpKind::XOR_ASSIGN` — the `_xor` slot of the adjoint_
// emitter dispatch table.  The adjoint renderer's XOR_ASSIGN case
// emits the LHS/RHS pair verbatim (self-adjoint), so once R-3 wires
// in the machine-emitted adjoint body is textually identical to the
// reversed forward sequence.
//
// Stub qbool — same minimal shape as the S-3 loop fixtures.
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
void xor_body(qbool& q0, qbool& q1, qbool& q2) {
    qbool* regs[3] = {&q0, &q1, &q2};
    // Straight-line XOR cascade — NO loops.  Three CX gates:
    //   CX(q0, q1), CX(q1, q2), CX(q0, q2)
    // The Phase R adjoint body is the same three statements in reversed
    // source order: CX(q0, q2), CX(q1, q2), CX(q0, q1).
    *regs[1] ^= *regs[0];
    *regs[2] ^= *regs[1];
    *regs[2] ^= *regs[0];
}
