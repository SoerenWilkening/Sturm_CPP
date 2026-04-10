// test_free_op_simulate.cpp — Test: simulation correctness for free operators
//   (sturm-auu)
//
// Tests SIMULATE mode for all 5 free operators:
//   c = a + b   — classical result correct, gate_count > 0, statevector correct
//   c = a - b   — same
//   c = a * b   — same
//   c = a / b   — same
//   c = a % b   — same
//
// Each test also verifies that 'a' is UNCHANGED after the operation (Bennett
// discipline: free operators must not modify the input operands).
//
// EXPECTED TO FAIL on current code: the free-operator stubs in
// qint_arith_backend.hpp have TODO(backend) no-ops — they do not emit circuits
// or allocate fresh result registers.  This test documents what the correct
// behaviour looks like so that sturm-6qo (the fix) has a green target.
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
#include <algorithm>

static constexpr double kTol = 1e-9;

// ── SIMULATE context helper ────────────────────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
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

// ── COUNT_ONLY context helper ──────────────────────────────────────────────────

struct CountCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit CountCtx(uint32_t max_q = 128u) {
        ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY, max_q);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~CountCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── Read a W-qubit register value from the statevector ────────────────────────
// Returns the unsigned integer stored in bits [base_q .. base_q+W-1] of the
// computational basis state that has amplitude 1.  n_total is the total number
// of Orkan qubits allocated.

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

// Variant: read qubits via explicit qubit index array (for after *=,/=,%=
// which may remap qubits).  n_total is the Orkan statevector size.

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

// ── Qubit layout for all tests (W=3) ─────────────────────────────────────────
// q[0..2] = a register (a_base = 0)
// q[3..5] = b register (b_base = 3)
// q[6..8] = c result register (c_base = 6) — allocated fresh by the fixed op
// q[9..]  = ancilla (carry, CCX temp, etc.)
// Orkan size: 17 qubits (kMaxQubits).
//
// With the current STUB: c shares a's qubits (0..2), no fresh register.
// With the FIX (sturm-6qo): c gets fresh qubits (6..8) with the computed result.

static constexpr std::size_t W = 3u;
static constexpr uint32_t a_base = 0u;
static constexpr uint32_t b_base = W;        // 3
static constexpr uint32_t c_base = 2u * W;   // 6  (expected after fix)
static constexpr uint32_t n_orkan = 17u;     // kMaxQubits

// Pre-reserve register qubits so pool ancilla gets indices >= 2*W+W = 9.
// Returns the count reserved.
static int reserved_qubits[2u * W];  // a (0..2) + b (3..5)

static void reserve_ab() {
    sturm::QubitPool::instance().reset_for_testing();
    for (uint32_t i = 0; i < 2u * W; ++i) {
        reserved_qubits[i] = sturm::QubitPool::instance().allocate();
    }
    // Sanity: pool assigned indices 0..5 in order.
    assert(reserved_qubits[0] == 0 && "pool must start at index 0");
}

static void release_ab() {
    for (uint32_t i = 0; i < 2u * W; ++i) {
        if (reserved_qubits[i] >= 0) {
            sturm::QubitPool::instance().release(reserved_qubits[i]);
            reserved_qubits[i] = -1;
        }
    }
}

// ── Initialize statevector: a=a_val, b=b_val ──────────────────────────────────

static void init_sv(orkan::state_t& sv, uint32_t a_val, uint32_t b_val) {
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sv, a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sv, b_base + i);
    }
}

// ── Build a qint_t<W> aliasing pre-reserved qubits ───────────────────────────

static sturm::qint_t<W> make_q(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (1u << W) - 1u;  // all bits quantum
    for (uint32_t i = 0; i < W; ++i) {
        q.qubits[i] = static_cast<int>(base + i);
    }
    return q;
}

// ── Prevent double-release by zeroing qubit indices ───────────────────────────

template <std::size_t N>
static void clear_q(sturm::qint_t<N>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0u;
}

// =============================================================================
// test_free_add_classical
// Verify: c = a + b produces the correct classical value and c.value == a+b.
// This should PASS even with the stub (value is always set correctly).
// =============================================================================

