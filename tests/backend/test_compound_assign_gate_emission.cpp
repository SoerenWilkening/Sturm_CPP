// test_compound_assign_gate_emission.cpp — Test: compound-assign gate emission
// baseline in COUNT_ONLY mode for all 5 compound-assign operators
// (+=, -=, *=, /=, %=).
//   (sturm-hvb)
//
// For each compound-assign operator (a op= b), verifies that gate_count
// increases after the expression is evaluated in COUNT_ONLY mode.  Each
// operator has its own test function so failures are individually visible.
//
// These tests SHOULD PASS on current code: compound-assign operators in
// qint_arith_v3.hpp are fully wired to the DSL library (lib_add_dsl,
// lib_sub_dsl, lib_mul_dsl, lib_div_dsl, lib_mod_dsl), which emit gates via
// execute_gate regardless of mode.
//
// This file establishes the working baseline before fixing free operators
// (sturm-6qo), which will delegate to these compound-assigns.
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

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(uint32_t max_q = 128u) {
        ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY, max_q);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── Qubit layout helpers ──────────────────────────────────────────────────────

// Pre-reserve 2*W register qubits so pool ancilla gets indices >= 2*W.
template <std::size_t W>
static void reserve_regs(int (&reserved)[2u * W]) {
    sturm::QubitPool::instance().reset_for_testing();
    for (uint32_t i = 0; i < 2u * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");
}

template <std::size_t W>
static void release_regs(int (&reserved)[2u * W]) {
    for (uint32_t i = 0; i < 2u * W; ++i) {
        if (reserved[i] >= 0) {
            sturm::QubitPool::instance().release(reserved[i]);
            reserved[i] = -1;
        }
    }
}

// Build a qint_t<W> aliasing pre-reserved qubits starting at base.
template <std::size_t W>
static sturm::qint_t<W> make_q(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (1u << W) - 1u;
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
// test_compound_add_assign_gate_emission
//
// a += b with a=3, b=5, W=4 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase (operator+= must emit adder gates).
// SHOULD PASS: operator+= calls lib_add_dsl which routes through execute_gate.
//
// W=4 to avoid carry overflow; b_base = W = 4.
// =============================================================================

static void test_compound_add_assign_gate_emission() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;   // 4

    int reserved[2u * W];
    reserve_regs<W>(reserved);
    ScopedCtx sc;

    {
        sturm::qint_t<W> a = make_q<W>(3LL, a_base);
        sturm::qint_t<W> b = make_q<W>(5LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        a += b;
        uint64_t after  = sc.ctx->gate_count;

        // 1. Classical value must be correct.
        assert(a.value == 8LL &&
               "a += b: classical value must be 3+5=8");

        // 2. Gate emission check — SHOULD PASS on current code.
        assert(after > before &&
               "a += b must emit gates in COUNT_ONLY mode");

        // Prevent double-release (reserved[] owns a and b qubits).
        clear_q(a);
        clear_q(b);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_compound_add_assign_gate_emission (3+=5 emits gates)\n");
}

// =============================================================================
// test_compound_sub_assign_gate_emission
//
// a -= b with a=7, b=3, W=3 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// SHOULD PASS: operator-= calls lib_sub_dsl which routes through execute_gate.
// =============================================================================

static void test_compound_sub_assign_gate_emission() {
    static constexpr std::size_t W = 3u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;   // 3

    int reserved[2u * W];
    reserve_regs<W>(reserved);
    ScopedCtx sc;

    {
        sturm::qint_t<W> a = make_q<W>(7LL, a_base);
        sturm::qint_t<W> b = make_q<W>(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        a -= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(a.value == 4LL &&
               "a -= b: classical value must be 7-3=4");

        assert(after > before &&
               "a -= b must emit gates in COUNT_ONLY mode");

        clear_q(a);
        clear_q(b);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_compound_sub_assign_gate_emission (7-=3 emits gates)\n");
}

// =============================================================================
// test_compound_mul_assign_gate_emission
//
// a *= b with a=2, b=3, W=3 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// SHOULD PASS: operator*= calls lib_mul_dsl which routes through execute_gate.
//
// operator*= allocates a 2*W result register (fresh indices) and moves the
// lower W result qubits into a.qubits[].  clear_q(a) after the operation
// prevents double-release of those newly allocated qubits.
// =============================================================================

static void test_compound_mul_assign_gate_emission() {
    static constexpr std::size_t W = 3u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;   // 3

    int reserved[2u * W];
    reserve_regs<W>(reserved);
    ScopedCtx sc;

    {
        sturm::qint_t<W> a = make_q<W>(2LL, a_base);
        sturm::qint_t<W> b = make_q<W>(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        a *= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(a.value == 6LL &&
               "a *= b: classical value must be 2*3=6");

        assert(after > before &&
               "a *= b must emit gates in COUNT_ONLY mode");

        // operator*= moved result qubits into a.qubits[] (new indices), so
        // we must clear a before release_regs to avoid double-release.
        clear_q(a);
        clear_q(b);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_compound_mul_assign_gate_emission (2*=3 emits gates)\n");
}

// =============================================================================
// test_compound_div_assign_gate_emission
//
// a /= b with a=3, b=2, W=2 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// SHOULD PASS: operator/= calls lib_div_dsl which routes through execute_gate.
//
// W=2 keeps the ancilla budget small; 3/2=1.
// operator/= moves quotient qubits into a.qubits[]; clear_q prevents
// double-release.
// =============================================================================

static void test_compound_div_assign_gate_emission() {
    static constexpr std::size_t W = 2u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;   // 2

    int reserved[2u * W];
    reserve_regs<W>(reserved);
    ScopedCtx sc;

    {
        sturm::qint_t<W> a = make_q<W>(3LL, a_base);
        sturm::qint_t<W> b = make_q<W>(2LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        a /= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(a.value == 1LL &&
               "a /= b: classical value must be 3/2=1");

        assert(after > before &&
               "a /= b must emit gates in COUNT_ONLY mode");

        clear_q(a);
        clear_q(b);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_compound_div_assign_gate_emission (3/=2 emits gates)\n");
}

// =============================================================================
// test_compound_mod_assign_gate_emission
//
// a %= b with a=3, b=2, W=2 in COUNT_ONLY mode.
// REQUIREMENT: gate_count must increase.
// SHOULD PASS: operator%= calls lib_mod_dsl which routes through execute_gate.
//
// W=2; 3%2=1.  operator%= moves remainder qubits into a.qubits[]; clear_q
// prevents double-release.
// =============================================================================

static void test_compound_mod_assign_gate_emission() {
    static constexpr std::size_t W = 2u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;   // 2

    int reserved[2u * W];
    reserve_regs<W>(reserved);
    ScopedCtx sc;

    {
        sturm::qint_t<W> a = make_q<W>(3LL, a_base);
        sturm::qint_t<W> b = make_q<W>(2LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        a %= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(a.value == 1LL &&
               "a %= b: classical value must be 3%2=1");

        assert(after > before &&
               "a %= b must emit gates in COUNT_ONLY mode");

        clear_q(a);
        clear_q(b);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: test_compound_mod_assign_gate_emission (3%%=2 emits gates)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_compound_assign_gate_emission: COUNT_ONLY gate emission "
                "baseline for compound-assign operators (+=, -=, *=, /=, %%=)\n"
                "NOTE: ALL 5 tests are EXPECTED TO PASS on current code.\n\n");

    test_compound_add_assign_gate_emission();
    test_compound_sub_assign_gate_emission();
    test_compound_mul_assign_gate_emission();
    test_compound_div_assign_gate_emission();
    test_compound_mod_assign_gate_emission();

    std::printf("\nAll test_compound_assign_gate_emission tests passed.\n");
    return 0;
}
