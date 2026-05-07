// test_dispatch_all_classical.cpp — M15: Layer A entry — all-classical path (TDD).
//
// Tests (as per the issue / implementation plan):
//   1. classical X flips bit                — FLIP effect mutates classical value.
//   2. classical CX behaves as xor          — FLIP on 2-qubit with classical ctrl=1.
//   3. classical SWAP exchanges values      — FLIP on 2-qubit SWAP.
//   4. classical H triggers promotion       — BRANCH effect: spy on promotion via
//                                             the gate counter (promotion emits X
//                                             gates through Layer B for bits=1).
//
// Harness: plain assert + printf (no gtest dependency).
//
// Infrastructure:
//   - BackendContext in COUNT_ONLY mode (no Orkan needed).
//   - dispatch_gate() is the entry point under test.
//   - All operands have super_mask == 0 (all classical).

#include "sturm/dispatch/dispatch_gate.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a BackendContext in COUNT_ONLY mode, install it as the thread context.
// Returns a raw pointer; the caller owns the lifetime (use sturm_backend_destroy).
static sturm_backend_context_t* make_context() {
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx && "sturm_backend_create failed");
    sturm_set_thread_context(ctx);
    return ctx;
}

static void destroy_context(sturm_backend_context_t* ctx) {
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Test 1: classical X flips a bit ─────────────────────────────────────────
//
// Gate: X (arity=1, FLIP).
// Operand: classical bit with value 0 → after dispatch: value = 1.
// Gate counter: X on a classical operand must NOT call Layer B,
//               so counter stays 0.

static void test_classical_x_flips_bit() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    // Classical qint-like structure: super_mask=0, value=0, bit=0.
    sturm::ClassicalOperand op;
    op.value    = 0;
    op.bit_pos  = 0;

    sturm::dispatch_gate_classical(STURM_GATE_X, &op, 1, 0.0);

    // Value should have flipped to 1.
    assert(op.value == 1 && "X on classical 0 should produce 1");

    // Apply again: should flip back to 0.
    sturm::dispatch_gate_classical(STURM_GATE_X, &op, 1, 0.0);
    assert(op.value == 0 && "X on classical 1 should produce 0");

    // FLIP on classical must NOT increment the counter (Layer B not called).
    uint64_t after = sturm_gate_count(ctx);
    assert(after == before && "classical X must not call Layer B");

    std::printf("  classical X flips bit: PASS\n");
    destroy_context(ctx);
}

// ── Test 2: classical CX behaves as XOR ──────────────────────────────────────
//
// Gate: CX (arity=2, FLIP).
// Operands[0]=ctrl (classical, bit=0 of ctrl_val), operands[1]=target.
//
// CX:
//   ctrl=0 → target unchanged.
//   ctrl=1 → target flipped.

static void test_classical_cx_xor() {
    sturm_backend_context_t* ctx = make_context();

    // ctrl=1, target=0 → after: target=1
    {
        sturm::ClassicalOperand ops[2];
        ops[0].value = 1;  ops[0].bit_pos = 0;  // ctrl
        ops[1].value = 0;  ops[1].bit_pos = 0;  // target

        sturm::dispatch_gate_classical(STURM_GATE_CX, ops, 2, 0.0);

        assert(ops[0].value == 1 && "ctrl must not change");
        assert(ops[1].value == 1 && "CX(ctrl=1,tgt=0) → tgt=1");
    }

    // ctrl=1, target=1 → after: target=0
    {
        sturm::ClassicalOperand ops[2];
        ops[0].value = 1;  ops[0].bit_pos = 0;
        ops[1].value = 1;  ops[1].bit_pos = 0;

        sturm::dispatch_gate_classical(STURM_GATE_CX, ops, 2, 0.0);
        assert(ops[1].value == 0 && "CX(ctrl=1,tgt=1) → tgt=0");
    }

    // ctrl=0, target=1 → after: target=1 (no change)
    {
        sturm::ClassicalOperand ops[2];
        ops[0].value = 0;  ops[0].bit_pos = 0;
        ops[1].value = 1;  ops[1].bit_pos = 0;

        sturm::dispatch_gate_classical(STURM_GATE_CX, ops, 2, 0.0);
        assert(ops[1].value == 1 && "CX(ctrl=0,tgt=1) → tgt unchanged");
    }

    // Counter must remain 0 (no Layer B calls for all-classical FLIP).
    assert(sturm_gate_count(ctx) == 0u && "classical CX must not call Layer B");

    std::printf("  classical CX xor: PASS\n");
    destroy_context(ctx);
}

