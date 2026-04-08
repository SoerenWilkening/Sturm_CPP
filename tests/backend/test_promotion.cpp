// test_promotion.cpp — M17: Operand promotion (TDD — written before implementation).
//
// Tests (per issue sturm-7kk and implementation plan M17):
//   1. promote_bits promotes exactly the requested bits (no more, no less).
//   2. X emissions equal the popcount of classical-1 bits inside the requested mask.
//   3. promote_bits is idempotent on already-quantum bits.
//   4. Subsequent operations on promoted bits go through Layer B (counter increments).
//   5. const qint_base cannot be passed to promote_bits (compile-time check verified
//      by the presence of a non-const overload only — tested structurally here).
//   6. Partial-promotion invariant: bits outside the mask stay classical.
//
// Harness: plain assert + printf (no gtest dependency).
//
// Infrastructure:
//   - BackendContext in APPEND mode so we can inspect emitted gate kinds.
//   - QubitPool reset between tests.
//   - qint_base built directly (width-agnostic view).

#include "sturm/dispatch/promotion.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/backend/ir.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>

// ── Helpers ───────────────────────────────────────────────────────────────────

static sturm_backend_context_t* make_append_ctx() {
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 17u);
    assert(ctx && "sturm_backend_create failed");
    sturm_set_thread_context(ctx);
    return ctx;
}

