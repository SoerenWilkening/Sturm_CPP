// test_when_control_stack_bridge.cpp — sturm-d9n / sturm-e5k:
//   WhenGuard bridges to control_stack + e2e phi/theta SIMULATE tests.
//
// Tests:
//   1. Outside WHEN: ctx->control_stack.depth() == 0.
//   2. Inside single superposed WHEN: depth == 1, top == expr qubit.
//   3. After WHEN exits: depth == 0 (restored).
//   4. Classical-true WHEN: depth remains 0 (no push).
//   5. Classical-false WHEN: depth remains 0 (no push).
//   6. Nested WHEN (swap): inside inner body depth == 1 (outer popped, inner pushed).
//   7. After inner WHEN exits: depth == 1 (outer control restored).
//   8. After outer WHEN exits: depth == 0.
//   9. SIMULATE: q.phi() += delta outside WHEN → RZ gate, verify statevector.
//  10. SIMULATE: q.theta() += delta outside WHEN → RY gate, verify statevector.
//  11. SIMULATE: WHEN(ctrl) { q.phi() += delta; } → CRZ gate (lifted), verify statevector.
//  12. SIMULATE: WHEN(ctrl) { q.theta() += delta; } → CRY gate (lifted), verify statevector.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/exec_simulate.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

using cx = std::complex<double>;
static constexpr double kTol = 1e-10;
static constexpr double kPi  = M_PI;

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

// ── Tests 6-8: nested WHEN swap ──────────────────────────────────────────────
// Invariant: depth always in {0, 1}; inner WHEN top is inner qubit.
// (Phase G / sturm-ewto: nested WHEN AND-fold moved to the transpiler, so the
// runtime guard just pop/pushes to preserve depth 1 — the inner expr qubit goes
// on the stack directly.)

static void test_nested_when_swap_invariant() {
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
            // 6 (inner): swap — outer is popped, inner is pushed in its place.
            // Depth must still be 1 (depth-1 invariant preserved).
            depth_inner = sc.bc().control_stack.depth();
            top_inner   = sc.bc().control_stack.top();

            // After the swap, the top-of-stack is the inner expr qubit directly
            // (no ancilla — Phase G moved AND-fold to the transpiler).
            assert(top_inner != static_cast<uint32_t>(outer_flag.qubits[0]) &&
                   "swap: inner WHEN top must NOT be outer qubit (outer popped)");
            assert(top_inner == static_cast<uint32_t>(inner_flag.qubits[0]) &&
                   "swap: inner WHEN top must be inner qubit (no fold)");
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
           "inside inner WHEN: depth must be 1");

    assert(depth_after_inner == 1u &&
           "after inner WHEN exits: depth must be 1 (outer restored)");
    assert(top_after_inner == static_cast<uint32_t>(outer_flag.qubits[0]) &&
           "after inner WHEN exits: top must be outer qubit (restored)");

    std::printf("PASS test_nested_when_swap_invariant "
                "(outer_depth=%u, inner_depth=%u, after_inner_depth=%u)\n",
                depth_outer, depth_inner, depth_after_inner);
}

// ── ScopedSimCtx (SIMULATE mode with Orkan) ───────────────────────────────────

struct ScopedSimCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    sturm::OrkanBridge*      bridge;

    ScopedSimCtx() {
        ctx    = sturm_backend_create(STURM_MODE_SIMULATE, sturm::kMaxQubits);
        assert(ctx && "sturm_backend_create failed");
        bridge = new sturm::OrkanBridge();
        bridge->allocate(sturm::kMaxQubits);
        ctx->orkan_state_ptr = bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedSimCtx() {
        sturm_set_thread_context(prev);
        ctx->orkan_state_ptr = nullptr;
        delete bridge;
        sturm_backend_destroy(ctx);
    }

    void reset() { bridge->reset_zero(); }

    void apply_1q(sturm_gate_kind_t kind, uint32_t q, double p = 0.0) {
        sturm::exec_simulate_1q(bridge->state(), kind, q, p);
    }

    cx amp(uint64_t idx) const {
        return orkan::amplitude(bridge->state(), idx);
    }

    sturm::BackendContext& bc() { return *ctx; }
};

static void assert_close(cx got, cx expected, const char* label) {
    double err = std::abs(got - expected);
    if (err > kTol) {
        std::fprintf(stderr, "FAIL %s: got (%g,%g) expected (%g,%g) err=%g\n",
                     label,
                     got.real(), got.imag(),
                     expected.real(), expected.imag(),
                     err);
        assert(false);
    }
}

