// test_free_bitwise_neg1_qubits.cpp -- M4: Verify free bitwise operators (&, |)
// handle -1 qubit indices (classical bits) correctly via BitProxy.
//   (sturm-5hy)
//
// Before fix: operator& and operator| in qint_bitwise_backend.hpp create
// qbool::make_non_owning(b.qubits[i]) where b.qubits[i] is -1 for classical
// operands, producing an invalid qubit reference (0xFFFFFFFF after uint32_t
// cast). After fix: gate loops use BitProxy, which checks is_quantum() and
// applies classical folding.
//
// Test cases:
//   1. c = a_quantum & b_classical: gates emitted, value correct
//   2. c = a_classical & b_quantum: gates emitted, value correct
//   3. c = a_quantum | b_classical: gates emitted, value correct
//   4. c = a_classical | b_quantum: gates emitted, value correct
//   5. Regression: both quantum still works (no change)
//   6. Regression: both classical still works (no gates emitted)
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

// -- COUNT_ONLY context RAII wrapper -----------------------------------------

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

// -- Helpers -----------------------------------------------------------------

static constexpr std::size_t W = 4u;

// Build a quantum qint_t<W> with allocated qubit indices.
static sturm::qint_t<W> make_quantum(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (1u << W) - 1u;
    for (uint32_t i = 0; i < W; ++i)
        q.qubits[i] = static_cast<int>(base + i);
    return q;
}

// Build a classical qint_t<W> (all qubits[i] = -1, super_mask = 0).
static sturm::qint_t<W> make_classical(int64_t val) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = 0;
    // qubits already -1 from default ctor
    return q;
}

// Prevent double-release by clearing qubit indices.
static void clear_q(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0;
}

// =============================================================================
// Test 1: c = a_quantum & b_classical
// a=5 (0101) quantum, b=6 (0110) classical -> c must be 4 (0100)
// Gate emission expected: only for bits where b=1 (CX folding).
// =============================================================================