static void test_free_add_classical() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(3LL, a_base);
        sturm::qint_t<W> b = make_q(5LL, b_base);

        sturm::qint_t<W> c = a + b;

        assert(c.value == 8LL && "c = a + b: classical value must be 3+5=8");

        clear_q(a);
        clear_q(b);
        clear_q(c);  // prevent double-release of stub's aliased qubits
    }

    release_ab();
    std::printf("  PASS: test_free_add_classical (c.value == 8)\n");
}

// =============================================================================
// test_free_sub_classical
// Verify: c = a - b produces the correct classical value.
// =============================================================================

static void test_free_sub_classical() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(7LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        sturm::qint_t<W> c = a - b;

        assert(c.value == 4LL && "c = a - b: classical value must be 7-3=4");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_sub_classical (c.value == 4)\n");
}

// =============================================================================
// test_free_mul_classical
// Verify: c = a * b produces the correct classical value.
// =============================================================================

static void test_free_mul_classical() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(2LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        sturm::qint_t<W> c = a * b;

        assert(c.value == 6LL && "c = a * b: classical value must be 2*3=6");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_mul_classical (c.value == 6)\n");
}

// =============================================================================
// test_free_div_classical
// Verify: c = a / b produces the correct classical value.
// =============================================================================

static void test_free_div_classical() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(6LL, a_base);
        sturm::qint_t<W> b = make_q(2LL, b_base);

        sturm::qint_t<W> c = a / b;

        assert(c.value == 3LL && "c = a / b: classical value must be 6/2=3");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_div_classical (c.value == 3)\n");
}

// =============================================================================
// test_free_mod_classical
// Verify: c = a % b produces the correct classical value.
// =============================================================================

static void test_free_mod_classical() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(7LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        sturm::qint_t<W> c = a % b;

        assert(c.value == 1LL && "c = a % b: classical value must be 7%3=1");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_mod_classical (c.value == 1)\n");
}

// =============================================================================
// test_free_add_gate_count
// Verify: c = a + b emits gates (gate_count > 0) in COUNT_ONLY mode.
// EXPECTED TO FAIL: stub has TODO(backend) and does not emit gates.
// =============================================================================

static void test_free_add_gate_count() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(3LL, a_base);
        sturm::qint_t<W> b = make_q(5LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a + b;
        uint64_t after = sc.ctx->gate_count;

        // EXPECT FAIL: stub does not emit gates.
        // After sturm-6qo fix: operator+(a,b) must call operator+=
        // internally, which goes through lib_add_dsl → gate emission.
        assert(after > before &&
               "c = a + b must emit gates (gate_count > 0)");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_add_gate_count (gates emitted)\n");
}

// =============================================================================
// test_free_sub_gate_count
// Verify: c = a - b emits gates in COUNT_ONLY mode.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_sub_gate_count() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(7LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a - b;
        uint64_t after = sc.ctx->gate_count;

        assert(after > before &&
               "c = a - b must emit gates (gate_count > 0)");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_sub_gate_count (gates emitted)\n");
}

// =============================================================================
// test_free_mul_gate_count
// Verify: c = a * b emits gates in COUNT_ONLY mode.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_mul_gate_count() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(2LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a * b;
        uint64_t after = sc.ctx->gate_count;

        assert(after > before &&
               "c = a * b must emit gates (gate_count > 0)");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_mul_gate_count (gates emitted)\n");
}

// =============================================================================
// test_free_div_gate_count
// Verify: c = a / b emits gates in COUNT_ONLY mode.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_div_gate_count() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(6LL, a_base);
        sturm::qint_t<W> b = make_q(2LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a / b;
        uint64_t after = sc.ctx->gate_count;

        assert(after > before &&
               "c = a / b must emit gates (gate_count > 0)");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_div_gate_count (gates emitted)\n");
}

// =============================================================================
// test_free_mod_gate_count
// Verify: c = a % b emits gates in COUNT_ONLY mode.
// EXPECTED TO FAIL: stub does not emit gates.
// =============================================================================

