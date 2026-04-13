// test_adder_dsl_template.cpp — M6: Template adder DSL tests.
//
// Verifies that maj_dsl, uma_dsl, lib_add_dsl, lib_sub_dsl work when
// instantiated with Bit=BitProxy (not just qbool).
// Also verifies backward compatibility: existing qbool call sites still work
// after the template conversion.
//
// Tests:
//   test_add_bitproxy_truth_table  — 3-bit addition via BitProxy (all 64 pairs)
//   test_sub_bitproxy_truth_table  — 3-bit subtraction via BitProxy
//   test_add_qbool_still_works     — qbool call site backward compatibility
//   test_sub_qbool_still_works     — qbool call site backward compatibility
//   test_maj_uma_bitproxy          — direct MAJ/UMA with BitProxy
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/lib/adder_dsl.hpp"
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

// ── Scoped context helper ─────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 32u) {
        ctx  = sturm_backend_create(mode, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::BackendContext& bc() { return *ctx; }
};

// ── SimCtx: context with OrkanBridge for SIMULATE mode ───────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 32u) {
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

// ── Amplitude helper ──────────────────────────────────────────────────────────

static uint32_t read_qubit_sim(orkan::state_t& sv, uint32_t q, uint32_t n_qubits) {
    uint64_t dim  = uint64_t{1} << n_qubits;
    uint64_t mask = uint64_t{1} << q;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

static uint32_t read_reg_sim(orkan::state_t& sv, uint32_t first_qubit, uint32_t n,
                              uint32_t n_qubits_total) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        val |= (read_qubit_sim(sv, first_qubit + i, n_qubits_total) << i);
    }
    return val;
}

// ── test_add_bitproxy_truth_table ────────────────────────────────────────────
// Exhaustive 3-bit addition using BitProxy arrays.

static void test_add_bitproxy_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n           = 3u;
    const uint32_t a_base      = 0u;
    const uint32_t b_base      = n;
    const uint32_t carry_qubit = 2u * n;
    const uint32_t n_qubits    = 2u * n + 1u;  // 7
    uint32_t pass_count = 0;

    int reserved[7];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    for (uint32_t a_val = 0; a_val < 8u; ++a_val) {
        for (uint32_t b_val = 0; b_val < 8u; ++b_val) {
            SimCtx sc{n_qubits + 1u, 32u};

            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
            }
            for (uint32_t i = 0; i < n; ++i) {
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create qbool arrays, then wrap as BitProxy.
            sturm::qbool a_qbools[3];
            sturm::qbool b_qbools[3];
            for (uint32_t i = 0; i < n; ++i) {
                a_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
            }
            sturm::qbool carry_qbool = sturm::qbool::make_non_owning(static_cast<int>(carry_qubit));

            // Wrap as BitProxy arrays.
            sturm::BitProxy a_bits[3];
            sturm::BitProxy b_bits[3];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::BitProxy(a_qbools[i]);
                b_bits[i] = sturm::BitProxy(b_qbools[i]);
            }
            sturm::BitProxy carry(carry_qbool);

            // Run the DSL adder with BitProxy: b += a
            sturm::lib_add_dsl(a_bits, b_bits, carry, n);

            uint32_t got_a     = read_reg_sim(sc.sv(), a_base, n, n_qubits + 1u);
            uint32_t got_b     = read_reg_sim(sc.sv(), b_base, n, n_qubits + 1u);
            uint32_t got_carry = read_qubit_sim(sc.sv(), carry_qubit, n_qubits + 1u);

            uint32_t expected_sum   = (a_val + b_val) & 0x7u;
            uint32_t expected_carry = (a_val + b_val) >> 3u;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL add bitproxy truth table: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != expected_sum) {
                std::fprintf(stderr, "FAIL add bitproxy truth table: a=%u b=%u: sum expected=%u got=%u\n",
                             a_val, b_val, expected_sum, got_b);
                assert(false);
            }
            if (got_carry != expected_carry) {
                std::fprintf(stderr, "FAIL add bitproxy truth table: a=%u b=%u: carry expected=%u got=%u\n",
                             a_val, b_val, expected_carry, got_carry);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_add_bitproxy_truth_table (%u cases)\n", pass_count);
}

// ── test_sub_bitproxy_truth_table ────────────────────────────────────────────
// Exhaustive 3-bit subtraction using BitProxy arrays.