// ── Test 9: SIMULATE q.phi() += delta outside WHEN → RZ gate, verify sv ───────
//
// qubit 0 starts in (|0>+|1>)/sqrt(2) after Hadamard.
// RZ(pi/2) produces:
//   amp[0] = e^{-i*pi/4}/sqrt(2),  amp[1] = e^{+i*pi/4}/sqrt(2)

static void test_simulate_phi_uncontrolled() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedSimCtx sc;
    sc.reset();

    // Allocate qubit index 0 from the pool.
    sturm::qint q(0);
    q.super_mask = 0x1u;
    q.qubits[0] = sturm::QubitPool::instance().allocate();  // qubit 0
    uint32_t tgt = static_cast<uint32_t>(q.qubits[0]);

    // Prepare: H|0> = (|0>+|1>)/sqrt(2)
    sc.apply_1q(STURM_GATE_H, tgt);

    // Execute phi += pi/2 → should emit RZ(pi/2) on tgt
    q.phi() += kPi / 2.0;

    double s = 1.0 / std::sqrt(2.0);
    cx exp0 = std::exp(cx{0.0, -kPi / 4.0}) * s;
    cx exp1 = std::exp(cx{0.0, +kPi / 4.0}) * s;
    assert_close(sc.amp(0u), exp0, "phi uncontrolled RZ amp[0]");
    assert_close(sc.amp(1u), exp1, "phi uncontrolled RZ amp[1]");

    std::printf("PASS test_simulate_phi_uncontrolled (RZ on qubit %u)\n", tgt);
}

// ── Test 10: SIMULATE q.theta() += delta outside WHEN → RY gate, verify sv ────
//
// qubit 0 starts in |0>.
// RY(pi/2) produces:
//   amp[0] = cos(pi/4) = 1/sqrt(2),  amp[1] = sin(pi/4) = 1/sqrt(2)

static void test_simulate_theta_uncontrolled() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedSimCtx sc;
    sc.reset();

    sturm::qint q(0);
    q.super_mask = 0x1u;
    q.qubits[0] = sturm::QubitPool::instance().allocate();  // qubit 0
    uint32_t tgt = static_cast<uint32_t>(q.qubits[0]);

    // Execute theta += pi/2 → should emit RY(pi/2) on tgt (starting from |0>)
    q.theta() += kPi / 2.0;

    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp(0u), {s, 0.0}, "theta uncontrolled RY amp[0]");
    assert_close(sc.amp(1u), {s, 0.0}, "theta uncontrolled RY amp[1]");

    std::printf("PASS test_simulate_theta_uncontrolled (RY on qubit %u)\n", tgt);
}

// ── Test 11: SIMULATE WHEN(ctrl) { q.phi() += delta; } → CRZ gate ─────────────
//
// Two-qubit layout: ctrl = qubit 0, tgt = qubit 1.
// Basis index: idx = ctrl_bit | (tgt_bit << 1)
//   idx=0: |ctrl=0,tgt=0>,  idx=1: |ctrl=1,tgt=0>
//   idx=2: |ctrl=0,tgt=1>,  idx=3: |ctrl=1,tgt=1>
//
// Setup: X(ctrl) then H(tgt) → (|ctrl=1,tgt=0> + |ctrl=1,tgt=1>)/sqrt(2)
//        = (idx1 + idx3)/sqrt(2)
// After CRZ(pi/2) on tgt, conditioned on ctrl=1:
//   amp[1] <- e^{-i*pi/4}/sqrt(2),  amp[3] <- e^{+i*pi/4}/sqrt(2)
//   amp[0] = amp[2] = 0

