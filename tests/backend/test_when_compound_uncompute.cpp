// test_when_compound_uncompute.cpp -- Tests for WHEN compound boolean
// expression uncomputation order.
//
// Verifies that WHEN((c | d) & e) correctly defers uncomputation of
// intermediate temporaries so they are uncomputed in reverse order after the
// WHEN body and guard have unwound.
//
// Uses the ScopedAppendCtx fixture (same pattern as test_qbool_uncompute.cpp).

#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── Fixture ───────────────────────────────────────────────────────────────────

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

// Helper: make a non-owning superposed qbool with a fixed qubit index.
static sturm::qbool make_qbool_at(int idx) {
    sturm::qbool q = sturm::qbool::make_non_owning(idx);
    q.super_mask = 1ULL;
    return q;
}

// ── Test A: WHEN((c | d) & e) ─────────────────────────────────────────────────
// Compound boolean expression in WHEN.
// Forward: OR(c,d) emits 3 gates, AND(t1,e) emits 1 gate, body runs.
// Uncompute: AND uncompute (1 gate), then OR uncompute (3 gates) via WhenCapture.
// The key invariant: all uncompute gates reference valid (not yet released) qubits.

static void test_compound_or_and() {
    ScopedAppendCtx sc;

    sturm::qbool c = make_qbool_at(10);
    sturm::qbool d = make_qbool_at(11);
    sturm::qbool e = make_qbool_at(12);

    std::size_t body_gate_count = 0;

    WHEN((c | d) & e) {
        // Record that we entered the WHEN body.
        // Emit a marker gate so we can identify the body in the IR.
        // Use an X gate on qubit 12 (e) as a marker.
        const uint32_t marker_q = 12u;
        execute_gate(*sc.ctx, STURM_GATE_X, &marker_q, 1u, 0.0);
        body_gate_count = 1;
    }

    // The body should have run (all qubits are superposed so the guard
    // evaluates as superposed = run_=true).
    assert(body_gate_count == 1 && "WHEN body must have executed");

    // Verify the IR contains the expected gate sequence.
    // Expected structure:
    //   [0..2]  OR forward (CX, CX, CCX)
    //   [3]     AND forward (CCX)
    //   [4]     Body marker (X on q12)
    //   [5]     AND uncompute (CCX) -- from _when_val_ destructor
    //   [6..8]  OR uncompute (CCX, CX, CX) -- from WhenCapture LIFO
    const std::size_t total = sc.ir().size();
    assert(total >= 9u && "expected at least 9 gates: 3 OR fwd + 1 AND fwd + 1 body + 1 AND unc + 3 OR unc");

    // Verify forward OR emission (gates 0-2): CX, CX, CCX
    assert(sc.ir().at(0).kind == STURM_GATE_CX  && "gate[0] must be CX (OR fwd)");
    assert(sc.ir().at(1).kind == STURM_GATE_CX  && "gate[1] must be CX (OR fwd)");
    assert(sc.ir().at(2).kind == STURM_GATE_CCX && "gate[2] must be CCX (OR fwd)");

    // Verify forward AND emission (gate 3): CCX
    assert(sc.ir().at(3).kind == STURM_GATE_CCX && "gate[3] must be CCX (AND fwd)");

    // Verify body marker (gate 4): X on qubit 12
    assert(sc.ir().at(4).kind == STURM_GATE_X && "gate[4] must be X (body marker)");
    assert(sc.ir().at(4).qubits[0] == 12u && "body marker must be on qubit 12");

    // Verify AND uncompute (gate 5): CCX
    assert(sc.ir().at(5).kind == STURM_GATE_CCX && "gate[5] must be CCX (AND uncompute)");

    // Verify OR uncompute (gates 6-8): CCX, CX, CX (reverse of forward)
    assert(sc.ir().at(6).kind == STURM_GATE_CCX && "gate[6] must be CCX (OR uncompute)");
    assert(sc.ir().at(7).kind == STURM_GATE_CX  && "gate[7] must be CX (OR uncompute)");
    assert(sc.ir().at(8).kind == STURM_GATE_CX  && "gate[8] must be CX (OR uncompute)");

    // Verify OR uncompute references the original source qubits (10, 11).
    // The CCX uncompute (gate 6) must have controls on qubits 10 and 11.
    assert(sc.ir().at(6).qubits[0] == 10u && "OR uncompute CCX ctrl0 must be c=10");
    assert(sc.ir().at(6).qubits[1] == 11u && "OR uncompute CCX ctrl1 must be d=11");

    std::printf("  test_compound_or_and: PASS\n");
}

