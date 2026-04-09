// test_c_n_and.cpp — M3 (PRD v2): tests for lib/c_n_and.hpp (recursive cascade).
//
// Tests:
//   - test_c_n_and_scaling: correctness for n=3, 4, 5, 6 controls.
//     For each n, enumerate all 2^n input combinations.  Target (q_n) flips
//     iff all n controls are 1.  All ancillas must be returned clean.
//
// Harness: plain assert + printf.

#include "sturm/lib/c_n_and.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

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

// Check that all qubits in [anc_start, anc_start+n_anc) are |0⟩.
static bool ancillas_clean(const sturm::v2::SimState& s,
                            uint32_t anc_start, uint32_t n_anc) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint32_t q = anc_start; q < anc_start + n_anc; ++q) {
        uint64_t mask = uint64_t{1} << q;
        for (uint64_t i = 0; i < dim; ++i) {
            if ((i & mask) != 0u) {
                if (std::abs(s.amplitude(i)) > kTol) return false;
            }
        }
    }
    return true;
}

// ── test_c_n_and_scaling ──────────────────────────────────────────────────────
// For n controls (n=3..6):
//   - Qubit layout: q0..q_{n-1} = controls, q_n = target, q_{n+1}.. = ancilla pool.
//   - Reserve enough ancilla qubits for the recursive sandwich (n-2 needed at most).
//   - For all 2^n input combos, verify target flips iff all controls are 1.
//   - After each call, verify ancilla qubits are |0⟩ and mgr.num_in_use()==0.
static void test_c_n_and_scaling() {
    for (uint32_t n = 3u; n <= 6u; ++n) {
        uint32_t tgt_q    = n;                // target qubit index
        uint32_t anc_start = n + 1u;          // ancilla pool start
        uint32_t n_anc    = n - 1u;           // max ancillas needed (n-2, but give n-1)
        uint32_t total_q  = anc_start + n_anc; // total qubits to allocate

        uint64_t n_inputs = uint64_t{1} << n;

        // Build control list [0, 1, ..., n-1].
        std::vector<uint32_t> ctrls(n);
        for (uint32_t i = 0; i < n; ++i) ctrls[i] = i;

        for (uint64_t ctrl_bits = 0; ctrl_bits < n_inputs; ++ctrl_bits) {
            sturm::v2::SimState s;
            s.allocate(total_q);
            s.load_basis(ctrl_bits);  // tgt=0, anc=0 initially

            sturm::v2::AncillaManager mgr(s, anc_start);
            sturm::v2::lib_c_n_AND(s, mgr, ctrls.data(), n, tgt_q);

            uint64_t got_idx = dominant_basis(s);
            uint64_t got_tgt = (got_idx >> tgt_q) & 1u;

            // Expected: tgt flips iff all n control bits are 1.
            uint64_t all_ones = n_inputs - 1u;  // 2^n - 1
            uint64_t exp_tgt  = (ctrl_bits == all_ones) ? 1u : 0u;

            if (got_tgt != exp_tgt) {
                std::printf("FAIL c_n_AND n=%u ctrl_bits=0x%llx exp=%llu got=%llu\n",
                            n, (unsigned long long)ctrl_bits,
                            (unsigned long long)exp_tgt, (unsigned long long)got_tgt);
                assert(false);
            }

            // All ancillas must be |0⟩.
            if (!ancillas_clean(s, anc_start, n_anc)) {
                std::printf("FAIL c_n_AND n=%u ctrl_bits=0x%llx: ancillas dirty\n",
                            n, (unsigned long long)ctrl_bits);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL c_n_AND n=%u ctrl_bits=0x%llx: ancillas not freed\n",
                            n, (unsigned long long)ctrl_bits);
                assert(false);
            }
        }
        std::printf("  PASS: c_n_AND n=%u all %llu input combos\n",
                    n, (unsigned long long)n_inputs);
    }
}

int main() {
    std::puts("=== test_c_n_and ===");
    test_c_n_and_scaling();
    std::puts("ALL PASS");
    return 0;
}
