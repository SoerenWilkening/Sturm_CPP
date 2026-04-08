// test_exec_simulate_1q.cpp — M10: SIMULATE executor — 1-qubit gates (TDD).
//
// Tests (per gate): apply to a known input state, compare resulting amplitudes
// against a hand-computed target with tolerance 1e-10.
// Parameterized gates (P, Rx, Ry, Rz) swept at theta in {0, pi/8, pi/4, pi/2, pi}.
//
// Harness: plain assert + main (no gtest).

#include "sturm/backend/exec_simulate.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

static constexpr double kTol = 1e-10;

// ── Helpers ───────────────────────────────────────────────────────────────────

using cx = std::complex<double>;

static void assert_close(cx got, cx expected, const char* label) {
    double err = std::abs(got - expected);
    if (err > kTol) {
        std::printf("FAIL %s: got (%g,%g) expected (%g,%g) err=%g\n",
                    label,
                    got.real(), got.imag(),
                    expected.real(), expected.imag(),
                    err);
        assert(false);
    }
}

// Retrieve amplitude from bridge's state.
static cx amp(const sturm::OrkanBridge& b, uint64_t idx) {
    return orkan::amplitude(b.state(), idx);
}

// ── Gate: X ──────────────────────────────────────────────────────────────────
// X|0⟩ = |1⟩  →  amp[0]=0, amp[1]=1

static void test_x_on_zero() {
    sturm::OrkanBridge b;
    b.allocate(1);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_X, 0u, 0.0);
    assert_close(amp(b, 0), {0.0, 0.0}, "X|0> amp[0]");
    assert_close(amp(b, 1), {1.0, 0.0}, "X|0> amp[1]");
}

// X|1⟩ = |0⟩
static void test_x_on_one() {
    sturm::OrkanBridge b;
    b.allocate(1);
    orkan::apply_x(b.state(), 0); // prepare |1⟩
    sturm::exec_simulate_1q(b.state(), STURM_GATE_X, 0u, 0.0);
    assert_close(amp(b, 0), {1.0, 0.0}, "X|1> amp[0]");
    assert_close(amp(b, 1), {0.0, 0.0}, "X|1> amp[1]");
}

// ── Gate: Y ──────────────────────────────────────────────────────────────────
// Y = [[0,-i],[i,0]]
// Y|0⟩ = i|1⟩  →  amp[0]=0, amp[1]=i

static void test_y_on_zero() {
    sturm::OrkanBridge b;
    b.allocate(1);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_Y, 0u, 0.0);
    assert_close(amp(b, 0), {0.0, 0.0}, "Y|0> amp[0]");
    assert_close(amp(b, 1), {0.0, 1.0}, "Y|0> amp[1]");
}

// Y|1⟩ = -i|0⟩
static void test_y_on_one() {
    sturm::OrkanBridge b;
    b.allocate(1);
    orkan::apply_x(b.state(), 0); // prepare |1⟩
    sturm::exec_simulate_1q(b.state(), STURM_GATE_Y, 0u, 0.0);
    assert_close(amp(b, 0), {0.0, -1.0}, "Y|1> amp[0]");
    assert_close(amp(b, 1), {0.0,  0.0}, "Y|1> amp[1]");
}

// ── Gate: Z ──────────────────────────────────────────────────────────────────
// Z = diag(1,-1)
// Z|0⟩ = |0⟩

static void test_z_on_zero() {
    sturm::OrkanBridge b;
    b.allocate(1);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_Z, 0u, 0.0);
    assert_close(amp(b, 0), {1.0, 0.0}, "Z|0> amp[0]");
    assert_close(amp(b, 1), {0.0, 0.0}, "Z|0> amp[1]");
}

// Z|1⟩ = -|1⟩
static void test_z_on_one() {
    sturm::OrkanBridge b;
    b.allocate(1);
    orkan::apply_x(b.state(), 0); // prepare |1⟩
    sturm::exec_simulate_1q(b.state(), STURM_GATE_Z, 0u, 0.0);
    assert_close(amp(b, 0), { 0.0, 0.0}, "Z|1> amp[0]");
    assert_close(amp(b, 1), {-1.0, 0.0}, "Z|1> amp[1]");
}

// ── Gate: H ──────────────────────────────────────────────────────────────────
// H = 1/sqrt(2) * [[1,1],[1,-1]]
// H|0⟩ = (|0⟩+|1⟩)/sqrt(2)

