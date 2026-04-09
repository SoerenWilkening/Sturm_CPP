// test_primitives_classical.cpp — M1 (PRD v2): classical truth tables for X, XOR, AND.
//
// Tests:
//   1. X flips a single qubit in every basis state.
//   2. XOR (CNOT) truth table: tgt flips iff ctrl=1.
//   3. AND (Toffoli) truth table: tgt flips iff ctrl0=1 AND ctrl1=1.
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

// Load a 3-qubit computational basis state |idx> and return the bridge.
static sturm::v2::SimState make3(uint64_t idx) {
    sturm::v2::SimState s;
    s.allocate(3);
    s.load_basis(idx);
    return s;
}

// Return the basis index with highest amplitude (assumes a pure computational basis state).
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

// ── X (NOT) truth table — 1 qubit ────────────────────────────────────────────
static void test_x_1qubit() {
    // Start |0>, X => |1>
    sturm::v2::SimState s;
    s.allocate(1);
    s.load_basis(0); // |0>

    sturm::v2::primitive_X(s, 0);

    assert(std::abs(s.amplitude(0)) < kTol);            // |0> gone
    assert(std::abs(s.amplitude(1) - cx{1,0}) < kTol);  // |1> present
    std::puts("  PASS: X|0> = |1>");

    // X again => |0>
    sturm::v2::primitive_X(s, 0);
    assert(std::abs(s.amplitude(0) - cx{1,0}) < kTol);
    std::puts("  PASS: X|1> = |0>");
}

// ── X on qubit 1 in a 2-qubit register ───────────────────────────────────────
static void test_x_2qubit_bit1() {
    // |00> -> X(q1) -> |10>  (bit-1 is bit index 1, so idx flips from 0b00 to 0b10=2)
    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0); // |00>

    sturm::v2::primitive_X(s, 1); // flip qubit 1

    assert(dominant_basis(s) == 2u); // |10> = idx 2
    std::puts("  PASS: X on qubit 1 of |00> gives |10>");
}

// ── XOR (CNOT) truth table — ctrl=0 ──────────────────────────────────────────
static void test_xor_ctrl0() {
    // |00>: ctrl=0 => tgt unchanged => |00>
    auto s00 = make3(0b000);
    sturm::v2::primitive_XOR(s00, /*ctrl=*/0, /*tgt=*/1);
    assert(dominant_basis(s00) == 0b000u);

    // |010>: qubit1=1, qubit0=0, ctrl=qubit0=0 => tgt (qubit1) unchanged => |010>
    auto s010 = make3(0b010);
    sturm::v2::primitive_XOR(s010, 0, 1);
    assert(dominant_basis(s010) == 0b010u);

    std::puts("  PASS: XOR ctrl=0 does not flip tgt");
}

// ── XOR (CNOT) truth table — ctrl=1 ──────────────────────────────────────────
static void test_xor_ctrl1() {
    // |001>: qubit0=1 (ctrl), qubit1=0 (tgt) => flip tgt => |011> = idx 3
    auto s001 = make3(0b001);
    sturm::v2::primitive_XOR(s001, 0, 1);
    assert(dominant_basis(s001) == 0b011u);

    // |011>: qubit0=1 (ctrl), qubit1=1 (tgt) => flip tgt => |001> = idx 1
    auto s011 = make3(0b011);
    sturm::v2::primitive_XOR(s011, 0, 1);
    assert(dominant_basis(s011) == 0b001u);

    std::puts("  PASS: XOR ctrl=1 flips tgt");
}

// ── AND (Toffoli) truth table — full 3-qubit ─────────────────────────────────
// Qubit layout: bit 0 = ctrl0, bit 1 = ctrl1, bit 2 = tgt
// Expected: tgt flips iff ctrl0=1 AND ctrl1=1
static void test_and_truth_table() {
    struct Entry { uint64_t in; uint64_t out; };
    // idx = ctrl0 + 2*ctrl1 + 4*tgt
    const Entry table[] = {
        {0b000, 0b000}, // ctrl0=0,ctrl1=0,tgt=0 => 0
        {0b001, 0b001}, // ctrl0=1,ctrl1=0,tgt=0 => 0
        {0b010, 0b010}, // ctrl0=0,ctrl1=1,tgt=0 => 0
        {0b011, 0b111}, // ctrl0=1,ctrl1=1,tgt=0 => 1 (flip tgt)
        {0b100, 0b100}, // ctrl0=0,ctrl1=0,tgt=1 => 1
        {0b101, 0b101}, // ctrl0=1,ctrl1=0,tgt=1 => 1
        {0b110, 0b110}, // ctrl0=0,ctrl1=1,tgt=1 => 1
        {0b111, 0b011}, // ctrl0=1,ctrl1=1,tgt=1 => 0 (unflip tgt)
    };

    for (auto& e : table) {
        auto s = make3(e.in);
        sturm::v2::primitive_AND(s, 0, 1, 2);
        uint64_t got = dominant_basis(s);
        if (got != e.out) {
            std::printf("FAIL AND: in=%llu expected out=%llu got=%llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }
    }
    std::puts("  PASS: AND (Toffoli) full truth table");
}

int main() {
    std::puts("=== test_primitives_classical ===");
    test_x_1qubit();
    test_x_2qubit_bit1();
    test_xor_ctrl0();
    test_xor_ctrl1();
    test_and_truth_table();
    std::puts("ALL PASS");
    return 0;
}
