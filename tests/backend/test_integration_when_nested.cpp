// test_integration_when_nested.cpp — M10 (PRD v2): Deep WHEN nesting drives
// c_n_AND with clean ancilla pool.
//
// Tests:
//   1. test_when_nested_n_controls — WHEN nesting depth 3, 4, 5 drives
//      c_n_AND (via WhenLift::lift_X) and confirms:
//        (a) target flips iff all controls are 1.
//        (b) ancilla pool is |0⟩ after each call.
//        (c) mgr.num_in_use() == 0.
//
//   2. test_when_nested_add — WHEN(ctrl1, ctrl2): b += a  (controlled Cuccaro
//      ADD under 2 explicit controls, which internally uses c_n_AND through
//      the WhenLift machinery for each gate).
//        (a) When both controls are 1: b = a + b.
//        (b) When either control is 0: b unchanged.
//        (c) Ancilla pool clean after each call.
//
//   3. test_when_nested_xor_chain — Nested WHEN builds a stack of 4 controls,
//      each via push_control, and the innermost operation (lift_XOR) must
//      synthesise a C^5-X gate through c_and_impl (4 controls + 1 src → 5).
//      Ancilla pool verified |0⟩.
//
// Qubit budget for test 1 (n_ctrl=5):
//   ctrls q[0..4]  5 bits
//   target q[5]    1 bit
//   pool   q[6..16] 11 slots  (c_n_AND needs at most n-2=3 ancilla)
//   Total: 17 qubits ✓
//
// Qubit budget for test 2 (n=2 bit ADD under 2 controls):
//   ctrl0 q[0], ctrl1 q[1]   2 bits
//   a     q[2..3]             2 bits
//   b     q[4..5]             2 bits
//   carry q[6]                1 bit
//   pool  q[7..16]            10 slots
//   Total: 17 qubits ✓
//   Peak: Cuccaro carry_anc(1) + WhenLift fold(1) + c_n_AND anc(n_extra)
//         For 3 controls (ctrl0, ctrl1, src) targeting (tgt), c_and_impl needs
//         1 ancilla.  Sequential, so peak = 1+1 = 2.  10 >> 2 ✓
//
// Qubit budget for test 3 (4-deep WHEN stack + lift_XOR, n_ctrl=4+1=5):
//   ctrl q[0..3]  4 bits (pushed individually)
//   src  q[4]     1 bit
//   tgt  q[5]     1 bit
//   pool q[6..16] 11 slots (c_n_AND needs 3 ancilla for 5-ctrl C^5X)
//   Total: 17 qubits ✓
//
// Harness: plain assert + printf (project convention).

#include "sturm/backend/when_lift.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/lib/add_cuccaro.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint64_t dominant_basis(const sturm::v2::SimState& s) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    uint64_t best = 0u;
    double   best_p = 0.0;
    for (uint64_t i = 0u; i < dim; ++i) {
        double p = std::norm(s.amplitude(i));
        if (p > best_p) { best_p = p; best = i; }
    }
    return best;
}

static uint32_t read_qubit(const sturm::v2::SimState& s, uint32_t q) {
    return static_cast<uint32_t>((dominant_basis(s) >> q) & 1u);
}

static uint32_t read_reg(const sturm::v2::SimState& s,
                         const uint32_t* idxs, uint32_t n) {
    uint64_t basis = dominant_basis(s);
    uint32_t val = 0u;
    for (uint32_t i = 0u; i < n; ++i)
        val |= static_cast<uint32_t>((basis >> idxs[i]) & 1u) << i;
    return val;
}

// Check all ancilla qubits in [anc_start, total) are |0⟩.
static void assert_ancillas_zero(const sturm::v2::SimState& s,
                                 uint32_t anc_start, uint32_t total,
                                 const char* label) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint32_t q = anc_start; q < total; ++q) {
        uint64_t mask = uint64_t{1} << q;
        for (uint64_t i = 0u; i < dim; ++i) {
            if ((i & mask) != 0u) {
                if (std::norm(s.amplitude(i)) > kTol) {
                    std::printf("FAIL %s: ancilla q[%u] not |0⟩\n", label, q);
                    assert(false);
                }
            }
        }
    }
}

// ── test_when_nested_n_controls ───────────────────────────────────────────────
//
// For n=3,4,5 controls: enumerate all 2^n control inputs.
// Push all n controls onto WhenLift, call lift_X(tgt).
// Verify: tgt flips iff all controls are 1.
// Verify: ancilla pool |0⟩ after each call.
//
// Qubit layout (n controls):
//   q[0..n-1]   = controls
//   q[n]        = target
//   q[n+1..16]  = ancilla pool (generous)