static void test_h_on_zero() {
    sturm::OrkanBridge b;
    b.allocate(1);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_H, 0u, 0.0);
    double s = 1.0 / std::sqrt(2.0);
    assert_close(amp(b, 0), {s, 0.0}, "H|0> amp[0]");
    assert_close(amp(b, 1), {s, 0.0}, "H|0> amp[1]");
}

// H|1⟩ = (|0⟩-|1⟩)/sqrt(2)
static void test_h_on_one() {
    sturm::OrkanBridge b;
    b.allocate(1);
    orkan::apply_x(b.state(), 0); // prepare |1⟩
    sturm::exec_simulate_1q(b.state(), STURM_GATE_H, 0u, 0.0);
    double s = 1.0 / std::sqrt(2.0);
    assert_close(amp(b, 0), { s, 0.0}, "H|1> amp[0]");
    assert_close(amp(b, 1), {-s, 0.0}, "H|1> amp[1]");
}

// ── Gate: S ──────────────────────────────────────────────────────────────────
// S = diag(1, i)
// S|0⟩ = |0⟩

static void test_s_on_zero() {
    sturm::OrkanBridge b;
    b.allocate(1);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_S, 0u, 0.0);
    assert_close(amp(b, 0), {1.0, 0.0}, "S|0> amp[0]");
    assert_close(amp(b, 1), {0.0, 0.0}, "S|0> amp[1]");
}

// S|1⟩ = i|1⟩
static void test_s_on_one() {
    sturm::OrkanBridge b;
    b.allocate(1);
    orkan::apply_x(b.state(), 0);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_S, 0u, 0.0);
    assert_close(amp(b, 0), {0.0, 0.0}, "S|1> amp[0]");
    assert_close(amp(b, 1), {0.0, 1.0}, "S|1> amp[1]");
}

// ── Gate: T ──────────────────────────────────────────────────────────────────
// T = diag(1, e^{i*pi/4})
// T|0⟩ = |0⟩

static void test_t_on_zero() {
    sturm::OrkanBridge b;
    b.allocate(1);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_T, 0u, 0.0);
    assert_close(amp(b, 0), {1.0, 0.0}, "T|0> amp[0]");
    assert_close(amp(b, 1), {0.0, 0.0}, "T|0> amp[1]");
}

// T|1⟩ = e^{i*pi/4}|1⟩ = (1/sqrt(2))(1+i)|1⟩
static void test_t_on_one() {
    sturm::OrkanBridge b;
    b.allocate(1);
    orkan::apply_x(b.state(), 0);
    sturm::exec_simulate_1q(b.state(), STURM_GATE_T, 0u, 0.0);
    double s = 1.0 / std::sqrt(2.0);
    assert_close(amp(b, 0), {0.0, 0.0}, "T|1> amp[0]");
    assert_close(amp(b, 1), {s,   s},   "T|1> amp[1]");
}

// ── Gate: P(theta) ────────────────────────────────────────────────────────────
// P(theta) = diag(1, e^{i*theta})
// P(theta)|0⟩ = |0⟩  (unchanged)

static void test_p_on_zero_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        sturm::exec_simulate_1q(b.state(), STURM_GATE_P, 0u, theta);
        assert_close(amp(b, 0), {1.0, 0.0}, "P|0> amp[0]");
        assert_close(amp(b, 1), {0.0, 0.0}, "P|0> amp[1]");
    }
}

// P(theta)|1⟩ = e^{i*theta}|1⟩
static void test_p_on_one_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        orkan::apply_x(b.state(), 0); // prepare |1⟩
        sturm::exec_simulate_1q(b.state(), STURM_GATE_P, 0u, theta);
        cx expected_1 = {std::cos(theta), std::sin(theta)};
        assert_close(amp(b, 0), {0.0, 0.0}, "P(t)|1> amp[0]");
        assert_close(amp(b, 1), expected_1, "P(t)|1> amp[1]");
    }
}

// ── Gate: Rx(theta) ───────────────────────────────────────────────────────────
// Rx(theta) = [[cos(t/2), -i*sin(t/2)],[-i*sin(t/2), cos(t/2)]]
// Rx(theta)|0⟩: amp[0]=cos(t/2), amp[1]=-i*sin(t/2)

