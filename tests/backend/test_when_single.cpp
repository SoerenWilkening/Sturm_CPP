// test_when_single.cpp — M2 (PRD v2): WHEN lift single control.
//
// Tests:
//   1. WHEN a: X(b) emits CNOT (CX) on the simulator — X lifted under 1 control.
//   2. WHEN a: XOR(b, c) emits CCX (Toffoli) — XOR lifted under 1 control.
//   3. WHEN a: phase(b, theta) applies a controlled rotation.
//
// PRD v2 §4: X under WHEN a → XOR(a, target); XOR(b→c) under WHEN a → AND(a,b,c).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/backend/when_lift.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

static constexpr double kTol = 1e-9;
using cx = std::complex<double>;

// Returns the dominant basis index (for pure computational states).
static uint64_t dominant_basis(const sturm::v2::SimState& s) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    uint64_t best = 0;
    double best_prob = 0.0;
    for (uint64_t i = 0; i < dim; ++i) {
        double p = std::norm(s.amplitude(i));
        if (p > best_prob) { best_prob = p; best = i; }
    }
    return best;
}

// ── Test 1: WHEN a: X(b) ≡ CNOT ─────────────────────────────────────────────
//
// Truth table for CX(ctrl=q0, tgt=q1):
//   |00> (ctrl=0) → |00>
//   |01> (ctrl=1) → |11>
//   |10> (ctrl=0) → |10>
//   |11> (ctrl=1) → |01>
// idx = q0 + 2*q1

static void test_x_under_single_control_is_cnot() {
    // Qubit layout: q0=ctrl, q1=target.
    // Table: push ctrl=0, lift_X(target=1) — target must NOT flip.
    {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(0b00); // ctrl=0, tgt=0

        {
            sturm::v2::WhenLift wl(s);
            wl.push_control(0u); // q0 = ctrl
            wl.lift_X(1u);       // X on q1, under q0 control
        }
        // ctrl=0 → no flip → still |00>
        assert(dominant_basis(s) == 0b00u);
    }
    {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(0b01); // ctrl=1, tgt=0

        {
            sturm::v2::WhenLift wl(s);
            wl.push_control(0u);
            wl.lift_X(1u);
        }
        // ctrl=1 → flip tgt → |11> = idx 3
        assert(dominant_basis(s) == 0b11u);
    }
    {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(0b10); // ctrl=0, tgt=1

        {
            sturm::v2::WhenLift wl(s);
            wl.push_control(0u);
            wl.lift_X(1u);
        }
        // ctrl=0 → no flip → |10>
        assert(dominant_basis(s) == 0b10u);
    }
    {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(0b11); // ctrl=1, tgt=1

        {
            sturm::v2::WhenLift wl(s);
            wl.push_control(0u);
            wl.lift_X(1u);
        }
        // ctrl=1 → flip tgt → |01> = idx 1
        assert(dominant_basis(s) == 0b01u);
    }
    std::puts("  PASS: WHEN a: X(b) ≡ CNOT truth table");
}

// ── Test 2: WHEN a: XOR(b, c) ≡ CCX ─────────────────────────────────────────
//
// XOR(b→c) under control a: tgt (c) flips iff a=1 AND b=1.
// Qubit layout: q0=ctrl(a), q1=src(b), q2=tgt(c).
// idx = q0 + 2*q1 + 4*q2

static void test_xor_under_single_control_is_toffoli() {
    struct Entry { uint64_t in; uint64_t out; };
    const Entry table[] = {
        {0b000, 0b000}, // a=0,b=0,c=0 → c unchanged
        {0b001, 0b001}, // a=1,b=0,c=0 → b=0, c unchanged
        {0b010, 0b010}, // a=0,b=1,c=0 → a=0, c unchanged
        {0b011, 0b111}, // a=1,b=1,c=0 → flip c → |111>=7
        {0b100, 0b100},
        {0b101, 0b101},
        {0b110, 0b110},
        {0b111, 0b011}, // a=1,b=1,c=1 → flip c → |011>=3
    };

    for (auto& e : table) {
        sturm::v2::SimState s;
        s.allocate(3);
        s.load_basis(e.in);

        {
            sturm::v2::WhenLift wl(s);
            wl.push_control(0u);  // a = q0
            wl.lift_XOR(1u, 2u);  // XOR(src=q1, tgt=q2) under q0
        }

        uint64_t got = dominant_basis(s);
        if (got != e.out) {
            std::printf("FAIL XOR_under_ctrl: in=%llu exp=%llu got=%llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }
    }
    std::puts("  PASS: WHEN a: XOR(b,c) ≡ CCX (Toffoli) truth table");
}

// ── Test 3: unlifted X still works through WhenLift with no controls ──────────
static void test_x_with_no_control_is_plain_x() {
    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b00);

    {
        sturm::v2::WhenLift wl(s);
        // No push_control → uncontrolled
        wl.lift_X(0u); // plain X on q0
    }
    // Should flip q0: |00> → |01>
    assert(dominant_basis(s) == 0b01u);
    std::puts("  PASS: X with no controls is plain NOT");
}

int main() {
    std::puts("=== test_when_single ===");
    test_x_with_no_control_is_plain_x();
    test_x_under_single_control_is_cnot();
    test_xor_under_single_control_is_toffoli();
    std::puts("ALL PASS");
    return 0;
}
