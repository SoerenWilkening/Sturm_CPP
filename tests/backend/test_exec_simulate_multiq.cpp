// test_exec_simulate_multiq.cpp — M11: SIMULATE executor — 2/3-qubit gates.
//
// Tests:
//   1. Bell-pair via H on qubit 0 then CX(ctrl=0, tgt=1):
//        start |00>, after H|0> + CX => (|00>+|11>)/sqrt(2)
//   2. Toffoli (CCX) truth table over all 8 basis states |abc> -> |ab, c XOR (a AND b)>
//   3. SWAP exchanges amplitudes of two qubits.
//   4. CY: ctrl=1 applies Y on target.
//   5. CZ: ctrl=1 applies Z-phase on target.
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

static cx amp(const sturm::OrkanBridge& b, uint64_t idx) {
    return orkan::amplitude(b.state(), idx);
}

// ── Bell pair via H, CX ───────────────────────────────────────────────────────
// Start |00> (idx=0b00=0).
// H on qubit 0 => (|0>+|1>)/sqrt(2) x |0> = (|00>+|10>)/sqrt(2)
// CX(ctrl=0, tgt=1): |00>->|00>, |10>->|11>
// Final: (|00>+|11>)/sqrt(2)
// Basis: qubit 0 is bit 0 (LSB), qubit 1 is bit 1.
//   |00> => idx=0, |01> => idx=1, |10> => idx=2, |11> => idx=3

static void test_bell_pair() {
    sturm::OrkanBridge b;
    b.allocate(2);

    // H on qubit 0
    sturm::exec_simulate_1q(b.state(), STURM_GATE_H, 0u, 0.0);
    // CX(ctrl=0, tgt=1)
    sturm::exec_simulate_multiq(b.state(), STURM_GATE_CX, 0u, 1u, 0u, 0.0);

    double s = 1.0 / std::sqrt(2.0);
    // (|00> + |11>)/sqrt(2): amp[0]=1/sqrt2, amp[1]=0, amp[2]=0, amp[3]=1/sqrt2
    assert_close(amp(b, 0), {s,   0.0}, "Bell amp[00]");
    assert_close(amp(b, 1), {0.0, 0.0}, "Bell amp[01]");
    assert_close(amp(b, 2), {0.0, 0.0}, "Bell amp[10]");
    assert_close(amp(b, 3), {s,   0.0}, "Bell amp[11]");
}

// ── Toffoli (CCX) truth table ─────────────────────────────────────────────────
// CCX(ctrl0, ctrl1, tgt): |abc> -> |ab, c XOR (a AND b)>
// We use qubits 0=ctrl0, 1=ctrl1, 2=tgt.
// Basis index: bit 0 = qubit 0, bit 1 = qubit 1, bit 2 = qubit 2.
//   idx = a + 2*b + 4*c
// Expected table (input idx -> output idx):
//   |000>=0 -> |000>=0  (c=0, a&b=0 -> 0 XOR 0 = 0)
//   |001>=1 -> |001>=1  (c=0, a=1,b=0 -> 0 XOR 0 = 0)  -- qubit0=1
//   |010>=2 -> |010>=2  (c=0, a=0,b=1 -> 0 XOR 0 = 0)  -- qubit1=1
//   |011>=3 -> |111>=7  (c=0, a=1,b=1 -> 0 XOR 1 = 1)  -- qubit0=1,qubit1=1 => flip qubit2
//   |100>=4 -> |100>=4  (c=1, a=0,b=0 -> 1 XOR 0 = 1)
//   |101>=5 -> |101>=5  (c=1, a=1,b=0 -> 1 XOR 0 = 1)
//   |110>=6 -> |110>=6  (c=1, a=0,b=1 -> 1 XOR 0 = 1)
//   |111>=7 -> |011>=3  (c=1, a=1,b=1 -> 1 XOR 1 = 0)  -- qubit0=1,qubit1=1 => flip qubit2

static void test_toffoli_truth_table() {
    // Test all 8 basis states as inputs.
    for (uint64_t input = 0; input < 8; ++input) {
        sturm::OrkanBridge b;
        b.allocate(3);
        // Prepare basis state |input>: flip required qubits from |000>
        for (int q = 0; q < 3; ++q) {
            if ((input >> q) & 1u) {
                orkan::apply_x(b.state(), q);
            }
        }

        // CCX(ctrl0=0, ctrl1=1, tgt=2)
        sturm::exec_simulate_multiq(b.state(), STURM_GATE_CCX, 0u, 1u, 2u, 0.0);

        // Compute expected output index
        int a = (input >> 0) & 1;  // qubit 0 (ctrl0)
        int bq = (input >> 1) & 1; // qubit 1 (ctrl1)
        int c = (input >> 2) & 1;  // qubit 2 (tgt)
        int c_out = c ^ (a & bq);
        uint64_t expected = (uint64_t)a | ((uint64_t)bq << 1) | ((uint64_t)c_out << 2);

        // Verify: amplitude at expected output should be 1, all others 0
        for (uint64_t idx = 0; idx < 8; ++idx) {
            cx want = (idx == expected) ? cx{1.0, 0.0} : cx{0.0, 0.0};
            char label[64];
            std::snprintf(label, sizeof(label),
                         "CCX input=%llu idx=%llu", (unsigned long long)input, (unsigned long long)idx);
            assert_close(amp(b, idx), want, label);
        }
    }
}

// ── SWAP exchanges amplitudes ──────────────────────────────────────────────────
// Prepare |01> (qubit 0 = 1, qubit 1 = 0).
// SWAP(0, 1) => |10>
// |01> => idx=1 (bit0=1), after SWAP: idx=2 (bit1=1)

