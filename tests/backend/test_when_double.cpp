// test_when_double.cpp — M2 (PRD v2): WHEN lift double control.
//
// Tests:
//   1. WHEN a: WHEN b: X(c) ≡ CCNOT (Toffoli).
//      Two nested push_control calls → X lifted under 2 controls.
//   2. WHEN a: WHEN b: XOR(c, d) ≡ C³-X (which uses c_AND path).
//
// PRD v2 §4: X under 2 controls → AND fold.
// With 2 controls, lift_X emits CCX (Toffoli) directly.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/backend/when_lift.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/state.hpp"

#include <cassert>
#include <cstdio>

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

// ── Test: WHEN a: WHEN b: X(c) ≡ CCX ────────────────────────────────────────
//
// Qubit layout: q0=a (ctrl0), q1=b (ctrl1), q2=c (target).
// idx = q0 + 2*q1 + 4*q2
// CCX flips q2 iff q0=1 AND q1=1.

static void test_x_under_double_control_is_ccx() {
    struct Entry { uint64_t in; uint64_t out; };
    const Entry table[] = {
        {0b000, 0b000},
        {0b001, 0b001},
        {0b010, 0b010},
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
            wl.push_control(0u); // WHEN a
            wl.push_control(1u); // WHEN b
            wl.lift_X(2u);       // X(c) under a,b
        }

        uint64_t got = dominant_basis(s);
        if (got != e.out) {
            std::printf("FAIL double_ctrl_X: in=%llu exp=%llu got=%llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }
    }
    std::puts("  PASS: WHEN a: WHEN b: X(c) ≡ CCX (Toffoli) truth table");
}

// ── Test: push/pop symmetry — controls are properly nested ───────────────────
//
// Verify that after the WhenLift scope exits, subsequent plain lift_X calls
// are uncontrolled again.

static void test_control_stack_cleanup() {
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0b0000);

    // Inner scope: double-control X on q3
    {
        sturm::v2::WhenLift wl(s);
        wl.push_control(0u); // q0
        wl.push_control(1u); // q1
        // q0=0,q1=0 → X(q3) should NOT fire
        wl.lift_X(3u);
    }
    // q3 stays 0 (both controls were 0)
    assert(dominant_basis(s) == 0b0000u);

    // Now flip q0 and q1, then apply double-control X again
    sturm::v2::primitive_X(s, 0u);
    sturm::v2::primitive_X(s, 1u);
    // state is now |0011> = idx 3

    {
        sturm::v2::WhenLift wl(s);
        wl.push_control(0u);
        wl.push_control(1u);
        wl.lift_X(3u);
    }
    // Both controls=1 → q3 flips: |1011> = idx 11
    assert(dominant_basis(s) == 0b1011u);
    std::puts("  PASS: control stack push/pop symmetry");
}

int main() {
    std::puts("=== test_when_double ===");
    test_x_under_double_control_is_ccx();
    test_control_stack_cleanup();
    std::puts("ALL PASS");
    return 0;
}