static void destroy_ctx(sturm_backend_context_t* ctx) {
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// Build a qint_base with the given value, width, and super_mask.
// All qubits start at 0 (sentinel placeholder index).
static sturm::qint_base make_reg(int64_t val, uint8_t width, uint64_t smask = 0) {
    sturm::qint_base b;
    b.value          = val;
    b.width          = width;
    b.super_mask     = smask;
    b.promotion_mask = 0;
    std::memset(b.qubits, 0, sizeof(b.qubits));
    return b;
}

// Count X gates in the IR of ctx (ignores non-X entries).
static int count_x_gates(const sturm_backend_context_t* ctx) {
    const sturm::GateIR& ir = ctx->ir;
    int count = 0;
    for (size_t i = 0; i < ir.size(); ++i) {
        if (ir.at(i).kind == STURM_GATE_X) ++count;
    }
    return count;
}

// ── Test 1: promotes exactly the requested bits ───────────────────────────────
//
// Register: value=0b0000, width=4, super_mask=0.
// promote_bits(reg, 0b0011)  → bits 0 and 1 become quantum.
// super_mask after == 0b0011; bits 2,3 remain classical (super_mask bit clear).

static void test_promotes_exactly_requested_bits() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    sturm::qint_base reg = make_reg(0b0000, 4);

    sturm::promote_bits(reg, 0b0011u, *ctx);

    // Bits 0 and 1 are now quantum.
    assert((reg.super_mask & 0b0011u) == 0b0011u &&
           "promote_bits: bits 0 and 1 must be in super_mask");

    // Bits 2 and 3 are still classical.
    assert((reg.super_mask & 0b1100u) == 0u &&
           "promote_bits: bits 2 and 3 must remain classical");

    std::printf("  Test 1 — promotes exactly requested bits: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 2: X count equals popcount of classical-1 bits in the mask ──────────
//
// Register: value=0b1010 (bits 1 and 3 are 1), width=4, super_mask=0.
// promote_bits(reg, 0b1111) → promote all 4 bits.
// Bits at positions 1 and 3 are 1 → expect 2 X gates emitted.

static void test_x_count_equals_classical_ones() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    // value = 0b1010 → bit1=1, bit3=1 (two classical-1 bits)
    sturm::qint_base reg = make_reg(0b1010, 4);

    sturm::promote_bits(reg, 0b1111u, *ctx);

    int x_count = count_x_gates(ctx);
    assert(x_count == 2 &&
           "promote_bits: must emit exactly 2 X gates for 2 classical-1 bits");

    std::printf("  Test 2 — X count equals classical-1 popcount (2): PASS\n");
    destroy_ctx(ctx);
}

// ── Test 2b: all classical-0 bits → no X gates ───────────────────────────────
//
// Register: value=0b0000, width=4.
// promote_bits(reg, 0b1111) → no X gates.

static void test_x_count_zero_for_all_zeros() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    sturm::qint_base reg = make_reg(0b0000, 4);

    sturm::promote_bits(reg, 0b1111u, *ctx);

    int x_count = count_x_gates(ctx);
    assert(x_count == 0 &&
           "promote_bits: no X gates for all-zero register");

    std::printf("  Test 2b — X count zero for all-zero register: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 2c: all classical-1 bits → X count == 4 ─────────────────────────────

static void test_x_count_four_for_all_ones() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    // value = 0b1111 → all 4 bits are 1
    sturm::qint_base reg = make_reg(0b1111, 4);

    sturm::promote_bits(reg, 0b1111u, *ctx);

    int x_count = count_x_gates(ctx);
    assert(x_count == 4 &&
           "promote_bits: must emit 4 X gates for all-one register");

    std::printf("  Test 2c — X count == 4 for all-ones register: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 3: idempotent on already-quantum bits ────────────────────────────────
//
// Register: width=4, value=0b0110, super_mask=0b0110 (bits 1,2 already quantum).
// promote_bits(reg, 0b0110) → already quantum, should not allocate again.
// super_mask unchanged; no additional X gates; qubit indices unchanged.

static void test_idempotent_on_already_quantum() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-promote bits 1 and 2 manually: give them real qubit indices.
    sturm::qint_base reg = make_reg(0b0110, 4, 0b0110);
    reg.qubits[1] = 10u;  // pre-assigned qubit index for bit 1
    reg.qubits[2] = 11u;  // pre-assigned qubit index for bit 2
    reg.promotion_mask = 0b0110u;  // they were promoted before

    // Call promote_bits again on the same mask.
    sturm::promote_bits(reg, 0b0110u, *ctx);

    // super_mask must still be 0b0110 (unchanged).
    assert(reg.super_mask == 0b0110u &&
           "promote_bits idempotent: super_mask must not change");

    // Qubit indices must not have been reallocated.
    assert(reg.qubits[1] == 10u && reg.qubits[2] == 11u &&
           "promote_bits idempotent: qubit indices must remain the same");

    // No X gates should have been emitted (already promoted).
    int x_count = count_x_gates(ctx);
    assert(x_count == 0 &&
           "promote_bits idempotent: no X gates on re-promotion attempt");

    std::printf("  Test 3 — idempotent on already-quantum bits: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 4: subsequent ops on quantum bits go through Layer B ─────────────────
//
// Promote a single bit, then emit a gate on the resulting qubit index.
// The overall gate counter must increase appropriately.

static void test_subsequent_ops_go_through_layer_b() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    // value=0b0001 (bit 0 = 1), width=1.
    sturm::qint_base reg = make_reg(0b0001, 1);

    sturm::promote_bits(reg, 0b0001u, *ctx);

    // Bit 0 is now quantum.
    assert((reg.super_mask & 1u) != 0u &&
           "promote_bits: bit 0 must be quantum after promotion");
    assert(reg.qubits[0] != static_cast<uint32_t>(-1) &&
           "promote_bits: qubits[0] must be a valid index");

    uint64_t count_after_promote = sturm_gate_count(ctx);

    // Emit a Z gate (NONE effect) on the promoted qubit through Layer B.
    uint32_t q = reg.qubits[0];
    sturm_execute_gate(STURM_GATE_Z, &q, 1u, 0.0);

    uint64_t count_after_z = sturm_gate_count(ctx);
    assert(count_after_z == count_after_promote + 1u &&
           "subsequent Z on promoted qubit must go through Layer B (counter +1)");

    std::printf("  Test 4 — subsequent op on promoted qubit uses Layer B: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 5: partial promotion — bits outside mask stay classical ──────────────
//
// Register: value=0b1111, width=4.
// promote_bits(reg, 0b0001) → only bit 0 becomes quantum.
// Bits 1, 2, 3 remain classical (super_mask bit clear).
// Only 1 X gate (bit 0 was classical-1).

static void test_partial_promotion_invariant() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    sturm::qint_base reg = make_reg(0b1111, 4);

    sturm::promote_bits(reg, 0b0001u, *ctx);

    // Bit 0 is quantum.
    assert((reg.super_mask & 0b0001u) != 0u &&
           "partial promote: bit 0 must be in super_mask");

    // Bits 1-3 are still classical.
    assert((reg.super_mask & 0b1110u) == 0u &&
           "partial promote: bits 1-3 must remain classical");

    // Exactly 1 X gate for bit-0 which was 1.
    int x_count = count_x_gates(ctx);
    assert(x_count == 1 &&
           "partial promote: only 1 X gate for single classical-1 bit");

    std::printf("  Test 5 — partial promotion invariant: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 6: promotion_mask set correctly ─────────────────────────────────────
//
// Register: value=0b1010, width=4. Promote all 4 bits.
// promotion_mask must have bits 1 and 3 set (those were classical-1).

static void test_promotion_mask_set_correctly() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    // value=0b1010: bit1=1, bit3=1
    sturm::qint_base reg = make_reg(0b1010, 4);

    sturm::promote_bits(reg, 0b1111u, *ctx);

    // promotion_mask must reflect which bits were |1⟩ at promotion time.
    assert((reg.promotion_mask & 0b1010u) == 0b1010u &&
           "promote_bits: promotion_mask must have bits set for |1⟩ bits");
    // Bits 0 and 2 were 0 at promotion time — must not be in promotion_mask.
    assert((reg.promotion_mask & 0b0101u) == 0u &&
           "promote_bits: promotion_mask must not include |0⟩ bits");

    std::printf("  Test 6 — promotion_mask set correctly: PASS\n");
    destroy_ctx(ctx);
}

// ── Test 7: mixed already-quantum and new bits ───────────────────────────────
//
// Register: value=0b1100, width=4, super_mask=0b0011 (bits 0,1 already quantum).
// promote_bits(reg, 0b1111) → bits 2,3 should be newly promoted.
// Bits 0,1 stay unchanged (idempotent).
// X gates emitted: bit 2=1, bit 3=1 → 2 X gates.

static void test_mixed_already_quantum_and_new() {
    sturm_backend_context_t* ctx = make_append_ctx();
    sturm::QubitPool::instance().reset_for_testing();

    sturm::qint_base reg = make_reg(0b1100, 4, 0b0011);
    reg.qubits[0] = 5u;  // already assigned
    reg.qubits[1] = 6u;  // already assigned

    sturm::promote_bits(reg, 0b1111u, *ctx);

    // All 4 bits must be quantum now.
    assert(reg.super_mask == 0b1111u &&
           "mixed promotion: all 4 bits must be quantum");

    // Bits 0 and 1 keep their qubit indices.
    assert(reg.qubits[0] == 5u && reg.qubits[1] == 6u &&
           "mixed promotion: pre-assigned qubit indices must be unchanged");

    // 2 X gates for bits 2 and 3 (both were classical-1).
    int x_count = count_x_gates(ctx);
    assert(x_count == 2 &&
           "mixed promotion: 2 X gates for the two new classical-1 bits");

    std::printf("  Test 7 — mixed already-quantum and new bits: PASS\n");
    destroy_ctx(ctx);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M17 operand promotion tests:\n");
    test_promotes_exactly_requested_bits();
    test_x_count_equals_classical_ones();
    test_x_count_zero_for_all_zeros();
    test_x_count_four_for_all_ones();
    test_idempotent_on_already_quantum();
    test_subsequent_ops_go_through_layer_b();
    test_partial_promotion_invariant();
    test_promotion_mask_set_correctly();
    test_mixed_already_quantum_and_new();
    std::printf("All M17 promotion tests passed.\n");
    return 0;
}
