// test_when_phase.cpp — M2 (PRD v2): controlled phase rotations.
//
// Tests:
//   1. WHEN a: phase(b, theta) — controlled R_y: applies iff ctrl=1.
//   2. WHEN a: phi_add(b, theta) — controlled R_z: applies iff ctrl=1.
//   3. Amplitudes match analytic expected values when control is set.
//   4. No rotation when control is clear.
//
// PRD v2 §5: Phase primitives also lift under WHEN. Controlled phase rotations
// are emitted directly by the backend (they are standard two-qubit gates).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/backend/when_lift.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/state.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

static constexpr double kTol = 1e-9;
using cx = std::complex<double>;

static void assert_close(cx got, cx expected, const char* label) {
    double err = std::abs(got - expected);
    if (err > kTol) {
        std::printf("FAIL %s: got (%g,%g) expected (%g,%g) err=%g\n",
                    label, got.real(), got.imag(),
                    expected.real(), expected.imag(), err);
        assert(false);
    }
}

// ── Test 1: WHEN ctrl=1: phase(target, theta) — control is 1, rotation applies ─

static void test_phase_under_ctrl1_applies() {
    const double theta = M_PI / 3.0;
    const double c = std::cos(theta / 2.0);
    const double sv = std::sin(theta / 2.0);

    // 2-qubit state: q0=ctrl, q1=target.
    // Start in |01⟩ (ctrl=1, target=0). idx=1.
    // R_y(theta) on target conditioned on ctrl=1:
    //   target |0⟩ → cos(t/2)|0⟩ + sin(t/2)|1⟩
    //   Full state: |1⟩ ⊗ (cos(t/2)|0⟩ + sin(t/2)|1⟩)
    //             = cos(t/2)|01⟩ + sin(t/2)|11⟩
    //   idx 1 (|01⟩) = ctrl=1,tgt=0: amplitude cos(t/2)
    //   idx 3 (|11⟩) = ctrl=1,tgt=1: amplitude sin(t/2)

    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b01u); // ctrl=1, tgt=0

    {
        sturm::v2::WhenLift wl(s);
        wl.push_control(0u);     // ctrl = q0
        wl.lift_phase(1u, theta); // phase on q1
    }

    assert_close(s.amplitude(0b01u), {c,  0.0}, "amp[01] after C-Ry ctrl=1");
    assert_close(s.amplitude(0b11u), {sv, 0.0}, "amp[11] after C-Ry ctrl=1");
    assert_close(s.amplitude(0b00u), {0.0, 0.0}, "amp[00] after C-Ry ctrl=1");
    assert_close(s.amplitude(0b10u), {0.0, 0.0}, "amp[10] after C-Ry ctrl=1");
    std::puts("  PASS: WHEN ctrl=1: phase(tgt, theta) applies rotation");
}

// ── Test 2: WHEN ctrl=0: phase(target, theta) — control is 0, no rotation ────

static void test_phase_under_ctrl0_no_change() {
    const double theta = M_PI / 3.0;

    // Start in |00⟩ (ctrl=0, tgt=0). ctrl=0 → no rotation.
    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b00u);

    {
        sturm::v2::WhenLift wl(s);
        wl.push_control(0u);
        wl.lift_phase(1u, theta);
    }

    // State must be unchanged: amplitude[0b00] = 1.
    assert_close(s.amplitude(0b00u), {1.0, 0.0}, "amp[00] after C-Ry ctrl=0");
    assert_close(s.amplitude(0b01u), {0.0, 0.0}, "amp[01] after C-Ry ctrl=0");
    std::puts("  PASS: WHEN ctrl=0: phase(tgt, theta) does nothing");
}

// ── Test 3: WHEN ctrl=1: phi_add(target, theta) — controlled R_z applies ─────

static void test_phi_add_under_ctrl1_applies() {
    const double theta = M_PI / 4.0;
    // R_z(theta)|1⟩ = e^{i*t/2}|1⟩
    const cx expected_phase = {std::cos(theta / 2.0), std::sin(theta / 2.0)};

    // 2-qubit: q0=ctrl, q1=target.
    // Start in |11⟩ (ctrl=1, tgt=1). idx=3.
    // C-R_z(theta) on q1 conditioned on q0=1:
    //   q1=|1⟩ → e^{i*t/2}|1⟩
    //   amplitude[0b11] = e^{i*t/2}

    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b11u); // ctrl=1, tgt=1

    {
        sturm::v2::WhenLift wl(s);
        wl.push_control(0u);
        wl.lift_phi_add(1u, theta);
    }

    assert_close(s.amplitude(0b11u), expected_phase, "amp[11] after C-Rz ctrl=1");
    assert_close(s.amplitude(0b00u), {0.0, 0.0}, "amp[00] must be 0");
    std::puts("  PASS: WHEN ctrl=1: phi_add(tgt, theta) applies phase rotation");
}

// ── Test 4: WHEN ctrl=0: phi_add(target, theta) — no phase change ─────────────

static void test_phi_add_under_ctrl0_no_change() {
    const double theta = M_PI / 4.0;

    // Start in |10⟩ (ctrl=0, tgt=1). idx=2 (bit0=q0=ctrl=0, bit1=q1=tgt=1).
    // ctrl=0 → no R_z applied.
    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b10u); // ctrl=0 (q0=0), tgt=1 (q1=1)

    {
        sturm::v2::WhenLift wl(s);
        wl.push_control(0u);
        wl.lift_phi_add(1u, theta);
    }

    // State unchanged: amplitude[0b10]=1.
    assert_close(s.amplitude(0b10u), {1.0, 0.0}, "amp[10] after C-Rz ctrl=0");
    assert_close(s.amplitude(0b11u), {0.0, 0.0}, "amp[11] must be 0");
    std::puts("  PASS: WHEN ctrl=0: phi_add(tgt, theta) does nothing");
}

// ── Test 5: phase with no control is plain R_y ─────────────────────────────

static void test_phase_no_control_is_plain_ry() {
    const double theta = M_PI / 2.0;
    const double c  = std::cos(theta / 2.0);
    const double sv = std::sin(theta / 2.0);

    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(0u); // |0⟩

    {
        sturm::v2::WhenLift wl(s);
        // No push_control → uncontrolled phase
        wl.lift_phase(0u, theta);
    }

    assert_close(s.amplitude(0u), {c,  0.0}, "amp[0] after R_y no ctrl");
    assert_close(s.amplitude(1u), {sv, 0.0}, "amp[1] after R_y no ctrl");
    std::puts("  PASS: phase with no controls is plain R_y");
}

int main() {
    std::puts("=== test_when_phase ===");
    test_phase_no_control_is_plain_ry();
    test_phase_under_ctrl0_no_change();
    test_phase_under_ctrl1_applies();
    test_phi_add_under_ctrl0_no_change();
    test_phi_add_under_ctrl1_applies();
    std::puts("ALL PASS");
    return 0;
}
