// test_dispatch_mixed.cpp — M16: Layer A entry — mixed and all-quantum paths (TDD).
//
// Tests (as per the issue / implementation plan):
//   1. CX with classical ctrl=0 does nothing (counter unchanged).
//   2. CX with classical ctrl=1 emits X on the target (counter +1).
//   3. CCX with one classical-1 and one quantum control emits CX (counter +1).
//   4. Fully-quantum CCX emits CCX directly (counter +1).
//
// Harness: plain assert + printf (no gtest dependency).
//
// Infrastructure:
//   - BackendContext in COUNT_ONLY mode (no Orkan needed).
//   - dispatch_gate_mixed() and dispatch_gate_quantum() are the entry points.
//   - GateOperand carries either a classical value or a quantum physical qubit index.

#include "sturm/dispatch/dispatch_gate.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── Helpers ───────────────────────────────────────────────────────────────────

static sturm_backend_context_t* make_context() {
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_COUNT_ONLY, 17u);
    assert(ctx && "sturm_backend_create failed");
    sturm_set_thread_context(ctx);
    return ctx;
}

static void destroy_context(sturm_backend_context_t* ctx) {
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Test 1: CX with classical ctrl=0 does nothing ────────────────────────────
//
// Mixed CX: ctrl is classical-0, target is quantum (qubit 3).
// Expectation: short-circuit, return without calling Layer B.
// Counter must remain unchanged.

static void test_cx_classical_ctrl0_noop() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    // ctrl=classical-0, target=quantum(qubit 3)
    sturm::GateOperand ops[2];
    ops[0] = sturm::GateOperand::classical(0);  // ctrl=0
    ops[1] = sturm::GateOperand::quantum(3);    // target at qubit index 3

    sturm::dispatch_gate_mixed(STURM_GATE_CX, ops, 2, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before && "CX(ctrl=0, quantum tgt): counter must not change");

    std::printf("  CX classical ctrl=0 → no-op (counter unchanged): PASS\n");
    destroy_context(ctx);
}

// ── Test 2: CX with classical ctrl=1 emits X on target ───────────────────────
//
// Mixed CX: ctrl is classical-1, target is quantum (qubit 5).
// The reduction table maps CX(ctrl=1, tgt) → X(tgt).
// Expectation: one gate call through Layer B (X on qubit 5).
// Counter increases by exactly 1.

static void test_cx_classical_ctrl1_emits_x() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    // ctrl=classical-1, target=quantum(qubit 5)
    sturm::GateOperand ops[2];
    ops[0] = sturm::GateOperand::classical(1);  // ctrl=1
    ops[1] = sturm::GateOperand::quantum(5);    // target at qubit index 5

    sturm::dispatch_gate_mixed(STURM_GATE_CX, ops, 2, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before + 1u &&
           "CX(ctrl=1, quantum tgt): should emit X on target (counter +1)");

    std::printf("  CX classical ctrl=1 → X on target (counter +1): PASS\n");
    destroy_context(ctx);
}

// ── Test 3: CCX with one classical-1 and one quantum control emits CX ────────
//
// Mixed CCX: ctrl0=classical-1, ctrl1=quantum(qubit 2), target=quantum(qubit 7).
// Reduction: CCX(ctrl0=1, ctrl1=q, tgt=q) → CX(ctrl1, tgt).
// Counter increases by 1 (one CX gate emitted).

static void test_ccx_one_classical1_one_quantum_emits_cx() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    // ctrl0=classical-1, ctrl1=quantum(qubit 2), tgt=quantum(qubit 7)
    sturm::GateOperand ops[3];
    ops[0] = sturm::GateOperand::classical(1);  // ctrl0=1
    ops[1] = sturm::GateOperand::quantum(2);    // ctrl1 at qubit 2
    ops[2] = sturm::GateOperand::quantum(7);    // tgt at qubit 7

    sturm::dispatch_gate_mixed(STURM_GATE_CCX, ops, 3, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before + 1u &&
           "CCX(ctrl0=1, ctrl1=q, tgt=q): should emit CX (counter +1)");

    std::printf("  CCX (ctrl0=1-classical, ctrl1=quantum) → CX (counter +1): PASS\n");
    destroy_context(ctx);
}

// ── Test 4: Fully-quantum CCX emits CCX directly ─────────────────────────────
//
// All-quantum CCX: ctrl0=quantum(qubit 0), ctrl1=quantum(qubit 1), tgt=quantum(qubit 2).
// No reduction — forward directly to Layer B as CCX.
// Counter increases by 1 (one CCX gate emitted).

static void test_ccx_all_quantum_emits_ccx() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    // All quantum operands
    sturm::GateOperand ops[3];
    ops[0] = sturm::GateOperand::quantum(0);
    ops[1] = sturm::GateOperand::quantum(1);
    ops[2] = sturm::GateOperand::quantum(2);

    sturm::dispatch_gate_quantum(STURM_GATE_CCX, ops, 3, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before + 1u &&
           "Fully-quantum CCX: should emit CCX directly (counter +1)");

    std::printf("  Fully-quantum CCX → emits CCX (counter +1): PASS\n");
    destroy_context(ctx);
}

// ── Test 5: CCX with classical ctrl0=0 → no-op ───────────────────────────────
//
// Mixed CCX: ctrl0=classical-0, ctrl1=quantum, tgt=quantum.
// Classical-0 control always short-circuits: gate cannot fire.
// Counter must remain unchanged.

static void test_ccx_classical_ctrl0_zero_noop() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    sturm::GateOperand ops[3];
    ops[0] = sturm::GateOperand::classical(0);  // ctrl0=0 → short circuit
    ops[1] = sturm::GateOperand::quantum(4);
    ops[2] = sturm::GateOperand::quantum(6);

    sturm::dispatch_gate_mixed(STURM_GATE_CCX, ops, 3, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before && "CCX(ctrl0=0, q, q): counter must not change");

    std::printf("  CCX classical ctrl0=0 → no-op (counter unchanged): PASS\n");
    destroy_context(ctx);
}

// ── Test 6: All-quantum single-qubit gate (X) emits directly ─────────────────
//
// All-quantum X on qubit 4 → emits X through Layer B.
// Counter increases by 1.

static void test_quantum_x_emits_x() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    sturm::GateOperand ops[1];
    ops[0] = sturm::GateOperand::quantum(4);

    sturm::dispatch_gate_quantum(STURM_GATE_X, ops, 1, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before + 1u &&
           "Fully-quantum X: should emit X (counter +1)");

    std::printf("  Fully-quantum X → emits X (counter +1): PASS\n");
    destroy_context(ctx);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M16 dispatch_gate mixed and all-quantum path tests:\n");
    test_cx_classical_ctrl0_noop();
    test_cx_classical_ctrl1_emits_x();
    test_ccx_one_classical1_one_quantum_emits_cx();
    test_ccx_all_quantum_emits_ccx();
    test_ccx_classical_ctrl0_zero_noop();
    test_quantum_x_emits_x();
    std::printf("All M16 mixed/all-quantum dispatch tests passed.\n");
    return 0;
}
