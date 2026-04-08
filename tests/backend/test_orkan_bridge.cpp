// test_orkan_bridge.cpp — M9: Orkan vendoring + bridge skeleton tests.
// TDD: written before implementation, drives orkan_bridge.hpp / orkan_bridge.cpp.
//
// Tests:
//   1. Constructing OrkanBridge(17) succeeds (no throw).
//   2. After construction, allocate(17) does not throw.
//   3. After allocate(17), the probability of the |0…0⟩ basis state is 1.0
//      within tolerance 1e-12 (confirming initialization to |0…0⟩).
//   4. reset_zero() leaves the state in |0…0⟩ after arbitrary single-qubit
//      perturbation (via orkan API or bridge helper).
//   5. num_qubits() reports the value passed to allocate().

#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>

static constexpr double kTol = 1e-12;

// ── Test 1: construction succeeds ────────────────────────────────────────────

static void test_construction() {
    // Should not throw.
    sturm::OrkanBridge bridge;
    (void)bridge;
}

// ── Test 2: allocate(17) succeeds ────────────────────────────────────────────

static void test_allocate_17() {
    sturm::OrkanBridge bridge;
    bridge.allocate(17u);
    assert(bridge.num_qubits() == 17u);
}

// ── Test 3: 17-qubit state initializes to |0…0⟩ ─────────────────────────────
//
// The probability of the |0…0⟩ basis state in an n-qubit register is the
// squared magnitude of amplitude index 0.  For a freshly-allocated register
// this must be 1.0 ± 1e-12.

static void test_init_zero_state() {
    sturm::OrkanBridge bridge;
    bridge.allocate(17u);

    // probability_of_zero_state() returns |<0…0|ψ>|^2.
    double p0 = bridge.probability_of_zero_state();
    assert(std::abs(p0 - 1.0) < kTol);
}

// ── Test 4: reset_zero() restores |0…0⟩ ─────────────────────────────────────
//
// After applying an X gate on qubit 0 (flipping it to |1⟩), reset_zero()
// must return the state to |0…0⟩.

static void test_reset_zero() {
    sturm::OrkanBridge bridge;
    bridge.allocate(17u);

    // Flip qubit 0: now the state should be |1 0…0⟩ and P(|0…0⟩) ≈ 0.
    bridge.apply_x(0u);
    double p_after_x = bridge.probability_of_zero_state();
    assert(std::abs(p_after_x) < kTol); // |0…0⟩ amplitude is ~0

    // Reset: must return to |0…0⟩.
    bridge.reset_zero();
    double p_after_reset = bridge.probability_of_zero_state();
    assert(std::abs(p_after_reset - 1.0) < kTol);
}

// ── Test 5: num_qubits reflects allocate() argument ──────────────────────────

static void test_num_qubits() {
    sturm::OrkanBridge bridge;
    bridge.allocate(17u);
    assert(bridge.num_qubits() == 17u);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_construction();
    test_allocate_17();
    test_init_zero_state();
    test_reset_zero();
    test_num_qubits();
    return 0;
}
