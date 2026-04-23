// reversible_synth_reference.cpp — Phase R / R-5 hand-written control
// for the straight-line automatic-adjoint-synthesis gate-stream
// equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the transpiler processes
// `reversible_synth_runtime.cpp`.  The demo body is a byte-for-byte
// twin of the runtime fixture — same forward XOR cascade, same
// reverse-statement-order adjoint — because R-3
// (matcher_reversible_drive) is not yet wired into
// transpile_consumer.cpp and the transpiler's pass for this fixture
// is currently a no-op (the transpiler emits nothing that reshapes
// the body).  When R-3 wires in and adjoint_emitter +
// auto_register_emitter take over, the runtime fixture will shrink to
// just the forward cascade and the reverse-statement-order adjoint
// will be machine-emitted as a sibling `__demo_adj` function + a
// `STURM_REGISTER_ADJOINT(demo, __demo_adj)` registration at namespace
// scope — this reference here stays unchanged as the ground-truth
// byte-stream the transpiler's emission must match.
//
// Having a hand-written control checked in directly is what turns
// R-5 into a meaningful capstone: the test is a cross-check between
// two independent realisations of the same circuit, and any drift in
// the transpiler's straight-line adjoint emission, the runtime
// `qbool::operator^=` decomposition, or the QubitPool non-owning
// index invariants would show up as a mismatch in the captured gate
// stream.
//
// Gate-stream witness
// -------------------
// Identical to the runtime fixture's per-invocation stream:
//   Forward:  CX(q0,q1), CX(q1,q2), CX(q2,q3), CX(q0,q3)   [4 CX]
//   Adjoint:  CX(q0,q3), CX(q2,q3), CX(q1,q2), CX(q0,q1)   [4 CX]
// Eight CX records per invocation; the harness invokes `demo` three
// times per capture (3 independent payloads per pair), so the total
// captured stream is 24 CX records.
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

namespace m12_reversible_synth_reference {

// Hand-written realisation of the Phase R straight-line
// reverse-statement-order adjoint.  Byte-for-byte twin of the runtime
// fixture: the pointer-array indirection, the forward 4-statement XOR
// cascade, and the reverse-statement-order adjoint cascade are all
// spelled the same way.  Using identical source keeps the captured
// gate streams byte-identical across the transpile vs. direct-compile
// boundary.
//
// Note: the hand-written control intentionally does NOT carry the
// `[[clang::annotate("sturm::reversible")]]` attribute — the
// attribute is a transpile-time opt-in with no runtime side effects,
// and omitting it on the reference side makes the two TUs textually
// distinct in exactly one axis (the attribute) while keeping the gate
// stream byte-identical.  This mirrors how the S-5 reference fixtures
// stay textually minimal while the runtime fixtures carry the
// `__has_include` guard.
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3) {
    sturm::qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward cascade (4 statements, straight-line).  Same shape as the
    // runtime fixture.
    *regs[1] ^= *regs[0];
    *regs[2] ^= *regs[1];
    *regs[3] ^= *regs[2];
    *regs[3] ^= *regs[0];

    // Reverse-statement-order adjoint (B10).  Same four statements,
    // reverse source order — cancels the forward pass bit-for-bit at
    // the state level and byte-for-byte at the gate-stream level.
    *regs[3] ^= *regs[0];
    *regs[3] ^= *regs[2];
    *regs[2] ^= *regs[1];
    *regs[1] ^= *regs[0];
}

} // namespace m12_reversible_synth_reference
