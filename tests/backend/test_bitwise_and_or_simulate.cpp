// test_bitwise_and_or_simulate.cpp — Test: statevector simulation for bitwise
// AND and OR (both free and compound-assign forms).
//   (sturm-9wz)
//
// Tests SIMULATE mode for all 4 bitwise AND/OR forms:
//   a &= b   — out-of-place AND compound-assign
//   a |= b   — out-of-place OR compound-assign
//   c = a & b — free AND operator
//   c = a | b — free OR operator
//
// For compound-assign forms (&= and |=):
//   1. Classical value is correct after the operation.
//   2. gate_count > 0 in SIMULATE mode (circuit actually ran).
//   3. Statevector shows result register holds the expected value.
//   4. The 'b' (RHS) register is UNCHANGED.
//
// For free-operator forms (& and |):
//   1. Classical value is correct.
//   2. gate_count > 0 in SIMULATE mode (circuit ran).
//   3. Statevector shows result register holds the expected value.
//   4. The input operands (a, b) are UNCHANGED.
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1
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

// ── SIMULATE context helper ────────────────────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx && "sturm_backend_create failed");
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

// ── Read a W-qubit register from statevector (contiguous base) ────────────────

static uint32_t read_reg(orkan::state_t& sv, uint32_t base_q, uint32_t n,
                          uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                val |= (static_cast<uint32_t>((s >> (base_q + k)) & 1u) << k);
            }
            return val;
        }
    }
    return 0u;
}

// ── Read a register via explicit qubit index array (for out-of-place ops) ─────

static uint32_t read_reg_idxs(orkan::state_t& sv, const int* qidx, uint32_t n,
                               uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                if (qidx[k] >= 0) {
                    val |= (static_cast<uint32_t>((s >> qidx[k]) & 1u) << k);
                }
            }
            return val;
        }
    }
    return 0u;
}

// =============================================================================
// test_and_assign_simulate
// a &= b: a=5 (0101), b=6 (0110), W=4 → a must hold 4 (0100) after, b unchanged.
//
// &= is out-of-place: allocates W fresh result qubits, computes
// result[i] ^= (a[i] & b[i]) per bit (Toffoli), then moves result into a.
//
// Qubit layout:
//   q[0..3]  = a (W=4, a_base=0)
//   q[4..7]  = b (W=4, b_base=4)
//   q[8..11] = result register (allocated by operator&=)
//   q[12+]   = ancilla headroom
// Orkan size: 16 qubits.
// =============================================================================