static void test_when_nested_n_controls() {
    for (uint32_t n = 3u; n <= 5u; ++n) {
        uint32_t tgt_q     = n;
        uint32_t anc_start = n + 1u;
        uint32_t total     = 17u;

        uint64_t n_inputs = uint64_t{1} << n;

        for (uint64_t ctrl_bits = 0u; ctrl_bits < n_inputs; ++ctrl_bits) {
            sturm::v2::SimState s;
            s.allocate(total);
            // Load control bits; target starts |0⟩.
            s.load_basis(ctrl_bits);

            sturm::v2::AncillaManager mgr(s, anc_start);
            sturm::v2::WhenLift wl(s, &mgr);

            // Push all n controls.
            for (uint32_t ci = 0u; ci < n; ++ci)
                wl.push_control(ci);

            // Emit the controlled NOT — routes through c_n_AND for n >= 3.
            wl.lift_X(tgt_q);

            // Pop all controls (LIFO).
            for (uint32_t ci = 0u; ci < n; ++ci)
                wl.pop_control();

            // Expected: tgt = 1 iff all n controls are 1.
            uint64_t all_ones = n_inputs - 1u;
            uint32_t exp_tgt  = (ctrl_bits == all_ones) ? 1u : 0u;
            uint32_t got_tgt  = read_qubit(s, tgt_q);

            if (got_tgt != exp_tgt) {
                std::printf("FAIL when_nested n=%u ctrl=0x%llx: exp=%u got=%u\n",
                            n, (unsigned long long)ctrl_bits, exp_tgt, got_tgt);
                assert(false);
            }

            // Ancilla pool must be |0⟩.
            assert_ancillas_zero(s, anc_start, total, "when_nested_n_controls");
            assert(mgr.num_in_use() == 0u && "when_nested: mgr not clean");
        }
        std::printf("  PASS: when_nested n=%u, all %llu combos, ancilla |0⟩\n",
                    n, (unsigned long long)n_inputs);
    }
}

// ── test_when_nested_add ──────────────────────────────────────────────────────
//
// WHEN(ctrl0, ctrl1): b += a  (controlled Cuccaro ADD under 2 explicit controls).
//
// For each (ctrl0, ctrl1, a, b) with n=2-bit a,b:
//   - If ctrl0=1 and ctrl1=1: b becomes a+b mod 4.
//   - Otherwise: b unchanged.
// Ancilla pool clean after each call.
//
// Qubit layout:
//   q[0]     = ctrl0
//   q[1]     = ctrl1
//   q[2..3]  = a  (2 bits)
//   q[4..5]  = b  (2 bits)
//   q[6]     = carry-out
//   q[7..16] = ancilla pool (10 slots)

static void test_when_nested_add() {
    static constexpr uint32_t N        = 2u;
    static constexpr uint32_t kCtrl0   = 0u;
    static constexpr uint32_t kCtrl1   = 1u;
    static constexpr uint32_t kAStart  = 2u;
    static constexpr uint32_t kBStart  = 4u;
    static constexpr uint32_t kCarry   = 6u;
    static constexpr uint32_t kAncStart= 7u;
    static constexpr uint32_t kTotal   = 17u;
    static constexpr uint32_t kMask    = (1u << N) - 1u;

    uint32_t a_idxs[N] = {kAStart, kAStart+1u};
    uint32_t b_idxs[N] = {kBStart, kBStart+1u};

    uint32_t pass = 0u;

    for (uint32_t c0 = 0u; c0 < 2u; ++c0) {
        for (uint32_t c1 = 0u; c1 < 2u; ++c1) {
            for (uint32_t av = 0u; av < (1u << N); ++av) {
                for (uint32_t bv = 0u; bv < (1u << N); ++bv) {
                    sturm::v2::SimState s;
                    s.allocate(kTotal);

                    uint64_t idx = 0u;
                    if (c0) idx |= uint64_t{1} << kCtrl0;
                    if (c1) idx |= uint64_t{1} << kCtrl1;
                    for (uint32_t i = 0u; i < N; ++i)
                        if ((av >> i) & 1u) idx |= uint64_t{1} << (kAStart + i);
                    for (uint32_t i = 0u; i < N; ++i)
                        if ((bv >> i) & 1u) idx |= uint64_t{1} << (kBStart + i);
                    s.load_basis(idx);

                    sturm::v2::AncillaManager mgr(s, kAncStart);
                    sturm::v2::WhenLift wl(s, &mgr);

                    // Push both controls for depth-2 nesting.
                    wl.push_control(kCtrl0);
                    wl.push_control(kCtrl1);

                    // Controlled Cuccaro ADD: b += a under (ctrl0, ctrl1).
                    sturm::v2::lib_add_cuccaro_when(wl, s, mgr,
                                                    a_idxs, b_idxs, kCarry, N);

                    wl.pop_control();
                    wl.pop_control();

                    // Expected result.
                    bool do_add = (c0 == 1u) && (c1 == 1u);
                    uint32_t exp_b = do_add ? ((av + bv) & kMask) : bv;

                    uint32_t got_b = read_reg(s, b_idxs, N);
                    uint32_t got_a = read_reg(s, a_idxs, N);

                    if (got_b != exp_b || got_a != av) {
                        std::printf(
                            "FAIL when_add c0=%u c1=%u a=%u b=%u: "
                            "expected b=%u got_b=%u got_a=%u\n",
                            c0, c1, av, bv, exp_b, got_b, got_a);
                        assert(false);
                    }

                    // Ancilla pool clean.
                    assert_ancillas_zero(s, kAncStart, kTotal, "when_nested_add");
                    assert(mgr.num_in_use() == 0u && "when_add: ancilla leaked");

                    ++pass;
                }
            }
        }
    }
    std::printf("  PASS: test_when_nested_add — %u cases (ctrl depth=2, n=2)\n",
                pass);
}

