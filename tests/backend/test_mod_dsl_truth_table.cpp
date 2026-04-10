// test_mod_dsl_truth_table.cpp — M17 (PRD v3): DSL-style modulo tests.
//
// Tests:
//   test_mod_dsl_truth_table — exhaustive 2-bit modulo.
//     For all a in [0,3] and b in [1,3], verify a % b matches classical.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/mod_dsl.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"
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

// ── test_mod_dsl_truth_table ──────────────────────────────────────────────────
//
// Exhaustive 2-bit modulo: a % b for all a in [0,3] and b in [1,3].
//
// Qubit layout:
//   q[0..1]  = dividend/a (2 bits, LSB=0)
//   q[2..3]  = divisor/b  (2 bits, LSB=2)
//   q[4..5]  = remainder  (2 bits, all start |0>)
//   Total: 6 register qubits.
//   + ancilla from QubitPool for lib_div_dsl internals.
//
// lib_div_dsl budget (n=2): scratch(2) + sgn(1) + overflow(1) + carry_anc(1) = 5.
// lib_mod_dsl calls lib_div_dsl twice: peak = 6 + quotient(2) + temp_rem(2) + div_ancilla = 6+2+2+5 = 15.
// Use 20 Orkan qubits for headroom.

static void test_mod_dsl_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n       = 2u;
    const uint32_t a_base  = 0u;
    const uint32_t b_base  = n;        // 2
    const uint32_t r_base  = 2u * n;   // 4
    const uint32_t n_reg   = 3u * n;   // 6
    const uint32_t orkan_n = 17u;

    // Pre-reserve register qubits.
    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 1; b_val < (1u << n); ++b_val) {  // b != 0
            SimCtx sc{orkan_n, 64u};

            // Initialize a (dividend) and b (divisor).
            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays.
            sturm::qbool a_bits[2];
            sturm::qbool b_bits[2];
            sturm::qbool r_bits[2];

            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                r_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(r_base + i));
            }

            // Run DSL mod: remainder = a % b.
            sturm::lib_mod_dsl(a_bits, n, b_bits, n, r_bits);

            // Verify a and b unchanged.
            uint32_t got_a = read_reg(sc.sv(), a_base, n, orkan_n);
            uint32_t got_b = read_reg(sc.sv(), b_base, n, orkan_n);
            if (got_a != a_val) {
                std::fprintf(stderr,
                    "FAIL mod_dsl truth table: a=%u b=%u: a changed to %u\n",
                    a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != b_val) {
                std::fprintf(stderr,
                    "FAIL mod_dsl truth table: a=%u b=%u: b changed to %u\n",
                    a_val, b_val, got_b);
                assert(false);
            }

            // Verify remainder.
            uint32_t got_r  = read_reg(sc.sv(), r_base, n, orkan_n);
            uint32_t exp_r  = a_val % b_val;

            if (got_r != exp_r) {
                std::fprintf(stderr,
                    "FAIL mod_dsl truth table: a=%u b=%u: remainder expected=%u got=%u\n",
                    a_val, b_val, exp_r, got_r);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_mod_dsl_truth_table (%u cases)\n", pass_count);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M17 DSL-style MOD tests:\n");
    test_mod_dsl_truth_table();
    std::printf("All M17 mod_dsl tests passed.\n");
    return 0;
}
