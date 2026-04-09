// test_logic_basic.cpp — M3 (PRD v2): truth-table tests for logic_basic.hpp.
//
// Tests:
//   - NOT_reg:  single-register bit-flip over all basis states
//   - OR:       2-qubit truth table (all 4 inputs), result in ancilla tgt
//   - NAND:     2-qubit truth table
//   - NOR:      2-qubit truth table
//   - XNOR:     2-qubit truth table
//
// Harness: plain assert + printf (matches project convention).

#include "sturm/lib/logic_basic.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// Return the dominant (highest probability) basis index in a pure state.
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

// Check whether bit `q` is set in basis index `idx`.
static bool bit(uint64_t idx, uint32_t q) { return (idx >> q) & 1u; }

// ── NOT_reg ───────────────────────────────────────────────────────────────────
// Layout: 2-qubit register [q0, q1].  NOT_reg flips both bits.
static void test_not_reg() {
    for (uint64_t in = 0; in < 4u; ++in) {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(in);

        sturm::v2::NOT_reg(s, 0u, 2u);  // flip qubits 0 and 1

        uint64_t got = dominant_basis(s);
        uint64_t exp = (~in) & 0x3u;  // invert both bits, 2-qubit mask
        if (got != exp) {
            std::printf("FAIL NOT_reg: in=%llu exp=%llu got=%llu\n",
                        (unsigned long long)in,
                        (unsigned long long)exp,
                        (unsigned long long)got);
            assert(false);
        }
    }
    std::puts("  PASS: NOT_reg all 2-bit inputs");
}

// ── OR truth table ────────────────────────────────────────────────────────────
// Layout: 3 qubits: q0=a, q1=b, q2=tgt (starts at 0).
// OR(a, b) -> tgt = a | b.  Using the identity: a|b = NOT(NOT(a) AND NOT(b)).
static void test_or_truth_table() {
    for (uint64_t ab = 0; ab < 4u; ++ab) {
        uint64_t a = (ab >> 0) & 1u;
        uint64_t b = (ab >> 1) & 1u;
        uint64_t expected_tgt = a | b;

        sturm::v2::SimState s;
        s.allocate(4);  // q0=a, q1=b, q2=tgt, q3=ancilla
        s.load_basis(ab);  // tgt=0, anc=0

        sturm::v2::AncillaManager mgr(s, 3u);  // ancilla pool starts at q3
        sturm::v2::lib_OR(s, mgr, 0u, 1u, 2u);

        uint64_t got_idx = dominant_basis(s);
        uint64_t got_tgt = bit(got_idx, 2);

        if (got_tgt != expected_tgt) {
            std::printf("FAIL OR: a=%llu b=%llu exp=%llu got=%llu\n",
                        (unsigned long long)a, (unsigned long long)b,
                        (unsigned long long)expected_tgt, (unsigned long long)got_tgt);
            assert(false);
        }
    }
    std::puts("  PASS: OR truth table");
}

// ── NAND truth table ──────────────────────────────────────────────────────────
// Layout: 3 qubits: q0=a, q1=b, q2=tgt (starts at 0).
// NAND(a, b) -> tgt = NOT(a AND b).
static void test_nand_truth_table() {
    for (uint64_t ab = 0; ab < 4u; ++ab) {
        uint64_t a = (ab >> 0) & 1u;
        uint64_t b = (ab >> 1) & 1u;
        uint64_t expected_tgt = ~(a & b) & 1u;

        sturm::v2::SimState s;
        s.allocate(3);  // q0=a, q1=b, q2=tgt
        s.load_basis(ab);

        sturm::v2::lib_NAND(s, 0u, 1u, 2u);

        uint64_t got_idx = dominant_basis(s);
        uint64_t got_tgt = bit(got_idx, 2);

        if (got_tgt != expected_tgt) {
            std::printf("FAIL NAND: a=%llu b=%llu exp=%llu got=%llu\n",
                        (unsigned long long)a, (unsigned long long)b,
                        (unsigned long long)expected_tgt, (unsigned long long)got_tgt);
            assert(false);
        }
    }
    std::puts("  PASS: NAND truth table");
}

// ── NOR truth table ───────────────────────────────────────────────────────────
// Layout: 3 qubits: q0=a, q1=b, q2=tgt (starts at 0).
// NOR(a, b) -> tgt = NOT(a OR b).
static void test_nor_truth_table() {
    for (uint64_t ab = 0; ab < 4u; ++ab) {
        uint64_t a = (ab >> 0) & 1u;
        uint64_t b = (ab >> 1) & 1u;
        uint64_t expected_tgt = ~(a | b) & 1u;

        sturm::v2::SimState s;
        s.allocate(4);  // q0=a, q1=b, q2=tgt, q3=ancilla
        s.load_basis(ab);

        sturm::v2::AncillaManager mgr(s, 3u);
        sturm::v2::lib_NOR(s, mgr, 0u, 1u, 2u);

        uint64_t got_idx = dominant_basis(s);
        uint64_t got_tgt = bit(got_idx, 2);

        if (got_tgt != expected_tgt) {
            std::printf("FAIL NOR: a=%llu b=%llu exp=%llu got=%llu\n",
                        (unsigned long long)a, (unsigned long long)b,
                        (unsigned long long)expected_tgt, (unsigned long long)got_tgt);
            assert(false);
        }
    }
    std::puts("  PASS: NOR truth table");
}

// ── XNOR truth table ──────────────────────────────────────────────────────────
// Layout: 3 qubits: q0=a, q1=b, q2=tgt (starts at 0).
// XNOR(a, b) -> tgt = NOT(a XOR b) = (a == b).
static void test_xnor_truth_table() {
    for (uint64_t ab = 0; ab < 4u; ++ab) {
        uint64_t a = (ab >> 0) & 1u;
        uint64_t b = (ab >> 1) & 1u;
        uint64_t expected_tgt = ~(a ^ b) & 1u;

        sturm::v2::SimState s;
        s.allocate(3);  // q0=a, q1=b, q2=tgt
        s.load_basis(ab);

        sturm::v2::lib_XNOR(s, 0u, 1u, 2u);

        uint64_t got_idx = dominant_basis(s);
        uint64_t got_tgt = bit(got_idx, 2);

        if (got_tgt != expected_tgt) {
            std::printf("FAIL XNOR: a=%llu b=%llu exp=%llu got=%llu\n",
                        (unsigned long long)a, (unsigned long long)b,
                        (unsigned long long)expected_tgt, (unsigned long long)got_tgt);
            assert(false);
        }
    }
    std::puts("  PASS: XNOR truth table");
}

int main() {
    std::puts("=== test_logic_basic ===");
    test_not_reg();
    test_or_truth_table();
    test_nand_truth_table();
    test_nor_truth_table();
    test_xnor_truth_table();
    std::puts("ALL PASS");
    return 0;
}