// ── test_when_nested_xor_chain ────────────────────────────────────────────────
//
// Demonstrates depth-4 WHEN nesting combined with lift_XOR, producing an
// effective C^5-X gate (4 stack controls + src = 5 total controls) through
// c_and_impl recursion.
//
// Qubit layout:
//   q[0..3]  = ctrl stack (4 qubits)
//   q[4]     = src (XOR source)
//   q[5]     = tgt (XOR target)
//   q[6..16] = pool (11 slots; c_n_AND for 5 ctrls needs 3 ancilla)

static void test_when_nested_xor_chain() {
    static constexpr uint32_t kNCtrl   = 4u;  // depth of WHEN stack
    static constexpr uint32_t kSrc     = 4u;
    static constexpr uint32_t kTgt     = 5u;
    static constexpr uint32_t kAncStart= 6u;
    static constexpr uint32_t kTotal   = 17u;

    uint32_t pass = 0u;

    // Enumerate all (ctrl[0..3], src) combinations = 2^5 = 32.
    for (uint32_t bits = 0u; bits < (1u << (kNCtrl + 1u)); ++bits) {
        sturm::v2::SimState s;
        s.allocate(kTotal);
        // Load ctrl[0..3] and src; tgt starts |0⟩.
        s.load_basis(static_cast<uint64_t>(bits));

        sturm::v2::AncillaManager mgr(s, kAncStart);
        sturm::v2::WhenLift wl(s, &mgr);

        // Push 4 controls (depth=4).
        for (uint32_t ci = 0u; ci < kNCtrl; ++ci)
            wl.push_control(ci);

        // lift_XOR(src, tgt): with 4 controls on the stack, this internally
        // calls c_and_impl with 5 total controls → exercises c_n_AND recursion.
        wl.lift_XOR(kSrc, kTgt);

        for (uint32_t ci = 0u; ci < kNCtrl; ++ci)
            wl.pop_control();

        // Expected: tgt = 1 iff all 4 stack controls are 1 AND src is 1.
        bool all_ctrl_ones = (bits & ((1u << kNCtrl) - 1u)) == ((1u << kNCtrl) - 1u);
        bool src_one       = ((bits >> kNCtrl) & 1u) != 0u;
        uint32_t exp_tgt   = (all_ctrl_ones && src_one) ? 1u : 0u;
        uint32_t got_tgt   = read_qubit(s, kTgt);

        if (got_tgt != exp_tgt) {
            std::printf("FAIL when_xor_chain bits=0x%02x exp=%u got=%u\n",
                        bits, exp_tgt, got_tgt);
            assert(false);
        }

        // Ancilla pool |0⟩.
        assert_ancillas_zero(s, kAncStart, kTotal, "when_nested_xor_chain");
        assert(mgr.num_in_use() == 0u && "when_xor_chain: ancilla leaked");

        ++pass;
    }
    std::printf("  PASS: test_when_nested_xor_chain — %u cases "
                "(depth-4 WHEN + lift_XOR → C^5-X, ancilla |0⟩)\n", pass);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::puts("=== M10 test_integration_when_nested: deep WHEN nesting ===");
    std::printf("  qubit cap: 17; ancilla pool verified |0⟩ after each call\n");

    std::puts("  running test_when_nested_n_controls...");
    test_when_nested_n_controls();

    std::puts("  running test_when_nested_add...");
    test_when_nested_add();

    std::puts("  running test_when_nested_xor_chain...");
    test_when_nested_xor_chain();

    std::puts("ALL PASS");
    return 0;
}
