// test_uncompute_run.cpp — M20: RAII uncompute runner (TDD — written before implementation).
//
// Tests:
//   1. NONE tag bypasses apply cleanly (no gates emitted, no crash).
//   2. Promotion-mask bits emit X gates before release.
//   3. Destruction order of multiple temps in a full expression is reverse of
//      construction order (verified via sequential gate-record stamps).
//
// Harness: plain assert + main (no gtest).

#include "sturm/uncompute/uncompute_run.hpp"
#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

// ── Fixture: scoped APPEND context ───────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::GateIR& ir() { return ctx->ir; }
};

// ── Test 1: NONE tag bypasses apply cleanly ───────────────────────────────────
//
// Run the runner with a NONE-tagged uncompute_op and an empty promotion_mask.
// No gates should be emitted and the pool should be untouched.

static void test_none_bypasses_cleanly() {
    ScopedAppendCtx sc;

    sturm::qint_base reg;
    reg.width          = 4;
    reg.value          = 5LL;
    reg.super_mask     = 0xFu;
    reg.promotion_mask = 0u;         // nothing to flip back
    reg.qubits[0] = 10u;
    reg.qubits[1] = 11u;
    reg.qubits[2] = 12u;
    reg.qubits[3] = 13u;

    // Manually register qubits as "in use" so release can return them.
    // We allocate from the context pool so we get valid indices.
    // (For this test we just need the runner to call release without exploding.)
    // We use dummy indices; the pool isn't checked for exact index values here.

    sturm::uncompute_op none_op;   // default NONE
    sturm::run_uncompute(none_op, reg, *sc.ctx);

    // No gates should have been emitted (NONE + no promotion bits).
    assert(sc.ir().size() == 0u && "NONE + empty promotion_mask must emit zero gates");

    std::printf("  test_none_bypasses_cleanly: PASS\n");
}

// ── Test 2: Promotion-mask X gates emitted before release ────────────────────
//
// Give a register with promotion_mask = 0b0101 (bits 0 and 2 need flipping).
// The runner must emit exactly 2 X gates on qubits[0] and qubits[2], in
// order, before releasing.

static void test_promotion_mask_x_gates() {
    ScopedAppendCtx sc;

    sturm::qint_base reg;
    reg.width          = 4;
    reg.value          = 0LL;
    reg.super_mask     = 0xFu;
    reg.promotion_mask = 0b0101u;   // bits 0 and 2 were |1> at promotion time
    reg.qubits[0] = 20u;
    reg.qubits[1] = 21u;
    reg.qubits[2] = 22u;
    reg.qubits[3] = 23u;

    sturm::uncompute_op none_op;   // NONE so apply() does nothing
    sturm::run_uncompute(none_op, reg, *sc.ctx);

    // Expect exactly 2 X gates: one on qubit 20, one on qubit 22.
    assert(sc.ir().size() == 2u && "promotion_mask 0b0101 must emit exactly 2 X gates");

    const sturm::GateRecord& r0 = sc.ir().at(0);
    assert(r0.kind == STURM_GATE_X && "first gate must be X");
    assert(r0.qubits[0] == 20u    && "first X must target qubit 20 (bit 0)");
    assert(r0.n == 1u);

    const sturm::GateRecord& r1 = sc.ir().at(1);
    assert(r1.kind == STURM_GATE_X && "second gate must be X");
    assert(r1.qubits[0] == 22u    && "second X must target qubit 22 (bit 2)");
    assert(r1.n == 1u);

    std::printf("  test_promotion_mask_x_gates: PASS\n");
}

// ── Test 3: Destruction order — reverse of construction ──────────────────────
//
// We simulate "three anonymous temporaries" by calling run_uncompute for three
// registers where each has a distinguishable ADD_CONST constant.  The
// apply() of ADD_CONST calls sub_const which emits STURM_GATE_X (with param=c
// per the qint_base stub).  We construct them in order A, B, C and destroy in
// order C, B, A (i.e. call run_uncompute in reverse). We then check the IR
// reflects that order.
//
// The "reverse of construction" property is guaranteed by C++ full-expression
// destruction rules — this test validates the runner itself doesn't reorder
// and that the protocol (apply → X-per-promotion-bit → release) works across
// multiple temporaries.