static void test_simulate_phi_controlled_via_when() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedSimCtx sc;
    sc.reset();

    // ctrl = qbool(0.5) → qubit 0.
    // Note: qbool(0.5) calls current_sink()->prepare() which uses CounterSink
    // (no-op on statevector); WhenGuard will see super_mask=1 and push qubit 0.
    sturm::qbool ctrl_flag(0.5);
    uint32_t ctrl_q = static_cast<uint32_t>(ctrl_flag.qubits[0]);  // 0

    // tgt → qubit 1 (next allocation after ctrl qubit 0).
    sturm::qint q(0);
    q.super_mask = 0x1u;
    q.qubits[0] = sturm::QubitPool::instance().allocate();  // qubit 1
    uint32_t tgt_q = static_cast<uint32_t>(q.qubits[0]);   // 1

    // Prepare state: X(ctrl) → ctrl=|1>, H(tgt) → tgt=superposed.
    sc.apply_1q(STURM_GATE_X, ctrl_q);
    sc.apply_1q(STURM_GATE_H, tgt_q);

    double s = 1.0 / std::sqrt(2.0);
    // Verify setup: (idx1 + idx3)/sqrt(2)
    assert_close(sc.amp(1u), {s, 0.0}, "CRZ setup amp[ctrl=1,tgt=0]");
    assert_close(sc.amp(3u), {s, 0.0}, "CRZ setup amp[ctrl=1,tgt=1]");

    // WHEN(ctrl_flag) { q.phi() += pi/2; } → emits CRZ(pi/2, ctrl_q, tgt_q)
    WHEN(ctrl_flag) {
        q.phi() += kPi / 2.0;
    }

    cx exp1 = std::exp(cx{0.0, -kPi / 4.0}) * s;
    cx exp3 = std::exp(cx{0.0, +kPi / 4.0}) * s;
    assert_close(sc.amp(0u), {0.0, 0.0}, "phi WHEN CRZ amp[00]");
    assert_close(sc.amp(1u), exp1,        "phi WHEN CRZ amp[ctrl=1,tgt=0]");
    assert_close(sc.amp(2u), {0.0, 0.0}, "phi WHEN CRZ amp[01]");
    assert_close(sc.amp(3u), exp3,        "phi WHEN CRZ amp[ctrl=1,tgt=1]");

    std::printf("PASS test_simulate_phi_controlled_via_when (CRZ ctrl=%u tgt=%u)\n",
                ctrl_q, tgt_q);
}

// ── Test 12: SIMULATE WHEN(ctrl) { q.theta() += delta; } → CRY gate ──────────
//
// Same two-qubit layout.
// Setup: X(ctrl) → ctrl=|1>, tgt stays in |0>.
// After CRY(pi/2) on tgt conditioned on ctrl=1:
//   starting from |ctrl=1,tgt=0> (idx=1):
//   Ry(pi/2) on tgt: (|ctrl=1,tgt=0> + |ctrl=1,tgt=1>)/sqrt(2) = (idx1+idx3)/sqrt(2)

static void test_simulate_theta_controlled_via_when() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedSimCtx sc;
    sc.reset();

    sturm::qbool ctrl_flag(0.5);
    uint32_t ctrl_q = static_cast<uint32_t>(ctrl_flag.qubits[0]);  // 0

    sturm::qint q(0);
    q.super_mask = 0x1u;
    q.qubits[0] = sturm::QubitPool::instance().allocate();  // qubit 1
    uint32_t tgt_q = static_cast<uint32_t>(q.qubits[0]);   // 1

    // Prepare: X(ctrl) → |ctrl=1,tgt=0> = idx=1
    sc.apply_1q(STURM_GATE_X, ctrl_q);
    assert_close(sc.amp(1u), {1.0, 0.0}, "CRY setup ctrl=1");

    // WHEN(ctrl_flag) { q.theta() += pi/2; } → emits CRY(pi/2, ctrl_q, tgt_q)
    WHEN(ctrl_flag) {
        q.theta() += kPi / 2.0;
    }

    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp(0u), {0.0, 0.0}, "theta WHEN CRY amp[00]");
    assert_close(sc.amp(1u), {s,   0.0}, "theta WHEN CRY amp[ctrl=1,tgt=0]");
    assert_close(sc.amp(2u), {0.0, 0.0}, "theta WHEN CRY amp[01]");
    assert_close(sc.amp(3u), {s,   0.0}, "theta WHEN CRY amp[ctrl=1,tgt=1]");

    std::printf("PASS test_simulate_theta_controlled_via_when (CRY ctrl=%u tgt=%u)\n",
                ctrl_q, tgt_q);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    // Tests 1-8: WhenGuard control_stack bridge (sturm-d9n)
    test_single_superposed_when_pushes_stack();
    test_classical_true_when_no_push();
    test_classical_false_when_no_push();
    test_nested_when_swap_invariant();

    // Tests 9-12: e2e SIMULATE for phi/theta uncontrolled and via WHEN (sturm-e5k)
    test_simulate_phi_uncontrolled();
    test_simulate_theta_uncontrolled();
    test_simulate_phi_controlled_via_when();
    test_simulate_theta_controlled_via_when();

    std::printf("All sturm-e5k WHEN control_stack bridge + e2e phi/theta tests passed.\n");
    return 0;
}
