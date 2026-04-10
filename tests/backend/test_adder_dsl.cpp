// test_adder_dsl.cpp — M14 (PRD v3): DSL-style Cuccaro adder/subtractor tests.
//
// Tests:
//   test_add_dsl_truth_table     — exhaustive 3-bit addition (all 64 input pairs)
//                                  verified via SIMULATE mode with OrkanBridge.
//   test_sub_dsl_truth_table     — exhaustive 3-bit subtraction.
//   test_add_dsl_gate_count      — verify gate count matches 6n+1 Cuccaro cost.
//   test_add_dsl_under_when      — b += a inside WHEN(ctrl); gates are lifted.
//   test_add_dsl_no_when_variant — same function works inside and outside WHEN,
//                                  but produces different gate counts due to lifting.
//
// Harness: plain assert + printf (no gtest).

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

    // n_qubits in orkan; max_qubits in pool
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

static void assert_close(cx got, cx expected, const char* label) {
    double err = std::abs(got - expected);
    if (err > kTol) {
        std::fprintf(stderr, "FAIL %s: got (%g,%g) expected (%g,%g) err=%g\n",
                     label, got.real(), got.imag(),
                     expected.real(), expected.imag(), err);
        assert(false);
    }
}

// Read qubit q's classical value (0 or 1) from a pure computational-basis state.
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

// Read an n-qubit register starting at qubit first_qubit (LSB = first_qubit).
static uint32_t read_reg_sim(orkan::state_t& sv, uint32_t first_qubit, uint32_t n,
                              uint32_t n_qubits_total) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        val |= (read_qubit_sim(sv, first_qubit + i, n_qubits_total) << i);
    }
    return val;
}

// ── test_add_dsl_truth_table ──────────────────────────────────────────────────
// Exhaustive 3-bit addition: all 64 (a,b) pairs.
//
// Qubit layout:
//   q[0..2]  = a (3 bits, LSB=0)
//   q[3..5]  = b (3 bits, LSB=3)
//   q[6]     = carry_out (must start |0>)
//   Total: 7 qubits (+ QubitPool for adder ancilla at idx 7+)

static void test_add_dsl_truth_table() {
    // Reset the QubitPool singleton so high_water starts at 0 for this test.
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n           = 3u;
    const uint32_t a_base      = 0u;
    const uint32_t b_base      = n;
    const uint32_t carry_qubit = 2u * n;
    const uint32_t n_qubits    = 2u * n + 1u;  // 7
    uint32_t pass_count = 0;

    // Pre-reserve qubits 0..6 in the pool so adder ancilla gets indices 7+.
    // (We allocate them all at once; since pool is fresh it gives 0,1,...,6.)
    int reserved[7];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    for (uint32_t a_val = 0; a_val < 8u; ++a_val) {
        for (uint32_t b_val = 0; b_val < 8u; ++b_val) {
            // Allocate enough Orkan qubits: 7 register + 1 adder carry_anc
            SimCtx sc{n_qubits + 1u, 32u};

            // Initialize state: load a_val into q[0..2], b_val into q[3..5].
            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
            }
            for (uint32_t i = 0; i < n; ++i) {
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create non-owning qbool arrays aliasing the register qubits.
            sturm::qbool a_bits[3];
            sturm::qbool b_bits[3];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
            }
            sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(carry_qubit));

            // Run the DSL adder: b += a
            // The adder allocates carry_anc from pool, which will be index 7.
            sturm::lib_add_dsl(a_bits, b_bits, carry, n);

            // Verify results.
            uint32_t got_a     = read_reg_sim(sc.sv(), a_base, n, n_qubits + 1u);
            uint32_t got_b     = read_reg_sim(sc.sv(), b_base, n, n_qubits + 1u);
            uint32_t got_carry = read_qubit_sim(sc.sv(), carry_qubit, n_qubits + 1u);

            uint32_t expected_sum   = (a_val + b_val) & 0x7u;
            uint32_t expected_carry = (a_val + b_val) >> 3u;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL add dsl truth table: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != expected_sum) {
                std::fprintf(stderr, "FAIL add dsl truth table: a=%u b=%u: sum expected=%u got=%u\n",
                             a_val, b_val, expected_sum, got_b);
                assert(false);
            }
            if (got_carry != expected_carry) {
                std::fprintf(stderr, "FAIL add dsl truth table: a=%u b=%u: carry expected=%u got=%u\n",
                             a_val, b_val, expected_carry, got_carry);
                assert(false);
            }
            ++pass_count;
        }
    }

    // Release the reserved qubits.
    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_add_dsl_truth_table (%u cases)\n", pass_count);
}