// ── Test 3: classical SWAP exchanges values ───────────────────────────────────
//
// Gate: SWAP (arity=2, FLIP).
// SWAP(a, b) → a' = b, b' = a.

static void test_classical_swap_exchanges() {
    sturm_backend_context_t* ctx = make_context();

    // SWAP(0, 1) → (1, 0)
    {
        sturm::ClassicalOperand ops[2];
        ops[0].value = 0;  ops[0].bit_pos = 0;
        ops[1].value = 1;  ops[1].bit_pos = 0;

        sturm::dispatch_gate_classical(STURM_GATE_SWAP, ops, 2, 0.0);
        assert(ops[0].value == 1 && "SWAP: first should become 1");
        assert(ops[1].value == 0 && "SWAP: second should become 0");
    }

    // SWAP(1, 0) → (0, 1)
    {
        sturm::ClassicalOperand ops[2];
        ops[0].value = 1;  ops[0].bit_pos = 0;
        ops[1].value = 0;  ops[1].bit_pos = 0;

        sturm::dispatch_gate_classical(STURM_GATE_SWAP, ops, 2, 0.0);
        assert(ops[0].value == 0 && "SWAP: first should become 0");
        assert(ops[1].value == 1 && "SWAP: second should become 1");
    }

    // SWAP(1, 1) → (1, 1) — same
    {
        sturm::ClassicalOperand ops[2];
        ops[0].value = 1;  ops[0].bit_pos = 0;
        ops[1].value = 1;  ops[1].bit_pos = 0;

        sturm::dispatch_gate_classical(STURM_GATE_SWAP, ops, 2, 0.0);
        assert(ops[0].value == 1 && "SWAP(1,1): first should remain 1");
        assert(ops[1].value == 1 && "SWAP(1,1): second should remain 1");
    }

    // Counter must remain 0.
    assert(sturm_gate_count(ctx) == 0u && "classical SWAP must not call Layer B");

    std::printf("  classical SWAP exchanges: PASS\n");
    destroy_context(ctx);
}

// ── Test 4: classical H triggers promotion (spy) ──────────────────────────────
//
// Gate: H (arity=1, BRANCH).
// A classical H on a bit with value=1 must:
//   a) promote the bit (set super_mask for that bit), AND
//   b) emit an X gate through Layer B to flip the promoted qubit from |0⟩ to |1⟩
//      (because the classical value was 1 before promotion).
//
// We spy on promotion via the gate counter:
//   - bit value 0: no X gate emitted → counter remains 0.
//   - bit value 1: one X gate emitted → counter = 1.
//
// After a BRANCH dispatch the operand's super_mask bit must be set.