// ── Test B: Qubit validity ────────────────────────────────────────────────────
// Verify that uncompute gates in the captured intermediates reference valid
// (still-allocated) ancilla qubits at uncompute time.
// We check this indirectly: if the OR intermediate's ancilla qubit had been
// released before the AND uncompute ran, the AND uncompute CCX would reference
// a freed qubit.  The WhenCapture defers release, so the ancilla is still live.

static void test_qubit_validity() {
    ScopedAppendCtx sc;

    sturm::qbool a = make_qbool_at(20);
    sturm::qbool b = make_qbool_at(21);
    sturm::qbool c = make_qbool_at(22);

    // Track the OR intermediate's ancilla qubit.
    int or_ancilla = -1;
    int and_ancilla = -1;

    WHEN((a | b) & c) {
        // The AND result (_when_val_) has its own ancilla qubit.
        // The OR intermediate was captured by WhenCapture.
        // Both ancillas must be valid right now.

        // We can inspect the IR to find the ancilla qubits.
        // OR forward emits CX(20,anc), CX(21,anc), CCX(20,21,anc) -- anc = gate[0].qubits[1]
        // AND forward emits CCX(or_anc, 22, and_anc) -- and_anc = gate[3].qubits[2]
        or_ancilla = static_cast<int>(sc.ir().at(0).qubits[1]);
        and_ancilla = static_cast<int>(sc.ir().at(3).qubits[2]);
        assert(or_ancilla >= 0 && "OR ancilla must be allocated");
        assert(and_ancilla >= 0 && "AND ancilla must be allocated");
    }

    // After WHEN: both ancillas should have been released (uncomputed then freed).
    // The AND uncompute CCX (gate after body) must reference the OR ancilla as
    // a control -- this is only valid if the OR ancilla wasn't freed prematurely.
    assert(or_ancilla >= 0 && and_ancilla >= 0 && "both ancillas must have been valid");

    // Verify AND uncompute references the OR ancilla.
    // Gate layout: [0-2] OR fwd, [3] AND fwd, [4] AND uncompute, [5-7] OR uncompute
    // AND uncompute gate should have the or_ancilla as one of its controls.
    // (The AND was: CCX(or_anc, c=22, and_anc))
    const sturm::GateRecord& and_unc = sc.ir().at(4);
    assert(and_unc.kind == STURM_GATE_CCX && "AND uncompute must be CCX");
    assert(static_cast<int>(and_unc.qubits[0]) == or_ancilla
           && "AND uncompute CCX ctrl0 must be OR ancilla (still valid)");

    std::printf("  test_qubit_validity: PASS\n");
}

// ── Test C: Simple WHEN(flag) regression ──────────────────────────────────────
// WHEN with a simple lvalue qbool should work exactly as before -- no capture
// overhead, same gate output.

static void test_simple_when_regression() {
    ScopedAppendCtx sc;

    sturm::qbool flag = make_qbool_at(30);

    bool body_ran = false;

    WHEN(flag) {
        body_ran = true;
        // Emit a marker gate.
        const uint32_t q = 30u;
        execute_gate(*sc.ctx, STURM_GATE_X, &q, 1u, 0.0);
    }

    assert(body_ran && "simple WHEN body must execute for superposed flag");

    // Only the body marker gate should be in the IR.
    // No forward/uncompute gates from WhenCapture (no intermediates captured).
    assert(sc.ir().size() == 1u && "simple WHEN: only body marker gate expected");
    assert(sc.ir().at(0).kind == STURM_GATE_X && "body marker must be X");
    assert(sc.ir().at(0).qubits[0] == 30u && "body marker must be on qubit 30");

    std::printf("  test_simple_when_regression: PASS\n");
}

// ── Test D: WHEN(a | b) — single intermediate ────────────────────────────────
// A single OR expression: the OR result itself is materialized as _when_val_,
// not captured.  No WhenCapture intermediates should exist.

