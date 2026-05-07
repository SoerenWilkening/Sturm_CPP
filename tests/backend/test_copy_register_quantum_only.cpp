// test_copy_register_quantum_only.cpp — M12: copy_register only copies quantum bits.
//
// Verifies that copy_register in qint_arith_backend.hpp only allocates fresh
// qubits for source bits that have qubits (qubits[i] >= 0), leaving classical
// bits (qubits[i] == -1) unallocated in the result.
//
// Test cases:
//   1. Fully quantum source: all result qubits allocated (baseline).
//   2. Mixed source (some qubits, some classical): only quantum bits get
//      fresh qubits; classical bits remain -1.
//   3. Fully classical source: no result qubits allocated.
//   4. CNOT emission: only emitted for quantum bits (gate count check).
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── COUNT_ONLY context RAII wrapper ──────────────────────────────────────────

struct CountCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit CountCtx(uint32_t max_q = 128u) {
        ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~CountCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// Prevent double-release by clearing qubit indices and super_mask.
template <std::size_t N>
static void clear_q(sturm::qint_t<N>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0u;
}

// ── Test 1: Fully quantum source → all result qubits allocated ──────────────

static void test_fully_quantum_all_allocated() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;

    // Pre-allocate source qubits.
    int src_qubits[W];
    for (std::size_t i = 0; i < W; ++i) {
        src_qubits[i] = sturm::QubitPool::instance().allocate();
    }

    sturm::qint_t<W> a;
    a.value      = 15;
    a.super_mask = 0xF;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = src_qubits[i];
    }

    // Call copy_register directly (it is static in the header, visible here).
    sturm::qint_t<W> result = sturm::copy_register(a);

    // All result qubits must be allocated (>= 0).
    for (std::size_t i = 0; i < W; ++i) {
        assert(result.qubits[i] >= 0 &&
               "fully quantum source: all result qubits must be allocated");
        // Result qubits must be different from source qubits.
        assert(result.qubits[i] != a.qubits[i] &&
               "result qubits must be fresh (not aliased to source)");
    }
    assert(result.value == 15 && "classical value must be copied");
    assert(result.super_mask == 0xF && "super_mask must be copied");

    clear_q(a);
    clear_q(result);

    for (std::size_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(src_qubits[i]);
    }

    std::puts("PASS: test_fully_quantum_all_allocated");
}

// ── Test 2: Mixed source → only quantum bits get fresh qubits ───────────────
//
// Source has qubits[0] and qubits[2] allocated (quantum), qubits[1] and
// qubits[3] are -1 (classical). After copy_register, result must mirror this:
// result.qubits[0] >= 0, result.qubits[1] == -1, result.qubits[2] >= 0,
// result.qubits[3] == -1.

static void test_mixed_source_only_quantum_copied() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;

    // Allocate only 2 qubits for the source (bits 0 and 2).
    int q0 = sturm::QubitPool::instance().allocate();
    int q2 = sturm::QubitPool::instance().allocate();

    sturm::qint_t<W> a;
    a.value      = 5;          // binary: 0101
    a.super_mask = 0x5;        // bits 0 and 2 are quantum
    a.qubits[0]  = q0;
    a.qubits[1]  = -1;         // classical
    a.qubits[2]  = q2;
    a.qubits[3]  = -1;         // classical

    sturm::qint_t<W> result = sturm::copy_register(a);

    // Quantum bits (0, 2) must have fresh qubits.
    assert(result.qubits[0] >= 0 &&
           "mixed source: quantum bit 0 must be allocated in result");
    assert(result.qubits[2] >= 0 &&
           "mixed source: quantum bit 2 must be allocated in result");

    // Classical bits (1, 3) must remain -1.
    assert(result.qubits[1] == -1 &&
           "mixed source: classical bit 1 must stay -1 in result");
    assert(result.qubits[3] == -1 &&
           "mixed source: classical bit 3 must stay -1 in result");

    // Result qubits must be fresh (not same as source).
    assert(result.qubits[0] != a.qubits[0] &&
           "result qubit 0 must be fresh");
    assert(result.qubits[2] != a.qubits[2] &&
           "result qubit 2 must be fresh");

    assert(result.value == 5 && "classical value must be copied");
    assert(result.super_mask == 0x5 && "super_mask must be copied");

    clear_q(a);
    clear_q(result);

    sturm::QubitPool::instance().release(q0);
    sturm::QubitPool::instance().release(q2);

    std::puts("PASS: test_mixed_source_only_quantum_copied");
}

