// test_free_arith_mixed_fastpath.cpp -- Test: free arithmetic operator
// fast-path fix for mixed quantum/classical operands (sturm-8w8, M3).
//
// Verifies that free operators (+, -, *, /, %) emit gates when one operand
// is quantum and the other is classical.  The bug was that the fast-path
// guard used || (either operand classical) instead of && (both operands
// classical), causing the fast path to be taken whenever either operand
// had no allocated qubits.
//
// Also verifies the regression: both-classical operands still take the
// fast path (zero gates, no qubits allocated).
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
        ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY, max_q);
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

template <std::size_t W>
static void reserve_regs(int (&reserved)[W]) {
    sturm::QubitPool::instance().reset_for_testing();
    for (uint32_t i = 0; i < W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");
}

template <std::size_t W>
static void release_regs(int (&reserved)[W]) {
    for (uint32_t i = 0; i < W; ++i) {
        if (reserved[i] >= 0) {
            sturm::QubitPool::instance().release(reserved[i]);
            reserved[i] = -1;
        }
    }
}

// Build a quantum qint_t<W> aliasing pre-reserved qubits starting at base.
template <std::size_t W>
static sturm::qint_t<W> make_quantum(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (1u << W) - 1u;  // all bits quantum
    for (uint32_t i = 0; i < W; ++i) {
        q.qubits[i] = static_cast<int>(base + i);
    }
    return q;
}

// Build a classical qint_t<W> (no qubits allocated, super_mask = 0).
template <std::size_t W>
static sturm::qint_t<W> make_classical(int64_t val) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = 0u;
    // qubits already defaulted to -1
    return q;
}

// Prevent double-release by clearing qubit indices and super_mask.
template <std::size_t N>
static void clear_q(sturm::qint_t<N>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0u;
}

// =============================================================================
// Test 1: c = quantum_a + classical_b must emit gates
// =============================================================================
static void test_free_add_mixed() {
    static constexpr std::size_t W = 3u;
    int reserved[W];
    reserve_regs<W>(reserved);
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_quantum<W>(3LL, 0u);
        sturm::qint_t<W> b = make_classical<W>(5LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a + b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 8LL &&
               "c = a + b: classical value must be 3+5=8");
        assert(after > before &&
               "c = quantum + classical must emit gates (fast-path fix)");

        clear_q(a);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_free_add_mixed (quantum + classical emits gates)\n");
}

// =============================================================================
// Test 2: c = quantum_a - classical_b must emit gates
// =============================================================================
static void test_free_sub_mixed() {
    static constexpr std::size_t W = 3u;
    int reserved[W];
    reserve_regs<W>(reserved);
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_quantum<W>(7LL, 0u);
        sturm::qint_t<W> b = make_classical<W>(3LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a - b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 4LL &&
               "c = a - b: classical value must be 7-3=4");
        assert(after > before &&
               "c = quantum - classical must emit gates (fast-path fix)");

        clear_q(a);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_free_sub_mixed (quantum - classical emits gates)\n");
}

// =============================================================================
// Test 3: c = quantum_a * classical_b must emit gates
// =============================================================================
static void test_free_mul_mixed() {
    static constexpr std::size_t W = 3u;
    int reserved[W];
    reserve_regs<W>(reserved);
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_quantum<W>(2LL, 0u);
        sturm::qint_t<W> b = make_classical<W>(3LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a * b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 6LL &&
               "c = a * b: classical value must be 2*3=6");
        assert(after > before &&
               "c = quantum * classical must emit gates (fast-path fix)");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_free_mul_mixed (quantum * classical emits gates)\n");
}

// =============================================================================
// Test 4: c = quantum_a / classical_b must emit gates
// =============================================================================
static void test_free_div_mixed() {
    static constexpr std::size_t W = 2u;
    int reserved[W];
    reserve_regs<W>(reserved);
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_quantum<W>(3LL, 0u);
        sturm::qint_t<W> b = make_classical<W>(2LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a / b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 1LL &&
               "c = a / b: classical value must be 3/2=1");
        assert(after > before &&
               "c = quantum / classical must emit gates (fast-path fix)");

        clear_q(a);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_free_div_mixed (quantum / classical emits gates)\n");
}

// =============================================================================
// Test 5: c = quantum_a % classical_b must emit gates
// =============================================================================
static void test_free_mod_mixed() {
    static constexpr std::size_t W = 2u;
    int reserved[W];
    reserve_regs<W>(reserved);
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_quantum<W>(3LL, 0u);
        sturm::qint_t<W> b = make_classical<W>(2LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a % b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 1LL &&
               "c = a %% b: classical value must be 3%%2=1");
        assert(after > before &&
               "c = quantum %% classical must emit gates (fast-path fix)");

        clear_q(a);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_free_mod_mixed (quantum %% classical emits gates)\n");
}

// =============================================================================
// Test 6: Regression -- c = classical_a + classical_b must NOT emit gates
// =============================================================================
static void test_free_add_both_classical() {
    sturm::QubitPool::instance().reset_for_testing();
    CountCtx sc;

    {
        sturm::qint_t<3> a = make_classical<3>(3LL);
        sturm::qint_t<3> b = make_classical<3>(5LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<3> c = a + b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 8LL &&
               "c = a + b: classical value must be 3+5=8");
        assert(c.super_mask == 0u &&
               "both-classical result must have super_mask == 0");
        assert(after == before &&
               "both-classical c = a + b must emit zero gates (regression)");

        clear_q(c);
    }

    std::printf("  PASS: test_free_add_both_classical (zero gates, regression)\n");
}

// =============================================================================
// Test 7: Regression -- c = classical_a - classical_b must NOT emit gates
// =============================================================================
static void test_free_sub_both_classical() {
    sturm::QubitPool::instance().reset_for_testing();
    CountCtx sc;

    {
        sturm::qint_t<3> a = make_classical<3>(7LL);
        sturm::qint_t<3> b = make_classical<3>(3LL);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<3> c = a - b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 4LL &&
               "c = a - b: classical value must be 7-3=4");
        assert(c.super_mask == 0u &&
               "both-classical result must have super_mask == 0");
        assert(after == before &&
               "both-classical c = a - b must emit zero gates (regression)");

        clear_q(c);
    }

    std::printf("  PASS: test_free_sub_both_classical (zero gates, regression)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_free_arith_mixed_fastpath: M3 fast-path fix for free "
                "arithmetic operators with mixed quantum/classical operands\n\n");

    test_free_add_mixed();
    test_free_sub_mixed();
    test_free_mul_mixed();
    test_free_div_mixed();
    test_free_mod_mixed();
    test_free_add_both_classical();
    test_free_sub_both_classical();

    std::printf("\nAll test_free_arith_mixed_fastpath tests passed.\n");
    return 0;
}
