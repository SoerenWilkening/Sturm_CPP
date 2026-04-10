// test_compare_dsl_truth_tables.cpp — M17 (PRD v3): DSL-style compare tests.
//
// Tests:
//   test_compare_dsl_truth_tables — exhaustive 3-bit comparison truth tables
//     for all 6 operators (EQ, LT, LE, GT, GE, NE) over all pairs (a,b) in [0,7].
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/compare_dsl.hpp"
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

// ── Read single qubit value from pure basis state ─────────────────────────────

static uint32_t read_qubit(orkan::state_t& sv, uint32_t q, uint32_t n_qubits) {
    uint64_t dim = uint64_t{1} << n_qubits;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

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

// ── test_compare_dsl_truth_tables ─────────────────────────────────────────────
//
// Exhaustive 3-bit comparison truth tables for EQ, LT, LE, GT, GE, NE.
// For each pair (a, b) where a,b in [0,7], test all 6 operators.
//
// Qubit layout (per test):
//   q[0..2]  = a (3 bits, LSB=0)
//   q[3..5]  = b (3 bits, LSB=3)
//   q[6]     = result (starts |0>)
//   Total: 7 register qubits.
//   Ancilla from QubitPool at q7+.
//
// Budget estimate (n=3):
//   lib_eq_dsl: 3 XNOR ancilla + lib_c_n_AND_dsl(3) = 3 + 1 = 4 ancilla.
//   lib_lt_dsl: 3 a_copy + 3 b_copy + 1 borrow + carries = ~10 ancilla.
//   lib_le_dsl: lt_anc + eq_anc + lib_or_dsl (~1 ancilla for OR) = ~12 ancilla.
//   Use 25 Orkan qubits total (7 register + 18 ancilla headroom).

static void test_compare_dsl_truth_tables() {
    const uint32_t n       = 3u;
    const uint32_t a_base  = 0u;
    const uint32_t b_base  = n;        // 3
    const uint32_t res_q   = 2u * n;   // 6
    const uint32_t n_reg   = 2u * n + 1u;  // 7
    const uint32_t orkan_n = 17u;  // 7 register + up to 10 ancilla (lib_le peak)

    // Pre-reserve register qubits so pool ancilla starts after them.
    sturm::QubitPool::instance().reset_for_testing();
    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 0; b_val < (1u << n); ++b_val) {

            uint32_t exp_eq = (a_val == b_val) ? 1u : 0u;
            uint32_t exp_lt = (a_val <  b_val) ? 1u : 0u;
            uint32_t exp_le = (a_val <= b_val) ? 1u : 0u;
            uint32_t exp_gt = (a_val >  b_val) ? 1u : 0u;
            uint32_t exp_ge = (a_val >= b_val) ? 1u : 0u;
            uint32_t exp_ne = (a_val != b_val) ? 1u : 0u;

            // ── EQ ────────────────────────────────────────────────────────────
            {
                SimCtx sc{orkan_n, 64u};
                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }
                sturm::qbool a_bits[3], b_bits[3];
                sturm::qbool result = sturm::qbool::make_non_owning(static_cast<int>(res_q));
                for (uint32_t i = 0; i < n; ++i) {
                    a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                }
                sturm::lib_eq_dsl(a_bits, b_bits, n, result);
                uint32_t got = read_qubit(sc.sv(), res_q, orkan_n);
                if (got != exp_eq) {
                    std::fprintf(stderr, "FAIL EQ a=%u b=%u: expected=%u got=%u\n",
                                 a_val, b_val, exp_eq, got);
                    assert(false);
                }
                // Verify a and b unchanged.
                uint32_t got_a = read_reg(sc.sv(), a_base, n, orkan_n);
                uint32_t got_b = read_reg(sc.sv(), b_base, n, orkan_n);
                if (got_a != a_val || got_b != b_val) {
                    std::fprintf(stderr, "FAIL EQ a=%u b=%u: inputs changed got_a=%u got_b=%u\n",
                                 a_val, b_val, got_a, got_b);
                    assert(false);
                }
                ++pass_count;
            }

            // ── LT ────────────────────────────────────────────────────────────
            {
                SimCtx sc{orkan_n, 64u};
                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }
                sturm::qbool a_bits[3], b_bits[3];
                sturm::qbool result = sturm::qbool::make_non_owning(static_cast<int>(res_q));
                for (uint32_t i = 0; i < n; ++i) {
                    a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                }
                sturm::lib_lt_dsl(a_bits, b_bits, n, result);
                uint32_t got = read_qubit(sc.sv(), res_q, orkan_n);
                if (got != exp_lt) {
                    std::fprintf(stderr, "FAIL LT a=%u b=%u: expected=%u got=%u\n",
                                 a_val, b_val, exp_lt, got);
                    assert(false);
                }
                ++pass_count;
            }

            // ── LE ────────────────────────────────────────────────────────────
            {
                SimCtx sc{orkan_n, 64u};
                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }
                sturm::qbool a_bits[3], b_bits[3];
                sturm::qbool result = sturm::qbool::make_non_owning(static_cast<int>(res_q));
                for (uint32_t i = 0; i < n; ++i) {
                    a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                }
                sturm::lib_le_dsl(a_bits, b_bits, n, result);
                uint32_t got = read_qubit(sc.sv(), res_q, orkan_n);
                if (got != exp_le) {
                    std::fprintf(stderr, "FAIL LE a=%u b=%u: expected=%u got=%u\n",
                                 a_val, b_val, exp_le, got);
                    assert(false);
                }
                ++pass_count;
            }

            // ── GT ────────────────────────────────────────────────────────────
            {
                SimCtx sc{orkan_n, 64u};
                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }
                sturm::qbool a_bits[3], b_bits[3];
                sturm::qbool result = sturm::qbool::make_non_owning(static_cast<int>(res_q));
                for (uint32_t i = 0; i < n; ++i) {
                    a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                }
                sturm::lib_gt_dsl(a_bits, b_bits, n, result);
                uint32_t got = read_qubit(sc.sv(), res_q, orkan_n);
                if (got != exp_gt) {
                    std::fprintf(stderr, "FAIL GT a=%u b=%u: expected=%u got=%u\n",
                                 a_val, b_val, exp_gt, got);
                    assert(false);
                }
                ++pass_count;
            }

            // ── GE ────────────────────────────────────────────────────────────
            {
                SimCtx sc{orkan_n, 64u};
                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }
                sturm::qbool a_bits[3], b_bits[3];
                sturm::qbool result = sturm::qbool::make_non_owning(static_cast<int>(res_q));
                for (uint32_t i = 0; i < n; ++i) {
                    a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                }
                sturm::lib_ge_dsl(a_bits, b_bits, n, result);
                uint32_t got = read_qubit(sc.sv(), res_q, orkan_n);
                if (got != exp_ge) {
                    std::fprintf(stderr, "FAIL GE a=%u b=%u: expected=%u got=%u\n",
                                 a_val, b_val, exp_ge, got);
                    assert(false);
                }
                ++pass_count;
            }

            // ── NE ────────────────────────────────────────────────────────────
            {
                SimCtx sc{orkan_n, 64u};
                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }
                sturm::qbool a_bits[3], b_bits[3];
                sturm::qbool result = sturm::qbool::make_non_owning(static_cast<int>(res_q));
                for (uint32_t i = 0; i < n; ++i) {
                    a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                }
                sturm::lib_ne_dsl(a_bits, b_bits, n, result);
                uint32_t got = read_qubit(sc.sv(), res_q, orkan_n);
                if (got != exp_ne) {
                    std::fprintf(stderr, "FAIL NE a=%u b=%u: expected=%u got=%u\n",
                                 a_val, b_val, exp_ne, got);
                    assert(false);
                }
                ++pass_count;
            }
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_compare_dsl_truth_tables (%u cases)\n", pass_count);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M17 DSL-style compare tests:\n");
    test_compare_dsl_truth_tables();
    std::printf("All M17 compare_dsl tests passed.\n");
    return 0;
}
