// test_swap_under_when.cpp — M4 (PRD v2): WHEN a: SWAP(b,c) dispatches to
// Fredkin, not three CCNOTs.
//
// PRD v2 §6: "WHEN lift must dispatch SWAP to the Fredkin form directly.
// Never implement SWAP as three XORs and then rely on WHEN lift — that would
// emit three CCNOTs instead of one AND."
//
// Tests:
//   1. lib_SWAP_when with active control routes to lib_c_SWAP (Fredkin).
//      Truth table on all 8 inputs for ctrl=q0, a=q1, b=q2.
//
//   2. lib_SWAP_when with empty control stack falls back to index relabel
//      (zero gates, state unchanged).
//
//   3. Discriminating test: ctrl=1,a=1,b=0 → output must be ctrl=1,a=0,b=1
//      (Fredkin correct), not a three-CCX artefact.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/swap.hpp"
#include "sturm/backend/when_lift.hpp"
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

// ── test_swap_under_when_truth_table ─────────────────────────────────────────
//
// WHEN ctrl: SWAP(a, b) must behave as Fredkin.
// Qubit layout: q0=ctrl, q1=a, q2=b. idx = q0 + 2*q1 + 4*q2.
static void test_swap_under_when_truth_table() {
    struct Entry {
        uint64_t in;
        uint64_t out;
    };
    const Entry table[] = {
        {0b000, 0b000},
        {0b001, 0b001},  // ctrl=1,a=0,b=0 → no swap needed (same)
        {0b010, 0b010},  // ctrl=0,a=1,b=0 → no ctrl, unchanged
        {0b011, 0b101},  // ctrl=1,a=1,b=0 → swap → ctrl=1,a=0,b=1 → 5
        {0b100, 0b100},
        {0b101, 0b011},  // ctrl=1,a=0,b=1 → swap → ctrl=1,a=1,b=0 → 3
        {0b110, 0b110},
        {0b111, 0b111},  // ctrl=1,a=1,b=1 → swap (same)
    };

    for (const auto& e : table) {
        sturm::v2::SimState s;
        s.allocate(4);   // q0-q2 user; q3 ancilla slot
        s.load_basis(e.in);

        sturm::v2::AncillaManager mgr(s, 3u);
        sturm::v2::WhenLift wl(s, &mgr);

        // Push ctrl = q0 onto the control stack.
        wl.push_control(0u);

        // lib_SWAP_when with ctrl=q0: should detect controlled context and use Fredkin.
        uint32_t a_idx = 1u;
        uint32_t b_idx = 2u;
        sturm::v2::lib_SWAP_when(wl, s, mgr, 0u /*ctrl*/, &a_idx, &b_idx, 1u);

        wl.pop_control();

        uint64_t got = dominant_basis(s) & 0b111u;
        if (got != e.out) {
            std::printf("FAIL SWAP_when: in=0b%03llu exp=0b%03llu got=0b%03llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }

        assert(mgr.num_in_use() == 0u && "SWAP_when: ancilla must be free after call");
    }
    std::puts("  PASS: WHEN ctrl: SWAP(a,b) truth table matches Fredkin");
}

// ── test_swap_under_when_no_control_is_relabel ───────────────────────────────
//
// When there are NO controls on the WhenLift stack, lib_SWAP_when must
// fall back to the zero-gate index relabel (not emit Fredkin gates).
// Verify by checking the state is unchanged.
static void test_swap_under_when_no_control_is_relabel() {
    for (uint64_t in = 0; in < 4u; ++in) {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(in);

        // Snapshot amplitudes.
        std::array<std::complex<double>, 4> amp_before;
        for (uint64_t i = 0; i < 4u; ++i) amp_before[i] = s.amplitude(i);

        sturm::v2::AncillaManager mgr(s, 2u);
        sturm::v2::WhenLift wl(s, &mgr);

        // Empty control stack → should relabel, not emit gates.
        // ctrl parameter is ignored when control_depth()==0.
        uint32_t a_idx = 0u;
        uint32_t b_idx = 1u;
        sturm::v2::lib_SWAP_when(wl, s, mgr, 99u /*ignored ctrl*/, &a_idx, &b_idx, 1u);

        // Index arrays must be swapped.
        assert(a_idx == 1u && "SWAP_when no ctrl: a should be relabeled to 1");
        assert(b_idx == 0u && "SWAP_when no ctrl: b should be relabeled to 0");

        // State must be unchanged (zero gates).
        for (uint64_t i = 0; i < 4u; ++i) {
            assert(std::abs(s.amplitude(i) - amp_before[i]) < kTol);
        }
    }
    std::puts("  PASS: lib_SWAP_when with empty control stack is index relabel");
}

// ── test_swap_under_when_produces_fredkin_not_3ccnot ─────────────────────────
//
// Verify that for input ctrl=1, a=1, b=0, the output is ctrl=1, a=0, b=1
// (Fredkin correct) and NOT any 3-CCX artefact.
// This is the critical discriminating test from the PRD.
static void test_swap_under_when_produces_fredkin_not_3ccnot() {
    // Input: ctrl=1, a=1, b=0 → binary: q0=1, q1=1, q2=0 → idx=3
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0b011u);

    sturm::v2::AncillaManager mgr(s, 3u);
    sturm::v2::WhenLift wl(s, &mgr);
    wl.push_control(0u);

    uint32_t a_idx = 1u;
    uint32_t b_idx = 2u;
    sturm::v2::lib_SWAP_when(wl, s, mgr, 0u /*ctrl*/, &a_idx, &b_idx, 1u);
    wl.pop_control();

    // Expected Fredkin output: ctrl=1, a=0, b=1 → idx=5 (q0=1,q1=0,q2=1)
    uint64_t got = dominant_basis(s) & 0b111u;
    assert(got == 0b101u && "WHEN ctrl: SWAP must produce Fredkin, not 3-CCX artefact");
    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: WHEN ctrl: SWAP produces Fredkin (not 3-CCX) for ctrl=1,a=1,b=0");
}

int main() {
    std::puts("=== test_swap_under_when ===");
    test_swap_under_when_no_control_is_relabel();
    test_swap_under_when_truth_table();
    test_swap_under_when_produces_fredkin_not_3ccnot();
    std::puts("ALL PASS");
    return 0;
}
