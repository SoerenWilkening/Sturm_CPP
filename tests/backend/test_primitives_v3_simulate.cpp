// test_primitives_v3_simulate.cpp — M11: primitives_v3 SIMULATE mode test.
//
// Verifies that the statevector matches expected amplitudes after calling
// each v3 primitive in SIMULATE mode (via OrkanBridge).
//
// Tests:
//   - X:   |0> -> |1>   (flip qubit 0)
//   - CX:  |10> -> |11> (CNOT ctrl=0, tgt=1; ctrl=|1> flips tgt)
//   - CCX: |110> -> |111> (Toffoli: ctrl0=0, ctrl1=1, tgt=2; both ctrls=|1> flip tgt)
//   - Ry:  |0> -Ry(pi/2)-> cos(pi/4)|0> + sin(pi/4)|1>
//   - Rz:  |+> -Rz(pi/2)-> phase rotation
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/exec_simulate.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

static constexpr double kTol = 1e-10;
using cx = std::complex<double>;

// ── Amplitude helper ──────────────────────────────────────────────────────────

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

// ── Fixture: context with OrkanBridge ────────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, 17u);
        assert(ctx);
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

// ── Test: primitive_X — |0> -> |1> ───────────────────────────────────────────
//
// Start in |0>.  After X on qubit 0: state is |1>.
// With 1 qubit: amp[0] = 0, amp[1] = 1.

static void test_primitive_X_simulate() {
    SimCtx sc{1u};

    // Initial state: |0>, amp[0]=1, amp[1]=0
    assert_close(orkan::amplitude(sc.sv(), 0), cx{1.0, 0.0}, "X before: amp[0]");
    assert_close(orkan::amplitude(sc.sv(), 1), cx{0.0, 0.0}, "X before: amp[1]");

    sturm::primitive_X(*sc.ctx, 0u);

    // After X: |1>, amp[0]=0, amp[1]=1
    assert_close(orkan::amplitude(sc.sv(), 0), cx{0.0, 0.0}, "X after: amp[0]");
    assert_close(orkan::amplitude(sc.sv(), 1), cx{1.0, 0.0}, "X after: amp[1]");
    assert(sc.ctx->gate_count == 1u);
    std::printf("  primitive_X SIMULATE: PASS\n");
}

// ── Test: primitive_XOR (CNOT) ────────────────────────────────────────────────
//
// Use 2 qubits.  Set ctrl (q0) = |1> via X, tgt (q1) = |0>.
// State: |01> (q0=1, q1=0), idx=1 (q0 bit 0, q1 bit 1).
// After CNOT(ctrl=0, tgt=1): |11>, idx=3.

static void test_primitive_XOR_simulate() {
    SimCtx sc{2u};

    // Set qubit 0 to |1>
    orkan::apply_x(sc.sv(), 0u);
    // State: |q1=0, q0=1> = |01> = idx 1
    assert_close(orkan::amplitude(sc.sv(), 1), cx{1.0, 0.0}, "XOR before: amp[1]");

    sturm::primitive_XOR(*sc.ctx, 0u, 1u);  // CNOT ctrl=0, tgt=1

    // After CNOT: qubit 1 flipped because qubit 0 is |1>.
    // State: |q1=1, q0=1> = |11> = idx 3
    assert_close(orkan::amplitude(sc.sv(), 0), cx{0.0, 0.0}, "XOR after: amp[0]");
    assert_close(orkan::amplitude(sc.sv(), 1), cx{0.0, 0.0}, "XOR after: amp[1]");
    assert_close(orkan::amplitude(sc.sv(), 2), cx{0.0, 0.0}, "XOR after: amp[2]");
    assert_close(orkan::amplitude(sc.sv(), 3), cx{1.0, 0.0}, "XOR after: amp[3]");
    assert(sc.ctx->gate_count == 1u);
    std::printf("  primitive_XOR (CX) SIMULATE: PASS\n");
}

// ── Test: primitive_AND (Toffoli/CCX) ────────────────────────────────────────
//
// Use 3 qubits. Set ctrl0 (q0) = |1> and ctrl1 (q1) = |1>, tgt (q2) = |0>.
// State: |q2=0, q1=1, q0=1> = |011> = idx (1 + 2) = 3.
// After CCX(ctrl0=0, ctrl1=1, tgt=2): q2 flipped.
// State: |q2=1, q1=1, q0=1> = |111> = idx 7.

