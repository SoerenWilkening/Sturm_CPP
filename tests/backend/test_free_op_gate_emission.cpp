// test_free_op_gate_emission.cpp — Test: free-operator gate emission in
// COUNT_ONLY mode for all 5 free arithmetic operators (+, -, *, /, %).
//   (sturm-n7w)
//
// For each free operator (c = a op b), verifies that gate_count increases
// after the expression is evaluated in COUNT_ONLY mode.  Each operator has
// its own test function so failures are individually visible.
//
// EXPECTED TO FAIL on current code: the free-operator bodies in
// qint_arith_backend.hpp carry TODO(backend) stubs that call (void)ctx and
// emit no gates.  This test documents the required behaviour so that
// sturm-6qo (the fix) has a concrete green target.
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

// ── Qubit layout (W=3) ───────────────────────────────────────────────────────
// q[0..2] = a register (a_base = 0)
// q[3..5] = b register (b_base = 3)
// q[6..8] = c result register (expected after sturm-6qo fix)
// q[9+]   = ancilla headroom

static constexpr std::size_t W = 3u;
static constexpr uint32_t a_base = 0u;
static constexpr uint32_t b_base = W;      // 3

// Pre-reserved qubit indices for a and b.
static int reserved_qubits[2u * W];

static void reserve_ab() {
    sturm::QubitPool::instance().reset_for_testing();
    for (uint32_t i = 0; i < 2u * W; ++i) {
        reserved_qubits[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved_qubits[0] == 0 && "pool must start at index 0");
}

static void release_ab() {
    for (uint32_t i = 0; i < 2u * W; ++i) {
        if (reserved_qubits[i] >= 0) {
            sturm::QubitPool::instance().release(reserved_qubits[i]);
            reserved_qubits[i] = -1;
        }
    }
}

// Build a qint_t<W> aliasing pre-reserved qubits starting at base.
static sturm::qint_t<W> make_q(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (1u << W) - 1u;  // all bits quantum
    for (uint32_t i = 0; i < W; ++i) {
        q.qubits[i] = static_cast<int>(base + i);
    }
    return q;
}

// Prevent double-release by clearing qubit indices and super_mask.
template <std::size_t N>
static void clear_q(sturm::qint_t<N>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0u;
}

// =============================================================================
// test_free_add_gate_emission
//
// c = a + b with a=3, b=5 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase (operator+ must emit adder gates).
// EXPECTED TO FAIL: current stub has (void)ctx — no gates emitted.
// =============================================================================

static void test_free_add_gate_emission() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(3LL, a_base);
        sturm::qint_t<W> b = make_q(5LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a + b;
        uint64_t after  = sc.ctx->gate_count;

        // Classical value should be correct regardless.
        assert(c.value == 8LL &&
               "c = a + b: classical value must be 3+5=8");

        // Gate emission check — EXPECTED TO FAIL until sturm-6qo.
        // operator+(a,b) must call operator+= internally which routes through
        // lib_add_dsl → execute_gate → gate_count incremented.
        assert(after > before &&
               "c = a + b must emit gates in COUNT_ONLY mode — "
               "stub has TODO(backend): fix in sturm-6qo");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_add_gate_emission (a+b emits gates)\n");
}

// =============================================================================
// test_free_sub_gate_emission
//
// c = a - b with a=7, b=3 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_sub_gate_emission() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(7LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a - b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 4LL &&
               "c = a - b: classical value must be 7-3=4");

        assert(after > before &&
               "c = a - b must emit gates in COUNT_ONLY mode — "
               "stub has TODO(backend): fix in sturm-6qo");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_sub_gate_emission (a-b emits gates)\n");
}

// =============================================================================
// test_free_mul_gate_emission
//
// c = a * b with a=2, b=3 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_mul_gate_emission() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(2LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a * b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 6LL &&
               "c = a * b: classical value must be 2*3=6");

        assert(after > before &&
               "c = a * b must emit gates in COUNT_ONLY mode — "
               "stub has TODO(backend): fix in sturm-6qo");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_mul_gate_emission (a*b emits gates)\n");
}

// =============================================================================
// test_free_div_gate_emission
//
// c = a / b with a=6, b=2 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_div_gate_emission() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(6LL, a_base);
        sturm::qint_t<W> b = make_q(2LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a / b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 3LL &&
               "c = a / b: classical value must be 6/2=3");

        assert(after > before &&
               "c = a / b must emit gates in COUNT_ONLY mode — "
               "stub has TODO(backend): fix in sturm-6qo");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_div_gate_emission (a/b emits gates)\n");
}

// =============================================================================
// test_free_mod_gate_emission
//
// c = a % b with a=7, b=3 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_mod_gate_emission() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(7LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a % b;
        uint64_t after  = sc.ctx->gate_count;

        assert(c.value == 1LL &&
               "c = a % b: classical value must be 7%3=1");

        assert(after > before &&
               "c = a % b must emit gates in COUNT_ONLY mode — "
               "stub has TODO(backend): fix in sturm-6qo");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_mod_gate_emission (a%%b emits gates)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_free_op_gate_emission: COUNT_ONLY gate emission for "
                "free operators (+, -, *, /, %%)\n"
                "NOTE: ALL 5 tests are EXPECTED TO FAIL on current code.\n"
                "      This file documents the bug until sturm-6qo lands.\n\n");

    test_free_add_gate_emission();
    test_free_sub_gate_emission();
    test_free_mul_gate_emission();
    test_free_div_gate_emission();
    test_free_mod_gate_emission();

    std::printf("\nAll test_free_op_gate_emission tests passed.\n");
    return 0;
}