static void test_free_and_quantum_classical() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve W qubits for a.
    int reserved[W];
    for (uint32_t i = 0; i < W; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    CountCtx sc;
    uint64_t gates_before = sc.ctx->gate_count;

    {
        auto a = make_quantum(5, 0);
        auto b = make_classical(6);

        auto c = a & b;

        // Classical value correct.
        assert(c.value == (5 & 6) && "c = a_q & b_cl: value must be 4");

        // Gates emitted (BitProxy handles mixed operands).
        assert(sc.ctx->gate_count > gates_before &&
               "c = a_q & b_cl: must emit gates");

        // Result has allocated qubits.
        for (uint32_t i = 0; i < W; ++i)
            assert(c.qubits[i] >= 0 && "result must have allocated qubits");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < W; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_free_and_quantum_classical (a_q & b_cl)\n");
}

// =============================================================================
// Test 2: c = a_classical & b_quantum
// a=5 (0101) classical, b=6 (0110) quantum -> c must be 4 (0100)
// =============================================================================

static void test_free_and_classical_quantum() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[W];
    for (uint32_t i = 0; i < W; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    CountCtx sc;
    uint64_t gates_before = sc.ctx->gate_count;

    {
        auto a = make_classical(5);
        auto b = make_quantum(6, 0);

        auto c = a & b;

        assert(c.value == (5 & 6) && "c = a_cl & b_q: value must be 4");
        assert(sc.ctx->gate_count > gates_before &&
               "c = a_cl & b_q: must emit gates");

        for (uint32_t i = 0; i < W; ++i)
            assert(c.qubits[i] >= 0 && "result must have allocated qubits");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < W; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_free_and_classical_quantum (a_cl & b_q)\n");
}

// =============================================================================
// Test 3: c = a_quantum | b_classical
// a=5 (0101) quantum, b=6 (0110) classical -> c must be 7 (0111)
// =============================================================================

static void test_free_or_quantum_classical() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[W];
    for (uint32_t i = 0; i < W; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    CountCtx sc;
    uint64_t gates_before = sc.ctx->gate_count;

    {
        auto a = make_quantum(5, 0);
        auto b = make_classical(6);

        auto c = a | b;

        assert(c.value == (5 | 6) && "c = a_q | b_cl: value must be 7");
        assert(sc.ctx->gate_count > gates_before &&
               "c = a_q | b_cl: must emit gates");

        for (uint32_t i = 0; i < W; ++i)
            assert(c.qubits[i] >= 0 && "result must have allocated qubits");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < W; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_free_or_quantum_classical (a_q | b_cl)\n");
}

// =============================================================================
// Test 4: c = a_classical | b_quantum
// a=5 (0101) classical, b=6 (0110) quantum -> c must be 7 (0111)
// =============================================================================

static void test_free_or_classical_quantum() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[W];
    for (uint32_t i = 0; i < W; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    CountCtx sc;
    uint64_t gates_before = sc.ctx->gate_count;

    {
        auto a = make_classical(5);
        auto b = make_quantum(6, 0);

        auto c = a | b;

        assert(c.value == (5 | 6) && "c = a_cl | b_q: value must be 7");
        assert(sc.ctx->gate_count > gates_before &&
               "c = a_cl | b_q: must emit gates");

        for (uint32_t i = 0; i < W; ++i)
            assert(c.qubits[i] >= 0 && "result must have allocated qubits");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < W; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_free_or_classical_quantum (a_cl | b_q)\n");
}

// =============================================================================
// Test 5: Regression - both quantum still works
// a=5 quantum, b=6 quantum -> & gives 4, | gives 7
// =============================================================================

static void test_free_and_or_both_quantum() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[2 * W];
    for (uint32_t i = 0; i < 2 * W; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    CountCtx sc;
    uint64_t gates_before = sc.ctx->gate_count;

    {
        auto a = make_quantum(5, 0);
        auto b = make_quantum(6, W);

        auto c = a & b;

        assert(c.value == (5 & 6) && "c = a_q & b_q: value must be 4");
        assert(sc.ctx->gate_count > gates_before &&
               "c = a_q & b_q: must emit gates");

        uint64_t gates_mid = sc.ctx->gate_count;

        auto d = a | b;

        assert(d.value == (5 | 6) && "d = a_q | b_q: value must be 7");
        assert(sc.ctx->gate_count > gates_mid &&
               "d = a_q | b_q: must emit gates");

        clear_q(a);
        clear_q(b);
        clear_q(c);
        clear_q(d);
    }

    for (uint32_t i = 0; i < 2 * W; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_free_and_or_both_quantum (regression)\n");
}

// =============================================================================
// Test 6: Regression - both classical still works (no gates, no crash)
// a=5 classical, b=6 classical -> & gives 4, | gives 7, zero gates
// =============================================================================

static void test_free_and_or_both_classical() {
    sturm::QubitPool::instance().reset_for_testing();

    CountCtx sc;
    uint64_t gates_before = sc.ctx->gate_count;

    {
        auto a = make_classical(5);
        auto b = make_classical(6);

        auto c = a & b;

        assert(c.value == (5 & 6) && "c = a_cl & b_cl: value must be 4");

        auto d = a | b;

        assert(d.value == (5 | 6) && "d = a_cl | b_cl: value must be 7");

        // No gates emitted for both-classical.
        assert(sc.ctx->gate_count == gates_before &&
               "both classical: zero gates expected");

        clear_q(a);
        clear_q(b);
        clear_q(c);
        clear_q(d);
    }

    std::printf("  PASS: test_free_and_or_both_classical (regression)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_free_bitwise_neg1_qubits: M4 tests for free bitwise "
                "operators with -1 qubit indices\n\n");

    test_free_and_quantum_classical();
    test_free_and_classical_quantum();
    test_free_or_quantum_classical();
    test_free_or_classical_quantum();
    test_free_and_or_both_quantum();
    test_free_and_or_both_classical();

    std::printf("\nAll test_free_bitwise_neg1_qubits tests passed.\n");
    return 0;
}