static void test_and_assign_simulate() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;         // 4
    static constexpr uint32_t n_reg  = 2u * W;    // 8
    static constexpr uint32_t n_orkan = 16u;

    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve 2*W register qubits so pool ancilla gets indices >= n_reg.
    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise statevector: a=5 (0101), b=6 (0110).
    for (uint32_t i = 0; i < W; ++i) {
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((6u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 5;
        a.super_mask = (1u << W) - 1u;
        b.value      = 6;
        b.super_mask = (1u << W) - 1u;

        a &= b;

        // 1. Classical value.
        assert(a.value == 4LL && "a &= b: classical value must be 5&6=4");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a &= b must emit gates in SIMULATE mode");

        // 3. Statevector: a.qubits[] (remapped to result register) must hold 4.
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        assert(a_sv == 4u &&
               "a &= b: statevector must show a holds 5&6=4");

        // 4. b is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 6u &&
               "a &= b: b must be unchanged in statevector");

        // Prevent double-release (reserved[] owns original a and b qubits;
        // a.qubits[] now points to the result register which will be released
        // by QubitPool when a goes out of scope — but since we filled -1 below,
        // we release them manually first through the qubits array).
        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_and_assign_simulate (5&6=4)\n");
}

// =============================================================================
// test_or_assign_simulate
// a |= b: a=5 (0101), b=6 (0110), W=4 → a must hold 7 (0111) after, b unchanged.
//
// |= is out-of-place: allocates W fresh result qubits, computes
// result[i] ^= (a[i] | b[i]) per bit via BitProxy's operator| / ^=
// (2 CX + Toffoli), then moves result into a.
//
// Qubit layout:
//   q[0..3]  = a (W=4, a_base=0)
//   q[4..7]  = b (W=4, b_base=4)
//   q[8..11] = result register (allocated by operator|=)
//   q[12+]   = ancilla headroom
// Orkan size: 16 qubits.
// =============================================================================

static void test_or_assign_simulate() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;         // 4
    static constexpr uint32_t n_reg  = 2u * W;    // 8
    static constexpr uint32_t n_orkan = 16u;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise statevector: a=5 (0101), b=6 (0110).
    for (uint32_t i = 0; i < W; ++i) {
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((6u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 5;
        a.super_mask = (1u << W) - 1u;
        b.value      = 6;
        b.super_mask = (1u << W) - 1u;

        a |= b;

        // 1. Classical value.
        assert(a.value == 7LL && "a |= b: classical value must be 5|6=7");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a |= b must emit gates in SIMULATE mode");

        // 3. Statevector: a.qubits[] (remapped to result register) must hold 7.
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        assert(a_sv == 7u &&
               "a |= b: statevector must show a holds 5|6=7");

        // 4. b is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 6u &&
               "a |= b: b must be unchanged in statevector");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_or_assign_simulate (5|6=7)\n");
}

// =============================================================================
// test_free_and_simulate
// c = a & b: a=5 (0101), b=6 (0110), W=4 → c must hold 4 (0100).
//
// Free operator& allocates W fresh result qubits and emits per-bit Toffoli
// gates: result[i] ^= (a[i] & b[i]).
// =============================================================================

static void test_free_and_simulate() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;         // 4
    static constexpr uint32_t n_reg  = 2u * W;    // 8
    static constexpr uint32_t n_orkan = 16u;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise statevector: a=5 (0101), b=6 (0110).
    for (uint32_t i = 0; i < W; ++i) {
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((6u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 5;
        a.super_mask = (1u << W) - 1u;
        b.value      = 6;
        b.super_mask = (1u << W) - 1u;

        sturm::qint_t<W> c = a & b;

        // 1. Classical value.
        assert(c.value == 4LL &&
               "c = a & b: classical value must be 5&6=4");

        // 2. Gate emission.
        assert(sc.ctx->gate_count > gates_before &&
               "c = a & b must emit gates in SIMULATE mode");

        // 3. Statevector: c register must hold 4.
        uint32_t c_sv = read_reg_idxs(sc.sv(), c.qubits.data(), W, n_orkan);
        assert(c_sv == 4u &&
               "c = a & b: statevector must show c holds 5&6=4");

        // 4. a and b are unchanged.
        uint32_t a_sv = read_reg(sc.sv(), a_base, W, n_orkan);
        assert(a_sv == 5u &&
               "c = a & b: a must be unchanged in statevector");
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 6u &&
               "c = a & b: b must be unchanged in statevector");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
        c.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_free_and_simulate (5&6=4)\n");
}

// =============================================================================
// test_free_or_simulate
// c = a | b: a=5 (0101), b=6 (0110), W=4 → c must hold 7 (0111).
//
// Free operator| allocates W fresh result qubits and emits per-bit OR
// gates: result[i] ^= (a[i] | b[i]) via BitProxy's operator| / ^=
// (2 CX + Toffoli per bit).
// =============================================================================

static void test_free_or_simulate() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;         // 4
    static constexpr uint32_t n_reg  = 2u * W;    // 8
    static constexpr uint32_t n_orkan = 16u;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise statevector: a=5 (0101), b=6 (0110).
    for (uint32_t i = 0; i < W; ++i) {
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((6u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 5;
        a.super_mask = (1u << W) - 1u;
        b.value      = 6;
        b.super_mask = (1u << W) - 1u;

        sturm::qint_t<W> c = a | b;

        // 1. Classical value.
        assert(c.value == 7LL &&
               "c = a | b: classical value must be 5|6=7");

        // 2. Gate emission.
        assert(sc.ctx->gate_count > gates_before &&
               "c = a | b must emit gates in SIMULATE mode");

        // 3. Statevector: c register must hold 7.
        uint32_t c_sv = read_reg_idxs(sc.sv(), c.qubits.data(), W, n_orkan);
        assert(c_sv == 7u &&
               "c = a | b: statevector must show c holds 5|6=7");

        // 4. a and b are unchanged.
        uint32_t a_sv = read_reg(sc.sv(), a_base, W, n_orkan);
        assert(a_sv == 5u &&
               "c = a | b: a must be unchanged in statevector");
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 6u &&
               "c = a | b: b must be unchanged in statevector");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
        c.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_free_or_simulate (5|6=7)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_bitwise_and_or_simulate: SIMULATE mode tests for bitwise "
                "AND/OR (&=, |=, &, |)\n\n");

    test_and_assign_simulate();
    test_or_assign_simulate();
    test_free_and_simulate();
    test_free_or_simulate();

    std::printf("\nAll test_bitwise_and_or_simulate tests passed.\n");
    return 0;
}