static void test_swap_exchanges() {
    sturm::OrkanBridge b;
    b.allocate(2);
    orkan::apply_x(b.state(), 0); // prepare |01> (qubit0=1)

    // Before: amp[1]=1.0, all others 0
    assert_close(amp(b, 1), {1.0, 0.0}, "before SWAP amp[01]");
    assert_close(amp(b, 2), {0.0, 0.0}, "before SWAP amp[10]");

    // SWAP(qubit 0, qubit 1)
    sturm::exec_simulate_multiq(b.state(), STURM_GATE_SWAP, 0u, 1u, 0u, 0.0);

    // After: amp[2]=1.0 (|10>), amp[1]=0
    assert_close(amp(b, 1), {0.0, 0.0}, "after SWAP amp[01]");
    assert_close(amp(b, 2), {1.0, 0.0}, "after SWAP amp[10]");
}

// ── CY: ctrl=1, tgt=0 ────────────────────────────────────────────────────────
// Start |10> (qubit1=1, qubit0=0): idx=2
// CY(ctrl=1, tgt=0): since ctrl qubit1=1, apply Y to qubit0.
// Y|0> = i|1>
// Result: i|11> => amp[3] = i

static void test_cy() {
    sturm::OrkanBridge b;
    b.allocate(2);
    orkan::apply_x(b.state(), 1); // prepare |10>: qubit1=1, qubit0=0, idx=2

    // CY(ctrl=1, tgt=0)
    sturm::exec_simulate_multiq(b.state(), STURM_GATE_CY, 1u, 0u, 0u, 0.0);

    // Y|0> = i|1>: amp[3] = {0, 1}
    assert_close(amp(b, 0), {0.0, 0.0}, "CY amp[00]");
    assert_close(amp(b, 1), {0.0, 0.0}, "CY amp[01]");
    assert_close(amp(b, 2), {0.0, 0.0}, "CY amp[10]");
    assert_close(amp(b, 3), {0.0, 1.0}, "CY amp[11]");
}

// ── CZ: ctrl=0, tgt=1 ────────────────────────────────────────────────────────
// Start |11> (qubit0=1, qubit1=1): idx=3
// CZ(ctrl=0, tgt=1): ctrl is |1>, apply Z to qubit1.
// Z|1> = -|1>
// Result: -|11> => amp[3] = -1

static void test_cz() {
    sturm::OrkanBridge b;
    b.allocate(2);
    orkan::apply_x(b.state(), 0);
    orkan::apply_x(b.state(), 1); // prepare |11>: idx=3

    // CZ(ctrl=0, tgt=1)
    sturm::exec_simulate_multiq(b.state(), STURM_GATE_CZ, 0u, 1u, 0u, 0.0);

    // Z|1> = -|1>, so amp[3] = -1
    assert_close(amp(b, 0), { 0.0, 0.0}, "CZ amp[00]");
    assert_close(amp(b, 1), { 0.0, 0.0}, "CZ amp[01]");
    assert_close(amp(b, 2), { 0.0, 0.0}, "CZ amp[10]");
    assert_close(amp(b, 3), {-1.0, 0.0}, "CZ amp[11]");
}

// ── CX: ctrl=1 does nothing when ctrl qubit is |0> ───────────────────────────
// Start |00>, CX(ctrl=0, tgt=1): ctrl is |0>, tgt unchanged.

static void test_cx_no_flip_when_ctrl_zero() {
    sturm::OrkanBridge b;
    b.allocate(2);

    // CX(ctrl=0, tgt=1): ctrl qubit 0 = 0, so no flip
    sturm::exec_simulate_multiq(b.state(), STURM_GATE_CX, 0u, 1u, 0u, 0.0);

    assert_close(amp(b, 0), {1.0, 0.0}, "CX no-op amp[00]");
    assert_close(amp(b, 1), {0.0, 0.0}, "CX no-op amp[01]");
    assert_close(amp(b, 2), {0.0, 0.0}, "CX no-op amp[10]");
    assert_close(amp(b, 3), {0.0, 0.0}, "CX no-op amp[11]");
}

// ── CX: ctrl=1 flips target ───────────────────────────────────────────────────
// Start |10> (qubit0=1, qubit1=0): idx=1
// CX(ctrl=0, tgt=1): ctrl qubit 0 = 1, flip qubit 1 => |11>: idx=3

static void test_cx_flip_when_ctrl_one() {
    sturm::OrkanBridge b;
    b.allocate(2);
    orkan::apply_x(b.state(), 0); // qubit0=1 => idx=1 = |10> in our notation

    // CX(ctrl=0, tgt=1)
    sturm::exec_simulate_multiq(b.state(), STURM_GATE_CX, 0u, 1u, 0u, 0.0);

    // Should become |11>: idx=3
    assert_close(amp(b, 0), {0.0, 0.0}, "CX flip amp[00]");
    assert_close(amp(b, 1), {0.0, 0.0}, "CX flip amp[01]");
    assert_close(amp(b, 2), {0.0, 0.0}, "CX flip amp[10]");
    assert_close(amp(b, 3), {1.0, 0.0}, "CX flip amp[11]");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_bell_pair();
    test_toffoli_truth_table();
    test_swap_exchanges();
    test_cy();
    test_cz();
    test_cx_no_flip_when_ctrl_zero();
    test_cx_flip_when_ctrl_one();

    std::printf("All M11 multi-qubit simulate tests passed.\n");
    return 0;
}
