// test_when_control_stack_bridge.cpp — sturm-d9n: WhenGuard bridges to control_stack.
//
// Tests:
//   1. Outside WHEN: ctx->control_stack.depth() == 0.
//   2. Inside single superposed WHEN: depth == 1, top == expr qubit.
//   3. After WHEN exits: depth == 0 (restored).
//   4. Classical-true WHEN: depth remains 0 (no push).
//   5. Classical-false WHEN: depth remains 0 (no push).
//   6. Nested WHEN (AND-fold): inside inner body depth == 1 (outer replaced by ancilla).
//   7. After inner WHEN exits: depth == 1 (outer control restored).
//   8. After outer WHEN exits: depth == 0.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>

// ── ScopedAppendCtx ───────────────────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 32u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::BackendContext& bc() { return *ctx; }
};

// ── Test 1-3: single superposed WHEN pushes/pops control_stack ───────────────

static void test_single_superposed_when_pushes_stack() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // 1. Baseline: depth 0.
    assert(sc.bc().control_stack.depth() == 0u &&
           "control_stack must be empty before WHEN");

    uint32_t top_inside = 9999u;
    uint32_t depth_inside = 0u;

    sturm::qbool flag(0.5);   // superposed
    int expected_qubit = flag.qubits[0] >= 0 ? flag.qubits[0]
                                              : -1; // will be set after ensure_qubit

    WHEN(flag) {
        // 2. Inside WHEN: depth == 1, top == flag qubit.
        depth_inside = sc.bc().control_stack.depth();
        if (depth_inside > 0u) {
            top_inside = sc.bc().control_stack.top();
        }
    }

    // 3. After WHEN: depth == 0.
    assert(sc.bc().control_stack.depth() == 0u &&
           "control_stack must be empty after WHEN exits");
    assert(depth_inside == 1u &&
           "control_stack depth must be 1 inside single superposed WHEN");
    // The pushed qubit must equal flag's physical qubit index.
    assert(top_inside == static_cast<uint32_t>(flag.qubits[0]) &&
           "control_stack top must equal flag qubit index inside WHEN");

    (void)expected_qubit;
    std::printf("PASS test_single_superposed_when_pushes_stack (depth_inside=%u, qubit=%u)\n",
                depth_inside, top_inside);
}

// ── Test 4: classical-true WHEN does NOT push control_stack ──────────────────

static void test_classical_true_when_no_push() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    assert(sc.bc().control_stack.depth() == 0u);

    uint32_t depth_inside = 9999u;
    sturm::qbool flag(true);   // classical true
    WHEN(flag) {
        depth_inside = sc.bc().control_stack.depth();
    }

    assert(depth_inside == 0u &&
           "classical-true WHEN must NOT push to control_stack");
    assert(sc.bc().control_stack.depth() == 0u &&
           "control_stack must be empty after classical-true WHEN");
    std::printf("PASS test_classical_true_when_no_push\n");
}

// ── Test 5: classical-false WHEN does NOT push control_stack ─────────────────

static void test_classical_false_when_no_push() {
    ScopedAppendCtx sc;

    assert(sc.bc().control_stack.depth() == 0u);

    uint32_t depth_checked = 0u; // body won't run
    sturm::qbool flag(false);   // classical false
    WHEN(flag) {
        // body does not execute
        depth_checked = 9999u;
    }

    assert(depth_checked == 0u &&
           "classical-false WHEN body must not execute");
    assert(sc.bc().control_stack.depth() == 0u &&
           "control_stack must be empty after classical-false WHEN");
    std::printf("PASS test_classical_false_when_no_push\n");
}

// ── Tests 6-8: nested WHEN AND-fold — invariant: depth always in {0, 1} ──────

static void test_nested_when_depth_invariant() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    assert(sc.bc().control_stack.depth() == 0u);

    uint32_t depth_outer        = 9999u;
    uint32_t depth_inner        = 9999u;
    uint32_t depth_after_inner  = 9999u;
    uint32_t top_outer          = 9999u;
    uint32_t top_inner          = 9999u;
    uint32_t top_after_inner    = 9999u;

    sturm::qbool outer_flag(0.5);
    sturm::qbool inner_flag(0.5);

    WHEN(outer_flag) {
        // 7 (outer): depth == 1, top == outer qubit.
        depth_outer = sc.bc().control_stack.depth();
        top_outer   = sc.bc().control_stack.top();

        WHEN(inner_flag) {
            // 6 (inner): AND-fold replaces outer with ancilla.
            // Depth must still be 1 (outer popped, ancilla pushed).
            depth_inner = sc.bc().control_stack.depth();
            top_inner   = sc.bc().control_stack.top();

            // Ancilla qubit is neither outer_flag's qubit nor inner_flag's qubit.
            assert(top_inner != static_cast<uint32_t>(outer_flag.qubits[0]) &&
                   "AND-fold: control_stack top must NOT be outer qubit");
            assert(top_inner != static_cast<uint32_t>(inner_flag.qubits[0]) &&
                   "AND-fold: control_stack top must NOT be inner qubit");
        }

        // 7 (after inner): outer control restored → depth == 1, top == outer qubit.
        depth_after_inner = sc.bc().control_stack.depth();
        top_after_inner   = sc.bc().control_stack.top();
    }

    // 8: after all WHEN scopes: depth == 0.
    assert(sc.bc().control_stack.depth() == 0u &&
           "control_stack must be empty after all WHEN scopes exit");

    assert(depth_outer == 1u &&
           "inside outer WHEN: depth must be 1");
    assert(top_outer == static_cast<uint32_t>(outer_flag.qubits[0]) &&
           "inside outer WHEN: top must be outer qubit");

    assert(depth_inner == 1u &&
           "inside inner WHEN (AND-fold): depth must be 1 (not 2)");

    assert(depth_after_inner == 1u &&
           "after inner WHEN exits: depth must be 1 (outer restored)");
    assert(top_after_inner == static_cast<uint32_t>(outer_flag.qubits[0]) &&
           "after inner WHEN exits: top must be outer qubit (restored)");

    std::printf("PASS test_nested_when_depth_invariant "
                "(outer_depth=%u, inner_depth=%u, after_inner_depth=%u)\n",
                depth_outer, depth_inner, depth_after_inner);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_single_superposed_when_pushes_stack();
    test_classical_true_when_no_push();
    test_classical_false_when_no_push();
    test_nested_when_depth_invariant();

    std::printf("All sturm-d9n WhenGuard control_stack bridge tests passed.\n");
    return 0;
}
