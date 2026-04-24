// test_pow_dsl_small.cpp — M17 (PRD v3): DSL-style exponentiation tests.
//
// Tests:
//   test_pow_dsl_small — small base/exponent pairs:
//     2^3=8, 3^2=9, 2^0=1, 0^3=0, 1^5=1.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/pow_dsl.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/qtypes/bit_proxy.hpp"    // BitProxy complete type for lib_div_dsl<BitProxy> adjoint registration pulled in via pow_dsl -> mul_dsl -> div_dsl (sturm-f8xf)
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;

// ── SimCtx: SIMULATE mode with OrkanBridge ────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
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

    sturm::BackendContext& bc() { return *ctx; }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Read n-qubit register starting at base_q ─────────────────────────────────

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

// ── run_pow_test ──────────────────────────────────────────────────────────────
//
// Test base^exp = expected_result.
// n_base: bits for base, n_exp: bits for exponent, n_res: bits for result.
// Uses enough Orkan qubits to accommodate register + ancilla.

static void run_pow_test(uint32_t base_val, size_t n_base,
                         uint32_t exp_val,  size_t n_exp,
                         uint32_t expected, size_t n_res,
                         const char* label) {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t base_base_q = 0u;
    const uint32_t exp_base_q  = static_cast<uint32_t>(n_base);
    const uint32_t res_base_q  = static_cast<uint32_t>(n_base + n_exp);
    const uint32_t n_reg       = static_cast<uint32_t>(n_base + n_exp + n_res);
    const uint32_t orkan_n     = 17u;  // max 17-qubit STURM cap

    // Pre-reserve register qubits.
    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{orkan_n, 64u};

    // Initialize base and exp registers.
    for (size_t i = 0; i < n_base; ++i) {
        if ((base_val >> i) & 1u) orkan::apply_x(sc.sv(), static_cast<uint32_t>(base_base_q + i));
    }
    for (size_t i = 0; i < n_exp; ++i) {
        if ((exp_val >> i) & 1u) orkan::apply_x(sc.sv(), static_cast<uint32_t>(exp_base_q + i));
    }

    // Create qbool arrays.
    std::vector<sturm::qbool> base_bits(n_base);
    std::vector<sturm::qbool> exp_bits(n_exp);
    std::vector<sturm::qbool> res_bits(n_res);

    for (size_t i = 0; i < n_base; ++i)
        base_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(base_base_q + i));
    for (size_t i = 0; i < n_exp; ++i)
        exp_bits[i]  = sturm::qbool::make_non_owning(static_cast<int>(exp_base_q + i));
    for (size_t i = 0; i < n_res; ++i)
        res_bits[i]  = sturm::qbool::make_non_owning(static_cast<int>(res_base_q + i));

    // Run DSL pow: result = base ^ exp.
    sturm::lib_pow_dsl(base_bits.data(), n_base,
                       exp_bits.data(),  n_exp,
                       res_bits.data(),  n_res);

    // Verify result.
    uint32_t got = read_reg(sc.sv(), res_base_q, static_cast<uint32_t>(n_res), orkan_n);
    if (got != expected) {
        std::fprintf(stderr,
            "FAIL pow_dsl %s: %u^%u expected=%u got=%u\n",
            label, base_val, exp_val, expected, got);
        assert(false);
    }

    // Verify base and exp unchanged.
    uint32_t got_base = read_reg(sc.sv(), base_base_q, static_cast<uint32_t>(n_base), orkan_n);
    uint32_t got_exp  = read_reg(sc.sv(), exp_base_q,  static_cast<uint32_t>(n_exp),  orkan_n);
    if (got_base != base_val || got_exp != exp_val) {
        std::fprintf(stderr,
            "FAIL pow_dsl %s: inputs changed base=%u->%u exp=%u->%u\n",
            label, base_val, got_base, exp_val, got_exp);
        assert(false);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: pow_dsl %s: %u^%u = %u\n", label, base_val, exp_val, got);
}

// ── test_pow_dsl_small ────────────────────────────────────────────────────────

static void test_pow_dsl_small() {
    // 2^3 = 8: base=2 (2 bits: 10), exp=3 (2 bits: 11), result needs 4 bits.
    run_pow_test(2u, 2u, 3u, 2u, 8u, 4u, "2^3=8");

    // 3^2 = 9: base=3 (2 bits: 11), exp=2 (2 bits: 10), result needs 4 bits.
    run_pow_test(3u, 2u, 2u, 2u, 9u, 4u, "3^2=9");

    // 2^0 = 1: base=2 (2 bits), exp=0 (1 bit: 0), result needs 2 bits.
    run_pow_test(2u, 2u, 0u, 1u, 1u, 2u, "2^0=1");

    // 0^3 = 0: base=0 (2 bits: 00), exp=3 (2 bits: 11), result needs 1 bit.
    run_pow_test(0u, 2u, 3u, 2u, 0u, 1u, "0^3=0");

    // 1^5 = 1: base=1 (1 bit), exp=5 (3 bits: 101), result needs 1 bit.
    run_pow_test(1u, 1u, 5u, 3u, 1u, 1u, "1^5=1");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M17 DSL-style POW tests:\n");
    test_pow_dsl_small();
    std::printf("All M17 pow_dsl tests passed.\n");
    return 0;
}
