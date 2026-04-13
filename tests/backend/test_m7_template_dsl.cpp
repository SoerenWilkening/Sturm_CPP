// test_m7_template_dsl.cpp -- M7: Template mul/div/mod DSL tests.
//
// Verifies that lib_mul_dsl, lib_div_dsl, lib_mod_dsl work when
// instantiated with Bit=BitProxy (not just qbool).
// Also verifies backward compatibility: existing qbool call sites still work
// after the template conversion.
//
// Tests:
//   test_mul_bitproxy_truth_table  -- 2-bit mul via BitProxy (all 16 pairs)
//   test_div_bitproxy_truth_table  -- 2-bit div via BitProxy (b!=0, 12 cases)
//   test_mod_bitproxy_truth_table  -- 2-bit mod via BitProxy (b!=0, 12 cases)
//   test_mul_qbool_still_works     -- qbool call site backward compatibility
//   test_div_qbool_still_works     -- qbool call site backward compatibility
//   test_mod_qbool_still_works     -- qbool call site backward compatibility
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"
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

// -- SimCtx: SIMULATE mode with OrkanBridge -----------------------------------

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

// -- Read helpers -------------------------------------------------------------

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

// -- test_mul_bitproxy_truth_table --------------------------------------------
// Exhaustive 2-bit multiplication using BitProxy arrays.
//
// Qubit layout (same as test_mul_dsl.cpp):
//   q[0..1]  = a (2 bits)
//   q[2..3]  = b (2 bits)
//   q[4..7]  = product (4 bits, start |0>)
//   Total: 8 register qubits + ancilla headroom.

static void test_mul_bitproxy_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n         = 2u;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = n;             // 2
    const uint32_t prod_base = 2u * n;        // 4
    const uint32_t n_reg     = prod_base + 2u * n;  // 8

    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 0; b_val < (1u << n); ++b_val) {
            SimCtx sc{14u, 64u};

            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays, then wrap as BitProxy.
            sturm::qbool a_qbools[2], b_qbools[2], prod_qbools[4];
            for (uint32_t i = 0; i < n; ++i) {
                a_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
            }
            for (uint32_t i = 0; i < 2u * n; ++i) {
                prod_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(prod_base + i));
            }

            sturm::BitProxy a_bits[2], b_bits[2], prod_bits[4];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::BitProxy(a_qbools[i]);
                b_bits[i] = sturm::BitProxy(b_qbools[i]);
            }
            for (uint32_t i = 0; i < 2u * n; ++i) {
                prod_bits[i] = sturm::BitProxy(prod_qbools[i]);
            }

            // Run DSL multiplier with BitProxy: product = a * b.
            sturm::lib_mul_dsl(a_bits, n, b_bits, n, prod_bits, 2u * n);

            uint32_t got_a    = read_reg(sc.sv(), a_base, n, 14u);
            uint32_t got_b    = read_reg(sc.sv(), b_base, n, 14u);
            uint32_t got_prod = read_reg(sc.sv(), prod_base, 2u * n, 14u);
            uint32_t expected = a_val * b_val;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL mul bitproxy: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != b_val) {
                std::fprintf(stderr, "FAIL mul bitproxy: a=%u b=%u: b changed to %u\n",
                             a_val, b_val, got_b);
                assert(false);
            }
            if (got_prod != expected) {
                std::fprintf(stderr, "FAIL mul bitproxy: a=%u b=%u: product expected=%u got=%u\n",
                             a_val, b_val, expected, got_prod);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_mul_bitproxy_truth_table (%u cases)\n", pass_count);
}

// -- test_div_bitproxy_truth_table --------------------------------------------
// Exhaustive 2-bit division using BitProxy arrays (b != 0).
//
// Qubit layout:
//   q[0..1]  = dividend/a (2 bits)
//   q[2..3]  = divisor/b  (2 bits)
//   q[4..5]  = quotient   (2 bits, start |0>)
//   q[6..7]  = remainder  (2 bits, start |0>)
//   Total: 8 register qubits + ancilla headroom.

