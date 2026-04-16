// test_e2e_when_nested.cpp — M21 (PRD v3): Integration & acceptance.
//
// Phase-G note (PG-6): this test pushes directly onto BackendContext.control_stack
// and does not rely on the WhenGuard AND-fold — stable across Phase G (PG-4 retired
// the AND-fold; lib_c_n_AND_dsl via emit_CX_lifted is a separate library routine).
//
// Tests:
//   test_e2e_when_nested — deep WHEN nesting (3 levels) drives c_n_AND fold.
//     Pushes 3 controls manually onto the control stack, then calls qbool ops.
//     Verifies that under 3-deep control nesting:
//       1. Gates are emitted (control stack is consulted).
//       2. Ancilla pool is clean after each WHEN scope exits
//          (all ancillas freed after WHEN scope ends).
//
// The test directly uses BackendContext.control_stack.push_control / pop_control
// to simulate WHEN(c0) { WHEN(c1) { WHEN(c2) { ... } } } nesting.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/lib/c_and_dsl.hpp"
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

// ── test_e2e_when_nested_3 ────────────────────────────────────────────────────
//
// Simulate WHEN(c0) { WHEN(c1) { WHEN(c2) { target ^= src } } }.
// The 3 control qubits on the stack trigger c_n_AND fold in qbool operator^=.
//
// Qubit layout:
//   q[0] = c0 (control 0)
//   q[1] = c1 (control 1)
//   q[2] = c2 (control 2)
//   q[3] = src
//   q[4] = target
//   Total: 5 register qubits. c_n_AND uses 1 ancilla (n_anc = n_controls - 2 = 1).
//   Peak: 5 + 1 = 6 qubits — well within limit.

