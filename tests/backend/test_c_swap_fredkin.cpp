// test_c_swap_fredkin.cpp — M4 (PRD v2): Fredkin (controlled SWAP) truth table.
//
// lib_c_SWAP(s, mgr, ctrl, a_idxs, b_idxs, n):
//   Implements Fredkin gate: if ctrl==1 then swap qubit-pairs (a_idxs[i], b_idxs[i]).
//   Each qubit pair uses XOR; AND; XOR (2 XORs + 1 AND).
//
// Truth table (3-qubit system: q0=ctrl, q1=a, q2=b):
//   ctrl=0: a,b unchanged
//   ctrl=1: a,b swapped
//
// Encoding: idx = q0 + 2*q1 + 4*q2
// All 8 input combinations tested.
//
// Also: verify ancilla is clean after each call (mgr.num_in_use() == 0).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/swap.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <array>

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

// ── test_c_swap_fredkin_truth_table ──────────────────────────────────────────
//
// Qubit layout: q0=ctrl, q1=a, q2=b.
// idx bits: bit0=q0, bit1=q1, bit2=q2.
// Fredkin: if ctrl==1, swap a and b.
static void test_c_swap_fredkin_truth_table() {
    struct Entry {
        uint64_t in;   // {ctrl, a, b}
        uint64_t out;  // expected output
    };
    // ctrl=q0, a=q1, b=q2
    // out: ctrl unchanged; if ctrl=1, a_out=b_in, b_out=a_in
    const Entry table[] = {
        // ctrl=0: no swap
        {0b000, 0b000},  // ctrl=0,a=0,b=0 → 0,0,0
        {0b001, 0b001},  // ctrl=1,a=0,b=0 → 1,0,0 (ctrl=1, a==b so same)
        {0b010, 0b010},  // ctrl=0,a=1,b=0 → 0,1,0
        {0b011, 0b101},  // ctrl=1,a=1,b=0 → swap: ctrl=1,a=0,b=1 → 1+0+4=5
        {0b100, 0b100},  // ctrl=0,a=0,b=1 → 0,0,1
        {0b101, 0b011},  // ctrl=1,a=0,b=1 → swap: ctrl=1,a=1,b=0 → 1+2+0=3
        {0b110, 0b110},  // ctrl=0,a=1,b=1 → 0,1,1
        {0b111, 0b111},  // ctrl=1,a=1,b=1 → swap (same): 1,1,1
    };

    for (const auto& e : table) {
        // Allocate 3 user qubits + 1 ancilla slot for mgr
        sturm::v2::SimState s;
        s.allocate(4);
        s.load_basis(e.in);

        sturm::v2::AncillaManager mgr(s, 3u);  // qubits 0-2 are user; 3 is ancilla

        // a_idxs = {q1}, b_idxs = {q2}, ctrl = q0
        uint32_t a_idx = 1u;
        uint32_t b_idx = 2u;
        sturm::v2::lib_c_SWAP(s, mgr, 0u /*ctrl*/, &a_idx, &b_idx, 1u);

        uint64_t got = dominant_basis(s) & 0b111u;  // only lower 3 bits
        if (got != e.out) {
            std::printf("FAIL Fredkin: in=0b%03llu exp=0b%03llu got=0b%03llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }

        // Ancilla must be clean.
        assert(mgr.num_in_use() == 0u && "Fredkin: ancilla must be free after call");
    }
    std::puts("  PASS: lib_c_SWAP Fredkin truth table (all 8 inputs)");
}

// ── test_c_swap_fredkin_ancilla_clean ─────────────────────────────────────────
//
// Verify ancilla cleanliness explicitly using is_qubit_zero logic.
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

static void test_c_swap_fredkin_ancilla_clean() {
    // ctrl=1, a=1, b=0: the case that exercises the Fredkin body most fully.
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0b011u); // ctrl=1, a=1, b=0

    sturm::v2::AncillaManager mgr(s, 3u);
    uint32_t a_idx = 1u;
    uint32_t b_idx = 2u;
    sturm::v2::lib_c_SWAP(s, mgr, 0u, &a_idx, &b_idx, 1u);

    // qubit 3 was used as ancilla; it must be |0>.
    // (The Fredkin XOR;AND;XOR decomposition does NOT require an ancilla,
    // so qubit 3 is never touched. This test confirms that invariant.)
    assert(is_qubit_zero(s, 3u) && "ancilla qubit must be |0> after Fredkin");
    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: Fredkin ancilla qubit is |0> after call");
}

int main() {
    std::puts("=== test_c_swap_fredkin ===");
    test_c_swap_fredkin_truth_table();
    test_c_swap_fredkin_ancilla_clean();
    std::puts("ALL PASS");
    return 0;
}