static void test_div_bitproxy_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n      = 2u;
    const uint32_t a_base = 0u;
    const uint32_t b_base = n;        // 2
    const uint32_t q_base = 2u * n;   // 4
    const uint32_t r_base = 3u * n;   // 6
    const uint32_t n_reg  = 4u * n;   // 8

    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 1; b_val < (1u << n); ++b_val) {  // b != 0
            SimCtx sc{17u, 64u};

            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays, then wrap as BitProxy.
            sturm::qbool a_qbools[2], b_qbools[2], q_qbools[2], r_qbools[2];
            for (uint32_t i = 0; i < n; ++i) {
                a_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                q_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(q_base + i));
                r_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(r_base + i));
            }

            sturm::BitProxy a_bits[2], b_bits[2], q_bits[2], r_bits[2];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::BitProxy(a_qbools[i]);
                b_bits[i] = sturm::BitProxy(b_qbools[i]);
                q_bits[i] = sturm::BitProxy(q_qbools[i]);
                r_bits[i] = sturm::BitProxy(r_qbools[i]);
            }

            // Run DSL divider with BitProxy: quotient = a / b, remainder = a % b.
            sturm::lib_div_dsl(a_bits, n, b_bits, n, q_bits, r_bits);

            uint32_t got_a = read_reg(sc.sv(), a_base, n, 17u);
            uint32_t got_b = read_reg(sc.sv(), b_base, n, 17u);
            uint32_t got_q = read_reg(sc.sv(), q_base, n, 17u);
            uint32_t got_r = read_reg(sc.sv(), r_base, n, 17u);
            uint32_t exp_q = a_val / b_val;
            uint32_t exp_r = a_val % b_val;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL div bitproxy: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != b_val) {
                std::fprintf(stderr, "FAIL div bitproxy: a=%u b=%u: b changed to %u\n",
                             a_val, b_val, got_b);
                assert(false);
            }
            if (got_q != exp_q) {
                std::fprintf(stderr, "FAIL div bitproxy: a=%u b=%u: quotient expected=%u got=%u\n",
                             a_val, b_val, exp_q, got_q);
                assert(false);
            }
            if (got_r != exp_r) {
                std::fprintf(stderr, "FAIL div bitproxy: a=%u b=%u: remainder expected=%u got=%u\n",
                             a_val, b_val, exp_r, got_r);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_div_bitproxy_truth_table (%u cases)\n", pass_count);
}

// -- test_mod_bitproxy_truth_table --------------------------------------------
// Exhaustive 2-bit modulo using BitProxy arrays (b != 0).
//
// Qubit layout:
//   q[0..1]  = dividend/a (2 bits)
//   q[2..3]  = divisor/b  (2 bits)
//   q[4..5]  = remainder  (2 bits, start |0>)
//   Total: 6 register qubits + ancilla headroom.

static void test_mod_bitproxy_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n       = 2u;
    const uint32_t a_base  = 0u;
    const uint32_t b_base  = n;        // 2
    const uint32_t r_base  = 2u * n;   // 4
    const uint32_t n_reg   = 3u * n;   // 6
    const uint32_t orkan_n = 17u;

    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < (1u << n); ++a_val) {
        for (uint32_t b_val = 1; b_val < (1u << n); ++b_val) {  // b != 0
            SimCtx sc{orkan_n, 64u};

            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays, then wrap as BitProxy.
            sturm::qbool a_qbools[2], b_qbools[2], r_qbools[2];
            for (uint32_t i = 0; i < n; ++i) {
                a_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                r_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(r_base + i));
            }

            sturm::BitProxy a_bits[2], b_bits[2], r_bits[2];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::BitProxy(a_qbools[i]);
                b_bits[i] = sturm::BitProxy(b_qbools[i]);
                r_bits[i] = sturm::BitProxy(r_qbools[i]);
            }

            // Run DSL mod with BitProxy: remainder = a % b.
            sturm::lib_mod_dsl(a_bits, n, b_bits, n, r_bits);

            uint32_t got_a = read_reg(sc.sv(), a_base, n, orkan_n);
            uint32_t got_b = read_reg(sc.sv(), b_base, n, orkan_n);
            uint32_t got_r = read_reg(sc.sv(), r_base, n, orkan_n);
            uint32_t exp_r = a_val % b_val;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL mod bitproxy: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != b_val) {
                std::fprintf(stderr, "FAIL mod bitproxy: a=%u b=%u: b changed to %u\n",
                             a_val, b_val, got_b);
                assert(false);
            }
            if (got_r != exp_r) {
                std::fprintf(stderr, "FAIL mod bitproxy: a=%u b=%u: remainder expected=%u got=%u\n",
                             a_val, b_val, exp_r, got_r);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_mod_bitproxy_truth_table (%u cases)\n", pass_count);
}