// ── test_sub_dsl_truth_table ──────────────────────────────────────────────────
// Exhaustive 3-bit subtraction: all 64 (a,b) pairs.
// b -= a; result = (b - a) mod 8; borrow = 1 iff b < a.
//
// Qubit layout same as add:
//   q[0..2]  = a, q[3..5] = b, q[6] = borrow_out

static void test_sub_dsl_truth_table() {
    // Reset and pre-reserve register qubits 0..6 to avoid pool collisions.
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n             = 3u;
    const uint32_t a_base        = 0u;
    const uint32_t b_base        = n;
    const uint32_t borrow_qubit  = 2u * n;
    const uint32_t n_qubits      = 2u * n + 1u;  // 7
    uint32_t pass_count = 0;

    // Pre-reserve qubits 0..6.
    int reserved[7];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    for (uint32_t a_val = 0; a_val < 8u; ++a_val) {
        for (uint32_t b_val = 0; b_val < 8u; ++b_val) {
            // Subtractor needs n (3) extra ancilla: 1 carry + n flips in a + restore.
            // Actually only 1 carry ancilla is borrowed. Use n_qubits + 2 Orkan qubits.
            SimCtx sc{n_qubits + 2u, 32u};

            // Initialize state.
            for (uint32_t i = 0; i < n; ++i) {
                if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
            }
            for (uint32_t i = 0; i < n; ++i) {
                if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
            }

            // Create non-owning qbool arrays.
            sturm::qbool a_bits[3];
            sturm::qbool b_bits[3];
            for (uint32_t i = 0; i < n; ++i) {
                a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
            }
            sturm::qbool borrow = sturm::qbool::make_non_owning(static_cast<int>(borrow_qubit));

            // Run DSL subtractor: b -= a
            sturm::lib_sub_dsl(a_bits, b_bits, borrow, n);

            // Verify results.
            uint32_t got_a      = read_reg_sim(sc.sv(), a_base, n, n_qubits + 2u);
            uint32_t got_b      = read_reg_sim(sc.sv(), b_base, n, n_qubits + 2u);
            uint32_t got_borrow = read_qubit_sim(sc.sv(), borrow_qubit, n_qubits + 2u);

            uint32_t expected_diff   = (b_val - a_val) & 0x7u;
            uint32_t expected_borrow = (b_val < a_val) ? 1u : 0u;

            if (got_a != a_val) {
                std::fprintf(stderr, "FAIL sub dsl truth table: a=%u b=%u: a changed to %u\n",
                             a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != expected_diff) {
                std::fprintf(stderr, "FAIL sub dsl truth table: a=%u b=%u: diff expected=%u got=%u\n",
                             a_val, b_val, expected_diff, got_b);
                assert(false);
            }
            if (got_borrow != expected_borrow) {
                std::fprintf(stderr, "FAIL sub dsl truth table: a=%u b=%u: borrow expected=%u got=%u\n",
                             a_val, b_val, expected_borrow, got_borrow);
                assert(false);
            }
            ++pass_count;
        }
    }

    // Release reserved qubits.
    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_sub_dsl_truth_table (%u cases)\n", pass_count);
}

// ── test_add_dsl_gate_count ───────────────────────────────────────────────────
// Verify gate count for n-bit addition matches 6n+1.
//
// For n=1: 6*1+1 = 7
// For n=2: 6*2+1 = 13
// For n=3: 6*3+1 = 19
// For n=4: 6*4+1 = 25

static void test_add_dsl_gate_count() {
    for (uint32_t n = 1u; n <= 4u; ++n) {
        // Reset pool so ancilla indices don't collide with register qubits.
        sturm::QubitPool::instance().reset_for_testing();

        ScopedCtx sc{STURM_MODE_COUNT_ONLY, 32u};

        // Pre-reserve qubits 0..(2n) so pool hands out 2n+1 for carry ancilla.
        const uint32_t n_reg = 2u * n + 1u;
        std::vector<int> reserved(n_reg);
        for (uint32_t i = 0; i < n_reg; ++i) {
            reserved[i] = sturm::QubitPool::instance().allocate();
        }

        // Allocate dummy qubit indices for a, b, carry.
        sturm::qbool a_bits[4];
        sturm::qbool b_bits[4];
        for (uint32_t i = 0; i < n; ++i) {
            a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
        }
        sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(2u * n));

        uint64_t before = sc.ctx->gate_count;
        sturm::lib_add_dsl(a_bits, b_bits, carry, n);
        uint64_t after = sc.ctx->gate_count;

        for (uint32_t i = 0; i < n_reg; ++i) {
            sturm::QubitPool::instance().release(reserved[i]);
        }

        uint64_t actual   = after - before;
        uint64_t expected = 6u * n + 1u;

        if (actual != expected) {
            std::fprintf(stderr, "FAIL gate_count: n=%u expected=%llu got=%llu\n",
                         n, (unsigned long long)expected, (unsigned long long)actual);
            assert(false);
        }
    }
    std::printf("  PASS: test_add_dsl_gate_count (n=1..4, 6n+1 each)\n");
}

