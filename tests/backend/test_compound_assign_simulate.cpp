// test_compound_assign_simulate.cpp — Test: simulation correctness for
// compound-assign operators (+=, -=, *=, /=, %=).
//   (sturm-dxr)
//
// For each of the 5 compound-assign operators:
//   1. Verify classical value is correct after the operation.
//   2. Verify gate_count > 0 in SIMULATE mode (circuit actually ran).
//   3. Read the statevector and verify the result register holds the expected
//      value (arithmetic correctness via statevector readout).
//   4. Verify the 'b' (RHS) register is UNCHANGED.
//
// Concrete values:
//   a += b : a=3, b=5  →  a=8  (W=4 to avoid overflow)
//   a -= b : a=7, b=3  →  a=4  (W=3)
//   a *= b : a=2, b=3  →  a=6  (W=3, product fits in 3 bits)
//   a /= b : a=3, b=2  →  a=1  (W=2, peak 13 qubits ≤ kMaxQubits=17)
//   a %= b : a=3, b=2  →  a=1  (W=2, peak 15 qubits ≤ kMaxQubits=17)
//
// These tests SHOULD PASS on current code — compound-assigns are wired to the
// DSL library (operator+=, -=, *=, /=, %= in qint_arith_v3.hpp).
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

// ── Read an n-qubit register from the statevector (contiguous base) ───────────

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

// ── Read a register via explicit qubit index array (for ops that remap qubits) ─

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
// test_compound_add_assign_simulate
// a += b: a=3, b=5, W=4 → a must hold 8 after, b unchanged.
//
// Qubit layout:
//   q[0..3]  = a (W=4)
//   q[4..7]  = b (W=4)
//   q[8]     = carry_out ancilla (allocated/released by operator+=)
//   q[9+]    = further ancilla headroom
// Orkan size: 17 qubits.
// =============================================================================

static void test_compound_add_assign_simulate() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;           // 4
    static constexpr uint32_t n_reg  = 2u * W;      // 8
    static constexpr uint32_t n_orkan = 17u;

    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve 2*W register qubits so pool ancilla gets indices >= n_reg.
    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise statevector: a=3 (0011), b=5 (0101).
    for (uint32_t i = 0; i < W; ++i) {
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 3;
        a.super_mask = (1u << W) - 1u;
        b.value      = 5;
        b.super_mask = (1u << W) - 1u;

        a += b;

        // 1. Classical value.
        assert(a.value == 8LL && "a += b: classical value must be 3+5=8");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a += b must emit gates in SIMULATE mode");

        // 3. Statevector: a register must hold 8.
        uint32_t a_sv = read_reg(sc.sv(), a_base, W, n_orkan);
        assert(a_sv == 8u &&
               "a += b: statevector must show a holds 3+5=8");

        // 4. b is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 5u &&
               "a += b: b must be unchanged in statevector");

        // Prevent double-release (reserved[] owns these qubits).
        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_compound_add_assign_simulate (3+5=8)\n");
}

// =============================================================================
// test_compound_sub_assign_simulate
// a -= b: a=7, b=3, W=3 → a must hold 4 after, b unchanged.
//
// Qubit layout:
//   q[0..2]  = a (W=3)
//   q[3..5]  = b (W=3)
//   q[6]     = borrow ancilla
//   q[7+]    = headroom
// Orkan size: 17 qubits.
// =============================================================================

