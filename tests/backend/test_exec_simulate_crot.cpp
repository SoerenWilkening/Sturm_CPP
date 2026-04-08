// test_exec_simulate_crot.cpp — M12: SIMULATE executor — CRx/CRy/CRz decomposition.
//
// Tests (TDD — written before implementation):
//
//   For each axis (x, y, z) and θ ∈ {π/4, π/2, π, π/8}:
//     1. Prepare each of the 4 computational basis states of a 2-qubit register.
//     2. Apply exec_simulate_crot(sv, STURM_GATE_CRx/CRy/CRz, ctrl, tgt, θ).
//     3. Compare all 4 amplitudes against a hand-computed reference unitary.
//
// Reference unitaries (ctrl=qubit0, tgt=qubit1, basis order |00>,|01>,|10>,|11>
// i.e. idx = ctrl_bit + 2*tgt_bit):
//
//   CRx(θ): upper 2x2 = I (ctrl=0), lower 2x2 = Rx(θ) (ctrl=1)
//   CRy(θ): upper 2x2 = I (ctrl=0), lower 2x2 = Ry(θ) (ctrl=1)
//   CRz(θ): upper 2x2 = I (ctrl=0), lower 2x2 = Rz(θ) (ctrl=1)
//
// Decompositions used (verified against 4×4 reference unitaries):
//   CRy(θ): CX(c,t); Ry(-θ/2)(t); CX(c,t); Ry(+θ/2)(t)
//   CRz(θ): CX(c,t); Rz(-θ/2)(t); CX(c,t); Rz(+θ/2)(t)
//   CRx(θ): Rz(+π/2)(t); CX(c,t); Ry(-θ/2)(t); CX(c,t); Ry(+θ/2)(t); Rz(-π/2)(t)
//           [basis change: Rz wrapping converts CRy sandwich to CRx]
//
// Tolerance: 1e-10 (PRD §12 and implementation plan).
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

using cx = std::complex<double>;

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

static cx amp(const sturm::OrkanBridge& b, uint64_t idx) {
    return orkan::amplitude(b.state(), idx);
}

// ── Reference unitary application ────────────────────────────────────────────
//
// 2-qubit register: qubit0=ctrl (bit 0 = LSB), qubit1=tgt (bit 1).
// Basis index: idx = (ctrl_bit) | (tgt_bit << 1)
//   idx=0: ctrl=0, tgt=0  |00>
//   idx=1: ctrl=1, tgt=0  |10>  ← ctrl=1 subspace
//   idx=2: ctrl=0, tgt=1  |01>
//   idx=3: ctrl=1, tgt=1  |11>  ← ctrl=1 subspace
//
// CR_axis(θ) acts as identity on {idx=0, idx=2} (ctrl=0 subspace) and as
// R_axis(θ) on {idx=1, idx=3} (ctrl=1 subspace), where the "row" within the
// ctrl=1 subspace is the tgt bit.
//
// R_axis(θ) matrix (2×2, row=tgt_out, col=tgt_in):
//   Rx(θ): [[cos(θ/2),   -i·sin(θ/2)],
//            [-i·sin(θ/2), cos(θ/2)  ]]
//   Ry(θ): [[cos(θ/2),  -sin(θ/2)],
//            [sin(θ/2),   cos(θ/2) ]]
//   Rz(θ): [[e^{-iθ/2}, 0         ],
//            [0,          e^{+iθ/2}]]
//
// Within the ctrl=1 subspace the tgt mapping is:
//   tgt=0 (in) → idx=1, tgt=1 (in) → idx=3
// So R_axis row/col indices map as: 0↔idx1, 1↔idx3.

static void apply_crx_ref(cx* out, const cx* in, double theta) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    // ctrl=0 subspace: identity
    out[0] = in[0];
    out[2] = in[2];
    // ctrl=1 subspace: Rx(θ) on tgt; tgt=0 ↔ idx 1, tgt=1 ↔ idx 3
    out[1] = cx{c, 0.0} * in[1] + cx{0.0, -s} * in[3];
    out[3] = cx{0.0, -s} * in[1] + cx{c, 0.0} * in[3];
}

static void apply_cry_ref(cx* out, const cx* in, double theta) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    // ctrl=0 subspace: identity
    out[0] = in[0];
    out[2] = in[2];
    // ctrl=1 subspace: Ry(θ) on tgt; tgt=0 ↔ idx 1, tgt=1 ↔ idx 3
    out[1] = cx{c, 0.0} * in[1] + cx{-s, 0.0} * in[3];
    out[3] = cx{s, 0.0} * in[1] + cx{c,  0.0} * in[3];
}

static void apply_crz_ref(cx* out, const cx* in, double theta) {
    // ctrl=0 subspace: identity
    out[0] = in[0];
    out[2] = in[2];
    // ctrl=1 subspace: Rz(θ) on tgt; tgt=0 ↔ idx 1, tgt=1 ↔ idx 3
    out[1] = std::exp(cx{0.0, -theta / 2.0}) * in[1];
    out[3] = std::exp(cx{0.0, +theta / 2.0}) * in[3];
}