// ── test_add_dsl_under_when ───────────────────────────────────────────────────
// Verify that b += a under WHEN(ctrl) produces lifted gates.
// With 1 control: CX -> CCX (+2 gates each), CCX -> 3 gates (c_AND fold).
// The gate count should be > the uncontrolled count (6n+1).

static void test_add_dsl_under_when() {
    const uint32_t n     = 2u;
    const uint32_t n_reg = 2u * n + 1u;  // qubits 0..4

    // Count gates without WHEN
    uint64_t uncontrolled_count = 0u;
    {
        sturm::QubitPool::instance().reset_for_testing();
        ScopedCtx sc{STURM_MODE_COUNT_ONLY, 32u};

        // Pre-reserve register qubits.
        std::vector<int> res(n_reg);
        for (uint32_t i = 0; i < n_reg; ++i) res[i] = sturm::QubitPool::instance().allocate();

        sturm::qbool a_bits[2];
        sturm::qbool b_bits[2];
        for (uint32_t i = 0; i < n; ++i) {
            a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
        }
        sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(2u * n));
        sturm::lib_add_dsl(a_bits, b_bits, carry, n);
        uncontrolled_count = sc.ctx->gate_count;

        for (uint32_t i = 0; i < n_reg; ++i) sturm::QubitPool::instance().release(res[i]);
    }

    // Count gates with WHEN(ctrl)
    uint64_t controlled_count = 0u;
    {
        sturm::QubitPool::instance().reset_for_testing();
        // ctrl = qubit 2n+1, a = [0..n-1], b = [n..2n-1], carry = 2n
        const uint32_t ctrl_qubit = n_reg;  // qubit after register: index 5
        const uint32_t n_total    = n_reg + 1u;  // including ctrl

        ScopedCtx sc{STURM_MODE_COUNT_ONLY, 32u};

        // Pre-reserve register qubits (including ctrl).
        std::vector<int> res(n_total);
        for (uint32_t i = 0; i < n_total; ++i) res[i] = sturm::QubitPool::instance().allocate();

        sturm::qbool a_bits[2];
        sturm::qbool b_bits[2];
        for (uint32_t i = 0; i < n; ++i) {
            a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
        }
        sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(2u * n));

        // Push control onto the stack (simulate WHEN(ctrl)).
        sc.bc().control_stack.push_control(ctrl_qubit);
        sturm::lib_add_dsl(a_bits, b_bits, carry, n);
        sc.bc().control_stack.pop_control();

        controlled_count = sc.ctx->gate_count;
        for (uint32_t i = 0; i < n_total; ++i) sturm::QubitPool::instance().release(res[i]);
    }

    // Under WHEN: every gate is lifted, so controlled count > uncontrolled count.
    if (controlled_count <= uncontrolled_count) {
        std::fprintf(stderr, "FAIL test_add_dsl_under_when: controlled=%llu <= uncontrolled=%llu\n",
                     (unsigned long long)controlled_count,
                     (unsigned long long)uncontrolled_count);
        assert(false);
    }
    std::printf("  PASS: test_add_dsl_under_when: uncontrolled=%llu controlled=%llu (lifted)\n",
                (unsigned long long)uncontrolled_count,
                (unsigned long long)controlled_count);
}

