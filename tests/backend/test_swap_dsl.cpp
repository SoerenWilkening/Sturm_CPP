// test_swap_dsl.cpp — M16 (PRD v3): DSL-style SWAP / Fredkin tests.
//
// Tests:
//   test_swap_dsl_zero_gates — uncontrolled SWAP emits zero gates; indices swapped.
//   test_c_swap_dsl_fredkin  — Fredkin truth table on all 8 inputs (ctrl,a,b).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/detail/lib/swap_dsl.hpp"
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

static constexpr double kTol = 1e-9;
using cx = std::complex<double>;

// ── ScopedCtx: COUNT_ONLY mode ────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 32u) {
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

// ── SimCtx: SIMULATE mode with OrkanBridge ────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 32u) {
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

// ── Read a single qubit from a pure basis state ───────────────────────────────

static uint32_t read_qubit_sim(orkan::state_t& sv, uint32_t q, uint32_t n_qubits) {
    uint64_t dim = uint64_t{1} << n_qubits;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

// ── test_swap_dsl_zero_gates ──────────────────────────────────────────────────
// Uncontrolled SWAP: verify zero gates emitted and qubit indices are swapped.
//
// Qubit layout:
//   q0 = qubit A (initially |1>)
//   q1 = qubit B (initially |0>)
// After lib_swap_dsl: A should point to qubit 1 (now reads 0) and
//                     B should point to qubit 0 (now reads 1).
// But since this is a pure index relabel, no gates are emitted.

static void test_swap_dsl_zero_gates() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve q0, q1.
    int r0 = sturm::QubitPool::instance().allocate();
    int r1 = sturm::QubitPool::instance().allocate();

    {
        ScopedCtx sc{STURM_MODE_COUNT_ONLY, 32u};

        // Create two non-owning qbools referencing q0 and q1.
        sturm::qbool a = sturm::qbool::make_non_owning(0);
        sturm::qbool b = sturm::qbool::make_non_owning(1);

        uint64_t before = sc.ctx->gate_count;
        sturm::lib_swap_dsl(a, b);
        uint64_t after = sc.ctx->gate_count;

        // Zero gates emitted.
        if (after - before != 0u) {
            std::fprintf(stderr, "FAIL test_swap_dsl_zero_gates: expected 0 gates, got %llu\n",
                         (unsigned long long)(after - before));
            assert(false);
        }

        // Qubit indices swapped: a.qubits[0] now == 1, b.qubits[0] now == 0.
        if (a.qubits[0] != 1) {
            std::fprintf(stderr, "FAIL test_swap_dsl_zero_gates: a.qubits[0] expected 1 got %d\n",
                         a.qubits[0]);
            assert(false);
        }
        if (b.qubits[0] != 0) {
            std::fprintf(stderr, "FAIL test_swap_dsl_zero_gates: b.qubits[0] expected 0 got %d\n",
                         b.qubits[0]);
            assert(false);
        }
    }

    sturm::QubitPool::instance().release(r0);
    sturm::QubitPool::instance().release(r1);

    std::printf("  PASS: test_swap_dsl_zero_gates\n");
}

// ── test_c_swap_dsl_fredkin ────────────────────────────────────────────────────
// Controlled SWAP (Fredkin) truth table on all 8 inputs (ctrl, a, b).
// WHEN(ctrl): swap(a, b).
// Expected:
//   ctrl=0: a,b unchanged.
//   ctrl=1: a,b swapped.
//
// Qubit layout:
//   q0 = ctrl
//   q1 = a
//   q2 = b
// Total Orkan qubits: 3 (+ ancilla headroom via QubitPool, maybe 1-2 extra)

static void test_c_swap_dsl_fredkin() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve q0, q1, q2.
    int reserved[3];
    for (int i = 0; i < 3; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    uint32_t pass_count = 0;

    for (uint32_t ctrl_val = 0; ctrl_val < 2u; ++ctrl_val) {
        for (uint32_t a_val = 0; a_val < 2u; ++a_val) {
            for (uint32_t b_val = 0; b_val < 2u; ++b_val) {
                // Fredkin: allocates ancilla from pool; need enough Orkan headroom.
                // The 3 CNOT (CX->CCX under ctrl) pattern uses no extra ancilla.
                // But emit_CX_lifted under 1 ctrl: CCX (1 gate). So 3 CCX gates,
                // each potentially needing ancilla for depth>1.
                // Under 1 control: emit_CX_lifted depth==1 emits CCX directly.
                // So no ancilla needed — 3 qubits suffice.
                SimCtx sc{4u, 32u};  // Extra Orkan qubit for any ancilla usage

                // Set initial state.
                if (ctrl_val) orkan::apply_x(sc.sv(), 0);
                if (a_val)    orkan::apply_x(sc.sv(), 1);
                if (b_val)    orkan::apply_x(sc.sv(), 2);

                // Create qbools.
                sturm::qbool ctrl_q = sturm::qbool::make_non_owning(0);
                sturm::qbool a_q    = sturm::qbool::make_non_owning(1);
                sturm::qbool b_q    = sturm::qbool::make_non_owning(2);

                // Push ctrl onto the control stack, then swap.
                sc.bc().control_stack.push_control(0u);
                sturm::lib_swap_dsl(a_q, b_q);
                sc.bc().control_stack.pop_control();

                // Read results.
                uint32_t got_a = read_qubit_sim(sc.sv(), 1u, 4u);
                uint32_t got_b = read_qubit_sim(sc.sv(), 2u, 4u);

                uint32_t exp_a, exp_b;
                if (ctrl_val) {
                    exp_a = b_val;  // a gets b's value
                    exp_b = a_val;  // b gets a's value
                } else {
                    exp_a = a_val;  // unchanged
                    exp_b = b_val;  // unchanged
                }

                if (got_a != exp_a || got_b != exp_b) {
                    std::fprintf(stderr,
                        "FAIL c_swap_dsl_fredkin: ctrl=%u a=%u b=%u: expected a=%u b=%u got a=%u b=%u\n",
                        ctrl_val, a_val, b_val, exp_a, exp_b, got_a, got_b);
                    assert(false);
                }
                ++pass_count;
            }
        }
    }

    for (int i = 0; i < 3; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_c_swap_dsl_fredkin (%u cases)\n", pass_count);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M16 DSL-style SWAP / Fredkin tests:\n");
    test_swap_dsl_zero_gates();
    test_c_swap_dsl_fredkin();
    std::printf("All M16 swap_dsl tests passed.\n");
    return 0;
}
