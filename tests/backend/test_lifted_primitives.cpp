// test_lifted_primitives.cpp — sturm-8x3: emit_RY_lifted / emit_RZ_lifted
//
// Tests (TDD — written before implementation):
//
//   1. depth=0, RY(theta): execute_gate called with STURM_GATE_RY, 1 qubit.
//      Verify gate_count increments by 1 (COUNT_ONLY mode).
//   2. depth=0, RZ(theta): same for STURM_GATE_RZ.
//   3. depth=1, RY(theta): execute_gate called with STURM_GATE_CRY, 2 qubits.
//      Verify gate_count increments by 1 (COUNT_ONLY mode).
//   4. depth=1, RZ(theta): same for STURM_GATE_CRZ.
//   5. SIMULATE depth=0 RY(pi/2): statevector matches Ry(pi/2)|0>.
//   6. SIMULATE depth=1 RY(pi/2): |ctrl=1,tgt=0> -> CRY applies, matches ref.
//   7. SIMULATE depth=0 RZ(pi/2): statevector matches Rz(pi/2)|0>.
//   8. SIMULATE depth=1 RZ(pi/2): |ctrl=1,tgt=0> -> CRZ applies, matches ref.
//
// Harness: plain assert + main (no gtest).

#include "sturm/ops/lifted_primitives.hpp"

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/exec_simulate.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

using namespace sturm;
using cx = std::complex<double>;

static constexpr double kTol = 1e-10;
static constexpr double kPi  = M_PI;

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// Scoped COUNT_ONLY context.
struct ScopedCountCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedCountCtx() {
        ctx  = sturm_backend_create(STURM_MODE_COUNT_ONLY, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCountCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    uint64_t gate_count() const { return ctx->gate_count; }
};

// Scoped SIMULATE context with Orkan.
struct ScopedSimCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    OrkanBridge*             bridge;

    ScopedSimCtx() {
        ctx    = sturm_backend_create(STURM_MODE_SIMULATE, 17u);
        assert(ctx && "sturm_backend_create failed");
        bridge = new OrkanBridge();
        bridge->allocate(kMaxQubits);
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
        exec_simulate_1q(bridge->state(), kind, q, p);
    }
    void apply_multiq(sturm_gate_kind_t kind, uint32_t q0, uint32_t q1,
                      uint32_t q2 = 0u, double p = 0.0) {
        exec_simulate_multiq(bridge->state(), kind, q0, q1, q2, p);
    }

    cx amp(uint64_t idx) const { return orkan::amplitude(bridge->state(), idx); }
};

// ── Tests 1-2: COUNT depth=0 ─────────────────────────────────────────────────

static void test_count_RY_depth0() {
    ScopedCountCtx sc;
    uint64_t before = sc.gate_count();
    emit_RY_lifted(*sturm_get_thread_context(), /*target=*/0u, kPi / 2.0);
    uint64_t after = sc.gate_count();
    assert(after - before == 1u && "RY depth=0 must emit exactly 1 gate");
    std::printf("PASS test_count_RY_depth0\n");
}

static void test_count_RZ_depth0() {
    ScopedCountCtx sc;
    uint64_t before = sc.gate_count();
    emit_RZ_lifted(*sturm_get_thread_context(), /*target=*/0u, kPi / 2.0);
    uint64_t after = sc.gate_count();
    assert(after - before == 1u && "RZ depth=0 must emit exactly 1 gate");
    std::printf("PASS test_count_RZ_depth0\n");
}

// ── Tests 3-4: COUNT depth=1 ─────────────────────────────────────────────────

static void test_count_RY_depth1() {
    ScopedCountCtx sc;
    sc.ctx->control_stack.push_control(1u);  // push ctrl qubit=1
    uint64_t before = sc.gate_count();
    emit_RY_lifted(*sturm_get_thread_context(), /*target=*/0u, kPi / 2.0);
    uint64_t after = sc.gate_count();
    sc.ctx->control_stack.pop_control();
    assert(after - before == 1u && "CRY depth=1 must emit exactly 1 gate");
    std::printf("PASS test_count_RY_depth1\n");
}

