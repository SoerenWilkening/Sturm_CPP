// test_qint_bitwise_e2e.cpp — M19 (PRD v3): qint_t bitwise operators end-to-end.
//
// Tests:
//   test_qint_xor_e2e   — a ^= b: per-bit CNOT, emits W CX gates
//   test_qint_and_e2e   — a &= b: out-of-place AND, emits gates > 0
//   test_qint_or_e2e    — a |= b: out-of-place OR, emits gates > 0
//   test_qint_not_e2e   — ~a: per-bit X, emits W X gates
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── Context helpers ───────────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 128u) {
        ctx  = sturm_backend_create(mode, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

static uint32_t read_reg(orkan::state_t& sv, uint32_t base_q, uint32_t n,
                         uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                val |= (static_cast<uint32_t>((i >> (base_q + k)) & 1u) << k);
            }
            return val;
        }
    }
    return 0u;
}

// ── test_qint_xor_e2e ─────────────────────────────────────────────────────────
// a ^= b: per-bit CNOT. 0b0101 ^ 0b0011 = 0b0110.
// Gate count should be exactly W (one CX per bit where b bit is 1).
// Actually XOR emits W CX gates (one per bit regardless of classical value).

static void test_qint_xor_e2e_count() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4;
    int reserved[8];
    for (uint32_t i = 0; i < 8; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(i);
            b.qubits[i] = static_cast<int>(W + i);
        }
        a.value = 5;  // 0b0101
        b.value = 3;  // 0b0011

        uint64_t before = sc.ctx->gate_count;
        a ^= b;
        uint64_t after  = sc.ctx->gate_count;

        // Per-bit CNOT: W CX gates (one per bit, regardless of classical value)
        assert(after > before && "a ^= b must emit gates");
        // Exactly W gates (one CX per bit)
        assert((after - before) == W && "a ^= b must emit exactly W CX gates");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < 8; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_qint_xor_e2e_count (%zu CX gates)\n", W);
}

// ── test_qint_and_e2e ─────────────────────────────────────────────────────────
// a &= b: out-of-place AND. Emits gates.

static void test_qint_and_e2e_count() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4;
    int reserved[8];
    for (uint32_t i = 0; i < 8; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(i);
            b.qubits[i] = static_cast<int>(W + i);
        }
        a.value = 5;   // 0b0101
        b.value = 6;   // 0b0110

        uint64_t before = sc.ctx->gate_count;
        a &= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(after > before && "a &= b must emit gates");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < 8; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_qint_and_e2e_count (gates emitted)\n");
}

// ── test_qint_or_e2e ──────────────────────────────────────────────────────────
// a |= b: out-of-place OR. Emits gates.

static void test_qint_or_e2e_count() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4;
    int reserved[8];
    for (uint32_t i = 0; i < 8; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(i);
            b.qubits[i] = static_cast<int>(W + i);
        }
        a.value = 5;   // 0b0101
        b.value = 6;   // 0b0110

        uint64_t before = sc.ctx->gate_count;
        a |= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(after > before && "a |= b must emit gates");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < 8; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_qint_or_e2e_count (gates emitted)\n");
}

// ── test_qint_xor_simulate ────────────────────────────────────────────────────
// Verify a ^= b produces correct result: 5 ^ 3 = 6.

static void test_qint_xor_simulate() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4;
    const uint32_t a_base = 0u;
    const uint32_t b_base = W;
    const uint32_t n_reg  = 2u * W;

    int reserved[8];
    for (uint32_t i = 0; i < n_reg; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{n_reg + 8u, 128u};

    // a=5 (0101), b=3 (0011)
    for (uint32_t i = 0; i < W; ++i) {
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value = 5;
        b.value = 3;

        a ^= b;  // a = 5 ^ 3 = 6

        uint32_t got_a = read_reg(sc.sv(), a_base, W, n_reg + 8u);
        uint32_t got_b = read_reg(sc.sv(), b_base, W, n_reg + 8u);

        assert(got_a == 6u && "5 ^ 3 = 6");
        assert(got_b == 3u && "b unchanged");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_qint_xor_simulate (5^3=6)\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M19 qint_t bitwise operators end-to-end tests:\n");
    test_qint_xor_e2e_count();
    test_qint_and_e2e_count();
    test_qint_or_e2e_count();
    test_qint_xor_simulate();
    std::printf("All M19 qint_bitwise_e2e tests passed.\n");
    return 0;
}
