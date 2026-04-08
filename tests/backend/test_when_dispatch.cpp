// test_when_dispatch.cpp — M24: WHEN entry-point dispatch.
//
// Tests:
//   1. active_control() returns nullptr when no WHEN is active.
//   2. active_control() returns a non-null pointer inside a superposed WHEN.
//   3. active_control() returns nullptr inside a classical-true WHEN.
//   4. Op inside WHEN (superposed) routes to c_* variant (gate count increases by
//      more than if called with no control — specifically uses controlled gate).
//      Uses when_dispatch_not() — the M24 production dispatch wrapper.
//   5. Op outside WHEN routes to uncontrolled variant (X gate).
//   6. Nested WHEN: inside inner body, active_control() points to an ancilla
//      (not to either of the two original qbool guards), so there is exactly
//      one control qubit presented to downstream ops.
//   7. After nested WHEN exits: TLS is correctly restored to outer control, then
//      to nullptr.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/control/when.hpp"           // WhenGuard, WHEN, active_control()
#include "sturm/control/when_dispatch.hpp"  // when_dispatch_not — M24 production dispatcher
#include "sturm/ops/c_quantum_not.hpp"      // c_quantum_not — the c_* variant under test
#include "sturm/core/context.hpp"           // BackendContext, execute_gate
#include "sturm/core/core.h"                // sturm_backend_create/destroy, STURM_MODE_APPEND
#include "sturm/backend/ir.hpp"             // GateIR — inspect emitted gates
#include "sturm/uncompute/qint_base.hpp"    // qint_base — register type for dispatch

#include <cassert>
#include <cstdio>

// ── ScopedAppendCtx ───────────────────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::GateIR& ir() { return ctx->ir; }
};

// ── helpers ───────────────────────────────────────────────────────────────────

// Count how many CX gates are in the IR.
static size_t count_cx(const sturm::GateIR& ir) {
    size_t n = 0;
    for (size_t i = 0; i < ir.size(); ++i) {
        if (ir.at(i).kind == STURM_GATE_CX) ++n;
    }
    return n;
}

// Count how many CCX gates are in the IR.
static size_t count_ccx(const sturm::GateIR& ir) {
    size_t n = 0;
    for (size_t i = 0; i < ir.size(); ++i) {
        if (ir.at(i).kind == STURM_GATE_CCX) ++n;
    }
    return n;
}

// ── Test 1: active_control() == nullptr outside any WHEN ──────────────────────

static void test_active_control_null_outside_when() {
    assert(sturm::WhenGuard::active_control() == nullptr &&
           "active_control() must be nullptr when no WHEN scope is active");
    std::printf("PASS test_active_control_null_outside_when\n");
}

// ── Test 2: active_control() != nullptr inside superposed WHEN ────────────────

static void test_active_control_set_inside_super_when() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::qbool* ctrl_inside = nullptr;

    sturm::qbool flag(0.5);   // superposed
    WHEN(flag) {
        ctrl_inside = sturm::WhenGuard::active_control();
    }
    assert(ctrl_inside != nullptr &&
           "active_control() must be non-null inside superposed WHEN");
    assert(ctrl_inside == &flag &&
           "active_control() must point to the flag inside a single superposed WHEN");
    assert(sturm::WhenGuard::active_control() == nullptr &&
           "active_control() must be nullptr after WHEN scope exits");
    std::printf("PASS test_active_control_set_inside_super_when\n");
}

// ── Test 3: active_control() == nullptr inside classical-true WHEN ────────────

static void test_active_control_null_inside_classical_when() {
    sturm::qbool* ctrl_inside = reinterpret_cast<sturm::qbool*>(0x1);
    sturm::qbool flag(true);  // classical true
    WHEN(flag) {
        ctrl_inside = sturm::WhenGuard::active_control();
    }
    assert(ctrl_inside == nullptr &&
           "active_control() must be nullptr inside classical-true WHEN (no quantum ctrl)");
    std::printf("PASS test_active_control_null_inside_classical_when\n");
}

// ── Test 4: op inside superposed WHEN emits CX (controlled gate) ─────────────
//
// We call when_dispatch_not() — the M24 production dispatch wrapper — inside a
// superposed WHEN.  The dispatcher inspects WhenGuard::active_control() and
// routes to c_quantum_not, which emits CX(ctrl, tgt) rather than X(tgt).
// Outside a WHEN, when_dispatch_not must emit X (uncontrolled).

static void test_op_inside_when_uses_controlled_variant() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qbool flag(0.5);   // superposed — active_control() will be non-null

    // Build a qint_base with one superposed bit at physical qubit 5.
    sturm::qint_base target;
    target.width      = 1u;
    target.super_mask = 1u;   // bit 0 is quantum
    target.qubits[0]  = 5u;

    size_t gates_before = sc.ir().size();
    WHEN(flag) {
        // Inside WHEN: active_control() != nullptr.
        // when_dispatch_not routes to c_quantum_not → emits CX(ctrl, tgt).
        assert(sturm::WhenGuard::active_control() != nullptr);
        sturm::when_dispatch_not(target, *sturm_get_thread_context());
    }
    size_t gates_after = sc.ir().size();

    // Must have emitted exactly one CX gate (CX(ctrl, tgt.qubits[0])).
    assert(gates_after > gates_before && "must emit gates inside WHEN");
    size_t cx_count = count_cx(sc.ir());
    assert(cx_count == 1u && "when_dispatch_not inside WHEN must emit exactly 1 CX for 1 target bit");
    std::printf("PASS test_op_inside_when_uses_controlled_variant (CX count=%zu)\n", cx_count);
}

