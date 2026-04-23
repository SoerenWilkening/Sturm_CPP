// test_gate_equivalence.cpp — M12 transpiler-MVP capstone harness.
//
// Proves PRD acceptance criterion #4: the gate stream emitted when the
// transpiler's output is compiled and run is byte-for-byte identical to
// the gate stream produced by a hand-written reference that pairs the
// forward `qbool tmp = a | b;` with an explicit `uncompute_or(tmp, a, b);`
// call before scope exit.
//
// How it works
// ------------
// Two separately compiled translation units each expose a namespaced
// `demo(const qbool&, const qbool&)` function:
//
//   m12_transpiled::demo  — built via the `add_quantum_executable()`
//                           helper.  The source file
//                           (`fixtures/or_single_runtime.cpp`) is routed
//                           through `sturm-transpile`; the generated TU
//                           lives at ${CMAKE_BINARY_DIR}/sturm_gen/... and
//                           is the object file linked into this harness.
//
//   m12_reference::demo   — built from `fixtures/or_single_reference.cpp`
//                           directly (no transpile step).  The source is
//                           already a hand-written realization of what
//                           the transpiler's output should look like.
//
// The harness installs a scoped APPEND-mode BackendContext, runs one
// `demo()` invocation against it, snapshots ctx->ir, clears the context,
// resets the QubitPool and repeats for the other fixture.  A
// gate-by-gate comparison of the two IR buffers is the test's pass/fail
// signal.
//
// Why both fixtures live in private namespaces
// --------------------------------------------
// Two TUs that both define a free `demo` in the global namespace would
// violate the ODR and fail to link.  By wrapping each in a distinct
// namespace the harness can unambiguously dispatch to one or the other
// and call them back-to-back.
//
// Input qbool preparation
// -----------------------
// `demo()` takes `const qbool&` rather than `qbool`, because the qbool
// copy constructor resets `qubits[0]` to -1 (see include/sturm/qtypes/
// qbool.hpp line 101).  A by-value parameter would arrive classical and
// the forward OR would take the classical short-circuit in
// qbool_ops.hpp, emitting zero gates on both sides — a trivially-equal
// comparison that would not actually verify the transpiler's output.
// Passing by reference keeps the caller-supplied qubit indices intact
// so the forward three-gate OR circuit (CX + CX + CCX) and its inverse
// both fire.
//
// The harness builds the two input qbools via `qbool::make_non_owning`
// on freshly-allocated QubitPool indices; ownership stays with the
// harness so the qbool destructors at the end of `demo` do not attempt
// to release them.

#include "sturm/sturm.hpp"

#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ── Fixture forward declarations ─────────────────────────────────────────────
// Each fixture TU is expected to define the same signature in its own
// namespace.  The harness does not include either fixture's source — it
// relies only on the linker to resolve the symbols.

namespace m12_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b);
// LP7: pattern mirroring examples/or_circuit.cpp (`qbool c = a | b;`).
void demo_or_circuit(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_transpiled

namespace m12_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b);
// LP7: hand-written reference for the or_circuit pattern with explicit
// `uncompute_or(c, a, b);`.
void demo_or_circuit(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_reference

// PG-7: Phase G nested-WHEN fixture pair.  The transpiler rewrites
// `WHEN(outer) { WHEN(inner) { target.flip(); } }` into an explicit
// `qbool __stu_ctrl0 = outer & inner;` decl + `WHEN(__stu_ctrl0)` +
// trailing `sturm::uncompute_and(__stu_ctrl0, outer, inner);` call.
// The reference fixture spells the lowering by hand.  Both demos
// take three qbools so the helper can stage outer + inner as the
// superposed controls and `target` as a separately-allocated classical-
// owning qbool with super_mask=1.
namespace m12_nested_transpiled {
// outer/inner/target are taken by non-const reference: WHEN(expr)
// requires a non-const lvalue at the materialize_when call site, so
// the transpiled variant must take the controls as non-const refs.
// See fixture TU comments for the full rationale.
void demo(sturm::qbool& outer,
          sturm::qbool& inner,
          sturm::qbool& target);
} // namespace m12_nested_transpiled

namespace m12_nested_reference {
void demo(sturm::qbool& outer,
          sturm::qbool& inner,
          sturm::qbool& target);
} // namespace m12_nested_reference

// PH-6b: Phase H if/else fixture pair.  The transpiler rewrites each
// branch's `qbool <var> = a | b;` into a decl + `sturm::uncompute_or(
// <var>, a, b);` pair before that branch's closing `}`, so the captured
// gate stream covers both branch lowerings when `demo` is invoked
// twice (cond=true, cond=false).  The reference fixture spells the two
// lowerings by hand.  `const qbool&` arguments preserve the caller's
// qubit indices across the classical `cond` selector.
namespace m12_if_branches_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b, bool cond);
} // namespace m12_if_branches_transpiled

namespace m12_if_branches_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b, bool cond);
} // namespace m12_if_branches_reference

// PH-6a: Phase H for-loop fixture pair.  The transpiler rewrites each
// iteration's `qbool tmp = a | b;` into a decl + `sturm::uncompute_or(
// tmp, a, b);` pair before the loop body's closing `}`, so every
// iteration emits six gates (three forward OR + three adjoint) against
// the SAME recycled ancilla index (the QubitPool free-list is LIFO, so
// tmp's RAII release at `}` hands the same index back to the next
// iteration's `allocate()`).  The reference fixture spells the per-
// iteration lowering by hand.  `const qbool&` arguments preserve the
// caller's qubit indices across the three iterations.
namespace m12_for_loop_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_for_loop_transpiled

namespace m12_for_loop_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_for_loop_reference

// PI-7: Phase I user-routine gate-equivalence pair.  The transpiler
// matches the registered forward call `ur_rotate_fwd_runtime(tmp, in,
// 3);` (Case 1 = local-intermediate output, per PI-3) and injects
// `invert(ur_rotate_fwd_runtime)(tmp, in, 3);` before the enclosing
// scope's close brace — which dispatches to the registered adjoint
// `ur_rotate_adj_runtime` via the PI-0 trait table.  The reference
// fixture spells both the forward and the adjoint by hand in LIFO
// order (forward, then adjoint).  Because `tmp` is created as a fresh
// local qbool in both fixtures against a harness-reset QubitPool, the
// allocated ancilla index matches across the two captures.  The demo
// takes `const qbool&` by reference so the caller-supplied quantum
// input index is preserved intact on both sides.
namespace m12_user_routine_transpiled {
void demo(const sturm::qbool& in);
} // namespace m12_user_routine_transpiled

namespace m12_user_routine_reference {
void demo(const sturm::qbool& in);
} // namespace m12_user_routine_reference

// PJ-1i: Phase J zero-ancilla fusion gate-equivalence pair.  The
// transpiler's PJ-1d peephole matcher fires on `qbool __t = a & b; x
// ^= __t;` (bare `&` init, adjacent `^=` consumer, exactly one reader
// of `__t` in scope) and rewrites the pair into a single
// `ccnot_inplace(x, a, b);` call with a matching self-adjoint
// `ccnot_inplace(x, a, b);` planted before the demo's closing `}` by
// the PJ-1c render case.  The reference fixture spells both calls by
// hand via `primitive_AND(ctx, a_idx, b_idx, x_idx)` directly against
// the 18-gate sink — NOT via `operator&` on qbool, because the
// pre-PK-2 expression-template materialisation path allocates an
// intermediate ancilla which the fusion explicitly collapses away;
// using it would give a trivially-mismatched comparison.  Expected stream: two CCX
// records on the three caller-supplied qubit indices.  `x` is taken
// by non-const reference because the runtime fixture's `x ^= __t;`
// needs a non-const lvalue and `ccnot_inplace` (the helper the
// transpiler's fused rewrite calls) takes its target by non-const
// reference.
namespace m12_fused_transpiled {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& x);
} // namespace m12_fused_transpiled

