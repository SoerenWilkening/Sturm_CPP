// test_primitives_phase.cpp — M1 (PRD v2): phase primitives R_y and R_z.
//
// Tests:
//   1. phase(q, theta) = R_y(theta): verify amplitudes on |0> and |+>.
//   2. phi_add(q, theta) = R_z(theta): verify phase change on |1>.
//
// Analytic expected values:
//   R_y(theta)|0> = cos(t/2)|0> + sin(t/2)|1>
//   R_z(theta)|1> = e^{i*t/2}|1>
//
// Harness: plain assert + main (no gtest).

#include "sturm/backend/primitives.hpp"
#include "sturm/backend/state.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

static constexpr double kTol = 1e-10;
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

// ── phase = R_y(theta) on |0> ─────────────────────────────────────────────────
// Expected: cos(t/2)|0> + sin(t/2)|1>
static void test_phase_ry_from_zero() {
    const double theta = M_PI / 3.0; // 60 degrees
    const double c = std::cos(theta / 2.0);
    const double sv = std::sin(theta / 2.0);

    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(0);

    sturm::v2::primitive_phase(s, 0, theta);

    assert_close(s.amplitude(0), {c,  0.0}, "phase R_y amp[0]");
    assert_close(s.amplitude(1), {sv, 0.0}, "phase R_y amp[1]");
    std::puts("  PASS: phase (R_y) on |0>");
}

// ── phase = R_y(pi) on |0> gives |1> (up to global phase) ───────────────────
static void test_phase_ry_pi() {
    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(0);

    sturm::v2::primitive_phase(s, 0, M_PI);

    // R_y(pi)|0> = sin(pi/2)|1> = |1>  and amp[0]=cos(pi/2)=0
    assert_close(s.amplitude(0), {0.0, 0.0}, "phase R_y(pi) amp[0]");
    assert_close(s.amplitude(1), {1.0, 0.0}, "phase R_y(pi) amp[1]");
    std::puts("  PASS: phase (R_y(pi)) |0> = |1>");
}

// ── phi_add = R_z(theta) on |1> ──────────────────────────────────────────────
// R_z(theta)|1> = e^{i*t/2}|1>
static void test_phi_add_rz_on_one() {
    const double theta = M_PI / 4.0; // 45 degrees
    const cx expected_phase = {std::cos(theta / 2.0), std::sin(theta / 2.0)};

    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(1); // |1>

    sturm::v2::primitive_phi_add(s, 0, theta);

    // amp[0] should be 0 (we started in |1>)
    assert_close(s.amplitude(0), {0.0, 0.0}, "phi_add R_z amp[0]");
    assert_close(s.amplitude(1), expected_phase, "phi_add R_z amp[1]");
    std::puts("  PASS: phi_add (R_z) on |1>");
}

// ── phi_add = R_z(theta) on |0> — only phase on |0> component ────────────────
// R_z(theta)|0> = e^{-i*t/2}|0>
static void test_phi_add_rz_on_zero() {
    const double theta = M_PI / 6.0;
    const cx expected_phase = {std::cos(theta / 2.0), -std::sin(theta / 2.0)};

    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(0); // |0>

    sturm::v2::primitive_phi_add(s, 0, theta);

    assert_close(s.amplitude(0), expected_phase, "phi_add R_z on |0> amp[0]");
    assert_close(s.amplitude(1), {0.0, 0.0},    "phi_add R_z on |0> amp[1]");
    std::puts("  PASS: phi_add (R_z) on |0>");
}

// ── Roundtrip: phase then -phase back to |0> ─────────────────────────────────
static void test_phase_roundtrip() {
    const double theta = 1.23456;

    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(0);

    sturm::v2::primitive_phase(s, 0, theta);
    sturm::v2::primitive_phase(s, 0, -theta);

    assert_close(s.amplitude(0), {1.0, 0.0}, "phase roundtrip amp[0]");
    assert_close(s.amplitude(1), {0.0, 0.0}, "phase roundtrip amp[1]");
    std::puts("  PASS: phase + inverse roundtrip => |0>");
}

int main() {
    std::puts("=== test_primitives_phase ===");
    test_phase_ry_from_zero();
    test_phase_ry_pi();
    test_phi_add_rz_on_one();
    test_phi_add_rz_on_zero();
    test_phase_roundtrip();
    std::puts("ALL PASS");
    return 0;
}