static void test_free_mod_gate_count() {
    reserve_ab();
    CountCtx sc;

    {
        sturm::qint_t<W> a = make_q(7LL, a_base);
        sturm::qint_t<W> b = make_q(3LL, b_base);

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> c = a % b;
        uint64_t after = sc.ctx->gate_count;

        assert(after > before &&
               "c = a % b must emit gates (gate_count > 0)");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_ab();
    std::printf("  PASS: test_free_mod_gate_count (gates emitted)\n");
}

// =============================================================================
// test_free_add_simulate
// Full statevector test: c = a + b.
//   a = 3 (011), b = 5 (101)  → c must hold 8 (1000 — but W=3 so c = 0 mod 8).
// Use W=4 to fit 3+5=8 without overflow.
//
// Qubit layout (W=4):
//   q[0..3] = a, q[4..7] = b, q[8..11] = c (fresh register after fix)
//   Total Orkan qubits: 17.
//
// After sturm-6qo fix:
//   - operator+(a,b) allocates q[8..11] fresh (all |0>)
//   - copies a's statevector state into q[8..11]
//   - calls c += b  → lib_add_dsl, produces correct sum
//   - a (q[0..3]) is UNCHANGED (still holds 3)
//   - c (q[8..11]) holds 3+5=8
//
// EXPECTED TO FAIL: current stub shares a's qubits with c, no gates emitted,
// statevector unchanged (c qubits == a qubits, still hold 3, not 8).
// =============================================================================

static void test_free_add_simulate() {
    // Use W=4 to avoid overflow: 3+5=8 needs 4 bits.
    static constexpr std::size_t W4 = 4u;
    static constexpr uint32_t a4_base = 0u;
    static constexpr uint32_t b4_base = W4;        // 4
    // After fix: c allocated at pool index 2*W4=8 onward.
    static constexpr uint32_t n_orkan4 = 17u;

    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve 2*W4 = 8 register qubits (a and b).
    int reserved4[2u * W4];
    for (uint32_t i = 0; i < 2u * W4; ++i) {
        reserved4[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved4[0] == 0 && "pool must start at index 0");

    SimCtx sc{n_orkan4, 128u};

    // Initialize statevector: a=3 (0011), b=5 (0101).
    for (uint32_t i = 0; i < W4; ++i) {
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), a4_base + i);
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), b4_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W4> a, b;
        for (uint32_t i = 0; i < W4; ++i) {
            a.qubits[i] = static_cast<int>(a4_base + i);
            b.qubits[i] = static_cast<int>(b4_base + i);
        }
        a.value      = 3;
        a.super_mask = (1u << W4) - 1u;
        b.value      = 5;
        b.super_mask = (1u << W4) - 1u;

        sturm::qint_t<W4> c = a + b;

        // 1. Classical result check (should PASS even with stub).
        assert(c.value == 8LL &&
               "c = a + b: classical value must be 3+5=8");

        // 2. Gate count check (EXPECTED TO FAIL with stub).
        uint64_t gates_after = sc.ctx->gate_count;
        assert(gates_after > gates_before &&
               "c = a + b must emit gates in SIMULATE mode");

        // 3. 'a' must be unchanged (Bennett discipline).
        uint32_t a_val_sv = read_reg(sc.sv(), a4_base, W4, n_orkan4);
        assert(a_val_sv == 3u &&
               "a must be unchanged after c = a + b (a's qubits must still hold 3)");

        // 4. Statevector correctness: c's qubits must hold 8.
        // After the fix, c.qubits will be the fresh result register.
        // With current stub, c.qubits == a.qubits (shared), and no circuit ran
        // so a's qubits still hold 3 — this will assert(8 == 3) and FAIL.
        int c_qidx[W4];
        for (uint32_t i = 0; i < W4; ++i) c_qidx[i] = c.qubits[i];
        uint32_t c_val_sv = read_reg_idxs(sc.sv(), c_qidx, W4, n_orkan4);
        assert(c_val_sv == 8u &&
               "c = a + b: statevector must show c holds 3+5=8");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < 2u * W4; ++i) {
        sturm::QubitPool::instance().release(reserved4[i]);
    }

    std::printf("  PASS: test_free_add_simulate (3+5=8 in statevector)\n");
}

// =============================================================================
// test_free_sub_simulate
// Full statevector test: c = a - b.
//   a = 7 (111), b = 3 (011) → c must hold 4 (100).
// W=3 suffices (7 > 3, result = 4 which fits in 3 bits).
//
// EXPECTED TO FAIL: stub shares a's qubits with c, no subtraction circuit run.
// =============================================================================