static void test_sub_bitproxy_truth_table() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n             = 3u;
    const uint32_t a_base        = 0u;
    const uint32_t b_base        = n;
    const uint32_t borrow_qubit  = 2u * n;
    const uint32_t n_qubits      = 2u * n + 1u;
    uint32_t pass_count = 0;

    int reserved[7];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    for (uint32_t a_val = 0; a_val < 8u; ++a_val) {
        for (uint32_t b_val = 0; b_val < 8u; ++b_val) {
            SimCtx sc{n_qubits + 2u, 32u};

            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
            }
            for (uint32_t i = 0; i < n; ++i) {
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            sturm::qbool a_qbools[3];
            sturm::qbool b_qbools[3];
            for (uint32_t i = 0; i < n; ++i) {
                a_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_qbools[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
            }
            sturm::qbool borrow_qbool = sturm::qbool::make_non_owning(static_cast<int>(borrow_qubit));

            sturm::BitProxy a_bits[3];
            sturm::BitProxy b_bits[3];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::BitProxy(a_qbools[i]);
                b_bits[i] = sturm::BitProxy(b_qbools[i]);
            }
            sturm::BitProxy borrow(borrow_qbool);

            sturm::lib_sub_dsl(a_bits, b_bits, borrow, n);

            uint32_t got_a      = read_reg_sim(sc.sv(), a_base, n, n_qubits + 2u);
            uint32_t got_b      = read_reg_sim(sc.sv(), b_base, n, n_qubits + 2u);
            uint32_t got_borrow = read_qubit_sim(sc.sv(), borrow_qubit, n_qubits + 2u);

            uint32_t expected_diff   = (b_val - a_val) & 0x7u;
            uint32_t expected_borrow = (b_val < a_val) ? 1u : 0u;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL sub bitproxy truth table: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != expected_diff) {
                std::fprintf(stderr, "FAIL sub bitproxy truth table: a=%u b=%u: diff expected=%u got=%u\n",
                             a_val, b_val, expected_diff, got_b);
                assert(false);
            }
            if (got_borrow != expected_borrow) {
                std::fprintf(stderr, "FAIL sub bitproxy truth table: a=%u b=%u: borrow expected=%u got=%u\n",
                             a_val, b_val, expected_borrow, got_borrow);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_sub_bitproxy_truth_table (%u cases)\n", pass_count);
}

// ── test_add_qbool_still_works ───────────────────────────────────────────────
// Verify backward compatibility: qbool call sites still compile and produce
// correct results after the template conversion.

static void test_add_qbool_still_works() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n = 2u;
    const uint32_t n_qubits = 2u * n + 1u;  // 5

    int reserved[5];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    // Test a=3, b=2: sum=1 (mod 4), carry=1
    SimCtx sc{n_qubits + 1u, 32u};

    uint32_t a_val = 3u;
    uint32_t b_val = 2u;

    for (uint32_t i = 0; i < n; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), i);
    }
    for (uint32_t i = 0; i < n; ++i) {
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), n + i);
    }

    sturm::qbool a_bits[2];
    sturm::qbool b_bits[2];
    for (uint32_t i = 0; i < n; ++i) {
        a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
        b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
    }
    sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(2u * n));

    // This call must still compile with qbool (Bit = qbool deduced).
    sturm::lib_add_dsl(a_bits, b_bits, carry, n);

    uint32_t got_b     = read_reg_sim(sc.sv(), n, n, n_qubits + 1u);
    uint32_t got_carry = read_qubit_sim(sc.sv(), 2u * n, n_qubits + 1u);

    assert(got_b == ((a_val + b_val) & 0x3u));
    assert(got_carry == ((a_val + b_val) >> 2u));

    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_add_qbool_still_works\n");
}

// ── test_sub_qbool_still_works ───────────────────────────────────────────────

static void test_sub_qbool_still_works() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n = 2u;
    const uint32_t n_qubits = 2u * n + 1u;

    int reserved[5];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    // Test a=1, b=3: diff=2, borrow=0
    SimCtx sc{n_qubits + 2u, 32u};

    uint32_t a_val = 1u;
    uint32_t b_val = 3u;

    for (uint32_t i = 0; i < n; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), i);
    }
    for (uint32_t i = 0; i < n; ++i) {
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), n + i);
    }

    sturm::qbool a_bits[2];
    sturm::qbool b_bits[2];
    for (uint32_t i = 0; i < n; ++i) {
        a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
        b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
    }
    sturm::qbool borrow = sturm::qbool::make_non_owning(static_cast<int>(2u * n));

    sturm::lib_sub_dsl(a_bits, b_bits, borrow, n);

    uint32_t got_b      = read_reg_sim(sc.sv(), n, n, n_qubits + 2u);
    uint32_t got_borrow = read_qubit_sim(sc.sv(), 2u * n, n_qubits + 2u);

    assert(got_b == ((b_val - a_val) & 0x3u));
    assert(got_borrow == ((b_val < a_val) ? 1u : 0u));

    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_sub_qbool_still_works\n");
}

// ── test_maj_uma_bitproxy ────────────────────────────────────────────────────
// Verify maj_dsl and uma_dsl compile and work with BitProxy directly.
// MAJ followed by UMA should restore original values.

static void test_maj_uma_bitproxy() {
    sturm::QubitPool::instance().reset_for_testing();

    // 3 qubits: a=0, b=1, c=2
    int res[3];
    for (int i = 0; i < 3; ++i) {
        res[i] = sturm::QubitPool::instance().allocate();
    }

    // Test: a=1, b=0, c=1 -> MAJ -> UMA should restore a=1, b=0, c=1
    SimCtx sc{3u, 32u};
    orkan::apply_x(sc.sv(), 0u);  // a=1
    // b=0
    orkan::apply_x(sc.sv(), 2u);  // c=1

    sturm::qbool aq = sturm::qbool::make_non_owning(0);
    sturm::qbool bq = sturm::qbool::make_non_owning(1);
    sturm::qbool cq = sturm::qbool::make_non_owning(2);

    sturm::BitProxy a(aq);
    sturm::BitProxy b(bq);
    sturm::BitProxy c(cq);

    // MAJ then UMA should restore original values.
    sturm::maj_dsl(a, b, c);
    sturm::uma_dsl(a, b, c);

    uint32_t got_a = read_qubit_sim(sc.sv(), 0u, 3u);
    uint32_t got_b = read_qubit_sim(sc.sv(), 1u, 3u);
    uint32_t got_c = read_qubit_sim(sc.sv(), 2u, 3u);

    assert(got_a == 1u);
    assert(got_b == 0u);
    assert(got_c == 1u);

    for (int i = 0; i < 3; ++i) {
        sturm::QubitPool::instance().release(res[i]);
    }

    std::printf("  PASS: test_maj_uma_bitproxy\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M6 Template adder DSL tests:\n");
    test_maj_uma_bitproxy();
    test_add_bitproxy_truth_table();
    test_sub_bitproxy_truth_table();
    test_add_qbool_still_works();
    test_sub_qbool_still_works();
    std::printf("All M6 template adder_dsl tests passed.\n");
    return 0;
}