static void test_compound_sub_assign_simulate() {
    static constexpr std::size_t W = 3u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;
    static constexpr uint32_t n_reg  = 2u * W;
    static constexpr uint32_t n_orkan = 17u;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan, 128u};

    // Initialise: a=7 (111), b=3 (011).
    for (uint32_t i = 0; i < W; ++i) {
        if ((7u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 7;
        a.super_mask = (1u << W) - 1u;
        b.value      = 3;
        b.super_mask = (1u << W) - 1u;

        a -= b;

        // 1. Classical value.
        assert(a.value == 4LL && "a -= b: classical value must be 7-3=4");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a -= b must emit gates in SIMULATE mode");

        // 3. Statevector: a register must hold 4.
        uint32_t a_sv = read_reg(sc.sv(), a_base, W, n_orkan);
        assert(a_sv == 4u &&
               "a -= b: statevector must show a holds 7-3=4");

        // 4. b is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 3u &&
               "a -= b: b must be unchanged in statevector");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_compound_sub_assign_simulate (7-3=4)\n");
}

// =============================================================================
// test_compound_mul_assign_simulate
// a *= b: a=2, b=3, W=3 → a must hold 6 after (2*3=6 fits in 3 bits).
//
// operator*= allocates a 2*W=6 result register (fresh, indices 6..11),
// computes lib_mul_dsl, then moves result qubits into a.qubits[].
// After *= , a.qubits[] points to the result register (new qubit indices).
//
// Qubit budget:
//   Register (a+b): 2*W=6 qubits (indices 0..5)
//   Result register: 2*W=6 qubits (indices 6..11)
//   carry_anc: 1 qubit (index 12)
//   CCX ancilla: 1 qubit (index 13)
//   Peak: 14 qubits — within Orkan size of 16.
// =============================================================================

static void test_compound_mul_assign_simulate() {
    static constexpr std::size_t W = 3u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;
    static constexpr uint32_t n_reg  = 2u * W;      // 6
    static constexpr uint32_t n_orkan = 16u;         // 14 peak, 16 for headroom

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan, 128u};

    // Initialise: a=2 (010), b=3 (011).
    for (uint32_t i = 0; i < W; ++i) {
        if ((2u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 2;
        a.super_mask = (1u << W) - 1u;
        b.value      = 3;
        b.super_mask = (1u << W) - 1u;

        a *= b;

        // 1. Classical value.
        assert(a.value == 6LL && "a *= b: classical value must be 2*3=6");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a *= b must emit gates in SIMULATE mode");

        // 3. Statevector: a.qubits[] (remapped to result register) must hold 6.
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        assert(a_sv == 6u &&
               "a *= b: statevector must show a holds 2*3=6");

        // 4. b register (qubits b_base..b_base+W-1) is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 3u &&
               "a *= b: b must be unchanged in statevector");

        // Prevent double-release.
        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_compound_mul_assign_simulate (2*3=6)\n");
}

// =============================================================================
// test_compound_div_assign_simulate
// a /= b: a=3, b=2, W=2 → a must hold 1 after (3/2=1).
//
// operator/= allocates quotient (W=2) and remainder (W=2) fresh registers,
// calls lib_div_dsl, moves quotient qubits into a.qubits[], releases remainder.
//
// Qubit budget (W=2, pre-reserved a+b=4 qubits):
//   Register (a+b):    2*W=4 qubits  (indices 0..3)
//   Quotient register: W=2  qubits   (indices 4..5)
//   Remainder register:W=2  qubits   (indices 6..7)
//   lib_div_dsl ancilla: scratch(2)+sgn(1)+overflow(1)+carry_anc(1) = 5 (8..12)
//   Peak: 13 qubits — within kMaxQubits (17).
// =============================================================================

static void test_compound_div_assign_simulate() {
    static constexpr std::size_t W = 2u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;
    static constexpr uint32_t n_reg  = 2u * W;      // 4
    static constexpr uint32_t n_orkan = 17u;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise: a=3 (11), b=2 (10).
    for (uint32_t i = 0; i < W; ++i) {
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((2u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 3;
        a.super_mask = (1u << W) - 1u;
        b.value      = 2;
        b.super_mask = (1u << W) - 1u;

        a /= b;

        // 1. Classical value.
        assert(a.value == 1LL && "a /= b: classical value must be 3/2=1");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a /= b must emit gates in SIMULATE mode");

        // 3. Statevector: a.qubits[] (remapped to quotient register) must hold 1.
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        assert(a_sv == 1u &&
               "a /= b: statevector must show a holds 3/2=1");

        // 4. b register is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 2u &&
               "a /= b: b must be unchanged in statevector");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_compound_div_assign_simulate (3/2=1)\n");
}

// =============================================================================
// test_compound_mod_assign_simulate
// a %= b: a=3, b=2, W=2 → a must hold 1 after (3%2=1).
//
// operator%= allocates a fresh remainder register (W=2), calls lib_mod_dsl,
// moves remainder qubits into a.qubits[].
//
// Qubit budget (W=2, pre-reserved a+b=4 qubits):
//   Register (a+b):         2*W=4 qubits  (indices 0..3)
//   Remainder register:     W=2   qubits   (index 4..5, allocated by %=)
//   lib_mod_dsl quotient:   W=2   qubits   (indices 6..7)
//   lib_div_dsl call 1:     scratch(2)+sgn(1)+overflow(1)+carry_anc(1)=5 (8..12)  peak=13
//   After div1 release: free 8..12
//   lib_mod_dsl temp_rem:   2 qubits (8..9, reused)
//   lib_div_dsl call 2:     scratch(2)+sgn(1)+overflow(1)+carry_anc(1)=5 (10..14) peak=15
//   Max high-water mark: 15 qubits — within kMaxQubits (17).
// =============================================================================

static void test_compound_mod_assign_simulate() {
    static constexpr std::size_t W = 2u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;
    static constexpr uint32_t n_reg  = 2u * W;      // 4
    static constexpr uint32_t n_orkan = 17u;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{n_orkan, 128u};

    // Initialise: a=3 (11), b=2 (10).
    for (uint32_t i = 0; i < W; ++i) {
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((2u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value      = 3;
        a.super_mask = (1u << W) - 1u;
        b.value      = 2;
        b.super_mask = (1u << W) - 1u;

        a %= b;

        // 1. Classical value.
        assert(a.value == 1LL && "a %= b: classical value must be 3%2=1");

        // 2. Gate count.
        assert(sc.ctx->gate_count > gates_before &&
               "a %= b must emit gates in SIMULATE mode");

        // 3. Statevector: a.qubits[] (remapped to remainder register) must hold 1.
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        assert(a_sv == 1u &&
               "a %= b: statevector must show a holds 3%2=1");

        // 4. b register is unchanged.
        uint32_t b_sv = read_reg(sc.sv(), b_base, W, n_orkan);
        assert(b_sv == 2u &&
               "a %= b: b must be unchanged in statevector");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_compound_mod_assign_simulate (3%%2=1)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_compound_assign_simulate: SIMULATE mode correctness for "
                "compound-assign operators (+=, -=, *=, /=, %%=)\n\n");

    test_compound_add_assign_simulate();
    test_compound_sub_assign_simulate();
    test_compound_mul_assign_simulate();
    test_compound_div_assign_simulate();
    test_compound_mod_assign_simulate();

    std::printf("\nAll test_compound_assign_simulate tests passed.\n");
    return 0;
}
