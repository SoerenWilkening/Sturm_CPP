// test_swap_uncontrolled_zero_gates.cpp — M4 (PRD v2): uncontrolled SWAP as
// index relabel emits zero primitives.
//
// Tests:
//   - lib_SWAP(s, mgr, qubit_array_a, qubit_array_b, n) on two 1-qubit registers
//     emits zero primitives (pure index swap on the wrapper arrays).
//   - After the index swap the logical qubit values have exchanged positions.
//
// The "zero gates" property is verified by checking that the SimState amplitudes
// are unchanged (no physical gate was applied); only the index arrays in the
// caller's wrapper should be updated.
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

// ── test_swap_uncontrolled_zero_gates ────────────────────────────────────────
//
// Prepare state |ab> where a = qubit 0, b = qubit 1.
// Call lib_SWAP with index arrays {0} and {1} (each a 1-qubit register).
// After the call:
//   - The index arrays are exchanged: a_idxs now holds {1}, b_idxs holds {0}.
//   - The SimState is UNCHANGED — no physical gate was emitted.
// We verify the second property by checking the amplitudes before and after.
static void test_swap_uncontrolled_zero_gates() {
    for (uint64_t in = 0; in < 4u; ++in) {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(in);

        // Snapshot amplitudes before.
        std::array<std::complex<double>, 4> amp_before;
        for (uint64_t i = 0; i < 4u; ++i) amp_before[i] = s.amplitude(i);

        sturm::v2::AncillaManager mgr(s, 2u);

        // Caller-side index arrays (simulate qint wrappers holding qubit indices).
        std::array<uint32_t, 1> a_idxs = {0u};
        std::array<uint32_t, 1> b_idxs = {1u};

        sturm::v2::lib_SWAP(s, mgr, a_idxs.data(), b_idxs.data(), 1u);

        // Index arrays must be swapped.
        assert(a_idxs[0] == 1u && "SWAP: a_idxs[0] should now be 1");
        assert(b_idxs[0] == 0u && "SWAP: b_idxs[0] should now be 0");

        // SimState must be UNCHANGED (zero gates emitted).
        for (uint64_t i = 0; i < 4u; ++i) {
            double diff = std::abs(s.amplitude(i) - amp_before[i]);
            if (diff > kTol) {
                std::printf("FAIL zero_gates: in=%llu amp[%llu] changed by %g\n",
                            (unsigned long long)in,
                            (unsigned long long)i, diff);
                assert(false);
            }
        }
    }
    std::puts("  PASS: lib_SWAP uncontrolled emits zero gates (state unchanged)");
}

// ── test_swap_uncontrolled_two_qubit_register ────────────────────────────────
//
// 2-qubit registers: a = {q0, q1}, b = {q2, q3}.
// Same zero-gates property.
static void test_swap_uncontrolled_two_qubit_register() {
    sturm::v2::SimState s;
    s.allocate(4);
    // Arbitrary input: q0=1, q1=0, q2=1, q3=1 → idx = 1 + 0 + 4 + 8 = 13
    s.load_basis(0b1101u);

    std::array<std::complex<double>, 16> amp_before;
    for (uint64_t i = 0; i < 16u; ++i) amp_before[i] = s.amplitude(i);

    sturm::v2::AncillaManager mgr(s, 4u);

    std::array<uint32_t, 2> a_idxs = {0u, 1u};
    std::array<uint32_t, 2> b_idxs = {2u, 3u};

    sturm::v2::lib_SWAP(s, mgr, a_idxs.data(), b_idxs.data(), 2u);

    // Index arrays must be exchanged.
    assert(a_idxs[0] == 2u && a_idxs[1] == 3u);
    assert(b_idxs[0] == 0u && b_idxs[1] == 1u);

    // SimState unchanged.
    for (uint64_t i = 0; i < 16u; ++i) {
        assert(std::abs(s.amplitude(i) - amp_before[i]) < kTol);
    }
    std::puts("  PASS: lib_SWAP uncontrolled 2-qubit register — zero gates");
}

int main() {
    std::puts("=== test_swap_uncontrolled_zero_gates ===");
    test_swap_uncontrolled_zero_gates();
    test_swap_uncontrolled_two_qubit_register();
    std::puts("ALL PASS");
    return 0;
}