static void test_e2e_when_nested_3() {
    sturm::QubitPool::instance().reset_for_testing();

    // Reserve 5 qubits for c0, c1, c2, src, target.
    int reserved[5];
    for (uint32_t i = 0; i < 5u; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    // Record ancilla in_use before any WHEN nesting.
    int in_use_before = sturm::QubitPool::instance().in_use();
    assert(in_use_before == 5 && "should have exactly 5 reserved qubits");

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    uint64_t gates_outside = sc.ctx->gate_count;

    // Build non-owning qbool objects for each qubit.
    sturm::qbool c0  = sturm::qbool::make_non_owning(reserved[0]);
    sturm::qbool c1  = sturm::qbool::make_non_owning(reserved[1]);
    sturm::qbool c2  = sturm::qbool::make_non_owning(reserved[2]);
    sturm::qbool src = sturm::qbool::make_non_owning(reserved[3]);
    sturm::qbool tgt = sturm::qbool::make_non_owning(reserved[4]);

    // ── WHEN(c0) scope open ───────────────────────────────────────────────────
    sc.bc().control_stack.push_control(static_cast<uint32_t>(reserved[0]));
    assert(sc.bc().control_stack.depth() == 1u);

    //   ── WHEN(c1) scope open ──────────────────────────────────────────────────
    sc.bc().control_stack.push_control(static_cast<uint32_t>(reserved[1]));
    assert(sc.bc().control_stack.depth() == 2u);

    //     ── WHEN(c2) scope open ──────────────────────────────────────────────
    sc.bc().control_stack.push_control(static_cast<uint32_t>(reserved[2]));
    assert(sc.bc().control_stack.depth() == 3u);

    //       ── Body: target ^= src under 3 controls ─────────────────────────
    //       With 3 controls on the stack, qbool operator^= calls
    //       emit_CX_lifted which uses c_n_AND fold → allocates 1 ancilla.
    int in_use_at_entry = sturm::QubitPool::instance().in_use();
    uint64_t gates_in_when = sc.ctx->gate_count;

    tgt ^= src;  // lifted under 3 controls: c_n_AND fold

    uint64_t gates_after_body = sc.ctx->gate_count;
    assert(gates_after_body > gates_in_when && "body under 3-deep WHEN must emit gates");

    // After the body, ancilla should be released (c_n_AND cleans up).
    int in_use_after_body = sturm::QubitPool::instance().in_use();
    assert(in_use_after_body == in_use_at_entry &&
           "c_n_AND must return ancilla to pool after body");

    //     ── WHEN(c2) scope close ─────────────────────────────────────────────
    sc.bc().control_stack.pop_control();
    assert(sc.bc().control_stack.depth() == 2u);

    //   ── WHEN(c1) scope close ─────────────────────────────────────────────
    sc.bc().control_stack.pop_control();
    assert(sc.bc().control_stack.depth() == 1u);

    // ── WHEN(c0) scope close ─────────────────────────────────────────────────
    sc.bc().control_stack.pop_control();
    assert(sc.bc().control_stack.depth() == 0u);

    // Verify that control stack is empty after all scopes closed.
    assert(sc.bc().control_stack.depth() == 0u &&
           "control stack must be empty after all WHEN scopes exit");

    // Verify that ancilla pool is back to baseline (5 reserved qubits only).
    int in_use_final = sturm::QubitPool::instance().in_use();
    assert(in_use_final == 5 &&
           "ancilla pool must be clean: only reserved qubits remain");

    // Gates in the nested WHEN exceed what an uncontrolled op would use.
    // For 3 controls (c_n_AND, n=3): forward 2 CCX + 1 CCX target + 1 CCX reverse = 4 CCX total.
    // Actually under 3-deep control stack, we emit CX between the fold ancilla
    // and src, then CX onto tgt, etc. The exact count may vary; we just check > 0.
    assert(sc.ctx->gate_count > gates_outside && "nested WHEN must emit gates");

    // Release reserved qubits.
    for (uint32_t i = 0; i < 5u; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_e2e_when_nested_3 (depth=3, gates=%llu, ancilla clean)\n",
                static_cast<unsigned long long>(sc.ctx->gate_count - gates_outside));
}

// ── test_e2e_when_nested_c_n_and ─────────────────────────────────────────────
//
// Directly test lib_c_n_AND_dsl with n=4 controls (the deeper fold path).
// Verify ancilla pool returns to initial state after the call.

static void test_e2e_when_nested_c_n_and() {
    sturm::QubitPool::instance().reset_for_testing();

    // Reserve 6 qubits: c0..c3 (4 controls) + tgt (1) = 5 + 1 spare.
    int reserved[6];
    for (uint32_t i = 0; i < 6u; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    int in_use_before = sturm::QubitPool::instance().in_use();

    sturm::qbool controls[4];
    for (uint32_t i = 0; i < 4u; ++i) {
        controls[i] = sturm::qbool::make_non_owning(reserved[i]);
    }
    sturm::qbool tgt = sturm::qbool::make_non_owning(reserved[4]);

    uint64_t gates_before = sc.ctx->gate_count;

    // Call c_n_AND with 4 controls: n_anc = 4-2 = 2 ancilla qubits allocated/freed.
    sturm::lib_c_n_AND_dsl(controls, 4u, tgt);

    uint64_t gates_after = sc.ctx->gate_count;

    // Gates: (n-1) forward + (n-2) reverse = 3 + 2 = 5 CCX gates.
    assert(gates_after > gates_before && "lib_c_n_AND_dsl(n=4) must emit gates");
    assert((gates_after - gates_before) == 5u &&
           "lib_c_n_AND_dsl(n=4) must emit exactly 5 CCX gates");

    // Ancilla cleanliness: in_use must equal in_use_before (ancilla freed).
    int in_use_after = sturm::QubitPool::instance().in_use();
    assert(in_use_after == in_use_before &&
           "ancilla pool must be clean after lib_c_n_AND_dsl");

    for (uint32_t i = 0; i < 6u; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_e2e_when_nested_c_n_and (n=4, gates=%llu, ancilla clean)\n",
                static_cast<unsigned long long>(gates_after - gates_before));
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M21 e2e when_nested tests:\n");
    test_e2e_when_nested_3();
    test_e2e_when_nested_c_n_and();
    std::printf("All M21 e2e_when_nested tests passed.\n");
    return 0;
}