namespace m12_fused_reference {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& x);
} // namespace m12_fused_reference

// PJ-4e: Phase J dead-ancilla elimination gate-equivalence pair.  The
// transpiler's PJ-4a peephole matcher fires on `qbool dead = a | b;`
// when `dead` has zero readers in the enclosing scope, emits one
// empty-text `QReplacement` over the decl's full stmt range, and pushes
// the same range into `QUnit::eliminated_stmt_ranges` so the downstream
// MVP OR matcher early-returns on the covered range.  Net effect on
// the transpiled path: the decl vanishes, no `uncompute_or(dead, a, b);`
// is planted at scope close, and the `a | b` RHS never allocates an
// ancilla — ZERO gates emitted for the dead slice.  The reference
// fixture omits the dead decl entirely so both fixtures emit the same
// sentinel `target.flip()` call (one X(target) record) and nothing
// else.  Expected stream: ONE X gate.  `target` is taken by non-const
// reference because `flip()` mutates the qbool's state via
// emit_X_lifted; `a` / `b` are `const qbool&` to preserve the caller's
// qubit indices across the demo boundary (even though the reference
// never touches them — the signature match lets the harness dispatch
// both fixtures through a single function-pointer shape).
namespace m12_dead_ancilla_transpiled {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& target);
} // namespace m12_dead_ancilla_transpiled

namespace m12_dead_ancilla_reference {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& target);
} // namespace m12_dead_ancilla_reference

// PJ-3h: Phase J uncompute-hoisting gate-equivalence pair.  The
// transpiler's PJ-3d `register_hoist_invariant_matcher` fires on the
// loop-body scope of `qbool t = a | b;` inside a `for (...)` body where
// both operands are loop-invariant function parameters, and sets
// `op.hoist_to_override` to the enclosing scope's close brace.  The M8
// synthesis pass consumes that override at uncompute-emission time per
// the priority ladder in `transpiler/src/uncompute_pass.cpp` and plants
// `sturm::uncompute_or(t, a, b);` AFTER the for-loop's `}`, not inside
// the for-body.  The outer predecl `qbool t;` + `t.ensure_qubit();`
// are load-bearing (see hoist_runtime.cpp's top-of-file prose): the
// hoisted call resolves to the OUTER `t` which has an allocated qubit
// so `r_q == true` at the hoisted call site, preventing the
// `assert(r_q, ...)` in `src/sturm/uncompute/uncompute_api.cpp` that
// would fire on quantum a/b alongside a classical outer t.  The
// reference fixture spells this same post-loop uncompute by hand.
// Because the inner `qbool t = a | b;` fires per-iteration (PJ-3d
// only moves the uncompute anchor — forward text-move is a future-
// phase concern), the per-iteration gate budget is six (3 forward OR
// + 3 transpiler-emitted uncompute calls) × 3 iterations = 18 gates
// on each side.  The post-loop
// `sturm::uncompute_or(t, a, b)` adds three more gates (both-quantum
// CCX+CX+CX against the outer t's ensure_qubit'd index), totalling
// twenty-one gates per capture.  `const qbool&` arguments preserve
// the caller's qubit indices across the demo boundary.
namespace m12_hoist_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_hoist_transpiled

namespace m12_hoist_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_hoist_reference

// PM5-8: Phase M peephole-reorder gate-equivalence pair.  The
// transpiler's PM5-5 `register_peephole_reorder_matcher` runs LAST
// over the populated scope.  For this fixture the matcher observes a
// compound-flattened PE-4 (OR, OR) decl pair followed by two user-
// written XOR_ASSIGN statements; no triple in scope.ops satisfies
// Gate 1 (A must be QOpKind::AND with a synthetic `__stu_t*`
// prefix) so the reorder does NOT fire.  The fixture therefore
// proves the stronger property that PM5 is a TRUE NO-OP on any
// program whose ops list does not present its trigger shape —
// preserving the gate stream byte-for-byte regardless of whether
// the reorder matcher is registered.
//
// The hand-written companion `reorder_reference.cpp` spells the
// same circuit by hand: PE-4's flattened decl pair, the two
// user-written `^=` statements, their LIFO self-adjoints, and the
// MVP OR uncompute calls at scope close.  Both fixtures emit the
// same gate stream when the harness runs each `demo` against an
// APPEND-mode BackendContext.
//
// `const sturm::qbool&` inputs preserve the caller's qubit
// indices across the demo boundary; `sturm::qbool& x / y` mirror
// the runtime fixture's non-const `^=` targets (the MVP qbool
// `operator^=` requires a non-const lvalue).  All qubits are
// caller-owned; neither fixture allocates.
namespace m12_reorder_transpiled {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          const sturm::qbool& c,
          sturm::qbool& x,
          sturm::qbool& y);
} // namespace m12_reorder_transpiled

namespace m12_reorder_reference {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          const sturm::qbool& c,
          sturm::qbool& x,
          sturm::qbool& y);
} // namespace m12_reorder_reference

// PN-8: Phase N rotation gate-equivalence pair.  The transpiler's PN-2
// matchers fire on `q.theta() += d;` / `q.theta() -= d;` / `q.phi() += d;`
// / `q.phi() -= d;` (each on a qint_t<W> LHS with a double-valued RHS)
// and stage one QOperation per forward rotation on the enclosing
// QScope — four on the demo body's QScope plus one on the inner WHEN
// body's QScope.  The PN-4 uncompute-pass arms then plant five sign-
// flipped inverses in LIFO order: one in-body inverse before the WHEN
// body's close brace (preserving B5's single-control-scope invariant
// for the depth-1 controlled rotation) and four at the demo body's
// close brace (LIFO dual chain for the four outer rotations in
// reverse-source order).  The hand-written reference
// `rotations_reference.cpp` spells the same five inverses by hand so
// the captured gate streams match byte-for-byte.  Expected stream:
// ten gates — RY(q_a, 0.3), RY(q_a, -0.1), RZ(q_b, 0.7), RZ(q_b, -0.2),
// CRY(q_c, q_a, 0.5), CRY(q_c, q_a, -0.5), RZ(q_b, 0.2), RZ(q_b, -0.7),
// RY(q_a, 0.1), RY(q_a, -0.3).  The demo takes no arguments because
// Phase N's rotation proxies auto-promote fully-classical qint_t<W>
// registers on first rotation (M15/M16 in qint_core.hpp:256-265 /
// 319-332), so the QubitPool-reset-then-allocate sequence in
// ScopedAppendContext gives both fixtures identical qubit indices
// without the harness needing to stage caller-owned qubits.
namespace m12_rotations_transpiled {
void demo();
} // namespace m12_rotations_transpiled

namespace m12_rotations_reference {
void demo();
} // namespace m12_rotations_reference