// ── Test 5: op outside WHEN emits X (uncontrolled) via when_dispatch_not ─────
//
// Calls when_dispatch_not() outside any WHEN scope.
// active_control() == nullptr → dispatcher must route to the uncontrolled path,
// emitting X(tgt) rather than CX(ctrl, tgt).

static void test_op_outside_when_uses_uncontrolled_variant() {
    ScopedAppendCtx sc;

    // Confirm no WHEN is active.
    assert(sturm::WhenGuard::active_control() == nullptr);

    // Build a qint_base with one superposed bit at physical qubit 3.
    sturm::qint_base target;
    target.width      = 1u;
    target.super_mask = 1u;
    target.qubits[0]  = 3u;

    size_t gates_before = sc.ir().size();
    // Outside WHEN: when_dispatch_not → uncontrolled path → X(tgt.qubits[0]).
    sturm::when_dispatch_not(target, *sturm_get_thread_context());
    size_t gates_after = sc.ir().size();

    assert(gates_after == gates_before + 1u && "must emit exactly 1 gate");
    assert(sc.ir().at(gates_before).kind == STURM_GATE_X && "must be an X gate (uncontrolled)");
    // No CX should be present.
    assert(count_cx(sc.ir()) == 0u && "no CX must be emitted outside WHEN");
    std::printf("PASS test_op_outside_when_uses_uncontrolled_variant\n");
}

// ── Test 6: nested WHEN — exactly one ancilla control ─────────────────────────
//
// With AND-fold (principle B5), inside a nested WHEN:
//   - outer WHEN sets current_control = &outer_flag
//   - inner WHEN sees current_control != nullptr; allocates ancilla and computes
//     ancilla = outer AND inner via CCX; sets current_control = &ancilla
//   - active_control() inside inner body points to the ancilla, not to either flag
//   - After inner scope: ancilla is uncomputed, current_control restored to &outer_flag
//   - After outer scope: current_control restored to nullptr
//
// The "exactly one ancilla control" invariant is that ops inside the inner body
// see exactly one control qubit — the ancilla — not two separate controls.

static void test_nested_when_single_ancilla_control() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qbool outer_flag(0.5);
    sturm::qbool inner_flag(0.5);

    sturm::qbool* outer_ctrl     = nullptr;
    sturm::qbool* inner_body_ctrl = nullptr;
    sturm::qbool* after_inner_ctrl = nullptr;

    WHEN(outer_flag) {
        outer_ctrl = sturm::WhenGuard::active_control();
        assert(outer_ctrl == &outer_flag &&
               "inside outer WHEN, active_control must point to outer_flag");

        WHEN(inner_flag) {
            inner_body_ctrl = sturm::WhenGuard::active_control();
            // B5: nested WHEN collapses to a single ancilla.
            // active_control() must NOT be &outer_flag and NOT be &inner_flag;
            // it must be a distinct ancilla.
            assert(inner_body_ctrl != nullptr &&
                   "active_control must be non-null inside nested WHEN");
            assert(inner_body_ctrl != &outer_flag &&
                   "inside nested WHEN, active_control must NOT be outer_flag");
            assert(inner_body_ctrl != &inner_flag &&
                   "inside nested WHEN, active_control must NOT be inner_flag (use ancilla)");
        }

        after_inner_ctrl = sturm::WhenGuard::active_control();
        assert(after_inner_ctrl == &outer_flag &&
               "after inner WHEN exits, active_control must be restored to outer_flag");
    }

    assert(sturm::WhenGuard::active_control() == nullptr &&
           "after all WHEN scopes, active_control must be nullptr");

    (void)outer_ctrl;
    (void)inner_body_ctrl;
    (void)after_inner_ctrl;

    std::printf("PASS test_nested_when_single_ancilla_control\n");
}

// ── Test 7: nested WHEN AND-fold emits CCX to compute ancilla ─────────────────
//
// The AND-fold (CCX on outer_ctrl, inner_ctrl, ancilla) must be visible in the
// IR when using APPEND mode.  We check that CCX gates appear on entry and exit
// of the inner WHEN scope.

static void test_nested_when_emits_ccx_for_ancilla() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qbool outer_flag(0.5);
    sturm::qbool inner_flag(0.5);

    size_t before = sc.ir().size();
    WHEN(outer_flag) {
        WHEN(inner_flag) {
            // body is intentionally empty — we only care about the CCX emitted
            // for AND-fold on scope entry and uncompute on scope exit.
        }
    }
    size_t after = sc.ir().size();

    // At minimum 2 CCX gates must appear: one for AND-fold on entry,
    // one for uncompute on exit.
    size_t ccx = count_ccx(sc.ir());
    assert(after > before && "nested WHEN must emit at least the AND-fold CCX gates");
    assert(ccx >= 2u && "nested WHEN must emit at least 2 CCX gates (forward + uncompute)");
    std::printf("PASS test_nested_when_emits_ccx_for_ancilla (CCX count=%zu)\n", ccx);
}

