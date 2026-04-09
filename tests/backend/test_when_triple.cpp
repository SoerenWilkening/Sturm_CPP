// test_when_triple.cpp — M2 (PRD v2): WHEN lift triple control via c_AND path.
//
// Tests:
//   1. WHEN a,b,c: X(d) — with 3 controls, lift_X must use the c_AND path
//      (Nielsen-Chuang sandwich with borrowed ancilla) rather than a naked CCCx.
//   2. The ancilla is returned clean (no leaked entanglement).
//   3. Correctness: target flips iff all 3 controls are 1.
//
// PRD v2 §4: AND(b,c→d) under WHEN a → C³-X, library code (c_AND) with 1 borrowed
// ancilla. No leaked ancillas: the mirror half uncomputes.
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

// Check whether qubit q is in |0⟩ state in s.
static bool is_qubit_zero(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim  = uint64_t{1} << s.num_qubits();
    uint64_t mask = uint64_t{1} << q;
    for (uint64_t i = 0; i < dim; ++i) {
        if ((i & mask) != 0u) {
            if (std::abs(s.amplitude(i)) > kTol) return false;
        }
    }
    return true;
}

// ── Test: WHEN a,b,c: X(d) correctness ───────────────────────────────────────
//
// Qubit layout: q0=a, q1=b, q2=c, q3=d (target), q4=ancilla (borrowed).
// idx = q0 + 2*q1 + 4*q2 + 8*q3  (q4 is ancilla, should always be 0)
//
// target (q3) flips iff q0=1 AND q1=1 AND q2=1.

static void test_triple_control_x_correctness() {
    // All 8 input combinations for q0,q1,q2 with q3=0 initially.
    for (uint64_t ctrl_bits = 0; ctrl_bits < 8u; ++ctrl_bits) {
        uint64_t in_idx = ctrl_bits; // q0..q2 set, q3=0, q4=0

        sturm::v2::SimState s;
        s.allocate(5); // 3 controls + 1 target + 1 ancilla
        s.load_basis(in_idx);

        // Skip qubits 0-3 (user qubits); ancilla pool starts at q4.
        sturm::v2::AncillaManager mgr(s, 4u);

        {
            sturm::v2::WhenLift wl(s, &mgr);
            wl.push_control(0u);
            wl.push_control(1u);
            wl.push_control(2u);
            wl.lift_X(3u);
        }

        uint64_t got = dominant_basis(s);
        // Expected: flip q3 iff all 3 controls are 1 (ctrl_bits == 0b111 = 7)
        uint64_t expected = in_idx;
        if (ctrl_bits == 0b111u) {
            expected ^= (1u << 3); // flip bit 3
        }

        if (got != expected) {
            std::printf("FAIL triple_ctrl: ctrl_bits=%llu exp=%llu got=%llu\n",
                        (unsigned long long)ctrl_bits,
                        (unsigned long long)expected,
                        (unsigned long long)got);
            assert(false);
        }

        // Ancilla (q4) must be clean (|0⟩) after the scope.
        assert(is_qubit_zero(s, 4u) && "ancilla must be clean after c_AND uncompute");
        assert(mgr.num_in_use() == 0u && "all ancillas must be freed");
    }
    std::puts("  PASS: WHEN a,b,c: X(d) correct for all 8 input combos");
}

// ── Test: ancilla is returned clean after triple-control ─────────────────────
static void test_triple_control_ancilla_clean() {
    sturm::v2::SimState s;
    s.allocate(5);
    // All controls set to 1: |0b111> = 7
    s.load_basis(0b00111u);

    // Skip qubits 0-3 (user qubits); ancilla pool starts at q4.
    sturm::v2::AncillaManager mgr(s, 4u);

    {
        sturm::v2::WhenLift wl(s, &mgr);
        wl.push_control(0u);
        wl.push_control(1u);
        wl.push_control(2u);
        wl.lift_X(3u);
    }

    // q4 (ancilla) must be |0⟩
    assert(is_qubit_zero(s, 4u));
    // AncillaManager pool must be clean
    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: ancilla clean after triple-control X");
}

int main() {
    std::puts("=== test_when_triple ===");
    test_triple_control_x_correctness();
    test_triple_control_ancilla_clean();
    std::puts("ALL PASS");
    return 0;
}