static void test_primitive_AND_simulate() {
    SimCtx sc{3u};

    // Set qubit 0 and qubit 1 to |1>
    orkan::apply_x(sc.sv(), 0u);
    orkan::apply_x(sc.sv(), 1u);
    // idx = (1 << 0) | (1 << 1) = 3
    assert_close(orkan::amplitude(sc.sv(), 3), cx{1.0, 0.0}, "CCX before: amp[3]");

    sturm::primitive_AND(*sc.ctx, 0u, 1u, 2u);  // CCX ctrl0=0, ctrl1=1, tgt=2

    // After CCX: q2 flipped (both ctrls were |1>).
    // idx = (1<<0) | (1<<1) | (1<<2) = 7
    assert_close(orkan::amplitude(sc.sv(), 7), cx{1.0, 0.0}, "CCX after: amp[7]");
    // All other amplitudes are 0
    for (int i = 0; i < 8; ++i) {
        if (i != 7) {
            char label[64];
            std::snprintf(label, sizeof(label), "CCX after: amp[%d]", i);
            assert_close(orkan::amplitude(sc.sv(), i), cx{0.0, 0.0}, label);
        }
    }
    assert(sc.ctx->gate_count == 1u);
    std::printf("  primitive_AND (CCX) SIMULATE: PASS\n");
}

// ── Test: primitive_phase (Ry) ────────────────────────────────────────────────
//
// Start in |0>.  After Ry(pi/2) on qubit 0:
// R_y(pi/2)|0> = cos(pi/4)|0> + sin(pi/4)|1>
//              = (1/sqrt(2))|0> + (1/sqrt(2))|1>

static void test_primitive_phase_simulate() {
    SimCtx sc{1u};

    const double theta = M_PI / 2.0;
    const double c = std::cos(theta / 2.0);  // cos(pi/4) = 1/sqrt(2)
    const double s = std::sin(theta / 2.0);  // sin(pi/4) = 1/sqrt(2)

    sturm::primitive_phase(*sc.ctx, 0u, theta);

    // R_y(theta)|0> = cos(theta/2)|0> + sin(theta/2)|1>
    assert_close(orkan::amplitude(sc.sv(), 0), cx{c, 0.0}, "Ry: amp[0]");
    assert_close(orkan::amplitude(sc.sv(), 1), cx{s, 0.0}, "Ry: amp[1]");
    assert(sc.ctx->gate_count == 1u);
    std::printf("  primitive_phase (Ry) SIMULATE: PASS\n");
}

// ── Test: primitive_phi_add (Rz) ─────────────────────────────────────────────
//
// Start with qubit in |+> = H|0> = (|0>+|1>)/sqrt(2).
// After Rz(pi/2): R_z(pi/2)|+> = e^{-i*pi/4}|0>/sqrt(2) + e^{+i*pi/4}|1>/sqrt(2)

static void test_primitive_phi_add_simulate() {
    SimCtx sc{1u};

    // Apply H to put in |+>
    orkan::apply_h(sc.sv(), 0u);
    double inv_sqrt2 = 1.0 / std::sqrt(2.0);

    const double theta = M_PI / 2.0;
    sturm::primitive_phi_add(*sc.ctx, 0u, theta);

    // R_z(theta) = diag(e^{-i*theta/2}, e^{+i*theta/2})
    // amp[0] = e^{-i*theta/2} / sqrt(2)
    // amp[1] = e^{+i*theta/2} / sqrt(2)
    cx exp_neg = std::exp(cx{0.0, -theta / 2.0});
    cx exp_pos = std::exp(cx{0.0, +theta / 2.0});

    assert_close(orkan::amplitude(sc.sv(), 0), inv_sqrt2 * exp_neg, "Rz: amp[0]");
    assert_close(orkan::amplitude(sc.sv(), 1), inv_sqrt2 * exp_pos, "Rz: amp[1]");
    assert(sc.ctx->gate_count == 1u);
    std::printf("  primitive_phi_add (Rz) SIMULATE: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M11 primitives_v3 SIMULATE tests:\n");
    test_primitive_X_simulate();
    test_primitive_XOR_simulate();
    test_primitive_AND_simulate();
    test_primitive_phase_simulate();
    test_primitive_phi_add_simulate();
    std::printf("All M11 primitives_v3 SIMULATE tests passed.\n");
    return 0;
}