// ── test_add_dsl_no_when_variant ──────────────────────────────────────────────
// Confirm the same function works both inside and outside WHEN.
// Outside WHEN: gate count == 6n+1.
// Inside WHEN: gate count > 6n+1 (lifting expands gates).
// There is no separate _when variant — the same function is used.

static void test_add_dsl_no_when_variant() {
    const uint32_t n        = 3u;
    const uint64_t expected = 6u * n + 1u;  // 19
    const uint32_t n_reg    = 2u * n + 1u;  // qubits 0..6

    // Outside WHEN.
    uint64_t count_no_when = 0u;
    {
        sturm::QubitPool::instance().reset_for_testing();
        ScopedCtx sc{STURM_MODE_COUNT_ONLY, 32u};

        std::vector<int> res(n_reg);
        for (uint32_t i = 0; i < n_reg; ++i) res[i] = sturm::QubitPool::instance().allocate();

        sturm::qbool a_bits[3];
        sturm::qbool b_bits[3];
        for (uint32_t i = 0; i < n; ++i) {
            a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
        }
        sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(2u * n));
        sturm::lib_add_dsl(a_bits, b_bits, carry, n);
        count_no_when = sc.ctx->gate_count;

        for (uint32_t i = 0; i < n_reg; ++i) sturm::QubitPool::instance().release(res[i]);
    }

    // Inside WHEN.
    uint64_t count_with_when = 0u;
    {
        sturm::QubitPool::instance().reset_for_testing();
        const uint32_t ctrl_qubit = n_reg;  // qubit 7
        const uint32_t n_total    = n_reg + 1u;

        ScopedCtx sc{STURM_MODE_COUNT_ONLY, 32u};

        std::vector<int> res(n_total);
        for (uint32_t i = 0; i < n_total; ++i) res[i] = sturm::QubitPool::instance().allocate();

        sturm::qbool a_bits[3];
        sturm::qbool b_bits[3];
        for (uint32_t i = 0; i < n; ++i) {
            a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(n + i));
        }
        sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(2u * n));

        sc.bc().control_stack.push_control(ctrl_qubit);
        sturm::lib_add_dsl(a_bits, b_bits, carry, n);
        sc.bc().control_stack.pop_control();
        count_with_when = sc.ctx->gate_count;

        for (uint32_t i = 0; i < n_total; ++i) sturm::QubitPool::instance().release(res[i]);
    }

    assert(count_no_when == expected && "uncontrolled must be 6n+1");
    assert(count_with_when > expected && "controlled must be > 6n+1 due to lifting");
    std::printf("  PASS: test_add_dsl_no_when_variant: no-when=%llu when=%llu\n",
                (unsigned long long)count_no_when,
                (unsigned long long)count_with_when);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M14 DSL-style Cuccaro adder/subtractor tests:\n");
    test_add_dsl_truth_table();
    test_sub_dsl_truth_table();
    test_add_dsl_gate_count();
    test_add_dsl_under_when();
    test_add_dsl_no_when_variant();
    std::printf("All M14 adder_dsl tests passed.\n");
    return 0;
}