static void test_free_sub_simulate() {
    static constexpr std::size_t W3 = 3u;
    static constexpr uint32_t a3_base = 0u;
    static constexpr uint32_t b3_base = W3;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved3[2u * W3];
    for (uint32_t i = 0; i < 2u * W3; ++i) {
        reserved3[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan, 128u};

    // Initialize: a=7 (111), b=3 (011).
    for (uint32_t i = 0; i < W3; ++i) {
        if ((7u >> i) & 1u) orkan::apply_x(sc.sv(), a3_base + i);
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), b3_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W3> a, b;
        for (uint32_t i = 0; i < W3; ++i) {
            a.qubits[i] = static_cast<int>(a3_base + i);
            b.qubits[i] = static_cast<int>(b3_base + i);
        }
        a.value      = 7;
        a.super_mask = (1u << W3) - 1u;
        b.value      = 3;
        b.super_mask = (1u << W3) - 1u;

        sturm::qint_t<W3> c = a - b;

        // 1. Classical result check.
        assert(c.value == 4LL &&
               "c = a - b: classical value must be 7-3=4");

        // 2. Gate count check (EXPECTED TO FAIL with stub).
        assert(sc.ctx->gate_count > gates_before &&
               "c = a - b must emit gates in SIMULATE mode");

        // 3. 'a' unchanged.
        uint32_t a_val_sv = read_reg(sc.sv(), a3_base, W3, n_orkan);
        assert(a_val_sv == 7u &&
               "a must be unchanged after c = a - b");

        // 4. Statevector correctness (EXPECTED TO FAIL with stub).
        int c_qidx[W3];
        for (uint32_t i = 0; i < W3; ++i) c_qidx[i] = c.qubits[i];
        uint32_t c_val_sv = read_reg_idxs(sc.sv(), c_qidx, W3, n_orkan);
        assert(c_val_sv == 4u &&
               "c = a - b: statevector must show c holds 7-3=4");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < 2u * W3; ++i) {
        sturm::QubitPool::instance().release(reserved3[i]);
    }

    std::printf("  PASS: test_free_sub_simulate (7-3=4 in statevector)\n");
}

// =============================================================================
// test_free_mul_simulate
// Full statevector test: c = a * b.
//   a = 2 (010), b = 3 (011) → c must hold 6 (110).
// W=3: 2*3=6 fits in 3 bits.
//
// After fix: operator*(a,b) allocates 2*W result register, calls a *= b
// (lib_mul_dsl), retains lower W bits in c.
//
// EXPECTED TO FAIL: stub shares a's qubits with c, no multiplication circuit.
// =============================================================================

static void test_free_mul_simulate() {
    static constexpr std::size_t W3 = 3u;
    static constexpr uint32_t a3_base = 0u;
    static constexpr uint32_t b3_base = W3;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved3[2u * W3];
    for (uint32_t i = 0; i < 2u * W3; ++i) {
        reserved3[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan, 128u};

    // Initialize: a=2 (010), b=3 (011).
    for (uint32_t i = 0; i < W3; ++i) {
        if ((2u >> i) & 1u) orkan::apply_x(sc.sv(), a3_base + i);
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), b3_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W3> a, b;
        for (uint32_t i = 0; i < W3; ++i) {
            a.qubits[i] = static_cast<int>(a3_base + i);
            b.qubits[i] = static_cast<int>(b3_base + i);
        }
        a.value      = 2;
        a.super_mask = (1u << W3) - 1u;
        b.value      = 3;
        b.super_mask = (1u << W3) - 1u;

        sturm::qint_t<W3> c = a * b;

        // 1. Classical result check.
        assert(c.value == 6LL &&
               "c = a * b: classical value must be 2*3=6");

        // 2. Gate count check (EXPECTED TO FAIL with stub).
        assert(sc.ctx->gate_count > gates_before &&
               "c = a * b must emit gates in SIMULATE mode");

        // 3. 'a' unchanged.
        uint32_t a_val_sv = read_reg(sc.sv(), a3_base, W3, n_orkan);
        assert(a_val_sv == 2u &&
               "a must be unchanged after c = a * b");

        // 4. Statevector correctness (EXPECTED TO FAIL with stub).
        int c_qidx[W3];
        for (uint32_t i = 0; i < W3; ++i) c_qidx[i] = c.qubits[i];
        uint32_t c_val_sv = read_reg_idxs(sc.sv(), c_qidx, W3, n_orkan);
        assert(c_val_sv == 6u &&
               "c = a * b: statevector must show c holds 2*3=6");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < 2u * W3; ++i) {
        sturm::QubitPool::instance().release(reserved3[i]);
    }

    std::printf("  PASS: test_free_mul_simulate (2*3=6 in statevector)\n");
}