// S-5: Phase S reversed-iteration gate-equivalence triple pairs
// (sturm-ha2k.6).  Three namespace pairs pinning the B11 loop-reversal
// contract: a forward for-loop + its reversed-iteration adjoint must
// produce a byte-identical gate stream across the transpiled and
// reference TUs.  R-2 (auto_register_emitter) is not yet landed, so
// both TUs are hand-written byte-for-byte twins (see each fixture's
// top-of-file prose for the full R-2 / PH-3 pass-through rationale);
// when Phase S's `loop_reversal` module lands, the runtime fixtures
// shrink to just the forward loop and the adjoint becomes machine-
// emitted — the reference twins stay as the ground-truth gate stream.
//
// Three scenarios, matching test_invert.cpp Test 6/7/8 (sturm-ha2k.7):
//   ripple        — 4 qbools, 3-statement XOR sweep reading regs[i-1].
//   bit_reversal  — 4 qbools, XOR-swap between regs[i] and regs[3-i].
//   adder         — 3 qbools, carry-chain propagation left-to-right.
// Each `demo` takes qbool references by non-const ref because the body
// routes them through a local `qbool*` pointer-array alias (which
// keeps PH-3 off; see runtime fixture prose).  The harness invokes
// each `demo` three times per capture so each pair is exercised on
// three independent payloads (total 18/36/36 CX records per pair).
namespace m12_reversible_loop_ripple_transpiled {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3);
} // namespace m12_reversible_loop_ripple_transpiled

namespace m12_reversible_loop_ripple_reference {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3);
} // namespace m12_reversible_loop_ripple_reference

namespace m12_reversible_loop_bit_reversal_transpiled {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3);
} // namespace m12_reversible_loop_bit_reversal_transpiled

namespace m12_reversible_loop_bit_reversal_reference {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3);
} // namespace m12_reversible_loop_bit_reversal_reference

namespace m12_reversible_loop_adder_transpiled {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2);
} // namespace m12_reversible_loop_adder_transpiled

namespace m12_reversible_loop_adder_reference {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2);
} // namespace m12_reversible_loop_adder_reference

// R-5 / T-5: Phase R/T straight-line automatic-adjoint-synthesis gate-
// equivalence pair (sturm-88d7.6 / sturm-xrob.6).  One namespace pair
// pinning the B10 straight-line adjoint-emission contract: a
// `[[sturm::reversible]]` forward routine whose body is a loop-free
// sequence of compound-assignment statements MUST be paired with a
// reverse-statement-order adjoint that is auto-synthesised by the
// transpiler (T-1) and registered via `STURM_REGISTER_ADJOINT` at
// global scope.
//
// Post T-1 wiring (sturm-xrob.2) matcher_reversible_drive is active
// in the consumer: the runtime fixture's `demo()` body contains ONLY
// the forward cascade, and the sibling `__demo_adj` function +
// global-scope `STURM_REGISTER_ADJOINT` line are auto-emitted by the
// transpiler at end-of-TU.  The reference fixture mirrors the same
// emission shape by hand (forward-only `demo()`, hand-written
// `__demo_adj`, hand-written global-scope `STURM_REGISTER_ADJOINT`)
// so the two TUs produce byte-identical forward gate streams when
// the harness captures `demo()` three times per capture.  Scenario:
//   synth — 4 qbools, 4-statement XOR cascade with regs[3]^regs[0]
//           re-touch so the gate fingerprint is distinguishable from
//           the S-5 ripple 3-statement sweep.
// The harness invokes `demo` three times per capture so the pair is
// exercised on three independent payloads — four CX records per
// invocation, for a total of 12 CX records per capture.  The auto-
// synthesised `__demo_adj` is NOT invoked by this harness; the
// forward-only byte-compare pins the B10 per-statement emission
// ordering plus the stable qubit-index invariant across multi-
// invocation runs.  T-3's roundtrip test in tests/test_invert.cpp
// (sturm-xrob.4) is the companion harness that exercises the
// `invert(&demo)` lookup path via the registered adjoint.
namespace m12_reversible_synth_transpiled {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3);
} // namespace m12_reversible_synth_transpiled

namespace m12_reversible_synth_reference {
void demo(sturm::qbool& q0,
          sturm::qbool& q1,
          sturm::qbool& q2,
          sturm::qbool& q3);
} // namespace m12_reversible_synth_reference

namespace {

// Scoped APPEND-mode BackendContext.  Construction installs the context
// as the thread-local; destruction restores the previous one and
// destroys the context.  The QubitPool is also reset here so each
// fixture sees identical qubit indices.
struct ScopedAppendContext {
    sturm_backend_context_t* ctx  = nullptr;
    sturm_backend_context_t* prev = nullptr;