static void test_single_or() {
    ScopedAppendCtx sc;

    sturm::qbool a = make_qbool_at(40);
    sturm::qbool b = make_qbool_at(41);

    bool body_ran = false;

    WHEN(a | b) {
        body_ran = true;
        const uint32_t q = 40u;
        execute_gate(*sc.ctx, STURM_GATE_X, &q, 1u, 0.0);
    }

    assert(body_ran && "WHEN(a | b) body must execute");

    // Expected: 3 OR forward + 1 body marker + 3 OR uncompute = 7 gates.
    // The OR result is _when_val_, not a captured intermediate.
    const std::size_t total = sc.ir().size();
    assert(total == 7u && "WHEN(a | b): 3 fwd + 1 body + 3 unc = 7 gates");

    // Verify forward
    assert(sc.ir().at(0).kind == STURM_GATE_CX  && "OR fwd gate[0] must be CX");
    assert(sc.ir().at(1).kind == STURM_GATE_CX  && "OR fwd gate[1] must be CX");
    assert(sc.ir().at(2).kind == STURM_GATE_CCX && "OR fwd gate[2] must be CCX");

    // Verify body marker
    assert(sc.ir().at(3).kind == STURM_GATE_X && "body marker must be X");

    // Verify uncompute (reverse order)
    assert(sc.ir().at(4).kind == STURM_GATE_CCX && "OR unc gate[4] must be CCX");
    assert(sc.ir().at(5).kind == STURM_GATE_CX  && "OR unc gate[5] must be CX");
    assert(sc.ir().at(6).kind == STURM_GATE_CX  && "OR unc gate[6] must be CX");

    std::printf("  test_single_or: PASS\n");
}

// ── Test E: Multi-bit intermediate no qubit leak ──────────────────────────────
// Verifies that when a multi-bit qint_t<4> temporary (e.g. from a + b) is
// destroyed inside an active WhenCapture, ALL of its qubits are captured and
// subsequently released -- not just the first one.

static void test_multibit_intermediate_no_leak() {
    ScopedAppendCtx sc;

    // Reset pool so in_use starts at 0.
    sturm::QubitPool::instance().reset_for_testing();
    assert(sturm::QubitPool::instance().in_use() == 0);

    {
        // Allocate a 4-bit qint to simulate a multi-bit intermediate.
        sturm::qint_t<4> temp;
        temp.value = 7;
        temp.owning_ = true;
        for (int i = 0; i < 4; ++i) {
            temp.qubits[i] = sturm::QubitPool::instance().allocate();
            temp.super_mask |= (1ULL << i);
        }
        // Stamp an uncompute op so the capture path triggers.
        temp.uncompute_ = sturm::uncompute_op::make_add_const(3);

        assert(sturm::QubitPool::instance().in_use() == 4
               && "4 qubits must be in use after allocation");

        // Install a WhenCapture and let temp be destroyed inside it.
        // The capture should intercept all 4 qubits.
        {
            sturm::detail::WhenCapture cap;
            // temp's destructor fires at the end of THIS scope (temp was declared
            // in the outer scope, but we need it destroyed while cap is active).
            // So move it into a local:
            sturm::qint_t<4> doomed = std::move(temp);
            // doomed destructs here -> captured by cap.
        }
        // cap destructs here -> uncomputes + releases all 4 qubits.
    }

    const int pool_final = sturm::QubitPool::instance().in_use();
    assert(pool_final == 0
           && "all 4 pool qubits must be released (no multi-bit leak)");

    // Also verify uncompute gates were emitted (ADD_CONST -> sub_const).
    // sub_const emits one STURM_GATE_X per superposed bit = 4 gates.
    assert(sc.ir().size() == 4u
           && "uncompute of 4-bit ADD_CONST must emit 4 gates");

    std::printf("  test_multibit_intermediate_no_leak: PASS\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("WHEN compound uncompute tests:\n");

    test_compound_or_and();
    test_qubit_validity();
    test_simple_when_regression();
    test_single_or();
    test_multibit_intermediate_no_leak();

    std::printf("All WHEN compound uncompute tests passed.\n");
    return 0;
}