// =============================================================================
// test_free_div_simulate
// Full statevector test: c = a / b.
//   a = 3 (11), b = 2 (10) → c must hold 1 (01).
// W=2 (required: W=3 peaks at 22 qubits, exceeding kMaxQubits=17).
//
// Qubit budget (W=2, pre-reserved a+b=4 qubits):
//   Register (a+b):     2*W=4  (indices 0..3)
//   copy_register:      W=2    (indices 4..5)
//   /= allocs quot+rem: 2*W=4  (indices 6..9)
//   lib_div_dsl (n=2):  scratch(2)+sgn(1)+overflow(1)+carry_anc(1)=5 (10..14)
//   Peak: 15 qubits — within kMaxQubits (17).
//
// EXPECTED TO FAIL: stub shares a's qubits with c, no division circuit.
// =============================================================================

static void test_free_div_simulate() {
    static constexpr std::size_t W2 = 2u;
    static constexpr uint32_t a2_base = 0u;
    static constexpr uint32_t b2_base = W2;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved2[2u * W2];
    for (uint32_t i = 0; i < 2u * W2; ++i) {
        reserved2[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan, 128u};

    // Initialize: a=3 (11), b=2 (10).
    for (uint32_t i = 0; i < W2; ++i) {
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), a2_base + i);
        if ((2u >> i) & 1u) orkan::apply_x(sc.sv(), b2_base + i);
    }

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W2> a, b;
        for (uint32_t i = 0; i < W2; ++i) {
            a.qubits[i] = static_cast<int>(a2_base + i);
            b.qubits[i] = static_cast<int>(b2_base + i);
        }
        a.value      = 3;
        a.super_mask = (1u << W2) - 1u;
        b.value      = 2;
        b.super_mask = (1u << W2) - 1u;

        sturm::qint_t<W2> c = a / b;

        // 1. Classical result check.
        assert(c.value == 1LL &&
               "c = a / b: classical value must be 3/2=1");

        // 2. Gate count check (EXPECTED TO FAIL with stub).
        assert(sc.ctx->gate_count > gates_before &&
               "c = a / b must emit gates in SIMULATE mode");

        // 3. 'a' unchanged.
        uint32_t a_val_sv = read_reg(sc.sv(), a2_base, W2, n_orkan);
        assert(a_val_sv == 3u &&
               "a must be unchanged after c = a / b");

        // 4. Statevector correctness (EXPECTED TO FAIL with stub).
        int c_qidx[W2];
        for (uint32_t i = 0; i < W2; ++i) c_qidx[i] = c.qubits[i];
        uint32_t c_val_sv = read_reg_idxs(sc.sv(), c_qidx, W2, n_orkan);
        assert(c_val_sv == 1u &&
               "c = a / b: statevector must show c holds 3/2=1");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < 2u * W2; ++i) {
        sturm::QubitPool::instance().release(reserved2[i]);
    }

    std::printf("  PASS: test_free_div_simulate (3/2=1 in statevector)\n");
}

// =============================================================================
// test_free_mod_simulate
// Full statevector test: c = a % b.
//   a = 1 (1-bit), b = 1 (1-bit) → c must hold 0 (1%1=0).
// W=1 (required: W=2 with copy+compound-assign peaks at 18 qubits, exceeding
//      kMaxQubits=17 via OrkanBridge::allocate limit).
//
// Qubit budget (W=1, pre-reserved a+b=2 qubits):
//   Register (a+b):                    2*W=2  (indices 0..1)
//   copy_register:                     W=1    (index 2)  + CNOT(0,2)
//   %= allocs rem:                     W=1    (index 3)
//   lib_mod_dsl quot:                  W=1    (index 4)
//   lib_div_dsl1 scratch+ov+carry:     3 ancilla (5..7)
//   lib_div_dsl1 emit_CCX_lifted anc:  index 8 → peak = 9 → released
//   lib_div_dsl1 final lib_add_dsl:    carry_anc=8, CCX anc=9 → peak = 10 → released
//   lib_mod temp_rem:                  index 5 (recycled)
//   lib_div_dsl2 scratch+ov+carry:     3 ancilla (6..8)
//   lib_div_dsl2 emit_CCX_lifted anc:  index 9 → peak = 10
//   lib_div_dsl2 final lib_add_dsl:    carry_anc=9, CCX anc=10 → peak = 11
//
// Orkan state needs at least 11 qubits.
//
// 'a' register qubit (0) holds |1> (a=1). After c = a % b:
//   - a's qubit (0) is unchanged: still |1>
//   - b's qubit (1) is unchanged: |1>  (b=1)
//   - c's qubit is the remainder register: 1%1=0, so |0>
//
// EXPECTED TO FAIL: stub shares a's qubits with c, no modulo circuit.
// =============================================================================