    ScopedAppendContext() {
        sturm::QubitPool::instance().reset_for_testing();
        ctx = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create(APPEND) returned null");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendContext() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::BackendContext& ref() { return *ctx; }
    const sturm::GateIR& ir() const { return ctx->ir; }
};

// Snapshot the IR buffer into a value-type vector so comparisons are
// independent of the originating context's lifetime.
std::vector<sturm::GateRecord> capture_ir(const sturm::GateIR& ir) {
    std::vector<sturm::GateRecord> out;
    out.reserve(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i) {
        out.push_back(ir.at(i));
    }
    return out;
}

// Human-readable rendering of a GateRecord.  Used only when the test
// fails so that the ctest log pinpoints the exact mismatch.
std::string render_gate(const sturm::GateRecord& g) {
    const sturm_gate_info_t* info = sturm_gate_info_of(g.kind);
    std::string out;
    out.reserve(48);
    out.append(info ? info->name : "?");
    out.append("(");
    for (uint8_t i = 0; i < g.n && i < 3u; ++i) {
        if (i) out.append(",");
        out.append(std::to_string(g.qubits[i]));
    }
    out.append(")");
    if (g.param != 0.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "@%.6g", g.param);
        out.append(buf);
    }
    return out;
}

// Call the fixture-supplied demo with two freshly-prepared quantum
// qbools, then return the captured gate stream.  The caller retains
// ownership of the two qubits allocated for `a`/`b`; the harness
// releases them after each capture so pool indices reset cleanly.
std::vector<sturm::GateRecord>
run_and_capture(void (*demo)(const sturm::qbool&, const sturm::qbool&)) {
    ScopedAppendContext sc;

    // Allocate two qubits for the input operands.  make_non_owning
    // builds a qbool view that references the allocated index without
    // taking ownership — the qbool destructor inside `demo` will not
    // release them.
    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;

    demo(a, b);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    return stream;
}

// PG-7: widened capture helper for the Phase G nested-WHEN fixture
// pair.  Three qubits: outer + inner are staged as superposed controls
// (super_mask=1) and target is staged as a classical-owning qbool
// (super_mask=1) so the inner WHEN body's lifted `target.flip()`
// emits CX(__stu_ctrl, target) against the active control stack.
// Ownership stays with the harness: `make_non_owning` builds qbool
// views that reference the allocated indices without taking
// ownership, so the qbool destructors inside `demo` do not attempt
// to release them.  The harness releases all three allocated qubits
// after each capture so pool indices reset cleanly across calls.
std::vector<sturm::GateRecord>
run_and_capture_nested(void (*demo)(sturm::qbool&,
                                    sturm::qbool&,
                                    sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qo = sturm::QubitPool::instance().allocate();
    const int qi = sturm::QubitPool::instance().allocate();
    const int qt = sturm::QubitPool::instance().allocate();
    sturm::qbool outer = sturm::qbool::make_non_owning(qo);
    outer.super_mask = 1ULL;
    sturm::qbool inner = sturm::qbool::make_non_owning(qi);
    inner.super_mask = 1ULL;
    sturm::qbool target = sturm::qbool::make_non_owning(qt);
    target.super_mask = 1ULL;

    demo(outer, inner, target);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qo);
    sturm::QubitPool::instance().release(qi);
    sturm::QubitPool::instance().release(qt);
    return stream;
}

// PH-6b: capture helper for the Phase H if/else fixture pair.  Two
// qubits (`a`, `b`) are allocated once per capture and passed by
// `make_non_owning` qbool views — ownership stays with the harness so
// the qbool destructors inside `demo` do not release them.  Each
// capture invokes `demo` TWICE — once with `cond=true`, once with
// `cond=false` — so both branches' lowerings show up in the stream.
// Pool indices reset after the capture so the transpiled and
// reference captures see identical qubit indices for `a`, `b`, and
// the per-branch intermediates.
std::vector<sturm::GateRecord>
run_and_capture_if_branches(void (*demo)(const sturm::qbool&,
                                         const sturm::qbool&,
                                         bool)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;

    demo(a, b, true);
    demo(a, b, false);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    return stream;
}

// PI-7: capture helper for the Phase I user-routine fixture pair.  The
// demo takes a single `const qbool& in` quantum input; one fresh qubit
// is allocated via `make_non_owning` so the harness retains ownership
// (the demo's own `qbool tmp; tmp.ensure_qubit();` allocates ITS ancilla
// through the same QubitPool, whose reset happens in `ScopedAppendContext`
// before each capture — so both the runtime and reference captures see
// identical qubit indices for `in` and `tmp`).  No multi-invocation
// loop: the demo is called exactly once per capture.
std::vector<sturm::GateRecord>
run_and_capture_user_routine(void (*demo)(const sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qin = sturm::QubitPool::instance().allocate();
    sturm::qbool in = sturm::qbool::make_non_owning(qin);
    in.super_mask = 1ULL;

    demo(in);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qin);
    return stream;
}

// PJ-1i: capture helper for the Phase J zero-ancilla fusion pair.  The
// demo takes `(const qbool& a, const qbool& b, qbool& x)`; three fresh
// qubits are allocated via `make_non_owning` so the harness retains
// ownership (the runtime demo's `qbool __t = a & b; x ^= __t;` is
// REPLACED in-place by `ccnot_inplace(x, a, b);` by the PJ-1d
// QReplacement — NO ancilla allocation survives the fusion.  The
// reference demo similarly calls `primitive_AND(ctx, a, b, x)`
// directly, never touching the QubitPool).  `x` is allocated via
// `make_non_owning` and passed by non-const reference because the
// runtime fixture's `^=` requires a non-const lvalue; ownership stays
// with the harness so the qbool destructor inside `demo` does not
// attempt to release the qubit.  Both the runtime and reference
// captures therefore observe the SAME three caller-supplied indices,
// which is the precondition for the byte-identical CCX(a, b, x) pair.
std::vector<sturm::GateRecord>
run_and_capture_fused(void (*demo)(const sturm::qbool&,
                                   const sturm::qbool&,
                                   sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    const int qx = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;
    sturm::qbool x = sturm::qbool::make_non_owning(qx);
    x.super_mask = 1ULL;

    demo(a, b, x);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    sturm::QubitPool::instance().release(qx);
    return stream;
}

// PJ-4e: capture helper for the Phase J dead-ancilla elimination pair.
// The demo takes `(const qbool& a, const qbool& b, qbool& target)`;
// three fresh qubits are allocated via `make_non_owning` so the
// harness retains ownership across the demo boundary.  `a` and `b`
// are passed through to preserve the runtime fixture's signature (the
// PJ-4a matcher fires on `qbool dead = a | b;` whose RHS references
// both argument DeclRefExprs — if `a` or `b` did not appear in the
// signature, the fixture would fail to parse).  `target` is allocated
// via `make_non_owning` and passed by non-const reference because
// `target.flip()` is a non-const member invocation that mutates the
// qbool's state via emit_X_lifted.  Both the runtime and reference
// captures therefore observe the SAME three caller-supplied indices,
// which is the precondition for the byte-identical X(target) record
// pair.  The runtime demo's `qbool dead = a | b;` is REPLACED in-place
// by empty text by the PJ-4a QReplacement — NO ancilla allocation
// survives the elimination; the reference demo similarly never
// allocates an ancilla because it simply does not carry the dead decl.
std::vector<sturm::GateRecord>
run_and_capture_dead_ancilla(void (*demo)(const sturm::qbool&,
                                          const sturm::qbool&,
                                          sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    const int qt = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;
    sturm::qbool target = sturm::qbool::make_non_owning(qt);
    target.super_mask = 1ULL;

    demo(a, b, target);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    sturm::QubitPool::instance().release(qt);
    return stream;
}

// PM5-8: capture helper for the Phase M peephole-reorder pair.  The
// demo takes `(const qbool& a, const qbool& b, const qbool& c,
// qbool& x, qbool& y)`; five fresh qubits are allocated via
// `make_non_owning` so the harness retains ownership across the demo
// boundary.  `a`, `b`, `c` are `const qbool&` to preserve the
// caller's qubit indices (the MVP OR's compound-flatten emission
// references all three through the `__stu_t0 | c` outer op plus the
// `(a | b)` inner op that PE-4 allocates `__stu_t0` for); `x` and
// `y` are non-const `qbool&` because the runtime fixture's `^=`
// operations require non-const lvalues (and `operator^=` mutates
// the qbool's state via emit_CX_lifted).  Both the runtime and
// reference captures therefore observe the SAME five caller-
// supplied indices, which is the precondition for a byte-identical
// gate stream comparison.
std::vector<sturm::GateRecord>
run_and_capture_reorder(void (*demo)(const sturm::qbool&,
                                     const sturm::qbool&,
                                     const sturm::qbool&,
                                     sturm::qbool&,
                                     sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    const int qc = sturm::QubitPool::instance().allocate();
    const int qx = sturm::QubitPool::instance().allocate();
    const int qy = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;
    sturm::qbool c = sturm::qbool::make_non_owning(qc);
    c.super_mask = 1ULL;
    sturm::qbool x = sturm::qbool::make_non_owning(qx);
    x.super_mask = 1ULL;
    sturm::qbool y = sturm::qbool::make_non_owning(qy);
    y.super_mask = 1ULL;

    demo(a, b, c, x, y);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    sturm::QubitPool::instance().release(qc);
    sturm::QubitPool::instance().release(qx);
    sturm::QubitPool::instance().release(qy);
    return stream;
}

// PN-8: capture helper for the Phase N rotation fixture pair.  The
// demo takes NO arguments because Phase N rotations auto-promote
// fully-classical qint_t<W> registers on first rotation (M15/M16 in
// qint_core.hpp:256-265 / 319-332) — the inner body allocates its
// own `qbool c(0.5)`, `qint_t<1> a`, `qint_t<1> b` via QubitPool
// straight after the ScopedAppendContext pool-reset.  Both the
// runtime (transpiled) and reference fixtures therefore observe the
// SAME pool state on entry, so the qbool c(0.5) prep allocates
// c.qubits[0] = 0, the first `a.theta() += 0.3` auto-promotes with
// a.qubits[0] = 1, and the first `b.phi() += 0.7` auto-promotes
// with b.qubits[0] = 2.  Identical qubit indices across the two
// captures is the precondition for the byte-identical RY / RZ /
// CRY gate stream comparison.  Note that the qbool(double)
// constructor calls current_sink()->prepare(qubits[0], p) (qbool.hpp
// line 62) which writes to the CounterSink ONLY — it does NOT emit
// a record to ctx->ir — so the captured GateRecord stream omits any
// "prepare" entry.
std::vector<sturm::GateRecord>
run_and_capture_rotations(void (*demo)()) {
    ScopedAppendContext sc;

    demo();

    return capture_ir(sc.ir());
}

// S-5: capture helper for the Phase S reversed-iteration ripple /
// bit_reversal pair.  Each demo takes four qbool references; four
// fresh qubits are allocated via `make_non_owning` so the harness
// retains ownership across the demo boundary.  All four qbools are
// passed by non-const reference because the runtime fixture's local
// `qbool* regs[4] = {&q0, &q1, ...}` pointer-array alias captures
// them by address, and the `*regs[i] ^= *regs[i-1]` call inside the
// for-body needs a non-const lvalue to mutate.  Both the transpiled
// and reference captures therefore observe the SAME four caller-
// supplied indices, which is the precondition for a byte-identical
// CX-stream comparison.
//
// 3 independent payloads: the harness invokes `demo` THREE times per
// capture — each invocation emits the same per-invocation gate stream
// (6 CX for ripple, 12 for bit_reversal) against the same caller-
// supplied qubit indices.  The byte-compare pins that iteration
// reversal produces a stable stream across repeated invocations as
// well as across the transpiled/reference boundary.
std::vector<sturm::GateRecord>
run_and_capture_reversible_loop_4(void (*demo)(sturm::qbool&,
                                               sturm::qbool&,
                                               sturm::qbool&,
                                               sturm::qbool&)) {
    ScopedAppendContext sc;

    const int q0 = sturm::QubitPool::instance().allocate();
    const int q1 = sturm::QubitPool::instance().allocate();
    const int q2 = sturm::QubitPool::instance().allocate();
    const int q3 = sturm::QubitPool::instance().allocate();
    sturm::qbool qb0 = sturm::qbool::make_non_owning(q0);
    qb0.super_mask = 1ULL;
    sturm::qbool qb1 = sturm::qbool::make_non_owning(q1);
    qb1.super_mask = 1ULL;
    sturm::qbool qb2 = sturm::qbool::make_non_owning(q2);
    qb2.super_mask = 1ULL;
    sturm::qbool qb3 = sturm::qbool::make_non_owning(q3);
    qb3.super_mask = 1ULL;

    // 3 independent payloads.
    demo(qb0, qb1, qb2, qb3);
    demo(qb0, qb1, qb2, qb3);
    demo(qb0, qb1, qb2, qb3);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(q0);
    sturm::QubitPool::instance().release(q1);
    sturm::QubitPool::instance().release(q2);
    sturm::QubitPool::instance().release(q3);
    return stream;
}

// S-5: capture helper for the Phase S reversed-iteration adder pair.
// Three qbool references — same mechanics as the 4-arg helper above.
std::vector<sturm::GateRecord>
run_and_capture_reversible_loop_3(void (*demo)(sturm::qbool&,
                                               sturm::qbool&,
                                               sturm::qbool&)) {
    ScopedAppendContext sc;

    const int q0 = sturm::QubitPool::instance().allocate();
    const int q1 = sturm::QubitPool::instance().allocate();
    const int q2 = sturm::QubitPool::instance().allocate();
    sturm::qbool qb0 = sturm::qbool::make_non_owning(q0);
    qb0.super_mask = 1ULL;
    sturm::qbool qb1 = sturm::qbool::make_non_owning(q1);
    qb1.super_mask = 1ULL;
    sturm::qbool qb2 = sturm::qbool::make_non_owning(q2);
    qb2.super_mask = 1ULL;

    // 3 independent payloads.
    demo(qb0, qb1, qb2);
    demo(qb0, qb1, qb2);
    demo(qb0, qb1, qb2);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(q0);
    sturm::QubitPool::instance().release(q1);
    sturm::QubitPool::instance().release(q2);
    return stream;
}

bool gates_equal(const sturm::GateRecord& x, const sturm::GateRecord& y) {
    if (x.kind != y.kind) return false;
    if (x.n    != y.n)    return false;
    for (uint8_t i = 0; i < x.n && i < 3u; ++i) {
        if (x.qubits[i] != y.qubits[i]) return false;
    }
    return x.param == y.param;
}

int assert_streams_equal(const std::vector<sturm::GateRecord>& got,
                         const std::vector<sturm::GateRecord>& ref) {
    if (got.size() != ref.size()) {
        std::fprintf(stderr,
                     "gate stream size mismatch: transpiled=%zu reference=%zu\n",
                     got.size(), ref.size());
        const std::size_t n = got.size() > ref.size() ? got.size() : ref.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::string g = i < got.size()
                ? render_gate(got[i]) : std::string("<missing>");
            const std::string r = i < ref.size()
                ? render_gate(ref[i]) : std::string("<missing>");
            std::fprintf(stderr, "  [%zu] transpiled=%s reference=%s\n",
                         i, g.c_str(), r.c_str());
        }
        return 1;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (!gates_equal(got[i], ref[i])) {
            std::fprintf(stderr,
                         "gate[%zu] mismatch: transpiled=%s reference=%s\n",
                         i, render_gate(got[i]).c_str(),
                         render_gate(ref[i]).c_str());
            return 1;
        }
    }
    return 0;
}

} // namespace

int main() {
    std::printf("M12 gate-stream equivalence test (or_single):\n");

    // Capture reference first so any qubit-pool state from a prior
    // harness call (e.g. global ctor) is already drained by the
    // run_and_capture reset.
    const auto ref_stream = run_and_capture(&m12_reference::demo);
    const auto got_stream = run_and_capture(&m12_transpiled::demo);

    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_stream.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_stream[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_stream.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_stream[i]).c_str());
    }

