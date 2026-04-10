// test_div_dsl.cpp — M16 (PRD v3): DSL-style division tests.
//
// Tests:
//   test_div_dsl_truth_table — exhaustive 2-bit division (quotient + remainder).
//                              a / b and a % b for valid combinations (b != 0).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/div_dsl.hpp"
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
using cx = std::complex<double>;

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

// ── Read n-qubit register starting at first_qubit ────────────────────────────

static uint32_t read_reg_sim(orkan::state_t& sv, uint32_t first_qubit, uint32_t n,
                              uint32_t n_qubits_total) {
    uint64_t dim = uint64_t{1} << n_qubits_total;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                val |= (static_cast<uint32_t>((i >> (first_qubit + k)) & 1u) << k);
            }
            return val;
        }
    }
    return 0u;
}

// ── test_div_dsl_truth_table ──────────────────────────────────────────────────
// Exhaustive 2-bit division for valid divisors (b in [1,3]).
//
// Qubit layout:
//   q[0..1]  = dividend/a (2 bits, LSB=0)
//   q[2..3]  = divisor/b  (2 bits, LSB=2)
//   q[4..5]  = quotient   (2 bits, all start |0>)
//   q[6..7]  = remainder  (2 bits, all start |0>)
//   Total: 8 register qubits
//   + ancilla from QubitPool for adder carries, signs, overflow
//
// Budget check (n=2): 4*2 data + ancilla.
// lib_div_dsl requires scratch(n) + sgn(n-1) + overflow(1) + carry_anc(1) = 2n+2 = 6.
// Total: 8 + 6 = 14. Well within 17.

static void test_div_dsl_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n          = 2u;
    const uint32_t a_base     = 0u;          // dividend
    const uint32_t b_base     = n;           // divisor (2)
    const uint32_t q_base     = 2u * n;      // quotient (4)
    const uint32_t r_base     = 3u * n;      // remainder (6)
    const uint32_t n_reg      = 4u * n;      // 8 register qubits

    // Pre-reserve q0..q7.
    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 1; b_val < (1u << n); ++b_val) {  // b != 0
            // Use 17 Orkan qubits (the cap) for headroom.
            SimCtx sc{17u, 64u};

            // Initialize a (dividend) and b (divisor).
            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays.
            sturm::qbool a_bits[2];
            sturm::qbool b_bits[2];
            sturm::qbool q_bits[2];
            sturm::qbool r_bits[2];

            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                q_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(q_base + i));
                r_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(r_base + i));
            }

            // Run DSL divider: quotient = a / b, remainder = a % b.
            sturm::lib_div_dsl(a_bits, n, b_bits, n, q_bits, r_bits);

            // Verify a and b unchanged.
            uint32_t got_a = read_reg_sim(sc.sv(), a_base, n, 17u);
            uint32_t got_b = read_reg_sim(sc.sv(), b_base, n, 17u);
            if (got_a != a_val) {
                std::fprintf(stderr,
                    "FAIL div_dsl truth table: a=%u b=%u: a changed to %u\n",
                    a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != b_val) {
                std::fprintf(stderr,
                    "FAIL div_dsl truth table: a=%u b=%u: b changed to %u\n",
                    a_val, b_val, got_b);
                assert(false);
            }

            // Verify quotient and remainder.
            uint32_t got_q    = read_reg_sim(sc.sv(), q_base, n, 17u);
            uint32_t got_r    = read_reg_sim(sc.sv(), r_base, n, 17u);
            uint32_t exp_q    = a_val / b_val;
            uint32_t exp_r    = a_val % b_val;

            if (got_q != exp_q) {
                std::fprintf(stderr,
                    "FAIL div_dsl truth table: a=%u b=%u: quotient expected=%u got=%u\n",
                    a_val, b_val, exp_q, got_q);
                assert(false);
            }
            if (got_r != exp_r) {
                std::fprintf(stderr,
                    "FAIL div_dsl truth table: a=%u b=%u: remainder expected=%u got=%u\n",
                    a_val, b_val, exp_r, got_r);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_div_dsl_truth_table (%u cases)\n", pass_count);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M16 DSL-style DIV tests:\n");
    test_div_dsl_truth_table();
    std::printf("All M16 div_dsl tests passed.\n");
    return 0;
}
