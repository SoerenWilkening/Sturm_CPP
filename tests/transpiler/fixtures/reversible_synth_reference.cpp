// reversible_synth_reference.cpp — Phase R / R-5 + Phase T / T-5 hand-
// written control for the straight-line automatic-adjoint-synthesis
// gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the transpiler processes
// `reversible_synth_runtime.cpp`.  The demo body here contains the
// SAME forward cascade as the runtime fixture (no adjoint inline) and
// the reverse-statement-order adjoint is spelled by hand as a sibling
// `__demo_adj` function registered via `STURM_REGISTER_ADJOINT` at
// GLOBAL scope — mirroring the emission shape the transpiler produces
// on the runtime side (see the "Auto-synthesis status" section in
// `reversible_synth_runtime.cpp`).
//
// Gate-stream witness
// -------------------
// Identical to the runtime fixture's per-invocation stream:
//   Forward:  CX(q0,q1), CX(q1,q2), CX(q2,q3), CX(q0,q3)   [4 CX]
// Four CX records per invocation; the harness invokes `demo` three
// times per capture (3 independent payloads per pair), so the total
// captured stream is 12 CX records.  The auto-synthesised / hand-
// written `__demo_adj` is NOT invoked by the M12 harness — it exists
// on both sides so the full `STURM_REGISTER_ADJOINT` surface
// (forward + adjoint + registration) is exercised at link time, even
// though the present test does not capture the adjoint path.  When
// the T-3 roundtrip test in `tests/test_invert.cpp` is flipped on,
// the hand-written adjoint here serves as the reference adjoint
// behaviour that the T-3 harness's `invert(&demo)` call must match.
//
// ODR note
// --------
// The harness links this TU together with the runtime fixture.  Both
// define `demo(qbool&, qbool&, qbool&, qbool&)` inside their own
// namespace to avoid collisions.  The hand-written `__demo_adj` also
// lives inside the namespace for symmetry with the transpiler's
// emission on the runtime side.  The `STURM_REGISTER_ADJOINT` line
// is emitted at GLOBAL scope (outside every namespace) because the
// macro's `::fn` / `::adj` spelling and its `namespace sturm {
// namespace _detail { ... } }` opener both require global-scope
// invocation to resolve correctly — see
// `include/sturm/routines/invert.hpp` line 84 for the macro contract.
//
// Preprocessor contract: this file is compiled directly (no transpile
// step), so it can include the real `sturm::qbool` / `qbool::operator^=`
// unconditionally — no `__has_include` guard is needed.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/routines/invert.hpp"

namespace m12_reversible_synth_reference {

// Hand-written realisation of the Phase R straight-line reversible
// forward.  Same forward cascade as the runtime fixture; the adjoint
// is spelled as a sibling `__demo_adj` below.  The pointer-array
// indirection mirrors the runtime fixture (keeps PA-3 pass-through
// for consistency even though this TU is not transpiled; if PA-3
// fired here it would inflate the reference gate stream and the
// byte-compare would mis-match).
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
}

// Hand-written reverse-statement-order adjoint (B10).  Same four
// statements as `demo`, in reversed source order — `^=` is self-
// inverse at the bit level, so no sign flip is required (unlike the
// Phase N rotation pair, where `+=` ↔ `-=`).  This is the shape
// Phase R's `adjoint_emitter` machine-emits on the runtime side;
// here we spell it by hand so the reference-side
// `STURM_REGISTER_ADJOINT` binding resolves at link time.
void __demo_adj(sturm::qbool& q0,
                sturm::qbool& q1,
                sturm::qbool& q2,
                sturm::qbool& q3) {
    sturm::qbool* regs[4] = {&q0, &q1, &q2, &q3};
    *regs[3] ^= *regs[0];
    *regs[3] ^= *regs[2];
    *regs[2] ^= *regs[1];
    *regs[1] ^= *regs[0];
}

} // namespace m12_reversible_synth_reference

// Global-scope STURM_REGISTER_ADJOINT binding.  Uses fully-qualified
// names so `::m12_reversible_synth_reference::demo` and
// `::m12_reversible_synth_reference::__demo_adj` resolve under the
// macro's `::fn` / `::adj` expansion, and so the specialization of
// `sturm::_detail::adjoint_of` lands at the canonical template path
// the runtime `invert(fn)` helper consults.  Emission shape matches
// what the transpiler produces at end-of-file on the runtime side
// (see matcher_reversible_drive.cpp's drive_reversible_forwards
// emission split for the T-5 rationale).
STURM_REGISTER_ADJOINT(m12_reversible_synth_reference::demo,
                       m12_reversible_synth_reference::__demo_adj)
