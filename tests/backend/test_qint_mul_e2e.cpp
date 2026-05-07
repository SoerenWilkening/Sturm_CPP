// test_qint_mul_e2e.cpp — M19 (PRD v3): qint_t operator*= end-to-end.
//
// Tests:
//   test_qint_mul_e2e_count    — a *= b emits gates in COUNT_ONLY mode
//   test_qint_mul_e2e_simulate — 2-bit multiplication: 2*3=6 (correct result)
//
// The operator*= uses lib_mul_dsl (out-of-place + move result back).
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
        ctx  = sturm_backend_create(mode);
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
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE);
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

// ── test_qint_mul_e2e_count ───────────────────────────────────────────────────

static void test_qint_mul_e2e_count() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 2;
    // Reserve 2W register qubits for a and b.
    int reserved[4];
    for (uint32_t i = 0; i < 4; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(i);
            b.qubits[i] = static_cast<int>(W + i);
        }
        a.value = 2;
        b.value = 3;

        uint64_t before = sc.ctx->gate_count;
        a *= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(after > before && "a *= b must emit gates");

        // The *=  result is in a's register (moved back from out-of-place result).
        // qubits may have been swapped; prevent double-release.
        // We clear all qubit slots to be safe.
        for (int& q : a.qubits) q = -1;
        for (int& q : b.qubits) q = -1;
    }

    for (uint32_t i = 0; i < 4; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_qint_mul_e2e_count (gates emitted)\n");
}

// ── test_qint_mul_e2e_simulate ────────────────────────────────────────────────
// 3-bit * 3-bit = 6-bit product: test a=2, b=3 -> product=6.
//
// Qubit budget with W=3 and 1 control (b[i]) in the multiply loop:
//   Register (a+b):    2*W = 6 qubits (indices 0..5)
//   Result register:   2*W = 6 qubits (indices 6..11)
//   lib_add carry_anc: 1 qubit (index 12)
//   emit_CCX_lifted under 1 control allocates 1 ancilla (index 13)
//   Peak total: 14 qubits — within kMaxQubits (17).
//
// Product 2*3=6 = 0b110. Lower 3 bits = 0b110 = 6. operator*= retains lower W bits.

static void test_qint_mul_e2e_simulate() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 3;
    const uint32_t a_base = 0u;
    const uint32_t b_base = W;
    const uint32_t n_reg  = 2u * W;  // 6 qubits

    // Pre-reserve register qubits (indices 0..5).
    int reserved[6];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    // Allocate 16 Orkan qubits (indices 0..15), well within kMaxQubits=17.
    // Peak usage: 14 qubits (6 register + 6 result + 1 carry_anc + 1 ccx_anc).
    SimCtx sc{16u, 128u};

    // Initialize statevector: a=2 (010), b=3 (011)
    for (uint32_t i = 0; i < W; ++i) {
        if ((2u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gate_count_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value = 2;
        b.value = 3;

        a *= b;

        // After *=: a.qubits[i] points to the lower W bits of the result register.
        // 2*3=6 = 0b110. Lower 3 bits = 6.
        static constexpr uint32_t n_total = 16u;
        uint64_t dim = uint64_t{1} << n_total;
        uint32_t got = 0u;
        for (uint64_t s = 0; s < dim; ++s) {
            if (std::norm(orkan::amplitude(sc.sv(), s)) > kTol) {
                for (uint32_t i = 0; i < W; ++i) {
                    if (a.qubits[i] >= 0) {
                        got |= (static_cast<uint32_t>((s >> a.qubits[i]) & 1u) << i);
                    }
                }
                break;
            }
        }

        assert(gate_count_before < sc.ctx->gate_count && "must emit gates");
        // 2 * 3 = 6 (fits in 3 bits: 0b110)
        assert(got == 6u && "a *= b: 2*3 must equal 6");

        // Prevent double-release of pool qubits managed by reserved[]
        for (int& q : a.qubits) q = -1;
        for (int& q : b.qubits) q = -1;
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_qint_mul_e2e_simulate (2*3=6)\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M19 qint_t operator*= end-to-end tests:\n");
    test_qint_mul_e2e_count();
    test_qint_mul_e2e_simulate();
    std::printf("All M19 qint_mul_e2e tests passed.\n");
    return 0;
}