// ── Count X gates in the IR ───────────────────────────────────────────────────
static size_t count_x(const sturm::GateIR& ir) {
    size_t n = 0;
    for (size_t i = 0; i < ir.size(); ++i) {
        if (ir.at(i).kind == STURM_GATE_X) ++n;
    }
    return n;
}

// ── Test 8: IR routing through when_dispatch_not ──────────────────────────────
//
// Verifies the three dispatch cases in the IR:
//   (a) X outside WHEN    — when_dispatch_not → uncontrolled → X
//   (b) CX inside WHEN    — when_dispatch_not → c_quantum_not → CX
//   (c) CX (ancilla ctrl) inside nested WHEN — ancilla becomes control qubit;
//       when_dispatch_not still emits CX (the ctrl is the ancilla qubit).
//
// This test is the canonical "IR shows" verification required by M24.

static void test_ir_routing_when_dispatch_not() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qbool outer_flag(0.5);
    sturm::qbool inner_flag(0.5);

    // Target register: 1 superposed bit at physical qubit 10.
    sturm::qint_base target;
    target.width      = 1u;
    target.super_mask = 1u;
    target.qubits[0]  = 10u;

    // ── (a) X outside WHEN ────────────────────────────────────────────────────
    size_t before_a = sc.ir().size();
    sturm::when_dispatch_not(target, *sturm_get_thread_context());
    size_t after_a = sc.ir().size();

    assert(after_a == before_a + 1u && "outside WHEN: must emit exactly 1 gate");
    assert(sc.ir().at(before_a).kind == STURM_GATE_X &&
           "outside WHEN: gate must be X (uncontrolled)");
    assert(count_cx(sc.ir()) == 0u && "outside WHEN: no CX expected");

    // ── (b) CX inside single WHEN ─────────────────────────────────────────────
    size_t before_b = sc.ir().size();
    WHEN(outer_flag) {
        sturm::when_dispatch_not(target, *sturm_get_thread_context());
    }
    size_t after_b = sc.ir().size();

    assert(after_b > before_b && "inside WHEN: must emit gates");
    // Exactly 1 CX emitted (for the single target bit).
    size_t cx_b = 0;
    for (size_t i = before_b; i < after_b; ++i) {
        if (sc.ir().at(i).kind == STURM_GATE_CX) ++cx_b;
    }
    assert(cx_b == 1u && "inside single WHEN: exactly 1 CX expected from when_dispatch_not");

    // ── (c) CX with ancilla control inside nested WHEN ────────────────────────
    // The AND-fold creates an ancilla; active_control() inside the nested body
    // points to that ancilla.  when_dispatch_not must use it as the control,
    // emitting CX(ancilla, target) — still exactly 1 CX per target bit.
    // (CCX gates are for the AND-fold itself, not for the NOT operation.)
    sturm::QubitPool::instance().reset_for_testing();  // re-use pool for outer/inner
    sturm::qbool outer2(0.5);
    sturm::qbool inner2(0.5);

    size_t before_c = sc.ir().size();
    WHEN(outer2) {
        WHEN(inner2) {
            // Inside nested WHEN: active_control() → ancilla qubit.
            sturm::qbool* anc_ctrl = sturm::WhenGuard::active_control();
            assert(anc_ctrl != nullptr && anc_ctrl != &outer2 && anc_ctrl != &inner2 &&
                   "inside nested WHEN, active_control must be ancilla");
            // when_dispatch_not routes through ancilla control → CX(ancilla, target).
            sturm::when_dispatch_not(target, *sturm_get_thread_context());
        }
    }
    size_t after_c = sc.ir().size();

    // Count CX gates emitted in the nested WHEN section.
    size_t cx_c = 0;
    for (size_t i = before_c; i < after_c; ++i) {
        if (sc.ir().at(i).kind == STURM_GATE_CX) ++cx_c;
    }
    // Exactly 1 CX from the when_dispatch_not call inside the nested body.
    assert(cx_c == 1u &&
           "inside nested WHEN: when_dispatch_not must emit exactly 1 CX (ancilla as ctrl)");

    std::printf("PASS test_ir_routing_when_dispatch_not "
                "(X outside=%zu, CX single=%zu, CX nested=%zu)\n",
                count_x(sc.ir()), cx_b, cx_c);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_active_control_null_outside_when();
    test_active_control_set_inside_super_when();
    test_active_control_null_inside_classical_when();
    test_op_inside_when_uses_controlled_variant();
    test_op_outside_when_uses_uncontrolled_variant();
    test_nested_when_single_ancilla_control();
    test_nested_when_emits_ccx_for_ancilla();
    test_ir_routing_when_dispatch_not();

    std::printf("All M24 WHEN dispatch tests passed.\n");
    return 0;
}