static void test_count_RZ_depth1() {
    ScopedCountCtx sc;
    sc.ctx->control_stack.push_control(1u);
    uint64_t before = sc.gate_count();
    emit_RZ_lifted(*sturm_get_thread_context(), /*target=*/0u, kPi / 2.0);
    uint64_t after = sc.gate_count();
    sc.ctx->control_stack.pop_control();
    assert(after - before == 1u && "CRZ depth=1 must emit exactly 1 gate");
    std::printf("PASS test_count_RZ_depth1\n");
}

// ── Test 5: SIMULATE RY(pi/2) depth=0 ────────────────────────────────────────
//
// Starting from |0>, Ry(pi/2) produces:
//   cos(pi/4)|0> + sin(pi/4)|1> = (1/sqrt(2))(|0> + |1>)

static void test_simulate_RY_depth0() {
    ScopedSimCtx sc;
    sc.reset();
    // qubit 0 starts in |0>
    emit_RY_lifted(*sturm_get_thread_context(), /*target=*/0u, kPi / 2.0);
    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp(0u), {s, 0.0}, "RY(pi/2) depth=0 amp[0]");
    assert_close(sc.amp(1u), {s, 0.0}, "RY(pi/2) depth=0 amp[1]");
    std::printf("PASS test_simulate_RY_depth0\n");
}

// ── Test 6: SIMULATE CRY(pi/2) depth=1 ───────────────────────────────────────
//
// Two-qubit register: qubit0=ctrl, qubit1=tgt.
// Basis index: idx = ctrl_bit | (tgt_bit << 1).
//   idx=0: |ctrl=0, tgt=0>
//   idx=1: |ctrl=1, tgt=0>
//   idx=2: |ctrl=0, tgt=1>
//   idx=3: |ctrl=1, tgt=1>
//
// Starting from |ctrl=1, tgt=0> (idx=1), CRY(pi/2) on tgt when ctrl=1:
//   Ry(pi/2) on tgt: cos(pi/4)|tgt=0> + sin(pi/4)|tgt=1>
// So result: (1/sqrt(2))(|ctrl=1,tgt=0> + |ctrl=1,tgt=1>) = (1/sqrt(2))(idx1 + idx3)

static void test_simulate_CRY_depth1() {
    ScopedSimCtx sc;
    sc.reset();
    // Prepare |ctrl=1, tgt=0>: apply X to qubit 0 (ctrl)
    sc.apply_1q(STURM_GATE_X, 0u);
    // assert setup: idx=1 should have amplitude 1
    assert_close(sc.amp(1u), {1.0, 0.0}, "CRY setup ctrl=1");

    // push ctrl qubit=0 onto control_stack
    sturm_get_thread_context()->control_stack.push_control(0u);
    emit_RY_lifted(*sturm_get_thread_context(), /*target=*/1u, kPi / 2.0);
    sturm_get_thread_context()->control_stack.pop_control();

    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp(0u), {0.0, 0.0}, "CRY depth=1 amp[00]");
    assert_close(sc.amp(1u), {s,   0.0}, "CRY depth=1 amp[10] (ctrl=1,tgt=0)");
    assert_close(sc.amp(2u), {0.0, 0.0}, "CRY depth=1 amp[01]");
    assert_close(sc.amp(3u), {s,   0.0}, "CRY depth=1 amp[11] (ctrl=1,tgt=1)");
    std::printf("PASS test_simulate_CRY_depth1\n");
}

