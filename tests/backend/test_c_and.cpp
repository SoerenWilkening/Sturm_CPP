// test_c_and.cpp — M3 (PRD v2): tests for lib/c_and.hpp (Nielsen-Chuang sandwich).
//
// Tests:
//   - Truth table: lib_c_AND on all 3-bit inputs (c0, c1, tgt) — tgt flips iff
//     c0=1 AND c1=1 (i.e. it behaves as Toffoli / C³-X with 2 controls).
//   - test_c_and_ancilla_clean: borrowed ancilla returns to |0⟩ after operation
//     for all input combinations.
//
// Harness: plain assert + printf.

#include "sturm/lib/c_and.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

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

// ── Truth table: lib_c_AND ────────────────────────────────────────────────────
// Layout: q0=c0, q1=c1, q2=tgt, q3=ancilla (pool start).
// lib_c_AND(s, mgr, c0=0, c1=1, tgt=2): tgt ^= (c0 AND c1).
static void test_c_and_truth_table() {
    for (uint64_t in = 0; in < 4u; ++in) {
        uint64_t c0  = (in >> 0) & 1u;
        uint64_t c1  = (in >> 1) & 1u;
        uint64_t exp_tgt = c0 & c1;  // tgt starts at 0, result = c0 AND c1

        sturm::v2::SimState s;
        s.allocate(4);  // q0=c0, q1=c1, q2=tgt, q3=ancilla
        s.load_basis(in);  // tgt=0, anc=0

        sturm::v2::AncillaManager mgr(s, 3u);
        sturm::v2::lib_c_AND(s, mgr, 0u, 1u, 2u);

        uint64_t got_idx = dominant_basis(s);
        uint64_t got_tgt = (got_idx >> 2) & 1u;

        if (got_tgt != exp_tgt) {
            std::printf("FAIL c_AND truth: c0=%llu c1=%llu exp=%llu got=%llu\n",
                        (unsigned long long)c0, (unsigned long long)c1,
                        (unsigned long long)exp_tgt, (unsigned long long)got_tgt);
            assert(false);
        }

        // Ancilla must be clean.
        assert(is_qubit_zero(s, 3u) && "c_AND ancilla must be |0> after call");
        assert(mgr.num_in_use() == 0u && "c_AND must free all ancillas");
    }
    std::puts("  PASS: lib_c_AND truth table (all 4 inputs)");
}

// ── test_c_and_ancilla_clean ──────────────────────────────────────────────────
// Additional stress: check ancilla cleanliness for both tgt=0 and tgt=1 starts.
static void test_c_and_ancilla_clean() {
    // Both controls set (worst case: ancilla is set then unset).
    for (uint64_t tgt_init = 0; tgt_init < 2u; ++tgt_init) {
        // c0=1, c1=1, tgt=tgt_init -> in_idx = 0b11 | (tgt_init << 2)
        uint64_t in_idx = 0b11u | (tgt_init << 2);

        sturm::v2::SimState s;
        s.allocate(4);
        s.load_basis(in_idx);

        sturm::v2::AncillaManager mgr(s, 3u);
        sturm::v2::lib_c_AND(s, mgr, 0u, 1u, 2u);

        assert(is_qubit_zero(s, 3u) && "ancilla must be |0> after c_AND");
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: c_AND ancilla clean for c0=c1=1, tgt=0 and tgt=1");
}

int main() {
    std::puts("=== test_c_and ===");
    test_c_and_truth_table();
    test_c_and_ancilla_clean();
    std::puts("ALL PASS");
    return 0;
}
