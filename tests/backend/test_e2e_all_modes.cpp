// test_e2e_all_modes.cpp — M21 (PRD v3): Integration & acceptance.
//
// Tests:
//   test_e2e_all_modes — same program runs in COUNT_ONLY, APPEND, SIMULATE modes.
//     Program: a += b (qint_t<3> with a=2, b=3 → a=5).
//     Verifies:
//       1. Gate count in COUNT_ONLY > 0.
//       2. Gate count in APPEND matches COUNT_ONLY (same # of records as count).
//       3. SIMULATE produces correct statevector (a=5 in register after a+=b).
//
// Note: We re-run the same arithmetic operation in each mode to compare counts.
// APPEND records.size() must equal COUNT_ONLY gate_count for the same program.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/lib/adder_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── Context helpers ───────────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 128u) {
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

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
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

// ── Shared program constants ──────────────────────────────────────────────────
// Program: b += a (adder: b is target, a is addend).
// W=3: a=2 (010), b=3 (011) → b = 2+3 = 5 (101).
// Qubit layout: q[0..2]=a, q[3..5]=b, q[6]=carry_anc (reserved).

static constexpr std::size_t W = 3u;
static constexpr uint32_t a_val = 2u;
static constexpr uint32_t b_val = 3u;
static constexpr uint32_t expected_sum = 5u;  // a + b = 5

// Pre-reserve register qubits: indices 0..5 + carry at 6.
static constexpr uint32_t N_REG = 2u * W + 1u;  // 7

// Helper: set up reserved qubits and return them.
static void setup_reserved(int reserved[7]) {
    for (uint32_t i = 0; i < N_REG; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
}

// Helper: run the addition program (b += a) via lib_add_dsl.
// Returns gate count for this run.
static uint64_t run_add_program(sturm_backend_context_t* ctx) {
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;
    static constexpr uint32_t c_base = 2u * W;

    sturm::qbool a_bits[W], b_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
        b_bits[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
    }
    sturm::qbool carry = sturm::qbool::make_non_owning(static_cast<int>(c_base));

    uint64_t before = ctx->gate_count;
    sturm::lib_add_dsl(a_bits, b_bits, carry, W);
    uint64_t after = ctx->gate_count;

    return after - before;
}

// ── test_e2e_all_modes_count ──────────────────────────────────────────────────

static uint64_t test_e2e_all_modes_count() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[7];
    setup_reserved(reserved);

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    uint64_t cnt = run_add_program(sc.ctx);
    assert(cnt > 0u && "COUNT_ONLY must produce gate_count > 0");

    for (uint32_t i = 0; i < N_REG; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  COUNT_ONLY: gate_count = %llu\n",
                static_cast<unsigned long long>(cnt));
    return cnt;
}

// ── test_e2e_all_modes_append ─────────────────────────────────────────────────

static uint64_t test_e2e_all_modes_append() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[7];
    setup_reserved(reserved);

    ScopedCtx sc{STURM_MODE_APPEND};
    uint64_t cnt = run_add_program(sc.ctx);

    // APPEND records size must equal gate_count (they are the same gates).
    uint64_t n_records = static_cast<uint64_t>(sc.ctx->ir.size());
    assert(n_records > 0u && "APPEND must populate ir records");
    assert(n_records == cnt &&
           "APPEND: ir.size() must equal gate_count");

    for (uint32_t i = 0; i < N_REG; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  APPEND: records = %llu (== gate_count)\n",
                static_cast<unsigned long long>(n_records));
    return n_records;
}

// ── test_e2e_all_modes_simulate ───────────────────────────────────────────────

static uint64_t test_e2e_all_modes_simulate() {
    sturm::QubitPool::instance().reset_for_testing();

    int reserved[7];
    setup_reserved(reserved);

    const uint32_t orkan_n = 17u;
    SimCtx sc{orkan_n, 128u};

    // Initialize statevector: a=2 (010), b=3 (011).
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), W + i);
    }

    uint64_t cnt = run_add_program(sc.ctx);
    assert(cnt > 0u && "SIMULATE must also count gates");

    // Read b register (result of b += a = 5).
    const uint32_t b_base = W;
    uint64_t dim = uint64_t{1} << orkan_n;
    uint32_t got_b = 0u;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sc.sv(), s)) > kTol) {
            for (uint32_t k = 0; k < W; ++k) {
                got_b |= (static_cast<uint32_t>((s >> (b_base + k)) & 1u) << k);
            }
            break;
        }
    }
    assert(got_b == expected_sum && "SIMULATE: b += a must produce correct result");

    // a register should be unchanged (addend is not modified by lib_add_dsl).
    uint32_t got_a = 0u;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sc.sv(), s)) > kTol) {
            for (uint32_t k = 0; k < W; ++k) {
                got_a |= (static_cast<uint32_t>((s >> k) & 1u) << k);
            }
            break;
        }
    }
    assert(got_a == a_val && "SIMULATE: a (addend) must be unchanged");

    for (uint32_t i = 0; i < N_REG; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  SIMULATE: b+a=%u (a=%u unchanged), gates=%llu\n",
                got_b, got_a, static_cast<unsigned long long>(cnt));
    return cnt;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M21 e2e all_modes tests (program: b += a, W=3, a=%u b=%u):\n",
                a_val, b_val);

    uint64_t count_only_gates = test_e2e_all_modes_count();
    uint64_t append_records   = test_e2e_all_modes_append();
    uint64_t simulate_gates   = test_e2e_all_modes_simulate();

    // Gate count must match across all three modes.
    assert(count_only_gates == append_records &&
           "COUNT_ONLY gate_count must match APPEND ir.size()");
    assert(count_only_gates == simulate_gates &&
           "COUNT_ONLY gate_count must match SIMULATE gate_count");

    std::printf("  Gate count consistency: COUNT=%llu == APPEND=%llu == SIMULATE=%llu\n",
                static_cast<unsigned long long>(count_only_gates),
                static_cast<unsigned long long>(append_records),
                static_cast<unsigned long long>(simulate_gates));

    std::printf("All M21 e2e_all_modes tests passed.\n");
    return 0;
}
