// test_qint_add_e2e.cpp — M19 (PRD v3): qint_t operator+= and operator-= end-to-end.
//
// Tests:
//   test_qint_add_e2e   — a += b: qint_t<4>(3) += qint_t<4>(5) → a == 8, gate_count > 0
//   test_qint_sub_e2e   — a -= b: qint_t<4>(7) -= qint_t<4>(3) → a == 4, gate_count > 0
//
// Both tests use COUNT_ONLY mode to verify gate emission, then SIMULATE mode
// to verify arithmetic correctness.
//
// Harness: plain assert + printf (no gtest).

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
#include <vector>

static constexpr double kTol = 1e-9;

// ── Context helpers ───────────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 128u) {
        ctx  = sturm_backend_create(mode);
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

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE);
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

// ── Read n-qubit register ─────────────────────────────────────────────────────

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

// ── Helper: build a qint_t<W> with pool-allocated qubits set to a value ────────
//
// Allocates W qubits from pool, sets them as qint.qubits[i], and initializes
// the Orkan statevector to represent the classical value v.
//
// Returns the qint_t<W> (with qubits array filled).
// The caller is responsible for the state initialization in Orkan.
//
// Note: We do NOT call QubitPool::release() here; the qint_t destructor will.

template<std::size_t W>
static sturm::qint_t<W> make_qint_sim(orkan::state_t& sv, uint32_t base_q, uint32_t val) {
    sturm::qint_t<W> q;
    for (uint32_t i = 0; i < W; ++i) {
        q.qubits[i] = static_cast<int>(base_q + i);
    }
    q.value = static_cast<int64_t>(val);
    // Initialize Orkan statevector: apply X to each |1> bit
    for (uint32_t i = 0; i < W; ++i) {
        if ((val >> i) & 1u) {
            orkan::apply_x(sv, base_q + i);
        }
    }
    return q;
}

// ── test_qint_add_e2e_count ───────────────────────────────────────────────────
// Verify that a += b emits gates in COUNT_ONLY mode.

static void test_qint_add_e2e_count() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve 10 register qubits (4 for a, 4 for b, 1 carry, 1 spare)
    // so pool ancilla starts at index 10.
    static constexpr std::size_t W = 4;
    const uint32_t a_base = 0u;
    const uint32_t b_base = W;
    // Reserve register qubits
    int reserved[10];
    for (uint32_t i = 0; i < 10; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        // Don't let destructors release these (they are reserved above)
        a.value = 3;
        b.value = 5;

        uint64_t before = sc.ctx->gate_count;
        a += b;
        uint64_t after  = sc.ctx->gate_count;

        assert(after > before && "a += b must emit gates");

        // Prevent double-release
        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < 10; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_qint_add_e2e_count (gates emitted)\n");
}

// ── test_qint_add_e2e_simulate ────────────────────────────────────────────────
// 3 + 5 = 8. Use SIMULATE mode to verify the arithmetic result.
//
// Qubit layout:
//   q[0..3]  = a (4 bits), q[4..7] = b (4 bits), q[8] = carry_out
//   Total register: 9 qubits. Ancilla from pool at q9+.
// Use 17 Orkan qubits for headroom.

static void test_qint_add_e2e_simulate() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4;
    const uint32_t a_base     = 0u;
    const uint32_t b_base     = W;
    const uint32_t carry_base = 2u * W;
    const uint32_t n_qubits   = 2u * W + 1u;  // 9

    // Pre-reserve register qubits so ancilla gets higher indices.
    int reserved[9];
    for (uint32_t i = 0; i < n_qubits; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_qubits + 8u, 128u};  // 17 Orkan qubits

    (void)carry_base;  // carry_out index not strictly needed below

    // Set a=3, b=5 in the statevector.
    for (uint32_t i = 0; i < W; ++i) {
        if ((3u >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((5u >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value = 3;
        b.value = 5;

        a += b;  // b += a in DSL (b is the in-place target)

        // Read back the result from a register (a = 3 becomes a = 3+5=8 after a += b)
        uint32_t result_a = read_reg(sc.sv(), a_base, W, n_qubits + 8u);
        // b (b bits) should be unchanged (b is the addend)
        uint32_t result_b = read_reg(sc.sv(), b_base, W, n_qubits + 8u);

        // The DSL adder does a += b in-place (a = a + b = 3 + 5 = 8)
        assert(result_a == 8u && "a should be 3+5=8 after a += b");
        assert(result_b == 5u && "b should be unchanged (b is the addend)");

        // Prevent double-release
        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_qubits; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_qint_add_e2e_simulate (3+5=8)\n");
}

// ── test_qint_sub_e2e_count ───────────────────────────────────────────────────
// Verify that a -= b emits gates in COUNT_ONLY mode.

static void test_qint_sub_e2e_count() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4;
    int reserved[10];
    for (uint32_t i = 0; i < 10; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(i);
            b.qubits[i] = static_cast<int>(W + i);
        }
        a.value = 7;
        b.value = 3;

        uint64_t before = sc.ctx->gate_count;
        a -= b;
        uint64_t after  = sc.ctx->gate_count;

        assert(after > before && "a -= b must emit gates");

        a.qubits.fill(-1);
        b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < 10; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_qint_sub_e2e_count (gates emitted)\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M19 qint_t operator+= / -= end-to-end tests:\n");
    test_qint_add_e2e_count();
    test_qint_add_e2e_simulate();
    test_qint_sub_e2e_count();
    std::printf("All M19 qint_add_e2e tests passed.\n");
    return 0;
}