// -- test_mul_qbool_still_works -----------------------------------------------
// Verify backward compatibility: qbool call sites still compile and produce
// correct results after the template conversion. Spot-check: 3 * 2 = 6.

static void test_mul_qbool_still_works() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n         = 2u;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = n;
    const uint32_t prod_base = 2u * n;
    const uint32_t n_reg     = prod_base + 2u * n;  // 8

    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t a_val = 3u, b_val = 2u;
    SimCtx sc{14u, 64u};

    for (uint32_t i = 0; i < n; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    sturm::qbool a_bits[2], b_bits[2], prod_bits[4];
    for (uint32_t i = 0; i < n; ++i) {
        a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
        b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
    }
    for (uint32_t i = 0; i < 2u * n; ++i) {
        prod_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(prod_base + i));
    }

    // This call must still compile with qbool (Bit = qbool deduced).
    sturm::lib_mul_dsl(a_bits, n, b_bits, n, prod_bits, 2u * n);

    uint32_t got_prod = read_reg(sc.sv(), prod_base, 2u * n, 14u);
    assert(got_prod == (a_val * b_val));

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_mul_qbool_still_works\n");
}

// -- test_div_qbool_still_works -----------------------------------------------
// Verify backward compatibility: qbool call sites. Spot-check: 3 / 2 = 1 r 1.

static void test_div_qbool_still_works() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n      = 2u;
    const uint32_t a_base = 0u;
    const uint32_t b_base = n;
    const uint32_t q_base = 2u * n;
    const uint32_t r_base = 3u * n;
    const uint32_t n_reg  = 4u * n;

    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t a_val = 3u, b_val = 2u;
    SimCtx sc{17u, 64u};

    for (uint32_t i = 0; i < n; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    sturm::qbool a_bits[2], b_bits[2], q_bits[2], r_bits[2];
    for (uint32_t i = 0; i < n; ++i) {
        a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
        b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
        q_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(q_base + i));
        r_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(r_base + i));
    }

    // This call must still compile with qbool (Bit = qbool deduced).
    sturm::lib_div_dsl(a_bits, n, b_bits, n, q_bits, r_bits);

    uint32_t got_q = read_reg(sc.sv(), q_base, n, 17u);
    uint32_t got_r = read_reg(sc.sv(), r_base, n, 17u);
    assert(got_q == (a_val / b_val));
    assert(got_r == (a_val % b_val));

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_div_qbool_still_works\n");
}

// -- test_mod_qbool_still_works -----------------------------------------------
// Verify backward compatibility: qbool call sites. Spot-check: 3 % 2 = 1.

static void test_mod_qbool_still_works() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n       = 2u;
    const uint32_t a_base  = 0u;
    const uint32_t b_base  = n;
    const uint32_t r_base  = 2u * n;
    const uint32_t n_reg   = 3u * n;
    const uint32_t orkan_n = 17u;

    std::vector<int> reserved(n_reg);
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    uint32_t a_val = 3u, b_val = 2u;
    SimCtx sc{orkan_n, 64u};

    for (uint32_t i = 0; i < n; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    sturm::qbool a_bits[2], b_bits[2], r_bits[2];
    for (uint32_t i = 0; i < n; ++i) {
        a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
        b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
        r_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(r_base + i));
    }

    // This call must still compile with qbool (Bit = qbool deduced).
    sturm::lib_mod_dsl(a_bits, n, b_bits, n, r_bits);

    uint32_t got_r = read_reg(sc.sv(), r_base, n, orkan_n);
    assert(got_r == (a_val % b_val));

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_mod_qbool_still_works\n");
}

// -- main ---------------------------------------------------------------------

int main() {
    std::printf("M7 Template mul/div/mod DSL tests:\n");
    test_mul_bitproxy_truth_table();
    test_div_bitproxy_truth_table();
    test_mod_bitproxy_truth_table();
    test_mul_qbool_still_works();
    test_div_qbool_still_works();
    test_mod_qbool_still_works();
    std::printf("All M7 template mul/div/mod DSL tests passed.\n");
    return 0;
}
