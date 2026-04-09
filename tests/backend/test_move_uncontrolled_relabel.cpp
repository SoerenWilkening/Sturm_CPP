// test_move_uncontrolled_relabel.cpp — M5 (PRD v2): move_result uncontrolled
// path emits zero gates and relabels the qubit-index arrays.
//
// Tests:
//   - move_result_when with wl==nullptr: swaps the index arrays, zero gates.
//   - move_result_when with wl pointing to a WhenLift with depth 0:
//       same zero-gate, index-relabel behaviour.
//   - Multi-qubit (2-bit) register relabel: all qubit indices swapped.
//   - SimState amplitudes are unchanged in all cases (zero gates).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/move.hpp"
#include "sturm/lib/swap.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <array>

static constexpr double kTol = 1e-9;

// Snapshot all amplitudes from a SimState (for zero-gate verification).
template <std::size_t N>
static void snapshot(const sturm::v2::SimState& s,
                     std::array<std::complex<double>, N>& out) {
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = s.amplitude(static_cast<uint64_t>(i));
    }
}

template <std::size_t N>
static bool amps_unchanged(const sturm::v2::SimState& s,
                            const std::array<std::complex<double>, N>& before) {
    for (std::size_t i = 0; i < N; ++i) {
        if (std::abs(s.amplitude(static_cast<uint64_t>(i)) - before[i]) > kTol)
            return false;
    }
    return true;
}

// ── test_move_null_wl ────────────────────────────────────────────────────────
//
// move_result_when with wl==nullptr (uncontrolled path).
// 1-qubit registers: dest={0}, src={1}.
// After call: dest={1}, src={0}. SimState unchanged.
static void test_move_null_wl() {
    for (uint64_t in = 0; in < 4u; ++in) {
        sturm::v2::SimState s;
        s.allocate(2);
        s.load_basis(in);

        std::array<std::complex<double>, 4> before;
        snapshot(s, before);

        sturm::v2::AncillaManager mgr(s, 2u);

        std::array<uint32_t, 1> dest = {0u};
        std::array<uint32_t, 1> src  = {1u};

        // wl == nullptr → uncontrolled relabel.
        sturm::v2::move_result_when(dest.data(), src.data(), 1u,
                                    s, mgr,
                                    /*wl=*/nullptr, /*ctrl=*/0u);

        // Index arrays must be swapped.
        assert(dest[0] == 1u);
        assert(src[0]  == 0u);

        // State must be unchanged.
        assert(amps_unchanged(s, before) && "move uncontrolled: state changed");
    }
    std::puts("  PASS: move_result_when(wl=null) — zero gates, index swapped");
}

// ── test_move_zero_depth_wl ───────────────────────────────────────────────────
//
// move_result_when with a WhenLift at depth 0 (no active control).
// Same zero-gate behaviour.
static void test_move_zero_depth_wl() {
    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b10u);  // q0=0, q1=1

    std::array<std::complex<double>, 4> before;
    snapshot(s, before);

    sturm::v2::AncillaManager mgr(s, 2u);
    sturm::v2::WhenLift wl(s, &mgr);
    // control_depth() == 0 → uncontrolled path.

    std::array<uint32_t, 1> dest = {0u};
    std::array<uint32_t, 1> src  = {1u};

    sturm::v2::move_result_when(dest.data(), src.data(), 1u,
                                s, mgr, &wl, /*ctrl=*/0u);

    assert(dest[0] == 1u);
    assert(src[0]  == 0u);
    assert(amps_unchanged(s, before));
    std::puts("  PASS: move_result_when(wl depth=0) — zero gates, index swapped");
}

// ── test_move_multi_qubit_relabel ─────────────────────────────────────────────
//
// 2-qubit registers: dest={0,1}, src={2,3}.
// After uncontrolled move: dest={2,3}, src={0,1}. SimState unchanged.
static void test_move_multi_qubit_relabel() {
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0b1010u);  // q0=0, q1=1, q2=0, q3=1

    std::array<std::complex<double>, 16> before;
    snapshot(s, before);

    sturm::v2::AncillaManager mgr(s, 4u);

    std::array<uint32_t, 2> dest = {0u, 1u};
    std::array<uint32_t, 2> src  = {2u, 3u};

    sturm::v2::move_result_when(dest.data(), src.data(), 2u,
                                s, mgr, nullptr, 0u);

    // All index pairs must be swapped.
    assert(dest[0] == 2u && dest[1] == 3u);
    assert(src[0]  == 0u && src[1]  == 1u);
    assert(amps_unchanged(s, before));
    std::puts("  PASS: move_result_when multi-qubit relabel — zero gates");
}

// ── test_move_result_direct ───────────────────────────────────────────────────
//
// Direct move_result() call: same zero-gate behaviour as move_result_when(null).
static void test_move_result_direct() {
    sturm::v2::SimState s;
    s.allocate(2);
    s.load_basis(0b01u);

    std::array<std::complex<double>, 4> before;
    snapshot(s, before);

    sturm::v2::AncillaManager mgr(s, 2u);

    std::array<uint32_t, 1> dest = {0u};
    std::array<uint32_t, 1> src  = {1u};

    sturm::v2::move_result(dest.data(), src.data(), 1u, s, mgr, /*wl=*/nullptr);

    assert(dest[0] == 1u);
    assert(src[0]  == 0u);
    assert(amps_unchanged(s, before));
    std::puts("  PASS: move_result(wl=null) direct — zero gates, index swapped");
}

int main() {
    std::puts("=== test_move_uncontrolled_relabel ===");
    test_move_null_wl();
    test_move_zero_depth_wl();
    test_move_multi_qubit_relabel();
    test_move_result_direct();
    std::puts("ALL PASS");
    return 0;
}
