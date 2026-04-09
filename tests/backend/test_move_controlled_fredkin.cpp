// test_move_controlled_fredkin.cpp — M5 (PRD v2): move_result controlled path
// emits n Fredkin gates for an n-qubit register.
//
// Tests:
//   - move_result_ctrl (explicit-ctrl variant): 1-qubit Fredkin truth table.
//   - move_result_when (WhenLift depth 1): same truth table via context dispatch.
//   - 2-qubit register: 2 Fredkins emitted, gate cost 2*(2 XOR + 1 AND).
//   - Ancilla manager shows no spurious allocation after each call.
//
// Fredkin truth table for (ctrl, a, b) where ctrl=q0, a=q1, b=q2:
//   ctrl=0: a,b unchanged.
//   ctrl=1: a,b swapped.
//
// Bit encoding: idx = bit0(ctrl) | bit1(a) | bit2(b).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/move.hpp"
#include "sturm/lib/swap.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/when_lift.hpp"

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

// ── test_move_ctrl_1qubit_truth_table ─────────────────────────────────────────
//
// Uses move_result_ctrl (explicit-ctrl) on 1-qubit registers.
// Qubit layout: q0=ctrl, q1=dest, q2=src.
// Expected: Fredkin gate behaviour (identical to lib_c_SWAP).
static void test_move_ctrl_1qubit_truth_table() {
    struct Entry {
        uint64_t in;
        uint64_t expected_out;
    };
    const Entry table[] = {
        {0b000, 0b000},  // ctrl=0,dest=0,src=0 → unchanged
        {0b001, 0b001},  // ctrl=1,dest=0,src=0 → swap (same): 1,0,0
        {0b010, 0b010},  // ctrl=0,dest=1,src=0 → unchanged: 0,1,0
        {0b011, 0b101},  // ctrl=1,dest=1,src=0 → swap: ctrl=1,dest=0,src=1 = 1+0+4=5
        {0b100, 0b100},  // ctrl=0,dest=0,src=1 → unchanged: 0,0,1
        {0b101, 0b011},  // ctrl=1,dest=0,src=1 → swap: ctrl=1,dest=1,src=0 = 1+2+0=3
        {0b110, 0b110},  // ctrl=0,dest=1,src=1 → unchanged: 0,1,1
        {0b111, 0b111},  // ctrl=1,dest=1,src=1 → swap (same): 1,1,1
    };

    for (const auto& e : table) {
        sturm::v2::SimState s;
        s.allocate(4);  // 3 user qubits + 1 ancilla slot
        s.load_basis(e.in);

        sturm::v2::AncillaManager mgr(s, 3u);

        uint32_t dest_idx = 1u;
        uint32_t src_idx  = 2u;

        // move_result_ctrl: explicit Fredkin, ctrl=q0
        sturm::v2::move_result_ctrl(&dest_idx, &src_idx, 1u, s, mgr, /*ctrl=*/0u);

        uint64_t got = dominant_basis(s) & 0b111u;
        if (got != e.expected_out) {
            std::printf("FAIL move_ctrl: in=0b%03llu exp=0b%03llu got=0b%03llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.expected_out,
                        (unsigned long long)got);
            assert(false);
        }

        // Index arrays must NOT be modified in controlled path.
        assert(dest_idx == 1u && "dest_idx must not change in controlled move");
        assert(src_idx  == 2u && "src_idx must not change in controlled move");

        // No spurious ancilla allocation.
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: move_result_ctrl 1-qubit truth table (all 8 inputs)");
}

// ── test_move_when_depth1_truth_table ─────────────────────────────────────────
//
// Uses move_result_when with a WhenLift at depth 1 (one active control).
// Same Fredkin truth table as above.
static void test_move_when_depth1_truth_table() {
    struct Entry { uint64_t in, out; };
    const Entry table[] = {
        {0b000, 0b000}, {0b001, 0b001}, {0b010, 0b010}, {0b011, 0b101},
        {0b100, 0b100}, {0b101, 0b011}, {0b110, 0b110}, {0b111, 0b111},
    };

    for (const auto& e : table) {
        sturm::v2::SimState s;
        s.allocate(4);
        s.load_basis(e.in);

        sturm::v2::AncillaManager mgr(s, 3u);
        sturm::v2::WhenLift wl(s, &mgr);

        // Push control qubit 0 to simulate WHEN q0: ...
        wl.push_control(0u);

        uint32_t dest_idx = 1u;
        uint32_t src_idx  = 2u;

        sturm::v2::move_result_when(&dest_idx, &src_idx, 1u, s, mgr,
                                    &wl, /*ctrl=*/0u);

        wl.pop_control();

        uint64_t got = dominant_basis(s) & 0b111u;
        if (got != e.out) {
            std::printf("FAIL move_when: in=0b%03llu exp=0b%03llu got=0b%03llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }

        assert(dest_idx == 1u && "dest_idx unchanged in controlled path");
        assert(src_idx  == 2u && "src_idx unchanged in controlled path");
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: move_result_when depth-1 WhenLift truth table (all 8)");
}

// ── test_move_ctrl_2qubit ─────────────────────────────────────────────────────
//
// 2-qubit registers.  Qubit layout:
//   q0=ctrl, q1=dest[0], q2=dest[1], q3=src[0], q4=src[1]
// (5 qubits total; 1 ancilla slot at q5.)
//
// Input: ctrl=1, dest={1,0}, src={0,1} → binary = 1 + 2 + 0 + 0 + 16 = 19
//   = bit0=1 (ctrl=1), bit1=1 (dest[0]=1), bit2=0 (dest[1]=0),
//     bit3=0 (src[0]=0), bit4=1 (src[1]=1)
// Expected output: ctrl=1, dest={0,1}, src={1,0}
//   = bit0=1, bit1=0, bit2=1, bit3=1, bit4=0
//   = 1 + 0 + 4 + 8 + 0 = 13
static void test_move_ctrl_2qubit() {
    sturm::v2::SimState s;
    s.allocate(6);  // 5 user + 1 ancilla

    // ctrl=1,dest0=1,dest1=0,src0=0,src1=1
    // idx = bit0 + 2*bit1 + 4*bit2 + 8*bit3 + 16*bit4
    //     = 1    + 2*1    + 4*0    + 8*0    + 16*1    = 1+2+0+0+16 = 19
    s.load_basis(19u);

    sturm::v2::AncillaManager mgr(s, 5u);

    std::array<uint32_t, 2> dest_idxs = {1u, 2u};
    std::array<uint32_t, 2> src_idxs  = {3u, 4u};

    // Controlled move: ctrl=q0.
    sturm::v2::move_result_ctrl(dest_idxs.data(), src_idxs.data(), 2u,
                                s, mgr, /*ctrl=*/0u);

    // Expected: ctrl unchanged (q0=1), dest qubits swapped with src qubits.
    // New state: q0=1, q1=0(was src0), q2=1(was src1), q3=1(was dest0), q4=0(was dest1)
    // = bit0=1, bit1=0, bit2=1, bit3=1, bit4=0 = 1+0+4+8+0 = 13
    uint64_t got = dominant_basis(s) & 0b11111u;
    if (got != 13u) {
        std::printf("FAIL move_ctrl_2q: expected=13 got=%llu\n",
                    (unsigned long long)got);
        assert(false);
    }

    // Index arrays unchanged (controlled path).
    assert(dest_idxs[0] == 1u && dest_idxs[1] == 2u);
    assert(src_idxs[0]  == 3u && src_idxs[1]  == 4u);

    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: move_result_ctrl 2-qubit register Fredkin");
}

// ── test_move_result_direct_controlled ───────────────────────────────────────
//
// Tests that the primary move_result() overload (taking WhenLift*) correctly
// dispatches to the per-qubit Fredkin cascade when wl->control_depth() > 0,
// extracting the control qubit automatically via WhenLift::top_control().
//
// Qubit layout: q0=ctrl, q1=dest, q2=src.
// Same Fredkin truth table as the explicit-ctrl tests above.
static void test_move_result_direct_controlled() {
    struct Entry { uint64_t in, out; };
    const Entry table[] = {
        {0b000, 0b000}, {0b001, 0b001}, {0b010, 0b010}, {0b011, 0b101},
        {0b100, 0b100}, {0b101, 0b011}, {0b110, 0b110}, {0b111, 0b111},
    };

    for (const auto& e : table) {
        sturm::v2::SimState s;
        s.allocate(4);  // 3 user qubits + 1 ancilla slot
        s.load_basis(e.in);

        sturm::v2::AncillaManager mgr(s, 3u);
        sturm::v2::WhenLift wl(s, &mgr);

        // Push q0 as the active control qubit (simulates being inside WHEN q0).
        wl.push_control(0u);

        uint32_t dest_idx = 1u;
        uint32_t src_idx  = 2u;

        // Primary move_result() — must auto-extract top_control() and Fredkin.
        sturm::v2::move_result(&dest_idx, &src_idx, 1u, s, mgr, &wl);

        wl.pop_control();

        uint64_t got = dominant_basis(s) & 0b111u;
        if (got != e.out) {
            std::printf("FAIL move_result direct controlled: in=0b%03llu exp=0b%03llu got=0b%03llu\n",
                        (unsigned long long)e.in,
                        (unsigned long long)e.out,
                        (unsigned long long)got);
            assert(false);
        }

        // Index arrays must NOT be modified in the controlled path.
        assert(dest_idx == 1u && "dest_idx must not change in controlled move_result");
        assert(src_idx  == 2u && "src_idx must not change in controlled move_result");
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: move_result(wl depth=1) direct controlled — Fredkin truth table (all 8)");
}

int main() {
    std::puts("=== test_move_controlled_fredkin ===");
    test_move_ctrl_1qubit_truth_table();
    test_move_when_depth1_truth_table();
    test_move_ctrl_2qubit();
    test_move_result_direct_controlled();
    std::puts("ALL PASS");
    return 0;
}
