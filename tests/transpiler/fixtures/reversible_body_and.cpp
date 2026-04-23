// reversible_body_and.cpp — Phase R / R-4 (sturm-88d7.5) positive
// straight-line fixture pinning the AND-family adjoint-synthesis
// contract.
//
// Canonical PRD §5.1 / P9c reversible-body shape
// ----------------------------------------------
// The routine carries `[[clang::annotate("sturm::reversible")]]` and its
// body is a straight-line (NO loops) sequence of CCNOT-fuse-equivalent
// AND + XOR_ASSIGN pairs.  Each pair computes a new target qbool from
// two control qbools through a temporary `qbool __tN = a & b;` followed
// by `target ^= __tN;` — the canonical Toffoli/CCX shape at the source
// level.  Phase R's `adjoint_emitter` (sturm-88d7.2) will machine-emit
// a sibling `__and_body_adj` function whose body is the same sequence
// in REVERSED statement order — per B11.  At the gate-stream level
// CCX is self-adjoint, so the reversed sequence is the correct
// uncomputation: the Phase R adjoint body is bit-wise the same
// statements in reversed order.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// The Phase J PJ-1d `matcher_ccnot_fuse` peephole matcher anchors on
// `qbool __t = a & b;` where BOTH `&` operands peel to bare
// `DeclRefExpr`s — it would normally collapse our pair into a single
// `ccnot_inplace(target, a, b);` and plant the self-adjoint inverse at
// scope close, mutating the body.  Routing each operand through
// `*regs[i]` gives each one a `UnaryOperator(ArraySubscriptExpr)` shape
// that PJ-1d's declRefExpr anchor does NOT match — the matcher skips,
// the body stays verbatim, and the snapshot golden is a clean byte-for-
// byte copy of the input (same workaround shape as the S-3
// `reversible_loop_ripple.cpp` fixture uses for PA-3 + PH-3).  The PE-4
// compound-flatten matcher also skips because the init is not a nested
// `(b|c) & d` shape.  PA-3 skips because the `^=` LHS is
// `UnaryOperator(ArraySubscriptExpr)`.
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
// in place — the golden gains the machine-emitted `__and_body_adj`
// companion (whose body is the same AND + XOR_ASSIGN sequence in
// reversed source order) plus the
// `STURM_REGISTER_ADJOINT(and_body, __and_body_adj);` line.
//
// Primitive coverage
// ------------------
// Exercises the `QOpKind::AND` + `QOpKind::XOR_ASSIGN` pair, which is
// the shape the CCNOT-fuse peephole PJ-1d collapses to
// `QOpKind::CCNOT_INPLACE` when both AND operands are bare DREs.  The
// fused CCNOT and the unfused AND+XOR_ASSIGN pair share the same
// reverse-statement-order adjoint story — this fixture pins the
// unfused form so the test is invariant to PJ-1d's state.  The
// adjoint_emitter dispatch table's AND arm emits `uncompute_and(r, a,
// b);` (see `uncompute_pass.cpp:render_uncompute` case AND) and the
// XOR_ASSIGN arm emits the compound assign verbatim (self-adjoint).
//
// Stub qbool — same minimal shape as the S-3 loop fixtures plus the
// binary `operator&` so the AND op-call resolves.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void and_body(qbool& q0, qbool& q1, qbool& q2, qbool& r) {
    qbool* regs[4] = {&q0, &q1, &q2, &r};
    // Straight-line AND + XOR_ASSIGN cascade — NO loops.  Two logical
    // Toffoli gates expressed as AND/XOR pairs:
    //   CCX(q0, q1 -> r), CCX(q1, q2 -> r)
    // Each `qbool __tN = *regs[i] & *regs[j];` is a lonely AND op in
    // the current IR (PJ-1d's fuse is blocked by the pointer-array
    // indirection) and each `*regs[3] ^= __tN;` is a lonely
    // XOR_ASSIGN.  The Phase R adjoint body is the same four
    // statements in reversed source order; since both AND and
    // XOR_ASSIGN are self-adjoint at the gate level no sign/operand
    // flip is required.
    qbool __t0 = *regs[0] & *regs[1];
    *regs[3] ^= __t0;
    qbool __t1 = *regs[1] & *regs[2];
    *regs[3] ^= __t1;
}