    // Sanity check: both fixtures must emit a non-empty gate stream,
    // otherwise the test would pass trivially (empty == empty) without
    // exercising the transpiler.  The forward-quantum OR emits exactly
    // three gates (CX + CX + CCX) and the inverse emits another three
    // (CCX + CX + CX), so we expect six gates on each side.
    if (ref_stream.empty()) {
        std::fprintf(stderr,
                     "reference fixture produced 0 gates — fixture is not "
                     "exercising the quantum OR circuit.\n");
        return 1;
    }

    if (int rc = assert_streams_equal(got_stream, ref_stream); rc != 0) {
        std::fprintf(stderr, "gate-stream mismatch — see log above.\n");
        return rc;
    }

    // LP7: re-assertion for the `examples/or_circuit.cpp` pattern —
    // same shape (`qbool c = a | b;`) but with a variable name that
    // matches the real example. This binds PRD acceptance #5 to the
    // example's actual spelling. Streams must match gate-for-gate and
    // operand-for-operand.
    std::printf("LP7 gate-stream equivalence test (or_circuit pattern):\n");
    const auto ref_c = run_and_capture(&m12_reference::demo_or_circuit);
    const auto got_c = run_and_capture(&m12_transpiled::demo_or_circuit);
    if (ref_c.empty()) {
        std::fprintf(stderr,
                     "or_circuit reference produced 0 gates — fixture not "
                     "exercising the quantum OR circuit.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_c, ref_c); rc != 0) {
        std::fprintf(stderr, "or_circuit gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  or_circuit streams match (%zu gates).\n", ref_c.size());

    // PG-7: Phase G nested-WHEN gate-equivalence pair.  Proves the
    // transpiler's nested-WHEN lowering (outer & inner → __stu_ctrl +
    // WHEN + uncompute_and) emits the same gate stream as the hand-
    // written reference.  Expected stream is three gates:
    //   CCX(outer, inner, __stu_ctrl)   // from `qbool __stu_ctrl = outer & inner;`
    //   CX (__stu_ctrl, target)         // from WHEN(__stu_ctrl) { target.flip(); }
    //   CCX(outer, inner, __stu_ctrl)   // from sturm::uncompute_and(...)
    std::printf("PG-7 gate-stream equivalence test (nested_when pattern):\n");
    const auto ref_n = run_and_capture_nested(&m12_nested_reference::demo);
    const auto got_n = run_and_capture_nested(&m12_nested_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_n.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_n[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_n.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_n[i]).c_str());
    }
    if (ref_n.empty()) {
        std::fprintf(stderr,
                     "nested_when reference produced 0 gates — fixture not "
                     "exercising the Phase G nested-WHEN lowering.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_n, ref_n); rc != 0) {
        std::fprintf(stderr, "nested_when gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  nested_when streams match (%zu gates).\n", ref_n.size());

    // PH-6b: Phase H if/else gate-equivalence pair.  Proves the
    // transpiler's per-branch OR-uncompute injection emits the same
    // gate stream as the hand-written reference.  Each branch is a
    // six-gate forward+adjoint sequence; the harness invokes `demo`
    // twice per capture (cond=true then cond=false) so BOTH branch
    // lowerings are exercised in a single stream — expected length is
    // twelve gates.
    std::printf("PH-6b gate-stream equivalence test (if_branches pattern):\n");
    const auto ref_if = run_and_capture_if_branches(&m12_if_branches_reference::demo);
    const auto got_if = run_and_capture_if_branches(&m12_if_branches_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_if.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_if[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_if.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_if[i]).c_str());
    }
    if (ref_if.empty()) {
        std::fprintf(stderr,
                     "if_branches reference produced 0 gates — fixture not "
                     "exercising the Phase H if/else OR lowering.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_if, ref_if); rc != 0) {
        std::fprintf(stderr, "if_branches gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  if_branches streams match (%zu gates).\n", ref_if.size());

    // PH-6a: Phase H for-loop gate-equivalence pair.  Proves the
    // transpiler's per-iteration OR-uncompute injection emits the same
    // gate stream as the hand-written reference.  Each iteration is a
    // six-gate forward+adjoint sequence; the body has three iterations
    // so the captured stream covers eighteen gates.  Because the
    // QubitPool is a LIFO free-list and `tmp`'s RAII destructor releases
    // the ancilla at the body's `}`, every iteration reuses the same
    // qubit index — the byte-compare therefore fails hard if the
    // transpiler ever drifts the injected call out of the per-iteration
    // scope (e.g. planting it after the loop, or once per function).
    std::printf("PH-6a gate-stream equivalence test (for_loop pattern):\n");
    const auto ref_for = run_and_capture(&m12_for_loop_reference::demo);
    const auto got_for = run_and_capture(&m12_for_loop_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_for.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_for[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_for.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_for[i]).c_str());
    }
    if (ref_for.empty()) {
        std::fprintf(stderr,
                     "for_loop reference produced 0 gates — fixture not "
                     "exercising the Phase H for-loop OR lowering.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_for, ref_for); rc != 0) {
        std::fprintf(stderr, "for_loop gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  for_loop streams match (%zu gates).\n", ref_for.size());

    // PI-7: Phase I user-routine gate-equivalence pair.  Proves the
    // transpiler's PI-2/PI-3/PI-4 rewrite — which injects an
    // `invert(<routine>)(...)` call at scope exit for a registered
    // forward routine with a local-intermediate output slot — emits
    // the same gate stream as a hand-written reference that spells
    // the forward + adjoint calls verbatim in LIFO order.  The
    // routine performs three `tmp.flip()` calls (three X gates),
    // followed by the adjoint (three more X gates), against a freshly
    // allocated `tmp` ancilla — expected stream length is six gates.
    std::printf("PI-7 gate-stream equivalence test (user_routine pattern):\n");
    const auto ref_ur = run_and_capture_user_routine(&m12_user_routine_reference::demo);
    const auto got_ur = run_and_capture_user_routine(&m12_user_routine_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_ur.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_ur[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_ur.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_ur[i]).c_str());
    }
    if (ref_ur.empty()) {
        std::fprintf(stderr,
                     "user_routine reference produced 0 gates — fixture not "
                     "exercising the Phase I invert() injection.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_ur, ref_ur); rc != 0) {
        std::fprintf(stderr, "user_routine gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  user_routine streams match (%zu gates).\n", ref_ur.size());

    // PJ-1i: Phase J zero-ancilla fusion gate-equivalence pair.  Proves
    // the transpiler's PJ-1d peephole matcher + PJ-1c render case —
    // which collapses `qbool __t = a & b; x ^= __t;` into a single
    // `ccnot_inplace(x, a, b);` and plants a self-adjoint
    // `ccnot_inplace(x, a, b);` at scope close — emits the same two-
    // CCX gate stream as a hand-written reference that calls
    // `primitive_AND(ctx, a, b, x)` directly against the 18-gate
    // sink (bypassing the pre-PK-2 expression-template materialisation
    // path, which would allocate an intermediate ancilla and therefore
    // produce a mismatched stream).  Expected stream length is TWO CCX records on the
    // three caller-supplied qubit indices — no ancilla allocation on
    // either side, which is the defining invariant of the fusion.
    std::printf("PJ-1i gate-stream equivalence test (fuse_xor_and pattern):\n");
    const auto ref_fu = run_and_capture_fused(&m12_fused_reference::demo);
    const auto got_fu = run_and_capture_fused(&m12_fused_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_fu.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_fu[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_fu.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_fu[i]).c_str());
    }
    if (ref_fu.empty()) {
        std::fprintf(stderr,
                     "fuse_xor_and reference produced 0 gates — fixture not "
                     "exercising the Phase J PJ-1 zero-ancilla fusion.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_fu, ref_fu); rc != 0) {
        std::fprintf(stderr, "fuse_xor_and gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  fuse_xor_and streams match (%zu gates).\n", ref_fu.size());

    // PJ-4e: Phase J dead-ancilla elimination gate-equivalence pair.
    // Proves the transpiler's PJ-4a peephole matcher + empty-text
    // `QReplacement` + `eliminated_stmt_ranges` backstop — which strips
    // `qbool dead = a | b;` (zero-reader condition) from the rewritten
    // source AND suppresses the downstream MVP OR matcher's per-decl
    // `uncompute_or(...)` injection — emits the same one-X gate stream
    // as a hand-written reference that simply OMITS the dead decl.
    // The reference's `target.flip()` sentinel emits exactly one X
    // record via emit_X_lifted; the transpiled demo's `target.flip()`
    // emits the same record, and the dead decl contributes ZERO gates
    // because PJ-4a strips it before the MVP OR matcher can anchor on
    // it.  Expected stream length is ONE X record on the caller-
    // supplied `target` qubit index — no ancilla allocation on either
    // side, which is the defining invariant of the PJ-4 elimination
    // (no ancilla allocated → no stray gates).
    std::printf("PJ-4e gate-stream equivalence test (dead_ancilla pattern):\n");
    const auto ref_da = run_and_capture_dead_ancilla(&m12_dead_ancilla_reference::demo);
    const auto got_da = run_and_capture_dead_ancilla(&m12_dead_ancilla_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_da.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_da[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_da.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_da[i]).c_str());
    }
    if (ref_da.empty()) {
        std::fprintf(stderr,
                     "dead_ancilla reference produced 0 gates — fixture not "
                     "exercising the Phase J PJ-4 dead-ancilla elimination "
                     "(the sentinel target.flip() call is missing).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_da, ref_da); rc != 0) {
        std::fprintf(stderr, "dead_ancilla gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  dead_ancilla streams match (%zu gates).\n", ref_da.size());

    // PJ-3h: Phase J uncompute-hoisting gate-equivalence pair.  Proves
    // the transpiler's PJ-3d `register_hoist_invariant_matcher` +
    // `op.hoist_to_override` field plumbing + M8 synthesis-pass
    // priority-ladder consumption — which relocates the
    // `sturm::uncompute_or(t, a, b);` call from the for-body's close
    // brace (where the MVP OR matcher would plant it) to the enclosing
    // scope's close brace (AFTER the for-loop's `}`) — emits the same
    // gate stream as a hand-written reference that spells that post-
    // loop uncompute verbatim.  The outer predecl `qbool t;` +
    // `t.ensure_qubit();` give the hoisted call a quantum `r` operand
    // so it takes the both-quantum branch in
    // `src/sturm/uncompute/uncompute_api.cpp` (three gates at the
    // hoisted site: CCX+CX+CX against (q_a, q_b, q_t_outer)).
    // Because PJ-3d only relocates the UNCOMPUTE anchor — the forward
    // `qbool t = a | b;` stays INSIDE the loop body, firing per-
    // iteration — the captured stream covers eighteen in-loop gates
    // (three forward OR per iter + three transpiler-emitted uncompute
    // calls per iter, for three iterations) PLUS three post-loop
    // hoisted gates = 21 gates per capture.  The QubitPool LIFO free-list hands the same ancilla
    // index back across iterations, so all in-loop gates share the
    // same qubit triples — a byte-compare that fails hard if the
    // transpiler ever drifts the hoist anchor INSIDE the loop body
    // (in which case the stream would contain four extra gates on
    // the inner `t` that the reference's post-loop outer-t variant
    // would not).
    std::printf("PJ-3h gate-stream equivalence test (hoist pattern):\n");
    const auto ref_ho = run_and_capture(&m12_hoist_reference::demo);
    const auto got_ho = run_and_capture(&m12_hoist_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_ho.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_ho[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_ho.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_ho[i]).c_str());
    }
    if (ref_ho.empty()) {
        std::fprintf(stderr,
                     "hoist reference produced 0 gates — fixture not "
                     "exercising the Phase J PJ-3 uncompute-hoisting "
                     "rewrite (the inner `qbool t = a | b;` may have "
                     "been stripped by PJ-4a dead-ancilla "
                     "elimination — check the `(void)t;` reader).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_ho, ref_ho); rc != 0) {
        std::fprintf(stderr, "hoist gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  hoist streams match (%zu gates).\n", ref_ho.size());

    // PM5-8: Phase M peephole-reorder gate-equivalence pair.  Proves the
    // transpiler's PM5-5 `register_peephole_reorder_matcher` — which
    // runs LAST over the populated scope and conservatively refuses to
    // fire on any triple that does not match its (A=AND-synthetic, B,
    // C=XOR_ASSIGN-on-A.result) shape — emits the same gate stream as
    // a hand-written reference that spells the same compound-flatten +
    // XOR_ASSIGN + uncompute sequence by hand.  For this fixture the
    // PE-4 outer OR wedges between the inner AND and any subsequent
    // XOR_ASSIGN in scope.ops, so PM5 does not fire; the transpiled
    // and reference streams are therefore byte-identical by
    // construction, pinning the "conservative refusal preserves the
    // gate stream" invariant that PM5's Gate-1/2/3/4 ordering is
    // designed to guarantee.  Stream shape: the compound-flatten
    // produces two OR expansions (each three gates on the happy
    // path), the two user-written `^=` contribute one gate each, and
    // the LIFO self-adjoints + OR uncomputes add the matching reverse
    // gate sequence — the exact per-gate accounting depends on
    // qbool's operator| / operator^= decomposition in the live
    // backend, but structural equivalence is the load-bearing
    // property (not a specific gate count).
    std::printf("PM5-8 gate-stream equivalence test (reorder pattern):\n");
    const auto ref_ro = run_and_capture_reorder(&m12_reorder_reference::demo);
    const auto got_ro = run_and_capture_reorder(&m12_reorder_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_ro.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_ro[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_ro.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_ro[i]).c_str());
    }
    if (ref_ro.empty()) {
        std::fprintf(stderr,
                     "reorder reference produced 0 gates — fixture not "
                     "exercising the Phase M peephole-reorder pipeline "
                     "(the PE-4 compound-flatten + MVP OR + PA-3 ^= "
                     "chain may have been short-circuited — check that "
                     "STURM_BACKEND_ENABLED is defined and `(a | b) | c` "
                     "is not resolving to the classical short-circuit "
                     "path).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_ro, ref_ro); rc != 0) {
        std::fprintf(stderr, "reorder gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  reorder streams match (%zu gates).\n", ref_ro.size());

    // PN-8: Phase N rotation gate-equivalence pair.  Proves the
    // transpiler's PN-2 rotation matchers + PN-4 uncompute arms —
    // which stage a QOperation per `q.theta() += d;` / `q.theta() -= d;`
    // / `q.phi() += d;` / `q.phi() -= d;` on the enclosing QScope
    // and emit a sign-flipped inverse (`+=` ↔ `-=`) before the scope
    // closes — emit the same gate stream as a hand-written reference
    // that spells the five inverses (one in-body depth-1 WHEN guard
    // + four LIFO duals at demo body close) by hand.  The runtime's
    // self-dual ThetaProxy::operator-= / PhiProxy::operator-= at
    // include/sturm/qtypes/qint_core.hpp:305,372 dispatches `-delta`
    // through the same emit_RY_lifted / emit_RZ_lifted path, so both
    // DSL spellings of a sign-flipped rotation (`a.theta() -= 0.3`
    // and `a.theta() += -0.3`) produce byte-identical GateRecord
    // entries.  Expected stream: ten gates spanning RY, RZ, CRY —
    // see rotations_runtime.cpp / rotations_reference.cpp for the
    // per-gate accounting.  The demos take no arguments because
    // Phase N rotations auto-promote fully-classical qint_t<W>
    // registers on first rotation (M15/M16), so the QubitPool-
    // reset-then-allocate sequence in ScopedAppendContext gives
    // both fixtures identical qubit indices without the harness
    // needing to stage caller-owned qubits.
    std::printf("PN-8 gate-stream equivalence test (rotations pattern):\n");
    const auto ref_rt = run_and_capture_rotations(&m12_rotations_reference::demo);
    const auto got_rt = run_and_capture_rotations(&m12_rotations_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_rt.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_rt[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_rt.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_rt[i]).c_str());
    }
    if (ref_rt.empty()) {
        std::fprintf(stderr,
                     "rotations reference produced 0 gates — fixture not "
                     "exercising the Phase N rotation pipeline (the four "
                     "outer theta/phi compound-assigns plus the depth-1 "
                     "WHEN-guarded theta rotation should collectively emit "
                     "ten gates against the active APPEND-mode context — "
                     "check that STURM_BACKEND_ENABLED is defined and the "
                     "qint_t<1> + qbool includes resolve to the real "
                     "headers, not the `__has_include` stub branch).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_rt, ref_rt); rc != 0) {
        std::fprintf(stderr, "rotations gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  rotations streams match (%zu gates).\n", ref_rt.size());

    // S-5: Phase S reversed-iteration gate-equivalence triple (sturm-ha2k.6).
    // Three pairs — ripple, bit_reversal, adder — each invoking `demo` 3
    // times per capture (3 independent payloads per pair) and asserting
    // byte-identical gate streams between the transpiled and reference
    // TUs.  Until Phase S's `loop_reversal` module + R-2
    // auto_register_emitter land, both TUs are hand-written byte-for-byte
    // twins; the test therefore pins (a) the deterministic qubit-index
    // invariant across multi-invocation runs and (b) the reversed-
    // iteration gate ordering that a future Phase S implementation must
    // reproduce.  The fixtures' top-of-file prose documents the full R-2
    // handoff contract.
    std::printf("S-5 gate-stream equivalence test (ripple pattern):\n");
    const auto ref_rl_r = run_and_capture_reversible_loop_4(
        &m12_reversible_loop_ripple_reference::demo);
    const auto got_rl_r = run_and_capture_reversible_loop_4(
        &m12_reversible_loop_ripple_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_rl_r.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_rl_r[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_rl_r.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_rl_r[i]).c_str());
    }
    if (ref_rl_r.empty()) {
        std::fprintf(stderr,
                     "ripple reference produced 0 gates — fixture not "
                     "exercising the Phase S reversed-iteration XOR sweep "
                     "(the `^=` operator may be resolving to a stub "
                     "branch — check STURM_BACKEND_ENABLED).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_rl_r, ref_rl_r); rc != 0) {
        std::fprintf(stderr, "ripple gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  ripple streams match (%zu gates).\n", ref_rl_r.size());

    std::printf("S-5 gate-stream equivalence test (bit_reversal pattern):\n");
    const auto ref_rl_b = run_and_capture_reversible_loop_4(
        &m12_reversible_loop_bit_reversal_reference::demo);
    const auto got_rl_b = run_and_capture_reversible_loop_4(
        &m12_reversible_loop_bit_reversal_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_rl_b.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_rl_b[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_rl_b.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_rl_b[i]).c_str());
    }
    if (ref_rl_b.empty()) {
        std::fprintf(stderr,
                     "bit_reversal reference produced 0 gates — fixture "
                     "not exercising the Phase S reversed-iteration XOR "
                     "swap (check STURM_BACKEND_ENABLED and the "
                     "pointer-array alias in the fixture body).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_rl_b, ref_rl_b); rc != 0) {
        std::fprintf(stderr,
                     "bit_reversal gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  bit_reversal streams match (%zu gates).\n",
                ref_rl_b.size());

    std::printf("S-5 gate-stream equivalence test (adder pattern):\n");
    const auto ref_rl_a = run_and_capture_reversible_loop_3(
        &m12_reversible_loop_adder_reference::demo);
    const auto got_rl_a = run_and_capture_reversible_loop_3(
        &m12_reversible_loop_adder_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_rl_a.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_rl_a[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_rl_a.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_rl_a[i]).c_str());
    }
    if (ref_rl_a.empty()) {
        std::fprintf(stderr,
                     "adder reference produced 0 gates — fixture not "
                     "exercising the Phase S reversed-iteration carry "
                     "chain (check STURM_BACKEND_ENABLED and the "
                     "pointer-array alias in the fixture body).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_rl_a, ref_rl_a); rc != 0) {
        std::fprintf(stderr, "adder gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  adder streams match (%zu gates).\n", ref_rl_a.size());

    // R-5 / T-5: Phase R/T straight-line automatic-adjoint-synthesis
    // gate-equivalence pair (sturm-88d7.6 / sturm-xrob.6).  One pair —
    // reversible_synth — invoking `demo` 3 times per capture (3
    // independent payloads) and asserting byte-identical gate streams
    // between the transpiled and reference TUs.  Post T-1 wiring
    // matcher_reversible_drive is active in the consumer: the runtime
    // fixture's `demo()` body contains ONLY the forward cascade, and
    // the transpiler auto-emits the sibling `__demo_adj` function +
    // global-scope `STURM_REGISTER_ADJOINT` line.  The reference
    // fixture mirrors this emission by hand.  The test therefore
    // pins (a) the deterministic qubit-index invariant across multi-
    // invocation runs and (b) the forward-cascade gate ordering that
    // auto-synthesis must leave untouched on both sides.  The auto-
    // synthesised `__demo_adj` body is NOT invoked by this harness —
    // the T-3 roundtrip test in tests/test_invert.cpp (sturm-xrob.4)
    // is the companion that exercises the `invert(&demo)` lookup
    // path.  The capture helper reused here is
    // `run_and_capture_reversible_loop_4` — the signature matches the
    // S-5 ripple / bit_reversal pairs (four qbool references, three
    // invocations per capture) but the demo body is straight-line
    // with NO for-loop, the structural signature of Phase R.
    std::printf("R-5 / T-5 gate-stream equivalence test (reversible_synth pattern):\n");
    const auto ref_rs = run_and_capture_reversible_loop_4(
        &m12_reversible_synth_reference::demo);
    const auto got_rs = run_and_capture_reversible_loop_4(
        &m12_reversible_synth_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_rs.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_rs[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_rs.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_rs[i]).c_str());
    }
    if (ref_rs.empty()) {
        std::fprintf(stderr,
                     "reversible_synth reference produced 0 gates — "
                     "fixture not exercising the Phase R straight-line "
                     "XOR cascade (the `^=` operator may be resolving "
                     "to a stub branch — check STURM_BACKEND_ENABLED "
                     "and the pointer-array alias in the fixture "
                     "body).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_rs, ref_rs); rc != 0) {
        std::fprintf(stderr,
                     "reversible_synth gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  reversible_synth streams match (%zu gates).\n",
                ref_rs.size());

    std::printf("pair 1: %zu gates match\n", ref_stream.size());
    std::printf("pair 2: %zu gates match\n", ref_c.size());
    std::printf("pair 3: %zu gates match\n", ref_n.size());
    std::printf("pair 4: %zu gates match\n", ref_if.size());
    std::printf("pair 5: %zu gates match\n", ref_for.size());
    std::printf("pair 6: %zu gates match\n", ref_ur.size());
    std::printf("pair 7: %zu gates match\n", ref_fu.size());
    std::printf("pair 8: %zu gates match\n", ref_da.size());
    std::printf("pair 9: %zu gates match\n", ref_ho.size());
    std::printf("pair 10: %zu gates match\n", ref_ro.size());
    std::printf("pair 11: %zu gates match\n", ref_rt.size());
    std::printf("pair 12: %zu gates match\n", ref_rl_r.size());
    std::printf("pair 13: %zu gates match\n", ref_rl_b.size());
    std::printf("pair 14: %zu gates match\n", ref_rl_a.size());
    std::printf("pair 15: %zu gates match\n", ref_rs.size());
    std::printf("PASS\n");
    return 0;
}