static void test_classical_h_triggers_promotion() {
    // Sub-test A: classical H on bit=0.
    // Steps: allocate qubit (value=0 → no X), then emit H through Layer B.
    // Expected counter delta: 0 (no X) + 1 (H itself) = 1.
    {
        sturm_backend_context_t* ctx = make_context();
        uint64_t before = sturm_gate_count(ctx);

        sturm::ClassicalOperand op;
        op.value   = 0;
        op.bit_pos = 0;

        sturm::PromotionResult pr = sturm::dispatch_gate_branch_classical(STURM_GATE_H, &op, 1, 0.0);

        // The bit must be promoted (super_mask bit set in result).
        assert(pr.promoted_mask & (1u << op.bit_pos) &&
               "H on classical bit must set the super_mask bit");

        // No X emitted for bit=0, but the H gate itself is sent through Layer B.
        // Counter: 0 for X + 1 for H = 1 total.
        uint64_t after = sturm_gate_count(ctx);
        assert(after == before + 1u &&
               "H on classical-0 bit: 1 gate (H) through Layer B, no X");

        std::printf("  classical H on 0: promotion detected (H gate emitted, no X): PASS\n");
        destroy_context(ctx);
    }

    // Sub-test B: classical H on bit=1.
    // Steps: allocate qubit (value=1 → emit X), then emit H through Layer B.
    // Expected counter delta: 1 (X for init) + 1 (H itself) = 2.
    {
        sturm_backend_context_t* ctx = make_context();
        uint64_t before = sturm_gate_count(ctx);

        sturm::ClassicalOperand op;
        op.value   = 1;
        op.bit_pos = 0;

        sturm::PromotionResult pr = sturm::dispatch_gate_branch_classical(STURM_GATE_H, &op, 1, 0.0);

        // The bit must be promoted.
        assert(pr.promoted_mask & (1u << op.bit_pos) &&
               "H on classical-1 bit must set super_mask bit");

        // One X emitted for init + one H emitted for the gate itself.
        uint64_t after = sturm_gate_count(ctx);
        assert(after == before + 2u &&
               "H on classical-1 bit: X (init) + H (gate) = 2 gates through Layer B");

        std::printf("  classical H on 1: promotion detected (X + H emitted): PASS\n");
        destroy_context(ctx);
    }
}

// ── Test 5: classical NONE gates are skipped ──────────────────────────────────
//
// Gate: Z (arity=1, NONE).
// All-classical operand → skip, no mutation, no Layer B call.

static void test_classical_none_skipped() {
    sturm_backend_context_t* ctx = make_context();
    uint64_t before = sturm_gate_count(ctx);

    sturm::ClassicalOperand op;
    op.value   = 1;
    op.bit_pos = 0;
    int64_t original_value = op.value;

    sturm::dispatch_gate_classical(STURM_GATE_Z, &op, 1, 0.0);

    // Value must be unchanged.
    assert(op.value == original_value && "NONE gate must not mutate classical value");

    // No Layer B call.
    assert(sturm_gate_count(ctx) == before && "NONE gate must not call Layer B");

    std::printf("  classical Z (NONE) skipped: PASS\n");
    destroy_context(ctx);
}

// ── Test 6: classical CCX (Toffoli) — all-1 controls flip target ──────────────
//
// CCX: ctrl0=1, ctrl1=1, target=0 → target=1.

static void test_classical_ccx() {
    sturm_backend_context_t* ctx = make_context();

    // ctrl0=1, ctrl1=1, tgt=0 → tgt=1
    {
        sturm::ClassicalOperand ops[3];
        ops[0].value = 1;  ops[0].bit_pos = 0;  // ctrl0
        ops[1].value = 1;  ops[1].bit_pos = 0;  // ctrl1
        ops[2].value = 0;  ops[2].bit_pos = 0;  // target

        sturm::dispatch_gate_classical(STURM_GATE_CCX, ops, 3, 0.0);
        assert(ops[2].value == 1 && "CCX(1,1,0) → tgt=1");
    }

    // ctrl0=1, ctrl1=0, tgt=1 → tgt unchanged
    {
        sturm::ClassicalOperand ops[3];
        ops[0].value = 1;  ops[0].bit_pos = 0;
        ops[1].value = 0;  ops[1].bit_pos = 0;
        ops[2].value = 1;  ops[2].bit_pos = 0;

        sturm::dispatch_gate_classical(STURM_GATE_CCX, ops, 3, 0.0);
        assert(ops[2].value == 1 && "CCX(1,0,1) → tgt unchanged");
    }

    // Counter remains 0 (classical FLIP, no Layer B).
    assert(sturm_gate_count(ctx) == 0u && "classical CCX must not call Layer B");

    std::printf("  classical CCX: PASS\n");
    destroy_context(ctx);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M15 dispatch_gate all-classical path tests:\n");
    test_classical_x_flips_bit();
    test_classical_cx_xor();
    test_classical_swap_exchanges();
    test_classical_h_triggers_promotion();
    test_classical_none_skipped();
    test_classical_ccx();
    std::printf("All M15 all-classical dispatch tests passed.\n");
    return 0;
}