// ── Test 7: SIMULATE RZ(pi/2) depth=0 ────────────────────────────────────────
//
// Starting from H|0> = (|0>+|1>)/sqrt(2), Rz(pi/2) produces:
//   e^{-i*pi/4}|0>/sqrt(2) + e^{+i*pi/4}|1>/sqrt(2)
// i.e.: amp[0] = e^{-i*pi/4}/sqrt(2), amp[1] = e^{+i*pi/4}/sqrt(2)

static void test_simulate_RZ_depth0() {
    ScopedSimCtx sc;
    sc.reset();
    sc.apply_1q(STURM_GATE_H, 0u);  // -> (|0>+|1>)/sqrt(2)
    emit_RZ_lifted(*sturm_get_thread_context(), /*target=*/0u, kPi / 2.0);
    double s = 1.0 / std::sqrt(2.0);
    cx amp0_expected = std::exp(cx{0.0, -kPi / 4.0}) * s;
    cx amp1_expected = std::exp(cx{0.0, +kPi / 4.0}) * s;
    assert_close(sc.amp(0u), amp0_expected, "RZ(pi/2) depth=0 amp[0]");
    assert_close(sc.amp(1u), amp1_expected, "RZ(pi/2) depth=0 amp[1]");
    std::printf("PASS test_simulate_RZ_depth0\n");
}

// ── Test 8: SIMULATE CRZ(pi/2) depth=1 ───────────────────────────────────────
//
// Starting from |ctrl=1, tgt=H|0>> =
//   (1/sqrt(2))(|ctrl=1,tgt=0> + |ctrl=1,tgt=1>) = (idx1 + idx3)/sqrt(2),
// CRZ(pi/2) on tgt (ctrl=1 always active here):
//   amp[idx1] <- e^{-i*pi/4} * (1/sqrt(2))
//   amp[idx3] <- e^{+i*pi/4} * (1/sqrt(2))
// idx=0, idx=2 remain 0.

static void test_simulate_CRZ_depth1() {
    ScopedSimCtx sc;
    sc.reset();
    // Prepare: ctrl=qubit0=|1>, tgt=qubit1=H|0>
    sc.apply_1q(STURM_GATE_X, 0u);   // ctrl = |1>
    sc.apply_1q(STURM_GATE_H, 1u);   // tgt  = (|0>+|1>)/sqrt(2)
    // State: (|ctrl=1,tgt=0> + |ctrl=1,tgt=1>)/sqrt(2) = (idx1+idx3)/sqrt(2)
    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp(1u), {s, 0.0}, "CRZ setup amp[ctrl=1,tgt=0]");
    assert_close(sc.amp(3u), {s, 0.0}, "CRZ setup amp[ctrl=1,tgt=1]");

    sturm_get_thread_context()->control_stack.push_control(0u);
    emit_RZ_lifted(*sturm_get_thread_context(), /*target=*/1u, kPi / 2.0);
    sturm_get_thread_context()->control_stack.pop_control();

    cx exp0 = std::exp(cx{0.0, -kPi / 4.0}) * s;
    cx exp3 = std::exp(cx{0.0, +kPi / 4.0}) * s;
    assert_close(sc.amp(0u), {0.0, 0.0}, "CRZ depth=1 amp[00]");
    assert_close(sc.amp(1u), exp0,        "CRZ depth=1 amp[ctrl=1,tgt=0]");
    assert_close(sc.amp(2u), {0.0, 0.0}, "CRZ depth=1 amp[01]");
    assert_close(sc.amp(3u), exp3,        "CRZ depth=1 amp[ctrl=1,tgt=1]");
    std::printf("PASS test_simulate_CRZ_depth1\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("sturm-8x3: lifted_primitives tests:\n");

    test_count_RY_depth0();
    test_count_RZ_depth0();
    test_count_RY_depth1();
    test_count_RZ_depth1();
    test_simulate_RY_depth0();
    test_simulate_CRY_depth1();
    test_simulate_RZ_depth0();
    test_simulate_CRZ_depth1();

    std::printf("All sturm-8x3 lifted_primitives tests passed.\n");
    return 0;
}
