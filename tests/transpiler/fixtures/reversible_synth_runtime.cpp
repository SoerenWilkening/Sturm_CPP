// reversible_synth_runtime.cpp — Phase R / R-5 transpiler INPUT fixture
// for the straight-line automatic-adjoint-synthesis gate-stream
// equivalence pair.
//
// Fifteenth gate-equivalence pair.  Whereas the S-5 triple
// (reversible_loop_{ripple,bit_reversal,adder}_runtime.cpp) pins down
// Phase S's B11 LOOP-REVERSAL contract, this fixture pins down the
// earlier Phase R (B10) STRAIGHT-LINE adjoint-emission contract: a
// `[[sturm::reversible]]` forward routine whose body is a sequence of
// compound-assignment statements (no for/while loops, no if/WHEN) MUST
// be paired with a reverse-statement-order adjoint that emits the same
// gate operands in the opposite order.  The one pair in this batch
// (reversible_synth) mirrors the R-6 roundtrip test's `parity_cascade`
// fixture in `tests/test_invert.cpp` — same four-statement XOR cascade
// shape, but operating on `sturm::qbool` references so the PA-3 matcher
// surface and the M12 gate-stream byte-compare can observe the
// synthesised adjoint end-to-end.
//
// R-3 status (matcher_reversible_drive)
// -------------------------------------
// Per the R-5 issue (sturm-88d7.6) and the plan's §2.3 R-3/R-5 handoff
// contract, R-3 (matcher_reversible_drive) has landed but is not yet
// wired into transpile_consumer.cpp.  The transpile step for this
// fixture is therefore a PASS-THROUGH: the forward straight-line
// cascade AND its hand-written reverse-statement-order adjoint are
// both spelled verbatim in the demo body, and sturm-transpile's
// existing matchers leave the body untouched (see the "Why the
// pointer-array indirection is load-bearing" section below for why
// PA-3 does not fire).  When R-3 is wired into transpile_consumer.cpp
// and auto_register_emitter + adjoint_emitter kick in, this fixture
// upgrades in place: the forward cascade alone will be the transpiler's
// input, and the reverse-statement-order adjoint will be machine-
// emitted as a sibling `__demo_adj` function registered via
// STURM_REGISTER_ADJOINT(demo, __demo_adj) — the captured gate stream
// must remain byte-identical to the reference fixture's hand-written
// twin.
//
// Demo shape (forward + hand-cloned reverse-statement-order adjoint)
// ------------------------------------------------------------------
// [[clang::annotate("sturm::reversible")]]
// void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
//     qbool* regs[4] = {&q0, &q1, &q2, &q3};
//     // Forward: 4-statement straight-line XOR cascade (NO loops).
//     *regs[1] ^= *regs[0];   // CX(q0, q1)
//     *regs[2] ^= *regs[1];   // CX(q1, q2)
//     *regs[3] ^= *regs[2];   // CX(q2, q3)
//     *regs[3] ^= *regs[0];   // CX(q0, q3)
//     // Reverse-statement-order adjoint (what adjoint_emitter would
//     // machine-produce once R-3 wires in).  XOR is self-inverse at
//     // the bit level, so the adjoint body is the same four statements
//     // in reversed source order.
//     *regs[3] ^= *regs[0];   // CX(q0, q3)
//     *regs[3] ^= *regs[2];   // CX(q2, q3)
//     *regs[2] ^= *regs[1];   // CX(q1, q2)
//     *regs[1] ^= *regs[0];   // CX(q0, q1)
// }
//
// Why the `[[clang::annotate("sturm::reversible")]]` attribute
// ------------------------------------------------------------
// The attribute is the user-facing opt-in for Phase R/S automatic
// adjoint synthesis (the `[[sturm::reversible]]` spelling is the
// surface syntax; `[[clang::annotate("sturm::reversible")]]` is the
// low-level portable form the matchers anchor on — see
// transpiler/src/reversible_attribute.cpp).  Before R-3 is wired into
// transpile_consumer.cpp the attribute is a NO-OP at transpile time
// (no matcher consumes it yet for this shape).  Today the attribute
// serves only as a forward-compatibility marker and as documentation
// that this fixture is the straight-line analogue of the S-5 loop
// triple.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// Each qbool `operator^=` call takes `const qbool&` RHS and returns
// `qbool&`, so a bare `q1 ^= q0` is a CXXOperatorCallExpr whose arg0
// is a DeclRefExpr on the parameter `q1`.  PA-3's
// XorAssignCallback (matcher_qbool_assign.cpp:45) anchors on that
// exact shape (`declRefExpr().bind("lhs")` at PH-3 matcher_outer_var_-
// guard.cpp:456 and PA-3 matcher_qbool_assign.cpp:54) — so PA-3 would
// register a QOperation on the enclosing QScope for every one of our
// forward + adjoint statements and the uncompute pass would plant
// inverses at `demo`'s close brace, doubling the gate stream.
// Routing the mutation through a local pointer array
// (`*regs[i] ^= *regs[j]`) changes the LHS AST node from DeclRefExpr
// to UnaryOperator(ArraySubscriptExpr), which does NOT match PA-3's
// DeclRefExpr anchor — the transpiler passes the body through
// unchanged.  This is the same portable workaround the S-5 loop
// fixtures use (see reversible_loop_ripple_runtime.cpp's "Why the
// pointer-array indirection is load-bearing" section for the fuller
// PH-3 story — the PA-3 story is structurally identical because the
// LHS binding pattern is shared).  When R-3 wires in AND the auto-
// uncompute pass learns to defer to adjoint_emitter inside
// `[[sturm::reversible]]` routines (the remaining piece of the
// Phase R handoff), the pointer-array indirection can be removed and
// the demo body becomes the more idiomatic `q1 ^= q0;` form.
//
// Gate-stream witness
// -------------------
// With STURM_BACKEND_ENABLED and an APPEND-mode BackendContext installed,
// `qbool::operator^=` emits `emit_CX_lifted(ctx, other.qubits[0],
// qubits[0])` — a single CX record per `^=`.  The demo's forward
// cascade emits four CX records — CX(q0,q1), CX(q1,q2), CX(q2,q3),
// CX(q0,q3) — in that order; the reverse-statement-order adjoint
// cascade emits the same four CX records in reversed order —
// CX(q0,q3), CX(q2,q3), CX(q1,q2), CX(q0,q1).  Per `demo()` invocation:
// 8 gates.
//
// The M12 harness runs `demo()` 3 times per capture (3 independent
// payloads per pair), each against the same caller-supplied qubit
// indices (the harness's `make_non_owning` qbool views hold qubit IDs
// stable across invocations within a capture).  Total captured stream:
// 24 CX records per capture — the reference fixture's hand-written
// twin produces an identical stream.  The byte-compare therefore pins
// BOTH (a) reverse-statement-order emission per B10 and (b) the
// stable qubit-index invariant across multi-invocation runs.
//
// ODR note
// --------
// The M12 harness links this TU together with
// `reversible_synth_reference.cpp`.  Both define a
// `demo(qbool&, qbool&, qbool&, qbool&)` function; wrapping each in a
// dedicated namespace avoids ODR collision.  Inside the namespace a
// `using sturm::qbool;` brings the qbool type into local scope so the
// DSL pattern reads as it would to the end user.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and the
// `qbool` symbol would never appear in the AST.  Gating the include on
// `__has_include` takes the stub branch during the transpile step
// (providing a minimal `sturm::qbool` with `operator^=`) and takes the
// real-header branch during the downstream compile (so the `^=` calls
// emit real CX records).  The stub shape mirrors the one used by the
// other m12 runtime fixtures.
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