static void test_rx_on_zero_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        sturm::exec_simulate_1q(b.state(), STURM_GATE_RX, 0u, theta);
        cx e0 = {std::cos(theta / 2.0), 0.0};
        cx e1 = {0.0, -std::sin(theta / 2.0)};
        assert_close(amp(b, 0), e0, "Rx|0> amp[0]");
        assert_close(amp(b, 1), e1, "Rx|0> amp[1]");
    }
}

// Rx(theta)|1⟩: amp[0]=-i*sin(t/2), amp[1]=cos(t/2)
static void test_rx_on_one_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        orkan::apply_x(b.state(), 0); // prepare |1⟩
        sturm::exec_simulate_1q(b.state(), STURM_GATE_RX, 0u, theta);
        cx e0 = {0.0, -std::sin(theta / 2.0)};
        cx e1 = {std::cos(theta / 2.0), 0.0};
        assert_close(amp(b, 0), e0, "Rx|1> amp[0]");
        assert_close(amp(b, 1), e1, "Rx|1> amp[1]");
    }
}

// ── Gate: Ry(theta) ───────────────────────────────────────────────────────────
// Ry(theta) = [[cos(t/2), -sin(t/2)],[sin(t/2), cos(t/2)]]
// Ry(theta)|0⟩: amp[0]=cos(t/2), amp[1]=sin(t/2)

static void test_ry_on_zero_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        sturm::exec_simulate_1q(b.state(), STURM_GATE_RY, 0u, theta);
        cx e0 = {std::cos(theta / 2.0), 0.0};
        cx e1 = {std::sin(theta / 2.0), 0.0};
        assert_close(amp(b, 0), e0, "Ry|0> amp[0]");
        assert_close(amp(b, 1), e1, "Ry|0> amp[1]");
    }
}

// Ry(theta)|1⟩: amp[0]=-sin(t/2), amp[1]=cos(t/2)
static void test_ry_on_one_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        orkan::apply_x(b.state(), 0); // prepare |1⟩
        sturm::exec_simulate_1q(b.state(), STURM_GATE_RY, 0u, theta);
        cx e0 = {-std::sin(theta / 2.0), 0.0};
        cx e1 = { std::cos(theta / 2.0), 0.0};
        assert_close(amp(b, 0), e0, "Ry|1> amp[0]");
        assert_close(amp(b, 1), e1, "Ry|1> amp[1]");
    }
}

// ── Gate: Rz(theta) ───────────────────────────────────────────────────────────
// Rz(theta) = diag(e^{-i*t/2}, e^{i*t/2})
// Rz(theta)|0⟩: amp[0]=e^{-i*t/2}, amp[1]=0

static void test_rz_on_zero_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        sturm::exec_simulate_1q(b.state(), STURM_GATE_RZ, 0u, theta);
        cx e0 = {std::cos(theta / 2.0), -std::sin(theta / 2.0)};
        assert_close(amp(b, 0), e0,       "Rz|0> amp[0]");
        assert_close(amp(b, 1), {0.0, 0.0}, "Rz|0> amp[1]");
    }
}

// Rz(theta)|1⟩: amp[0]=0, amp[1]=e^{i*t/2}
static void test_rz_on_one_swept() {
    static const double thetas[] = {0.0, M_PI/8, M_PI/4, M_PI/2, M_PI};
    for (double theta : thetas) {
        sturm::OrkanBridge b;
        b.allocate(1);
        orkan::apply_x(b.state(), 0); // prepare |1⟩
        sturm::exec_simulate_1q(b.state(), STURM_GATE_RZ, 0u, theta);
        cx e1 = {std::cos(theta / 2.0), std::sin(theta / 2.0)};
        assert_close(amp(b, 0), {0.0, 0.0}, "Rz|1> amp[0]");
        assert_close(amp(b, 1), e1,         "Rz|1> amp[1]");
    }
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    // X
    test_x_on_zero();
    test_x_on_one();

    // Y
    test_y_on_zero();
    test_y_on_one();

    // Z
    test_z_on_zero();
    test_z_on_one();

    // H
    test_h_on_zero();
    test_h_on_one();

    // S
    test_s_on_zero();
    test_s_on_one();

    // T
    test_t_on_zero();
    test_t_on_one();

    // P (swept)
    test_p_on_zero_swept();
    test_p_on_one_swept();

    // Rx (swept)
    test_rx_on_zero_swept();
    test_rx_on_one_swept();

    // Ry (swept)
    test_ry_on_zero_swept();
    test_ry_on_one_swept();

    // Rz (swept)
    test_rz_on_zero_swept();
    test_rz_on_one_swept();

    return 0;
}