// ── Test 3: Fully classical source → no result qubits allocated ─────────────

static void test_fully_classical_no_qubits() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;

    sturm::qint_t<W> a;
    a.value      = 7;
    a.super_mask = 0;
    // All qubits are -1 (default).

    sturm::qint_t<W> result = sturm::copy_register(a);

    for (std::size_t i = 0; i < W; ++i) {
        assert(result.qubits[i] == -1 &&
               "fully classical: result qubits must all be -1");
    }
    assert(result.value == 7 && "classical value must be copied");
    assert(result.super_mask == 0 && "super_mask must be copied (0)");

    std::puts("PASS: test_fully_classical_no_qubits");
}

// ── Test 4: CNOT gate count matches quantum bit count ───────────────────────
//
// With a mixed source (2 of 4 bits quantum), copy_register should emit
// exactly 2 CNOTs (one per quantum bit), not 4.

static void test_cnot_count_matches_quantum_bits() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;

    // Allocate 2 source qubits (bits 0 and 2 are quantum).
    int q0 = sturm::QubitPool::instance().allocate();
    int q2 = sturm::QubitPool::instance().allocate();

    sturm::qint_t<W> a;
    a.value      = 5;
    a.super_mask = 0x5;
    a.qubits[0]  = q0;
    a.qubits[1]  = -1;
    a.qubits[2]  = q2;
    a.qubits[3]  = -1;

    CountCtx sc;

    uint64_t before = sc.ctx->gate_count;
    sturm::qint_t<W> result = sturm::copy_register(a);
    uint64_t after = sc.ctx->gate_count;

    // Exactly 2 CNOTs should be emitted (one per quantum bit).
    uint64_t cnot_count = after - before;
    assert(cnot_count == 2 &&
           "copy_register must emit exactly 2 CNOTs for 2 quantum bits");

    clear_q(a);
    clear_q(result);

    sturm::QubitPool::instance().release(q0);
    sturm::QubitPool::instance().release(q2);

    std::puts("PASS: test_cnot_count_matches_quantum_bits");
}

// ── Test 5: Fully quantum 4-bit → 4 CNOTs ──────────────────────────────────
//
// Baseline: fully quantum source with all 4 bits should emit exactly 4 CNOTs.

static void test_fully_quantum_cnot_count() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;

    int src_qubits[W];
    for (std::size_t i = 0; i < W; ++i) {
        src_qubits[i] = sturm::QubitPool::instance().allocate();
    }

    sturm::qint_t<W> a;
    a.value      = 15;
    a.super_mask = 0xF;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = src_qubits[i];
    }

    CountCtx sc;

    uint64_t before = sc.ctx->gate_count;
    sturm::qint_t<W> result = sturm::copy_register(a);
    uint64_t after = sc.ctx->gate_count;

    uint64_t cnot_count = after - before;
    assert(cnot_count == 4 &&
           "copy_register must emit exactly 4 CNOTs for 4 quantum bits");

    clear_q(a);
    clear_q(result);

    for (std::size_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(src_qubits[i]);
    }

    std::puts("PASS: test_fully_quantum_cnot_count");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_fully_quantum_all_allocated();
    test_mixed_source_only_quantum_copied();
    test_fully_classical_no_qubits();
    test_cnot_count_matches_quantum_bits();
    test_fully_quantum_cnot_count();

    std::puts("\nAll copy_register quantum-only tests passed.");
    return 0;
}