static void test_free_mod_simulate() {
    static constexpr std::size_t W1 = 1u;
    static constexpr uint32_t a1_base = 0u;
    static constexpr uint32_t b1_base = W1;   // 1
    static constexpr uint32_t n_orkan1 = 12u; // peak budget = 11 qubits, use 12

    sturm::QubitPool::instance().reset_for_testing();

    int reserved1[2u * W1];
    for (uint32_t i = 0; i < 2u * W1; ++i) {
        reserved1[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan1, 128u};

    // Initialize: a=1 (|1>), b=1 (|1>).
    orkan::apply_x(sc.sv(), a1_base);  // a qubit = |1>
    orkan::apply_x(sc.sv(), b1_base);  // b qubit = |1>

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W1> a, b;
        a.qubits[0] = static_cast<int>(a1_base);
        b.qubits[0] = static_cast<int>(b1_base);
        a.value      = 1;
        a.super_mask = 1u;
        b.value      = 1;
        b.super_mask = 1u;

        sturm::qint_t<W1> c = a % b;

        // 1. Classical result check: 1 % 1 = 0.
        assert(c.value == 0LL &&
               "c = a % b: classical value must be 1%1=0");

        // 2. Gate count check (EXPECTED TO FAIL with stub).
        assert(sc.ctx->gate_count > gates_before &&
               "c = a % b must emit gates in SIMULATE mode");

        // 3. 'a' unchanged: qubit 0 must still be |1> (value 1).
        uint32_t a_val_sv = read_reg(sc.sv(), a1_base, W1, n_orkan1);
        assert(a_val_sv == 1u &&
               "a must be unchanged after c = a % b");

        // 4. Statevector correctness: c must hold 0 (1%1=0 → all |0> bits).
        int c_qidx[W1];
        c_qidx[0] = c.qubits[0];
        uint32_t c_val_sv = read_reg_idxs(sc.sv(), c_qidx, W1, n_orkan1);
        assert(c_val_sv == 0u &&
               "c = a % b: statevector must show c holds 1%1=0");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    for (uint32_t i = 0; i < 2u * W1; ++i) {
        sturm::QubitPool::instance().release(reserved1[i]);
    }

    std::printf("  PASS: test_free_mod_simulate (1%%1=0 in statevector)\n");
}

// =============================================================================
// main
// Run classical checks first (expected PASS), then gate-count checks (expected
// FAIL), then statevector checks (expected FAIL).
// =============================================================================

int main() {
    std::printf("test_free_op_simulate: simulation correctness for free operators\n");
    std::printf("NOTE: gate-count and statevector tests EXPECTED TO FAIL until\n");
    std::printf("      sturm-6qo (wire free operators through compound assigns) is fixed.\n\n");

    // ── Phase 1: Classical value checks (should all PASS) ─────────────────────
    std::printf("Phase 1: Classical value checks\n");
    test_free_add_classical();
    test_free_sub_classical();
    test_free_mul_classical();
    test_free_div_classical();
    test_free_mod_classical();

    // ── Phase 2: Gate count checks (EXPECTED TO FAIL with current stubs) ──────
    std::printf("\nPhase 2: Gate count checks (expected to fail until sturm-6qo)\n");
    test_free_add_gate_count();
    test_free_sub_gate_count();
    test_free_mul_gate_count();
    test_free_div_gate_count();
    test_free_mod_gate_count();

    // ── Phase 3: Statevector correctness (EXPECTED TO FAIL with current stubs) ─
    std::printf("\nPhase 3: Statevector correctness (expected to fail until sturm-6qo)\n");
    test_free_add_simulate();
    test_free_sub_simulate();
    test_free_mul_simulate();
    test_free_div_simulate();
    test_free_mod_simulate();

    std::printf("\nAll test_free_op_simulate tests passed.\n");
    return 0;
}