namespace m12_reversible_synth_transpiled {
using sturm::qbool;

// Phase R straight-line adjoint-synthesis input.  Four qbool references
// + a local pointer-array alias so each `*regs[i] ^= *regs[j]` lands as
// a UnaryOperator(ArraySubscriptExpr) on the LHS — see top-of-file
// prose on why this matters for PA-3 pass-through.  The
// `[[clang::annotate("sturm::reversible")]]` attribute is the user-
// facing opt-in for automatic adjoint synthesis; today it is a
// forward-compatibility marker because R-3 is not yet wired into
// transpile_consumer.cpp.
//
// Per-invocation gate stream (eight CX records):
//   Forward:  CX(q0,q1), CX(q1,q2), CX(q2,q3), CX(q0,q3)
//   Adjoint:  CX(q0,q3), CX(q2,q3), CX(q1,q2), CX(q0,q1)
//
// The reverse-statement-order adjoint cancels the forward pass bitwise
// at the state level.  At the gate-stream level, the reversal is the
// load-bearing invariant the byte-compare pins.
[[clang::annotate("sturm::reversible")]]
void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
    qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward: 4-statement straight-line XOR cascade (NO loops).  The
    // fourth statement re-touches regs[3] with regs[0] so the forward
    // gate stream is distinguishable from the S-5 ripple triple's
    // 3-statement left-to-right sweep.  Each statement is a single
    // CXXOperatorCallExpr whose LHS is `*regs[i]` — a UnaryOperator
    // dereference of an ArraySubscriptExpr — which keeps PA-3 off
    // (see top-of-file prose for the full rationale).
    *regs[1] ^= *regs[0];
    *regs[2] ^= *regs[1];
    *regs[3] ^= *regs[2];
    *regs[3] ^= *regs[0];

    // Reverse-statement-order adjoint (B10).  The body is the same
    // four statements in reversed source order — `^=` is self-inverse
    // at the bit level, so no sign flip is required (unlike the Phase
    // N rotation pair, where `+=` ↔ `-=`).  This is the shape
    // Phase R's `adjoint_emitter` module will machine-emit once R-3
    // wires into transpile_consumer.cpp; until then the hand-written
    // clone serves the same role.  When R-3 lands, the four statements
    // below are deleted in place and replaced by a sibling
    // `__demo_adj` function + STURM_REGISTER_ADJOINT registration at
    // namespace scope — the captured gate stream must stay byte-
    // identical to the reference fixture's twin.
    *regs[3] ^= *regs[0];
    *regs[3] ^= *regs[2];
    *regs[2] ^= *regs[1];
    *regs[1] ^= *regs[0];
}

} // namespace m12_reversible_synth_transpiled