// ── Per-axis test driver ──────────────────────────────────────────────────────
//
// For each of the 4 basis states as input, verify decomposition output matches
// reference unitary.  ctrl=0 (qubit 0), tgt=1 (qubit 1).

static void test_crot_axis(sturm_gate_kind_t kind, double theta,
                           void (*ref_fn)(cx*, const cx*, double),
                           const char* name) {
    for (uint64_t input_idx = 0; input_idx < 4; ++input_idx) {
        // Build input amplitude vector
        cx in_amp[4] = {cx{0}, cx{0}, cx{0}, cx{0}};
        in_amp[input_idx] = cx{1.0, 0.0};

        // Compute reference output
        cx expected[4];
        ref_fn(expected, in_amp, theta);

        // Run decomposition
        sturm::OrkanBridge b;
        b.allocate(2);
        // Set basis state by flipping bits from |00>
        if ((input_idx >> 0) & 1u) orkan::apply_x(b.state(), 0); // ctrl bit
        if ((input_idx >> 1) & 1u) orkan::apply_x(b.state(), 1); // tgt  bit

        // Apply via exec_simulate_crot (to be implemented)
        sturm::exec_simulate_crot(b.state(), kind, /*ctrl=*/0u, /*tgt=*/1u, theta);

        // Compare amplitudes
        char label[128];
        for (uint64_t idx = 0; idx < 4; ++idx) {
            std::snprintf(label, sizeof(label),
                          "%s theta=%.4f input=%llu amp[%llu]",
                          name, theta,
                          (unsigned long long)input_idx,
                          (unsigned long long)idx);
            assert_close(amp(b, idx), expected[idx], label);
        }
    }
}

// ── Individual axis tests ─────────────────────────────────────────────────────

static void test_crx() {
    const double angles[] = {M_PI / 8.0, M_PI / 4.0, M_PI / 2.0, M_PI};
    for (double theta : angles) {
        test_crot_axis(STURM_GATE_CRX, theta, apply_crx_ref, "CRx");
    }
    std::printf("  CRx: PASS\n");
}

static void test_cry() {
    const double angles[] = {M_PI / 8.0, M_PI / 4.0, M_PI / 2.0, M_PI};
    for (double theta : angles) {
        test_crot_axis(STURM_GATE_CRY, theta, apply_cry_ref, "CRy");
    }
    std::printf("  CRy: PASS\n");
}

static void test_crz() {
    const double angles[] = {M_PI / 8.0, M_PI / 4.0, M_PI / 2.0, M_PI};
    for (double theta : angles) {
        test_crot_axis(STURM_GATE_CRZ, theta, apply_crz_ref, "CRz");
    }
    std::printf("  CRz: PASS\n");
}

// ── Identity: θ=0 leaves state unchanged ─────────────────────────────────────

static void test_crot_zero_angle() {
    for (auto kind : {STURM_GATE_CRX, STURM_GATE_CRY, STURM_GATE_CRZ}) {
        for (uint64_t input_idx = 0; input_idx < 4; ++input_idx) {
            sturm::OrkanBridge b;
            b.allocate(2);
            if ((input_idx >> 0) & 1u) orkan::apply_x(b.state(), 0);
            if ((input_idx >> 1) & 1u) orkan::apply_x(b.state(), 1);

            sturm::exec_simulate_crot(b.state(), kind, 0u, 1u, 0.0);

            // θ=0 → identity
            for (uint64_t idx = 0; idx < 4; ++idx) {
                cx want = (idx == input_idx) ? cx{1.0, 0.0} : cx{0.0, 0.0};
                char label[64];
                std::snprintf(label, sizeof(label),
                              "zero_angle kind=%d input=%llu amp[%llu]",
                              (int)kind,
                              (unsigned long long)input_idx,
                              (unsigned long long)idx);
                assert_close(amp(b, idx), want, label);
            }
        }
    }
    std::printf("  θ=0 identity: PASS\n");
}

// ── exec_simulate_crot rejects non-CRx/CRy/CRz kinds ─────────────────────────

static void test_invalid_kind_throws() {
    sturm::OrkanBridge b;
    b.allocate(2);
    bool caught = false;
    try {
        sturm::exec_simulate_crot(b.state(), STURM_GATE_CX, 0u, 1u, 1.0);
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    assert(caught && "exec_simulate_crot must throw for non-CRx/CRy/CRz kinds");
    std::printf("  invalid kind throws: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M12 CRx/CRy/CRz decomposition tests:\n");
    test_crx();
    test_cry();
    test_crz();
    test_crot_zero_angle();
    test_invalid_kind_throws();
    std::printf("All M12 CRx/CRy/CRz decomposition tests passed.\n");
    return 0;
}
