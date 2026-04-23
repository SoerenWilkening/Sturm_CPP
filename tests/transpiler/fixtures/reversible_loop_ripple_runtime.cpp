// reversible_loop_ripple_runtime.cpp — Phase S / S-5 transpiler INPUT
// fixture for the ripple reversed-iteration gate-stream equivalence pair.
//
// Twelfth gate-equivalence pair.  Whereas the earlier m12 fixtures exercise
// the OR matcher's per-iteration / per-branch / nested-WHEN lowerings, the
// user-routine rewrite, the Phase J fusion / dead-ancilla / hoist
// peepholes, the Phase M peephole-reorder no-op and the Phase N rotation
// pipeline, this fixture pins down the Phase S (B11) LOOP-REVERSAL
// contract: a forward for-loop that XORs each cell with its left neighbour
// MUST be paired with a reversed-iteration adjoint that walks the loop
// right-to-left, emitting the same three CX gates in the opposite order.
// The three pairs in this batch — ripple, bit_reversal, adder — mirror the
// three scenarios covered by tests/test_invert.cpp Test 6/7/8 (sturm-ha2k.7
// roundtrip tests).
//
// R-2 status (auto_register_emitter)
// ----------------------------------
// Per the S-5 issue (sturm-ha2k.6) and the plan's §2.4 S-B handoff
// contract, R-2 (auto_register_emitter) is not yet landed.  The transpile
// step for this fixture is therefore a PASS-THROUGH: the forward for-loop
// and its hand-written reversed adjoint are both spelled verbatim in the
// demo body, and sturm-transpile's existing matchers leave the body
// untouched (PH-3's outer-var-guard anchors on `declRefExpr().bind("lhs")`
// — our `*regs[i] ^= *regs[i-1]` shape has a UnaryOperator-dereferenced
// ArraySubscriptExpr on the LHS, not a DeclRefExpr, so PH-3 does not
// fire).  When Phase S's `loop_reversal` module lands and R-2 wires up
// auto-registration, this fixture upgrades in place: the forward loop
// alone will be the transpiler's input, and the reversed adjoint loop
// will be machine-emitted — the captured gate stream must remain
// byte-identical to the reference fixture's hand-written twin.
//
// Demo shape (forward + hand-cloned reversed adjoint)
// ---------------------------------------------------
// void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
//     qbool* regs[4] = {&q0, &q1, &q2, &q3};
//     // Forward: iterate left-to-right, each cell XORs its left neighbour.
//     for (int i = 1; i < 4; ++i) {
//         *regs[i] ^= *regs[i-1];
//     }
//     // Reversed-iteration adjoint: iterate right-to-left — same body.
//     for (int i = 3; i >= 1; --i) {
//         *regs[i] ^= *regs[i-1];
//     }
// }
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// Each qbool `operator^=` call takes `const qbool&` RHS and returns
// `qbool&`, so a bare `q1 ^= q0` inside a for-body is a
// CXXOperatorCallExpr whose arg0 is a DeclRefExpr on the parameter `q1`.
// PH-3's outer-variable-guard matcher (matcher_outer_var_guard.cpp:456)
// anchors on exactly that shape (`declRefExpr().bind("lhs")`) — it would
// flag `q1 ^= q0` as a PH-3 outer-scope mutation inside a ForStmt,
// setting `skip_uncompute=true` and emitting a stderr diagnostic (which
// would also mark this fixture as non-idempotent for the golden
// transpile-cycle tests).  Routing the mutation through a local pointer
// array (`*regs[i] ^= *regs[i-1]`) changes the LHS AST node from
// DeclRefExpr to UnaryOperator(ArraySubscriptExpr), which does NOT match
// PH-3's pattern — the transpiler passes the body through unchanged.
// This is the same intent Phase S's `matcher_outer_var_guard` edit
// (sturm-ha2k.3, already merged) bakes into a first-class opt-in via
// the `[[sturm::reversible]]` attribute: the PH-3 handoff is suppressed
// inside synthesis context.  Until R-2 lands, the pointer-array
// indirection is the portable workaround that keeps the fixture pass-
// through under both the pre-S-B and post-S-B transpiler.
//
// Gate-stream witness
// -------------------
// With STURM_BACKEND_ENABLED and an APPEND-mode BackendContext installed,
// `qbool::operator^=` emits `emit_CX_lifted(ctx, other.qubits[0],
// qubits[0])` — a single CX record per `^=`.  The demo's forward loop
// emits three CX records — CX(q0,q1), CX(q1,q2), CX(q2,q3) — in that
// order; the reversed adjoint loop emits the same three CX records in
// reversed order — CX(q2,q3), CX(q1,q2), CX(q0,q1).  Per `demo()`
// invocation: 6 gates.
//
// The M12 harness runs `demo()` 3 times per capture (3 independent
// payloads per pair), each against the same caller-supplied qubit
// indices (the harness's `make_non_owning` qbool views hold qubit IDs
// stable across invocations within a capture).  Total captured stream:
// 18 CX records per capture — the reference fixture's hand-written twin
// produces an identical stream.  The byte-compare therefore pins BOTH
// (a) iteration-order reversal per B11 and (b) the stable qubit-index
// invariant across multi-invocation runs.
//
// ODR note
// --------
// The M12 harness links this TU together with
// `reversible_loop_ripple_reference.cpp`.  Both define a
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

namespace m12_reversible_loop_ripple_transpiled {
using sturm::qbool;

// Phase S ripple-sweep input.  Four qbool references + a local
// pointer-array alias so the body's `*regs[i] ^= *regs[i-1]` lands as a
// UnaryOperator(ArraySubscriptExpr) on the LHS — see top-of-file prose
// on why this matters for PH-3 pass-through.
//
// Per-invocation gate stream (six CX records):
//   Forward i=1..3:   CX(q0,q1), CX(q1,q2), CX(q2,q3)
//   Adjoint i=3..1:   CX(q2,q3), CX(q1,q2), CX(q0,q1)
//
// The reversed-iteration adjoint cancels the forward pass bitwise at the
// state level.  At the gate-stream level, the reversal is the load-
// bearing invariant the byte-compare pins.
void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
    qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward: iterate left-to-right, each cell XORs its left neighbour.
    // Inter-iteration data dependency on `regs[i-1]` (which was written
    // by iteration i-2, two iters back for this shape) forces the
    // reversed adjoint below.
    for (int i = 1; i < 4; ++i) {
        *regs[i] ^= *regs[i-1];
    }

    // Reversed-iteration adjoint (B11).  The body is identical to the
    // forward loop — `^=` is self-inverse at the bit level — and only
    // the iteration order is reversed.  This is the shape Phase S's
    // `loop_reversal` module will machine-emit once R-2 lands; until
    // then the hand-written clone serves the same role.
    for (int i = 3; i >= 1; --i) {
        *regs[i] ^= *regs[i-1];
    }
}

} // namespace m12_reversible_loop_ripple_transpiled
