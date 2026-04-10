// test_mul_dsl.cpp — M16 (PRD v3): DSL-style multiplication tests.
//
// Tests:
//   test_mul_dsl_truth_table — exhaustive 2-bit multiplication.
//                              a * b for all a,b in [0,3]. Verify correct result.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/mul_dsl.hpp"
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

// ── test_mul_dsl_truth_table ──────────────────────────────────────────────────
// Exhaustive 2-bit multiplication: a * b for all a,b in [0,3].
// Product is 4 bits (2*2).
//
// Qubit layout:
//   q[0..1]  = a (2 bits, LSB=0)
//   q[2..3]  = b (2 bits, LSB=2)
//   q[4..7]  = product (4 bits, all start |0>)
//   Total: 8 register qubits
//   + ancilla for lib_add_dsl internally (carries, etc.)
//
// We use 17 Orkan qubits max (STURM cap). 8 register + headroom for ancilla.
// For n=2, lib_mul_dsl does 2 controlled-adds each needing: 1 carry_anc (idx 8+).
// Total: 8 + a few ancilla = well within 17.

static void test_mul_dsl_truth_table() {
    // Reset pool so ancilla indices are fresh.
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n         = 2u;  // 2-bit operands
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = n;             // 2
    const uint32_t prod_base = 2u * n;        // 4
    const uint32_t n_reg     = prod_base + 2u * n;  // 4+4=8 register qubits

    // Pre-reserve q0..q7 so pool ancilla starts at q8.
    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 0; b_val < (1u << n); ++b_val) {
            // We need enough Orkan qubits for register + ancilla.
            // n=2: lib_mul_dsl calls lib_add_dsl n times under WHEN, each
            // needs 1 carry ancilla. The c_AND fold for WHEN(b[i]) adds another
            // ancilla when depth>1. Total ancilla ~2-4 on top of register.
            // Use 14 Orkan qubits to be safe (within 17-qubit cap).
            SimCtx sc{14u, 64u};

            // Initialize a and b.
            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays.
            sturm::qbool a_bits[2];
            sturm::qbool b_bits[2];
            sturm::qbool prod_bits[4];

            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i]    = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_bits[i]    = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
            }
            for (uint32_t i = 0; i < 2u * n; ++i) {
                prod_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(prod_base + i));
            }

            // Run DSL multiplier: product = a * b.
            sturm::lib_mul_dsl(a_bits, n, b_bits, n, prod_bits, 2u * n);

            // Verify a and b unchanged.
            uint32_t got_a = read_reg_sim(sc.sv(), a_base, n, 14u);
            uint32_t got_b = read_reg_sim(sc.sv(), b_base, n, 14u);
            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL mul_dsl truth table: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != b_val) {
                std::fprintf(stderr, "FAIL mul_dsl truth table: a=%u b=%u: b changed to %u\n",
                             a_val, b_val, got_b);
                assert(false);
            }

            // Verify product.
            uint32_t got_prod    = read_reg_sim(sc.sv(), prod_base, 2u * n, 14u);
            uint32_t expected    = a_val * b_val;
            if (got_prod != expected) {
                std::fprintf(stderr,
                    "FAIL mul_dsl truth table: a=%u b=%u: product expected=%u got=%u\n",
                    a_val, b_val, expected, got_prod);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_mul_dsl_truth_table (%u cases)\n", pass_count);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M16 DSL-style MUL tests:\n");
    test_mul_dsl_truth_table();
    std::printf("All M16 mul_dsl tests passed.\n");
    return 0;
}