static void test_destruction_order_reverse_of_construction() {
    ScopedAppendCtx sc;

    // Three registers, each with 1 quantum bit and different qubit indices.
    auto make_reg = [](uint32_t qubit_idx) {
        sturm::qint_base r;
        r.width          = 1;
        r.value          = 0LL;
        r.super_mask     = 0x1u;
        r.promotion_mask = 0u;
        r.qubits[0]      = qubit_idx;
        return r;
    };

    sturm::qint_base regA = make_reg(30u);  // "constructed first"
    sturm::qint_base regB = make_reg(31u);
    sturm::qint_base regC = make_reg(32u);  // "constructed last"

    // Each has ADD_CONST with a unique constant to produce identifiable gates.
    // add_const emits STURM_GATE_H per the stub; sub_const emits STURM_GATE_X.
    // ADD_CONST.apply -> sub_const -> emits X on the qubit.
    auto opA = sturm::uncompute_op::make_add_const(100LL);
    auto opB = sturm::uncompute_op::make_add_const(200LL);
    auto opC = sturm::uncompute_op::make_add_const(300LL);

    // Destroy in reverse order: C first, then B, then A.
    sturm::run_uncompute(opC, regC, *sc.ctx);
    sturm::run_uncompute(opB, regB, *sc.ctx);
    sturm::run_uncompute(opA, regA, *sc.ctx);

    // Each run_uncompute(ADD_CONST) emits exactly 1 X gate (sub_const on 1 quantum bit).
    // So we expect 3 X gates total (no promotion_mask bits set on any register).
    assert(sc.ir().size() == 3u && "three single-bit ADD_CONST runs must emit 3 X gates");

    // Gate 0: from regC (qubit 32)
    const sturm::GateRecord& g0 = sc.ir().at(0);
    assert(g0.kind == STURM_GATE_X);
    assert(g0.qubits[0] == 32u && "first destroyed (C) must emit on qubit 32");

    // Gate 1: from regB (qubit 31)
    const sturm::GateRecord& g1 = sc.ir().at(1);
    assert(g1.kind == STURM_GATE_X);
    assert(g1.qubits[0] == 31u && "second destroyed (B) must emit on qubit 31");

    // Gate 2: from regA (qubit 30)
    const sturm::GateRecord& g2 = sc.ir().at(2);
    assert(g2.kind == STURM_GATE_X);
    assert(g2.qubits[0] == 30u && "third destroyed (A) must emit on qubit 30");

    std::printf("  test_destruction_order_reverse_of_construction: PASS\n");
}

// ── Test 4: apply runs before promotion-mask X gates ─────────────────────────
//
// When both an uncompute_op (ADD_CONST → sub_const → X) and a promotion_mask
// bit are present, the apply X must come first in the IR, then the
// promotion-mask X.

static void test_apply_before_promotion_x() {
    ScopedAppendCtx sc;

    sturm::qint_base reg;
    reg.width          = 2;
    reg.value          = 0LL;
    reg.super_mask     = 0x3u;
    reg.promotion_mask = 0x2u;   // bit 1 needs flipping (qubit 41)
    reg.qubits[0]      = 40u;
    reg.qubits[1]      = 41u;

    // ADD_CONST on 2-bit register: sub_const emits X on qubit 40 and qubit 41.
    auto op = sturm::uncompute_op::make_add_const(7LL);
    sturm::run_uncompute(op, reg, *sc.ctx);

    // Expected IR (in order):
    //   [0] X(40)  — from sub_const (apply), bit 0
    //   [1] X(41)  — from sub_const (apply), bit 1
    //   [2] X(41)  — from promotion_mask bit 1
    assert(sc.ir().size() == 3u && "apply(2 bits) + promotion_mask(1 bit) must emit 3 gates");

    assert(sc.ir().at(0).kind == STURM_GATE_X);
    assert(sc.ir().at(0).qubits[0] == 40u && "apply X on qubit 40 first");

    assert(sc.ir().at(1).kind == STURM_GATE_X);
    assert(sc.ir().at(1).qubits[0] == 41u && "apply X on qubit 41 second");

    assert(sc.ir().at(2).kind == STURM_GATE_X);
    assert(sc.ir().at(2).qubits[0] == 41u && "promotion_mask X on qubit 41 last");

    std::printf("  test_apply_before_promotion_x: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M20 uncompute_run tests:\n");
    test_none_bypasses_cleanly();
    test_promotion_mask_x_gates();
    test_destruction_order_reverse_of_construction();
    test_apply_before_promotion_x();
    std::printf("All M20 uncompute_run tests passed.\n");
    return 0;
}
