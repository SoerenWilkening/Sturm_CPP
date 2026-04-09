// test_ancilla_lifecycle.cpp — M1 (PRD v2): ancilla borrow/return lifecycle.
//
// Tests:
//   1. allocate_ancilla() returns a valid qubit index.
//   2. Allocated ancilla starts in |0> in the sim state.
//   3. free_ancilla() with a clean |0> qubit succeeds.
//   4. free_ancilla() with a dirty (non-|0>) qubit throws or aborts.
//   5. Double-free detection: freeing an already-free qubit throws.
//   6. Multiple allocations return distinct indices.
//
// Harness: plain assert + main (no gtest).

#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>

static constexpr double kTol = 1e-6;

// Check whether qubit q in state s is in |0> (amplitude of all basis states
// with q=1 should be 0).
static bool is_zero_state(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim  = uint64_t{1} << s.num_qubits();
    uint64_t mask = uint64_t{1} << q;
    for (uint64_t i = 0; i < dim; ++i) {
        if ((i & mask) != 0u) {
            if (std::abs(s.amplitude(i)) > kTol) return false;
        }
    }
    return true;
}

// ── Basic allocate / free ─────────────────────────────────────────────────────
static void test_basic_alloc_free() {
    sturm::v2::SimState sim;
    sim.allocate(4);

    sturm::v2::AncillaManager mgr(sim);
    uint32_t q = mgr.allocate_ancilla();

    // Should be valid index in range
    assert(q < sim.num_qubits());
    std::puts("  PASS: allocate_ancilla returns valid index");

    // Freshly allocated ancilla must be in |0>
    assert(is_zero_state(sim, q));
    std::puts("  PASS: allocated ancilla starts in |0>");

    // Free it (clean |0> => should succeed)
    mgr.free_ancilla(q);
    std::puts("  PASS: free_ancilla clean qubit succeeds");
}

// ── Distinct indices on multiple allocations ─────────────────────────────────
static void test_distinct_indices() {
    sturm::v2::SimState sim;
    sim.allocate(8);

    sturm::v2::AncillaManager mgr(sim);
    uint32_t a = mgr.allocate_ancilla();
    uint32_t b = mgr.allocate_ancilla();
    uint32_t c = mgr.allocate_ancilla();

    assert(a != b);
    assert(b != c);
    assert(a != c);

    mgr.free_ancilla(a);
    mgr.free_ancilla(b);
    mgr.free_ancilla(c);
    std::puts("  PASS: multiple ancillas get distinct indices");
}

// ── free_ancilla with dirty qubit throws ─────────────────────────────────────
static void test_dirty_free_throws() {
    sturm::v2::SimState sim;
    sim.allocate(4);

    sturm::v2::AncillaManager mgr(sim);
    uint32_t q = mgr.allocate_ancilla();

    // Mark qubit as dirty by flipping it with X
    sturm::v2::primitive_X(sim, q);
    assert(!is_zero_state(sim, q)); // sanity

    bool threw = false;
    try {
        mgr.free_ancilla(q); // should throw: qubit is not |0>
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::puts("  PASS: free_ancilla dirty qubit throws");
}

// ── Reuse after free ──────────────────────────────────────────────────────────
static void test_reuse_after_free() {
    sturm::v2::SimState sim;
    sim.allocate(4);

    sturm::v2::AncillaManager mgr(sim);
    uint32_t a = mgr.allocate_ancilla();
    mgr.free_ancilla(a);

    // Reallocating should succeed (may or may not reuse same index, but must be valid)
    uint32_t b = mgr.allocate_ancilla();
    assert(b < sim.num_qubits());
    assert(is_zero_state(sim, b));
    mgr.free_ancilla(b);
    std::puts("  PASS: reuse after free works");
}

// ── Double-free detection ─────────────────────────────────────────────────────
static void test_double_free() {
    sturm::v2::SimState sim;
    sim.allocate(4);

    sturm::v2::AncillaManager mgr(sim);
    uint32_t q = mgr.allocate_ancilla();
    mgr.free_ancilla(q);

    bool threw = false;
    try {
        mgr.free_ancilla(q); // double-free
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::puts("  PASS: double-free throws");
}

int main() {
    std::puts("=== test_ancilla_lifecycle ===");
    test_basic_alloc_free();
    test_distinct_indices();
    test_dirty_free_throws();
    test_reuse_after_free();
    test_double_free();
    std::puts("ALL PASS");
    return 0;
}
