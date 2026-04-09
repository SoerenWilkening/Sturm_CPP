// test_add_cuccaro.cpp — M6 (PRD v2): exhaustive 4-bit ADD truth table,
// controlled ADD under WHEN, overflow/carry documented.
//
// Tests:
//   1. test_add_exhaustive_4bit — all 256 inputs (a[0..15] + b[0..15]).
//   2. test_add_no_carry_out    — small values, verify carry qubit stays |0>.
//   3. test_add_overflow        — a+b > 15, verify carry qubit captures overflow.
//   4. test_add_controlled      — controlled ADD under WHEN: if ctrl=0 b unchanged,
//                                 if ctrl=1 b = a+b.
//   5. test_add_identity        — b += 0 leaves b unchanged.
//
// Harness: plain assert + printf.

#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Return qubit q's value (0 or 1) from a pure computational-basis state.
static uint32_t read_qubit(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim  = uint64_t{1} << s.num_qubits();
    uint64_t mask = uint64_t{1} << q;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(s.amplitude(i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

// Read an n-qubit register starting at qubit first_qubit (LSB = first_qubit).
static uint32_t read_reg(const sturm::v2::SimState& s,
                         uint32_t first_qubit, uint32_t n) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        val |= (read_qubit(s, first_qubit + i) << i);
    }
    return val;
}

// Load an n-qubit value into qubits [first_qubit, first_qubit+n) of a SimState
// that has already been allocate()'d and load_basis()'d.
// This is a helpers that builds the full basis index for a two-register state.
// Layout used in all tests:
//   qubits [0..n-1]  = a (n bits, LSB=0)
//   qubits [n..2n-1] = b (n bits, LSB=n)
//   qubit  [2n]      = carry_out (ancilla, must start |0>)
//   qubits [2n+1..]  = ancilla pool for AncillaManager

static sturm::v2::SimState make_add_state(uint32_t n,
                                          uint32_t a_val, uint32_t b_val) {
    // Total qubits: n (a) + n (b) + 1 (carry_out) + n (ancilla pool for Cuccaro)
    // The Cuccaro adder needs 1 carry ancilla per bit in the MAJ chain.
    // We allocate n+2 extra ancilla slots to be safe.
    uint32_t total = n + n + 1u + n + 2u;  // generous pool
    if (total > 17u) total = 17u;           // hard cap

    sturm::v2::SimState s;
    s.allocate(total);

    // Build basis index: set a_val in [0..n-1], b_val in [n..2n-1].
    uint64_t idx = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        if ((a_val >> i) & 1u) idx |= (uint64_t{1} << i);
    }
    for (uint32_t i = 0; i < n; ++i) {
        if ((b_val >> i) & 1u) idx |= (uint64_t{1} << (n + i));
    }
    s.load_basis(idx);
    return s;
}

// ── test_add_exhaustive_4bit ──────────────────────────────────────────────────
// For all a in [0..15] and b in [0..15]:
//   After lib_add_cuccaro(s, mgr, a_qubits, b_qubits, carry_out, 4):
//     sum = (a + b) mod 16; carry_out = (a + b) >> 4.
//
// Qubit layout:
//   q[0..3]  = a  (unchanged after add)
//   q[4..7]  = b  (becomes sum mod 16)
//   q[8]     = carry_out (captures bit 4 of a+b)
//   q[9..]   = ancilla pool
static void test_add_exhaustive_4bit() {
    const uint32_t n = 4u;
    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < 16u; ++a_val) {
        for (uint32_t b_val = 0; b_val < 16u; ++b_val) {
            auto s = make_add_state(n, a_val, b_val);

            // a_idxs = [0,1,2,3], b_idxs = [4,5,6,7], carry_out = qubit 8
            // ancilla pool starts at qubit 9
            uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
            uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
            uint32_t carry_out = 8u;

            sturm::v2::AncillaManager mgr(s, 9u);
            sturm::v2::lib_add_cuccaro(s, mgr, a_idxs, b_idxs, carry_out, n);

            uint32_t got_a   = read_reg(s, 0u, n);
            uint32_t got_b   = read_reg(s, n,  n);
            uint32_t got_c   = read_qubit(s, carry_out);
            uint32_t expected_sum   = (a_val + b_val) & 0xFu;
            uint32_t expected_carry = (a_val + b_val) >> 4u;

            if (got_a != a_val) {
                std::printf("FAIL add: a=%u b=%u: a changed to %u\n",
                            a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != expected_sum) {
                std::printf("FAIL add: a=%u b=%u: sum expected=%u got=%u\n",
                            a_val, b_val, expected_sum, got_b);
                assert(false);
            }
            if (got_c != expected_carry) {
                std::printf("FAIL add carry: a=%u b=%u: carry expected=%u got=%u\n",
                            a_val, b_val, expected_carry, got_c);
                assert(false);
            }
            // ancilla pool must be fully returned
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL add: a=%u b=%u: %u ancillas leaked\n",
                            a_val, b_val, mgr.num_in_use());
                assert(false);
            }
            ++pass_count;
        }
    }
    std::printf("  PASS: exhaustive 4-bit add (%u cases)\n", pass_count);
}

// ── test_add_no_carry_out ─────────────────────────────────────────────────────
// 3 + 5 = 8 — no carry; verify carry qubit stays |0> after the add.
static void test_add_no_carry_out() {
    const uint32_t n = 4u;
    auto s = make_add_state(n, 3u, 5u);
    uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
    uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
    uint32_t carry_out = 8u;

    sturm::v2::AncillaManager mgr(s, 9u);
    sturm::v2::lib_add_cuccaro(s, mgr, a_idxs, b_idxs, carry_out, n);

    assert(read_reg(s, n, n) == 8u  && "3+5 = 8");
    assert(read_qubit(s, carry_out) == 0u && "no carry for 3+5");
    std::puts("  PASS: 3+5=8, carry=0");
}

// ── test_add_overflow ─────────────────────────────────────────────────────────
// 9 + 10 = 19 = 3 (mod 16) with carry=1.
// Documents overflow: carry_out qubit captures the overflow bit.
static void test_add_overflow() {
    const uint32_t n = 4u;
    auto s = make_add_state(n, 9u, 10u);
    uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
    uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
    uint32_t carry_out = 8u;

    sturm::v2::AncillaManager mgr(s, 9u);
    sturm::v2::lib_add_cuccaro(s, mgr, a_idxs, b_idxs, carry_out, n);

    assert(read_reg(s, n, n) == 3u  && "9+10 mod 16 = 3");
    assert(read_qubit(s, carry_out) == 1u && "9+10 overflows 4-bit");
    std::puts("  PASS: 9+10=19: sum=3, carry=1 (overflow documented)");
}

// ── test_add_identity ─────────────────────────────────────────────────────────
// b += 0 leaves b unchanged for all b in [0..15].
static void test_add_identity() {
    const uint32_t n = 4u;
    for (uint32_t b_val = 0; b_val < 16u; ++b_val) {
        auto s = make_add_state(n, 0u, b_val);
        uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
        uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
        uint32_t carry_out = 8u;

        sturm::v2::AncillaManager mgr(s, 9u);
        sturm::v2::lib_add_cuccaro(s, mgr, a_idxs, b_idxs, carry_out, n);

        assert(read_reg(s, n, n) == b_val && "b += 0 must leave b unchanged");
        assert(read_qubit(s, carry_out) == 0u && "no carry for 0+b");
    }
    std::puts("  PASS: b += 0 identity (all 4-bit b)");
}

// ── test_add_controlled ───────────────────────────────────────────────────────
// Controlled ADD under WHEN:
//   ctrl=0: b remains b_val, carry_out stays 0.
//   ctrl=1: b becomes (a+b) mod 16, carry_out captures overflow.
//
// Spot-checks: a=5, b=7 (sum=12, no overflow) and a=12, b=11 (sum=7, overflow).
static void test_add_controlled() {
    const uint32_t n = 4u;

    // ── ctrl = 0: b unchanged ────────────────────────────────────────────────
    {
        // Layout: q0=ctrl, q[1..4]=a, q[5..8]=b, q[9]=carry, q[10..]=ancilla
        uint32_t total = 1u + n + n + 1u + n + 4u;
        if (total > 17u) total = 17u;
        sturm::v2::SimState s;
        s.allocate(total);

        uint32_t a_val = 5u, b_val = 7u;
        uint64_t idx = 0u; // ctrl=0
        for (uint32_t i = 0; i < n; ++i) if ((a_val >> i)&1) idx |= uint64_t{1} << (1u+i);
        for (uint32_t i = 0; i < n; ++i) if ((b_val >> i)&1) idx |= uint64_t{1} << (1u+n+i);
        s.load_basis(idx);

        uint32_t ctrl = 0u;
        uint32_t a_idxs[4] = {1u, 2u, 3u, 4u};
        uint32_t b_idxs[4] = {5u, 6u, 7u, 8u};
        uint32_t carry_out = 9u;
        sturm::v2::AncillaManager mgr(s, 10u);
        sturm::v2::WhenLift wl(s, &mgr);

        wl.push_control(ctrl);
        sturm::v2::lib_add_cuccaro_when(wl, s, mgr, a_idxs, b_idxs, carry_out, n);
        wl.pop_control();

        assert(read_reg(s, 1u+n, n) == b_val && "ctrl=0: b unchanged");
        assert(read_qubit(s, carry_out) == 0u && "ctrl=0: carry=0");
        std::puts("  PASS: controlled ADD ctrl=0 — b unchanged");
    }

    // ── ctrl = 1: b becomes a+b ──────────────────────────────────────────────
    {
        uint32_t total = 1u + n + n + 1u + n + 4u;
        if (total > 17u) total = 17u;
        sturm::v2::SimState s;
        s.allocate(total);

        uint32_t a_val = 5u, b_val = 7u;
        uint64_t idx = 1u; // ctrl=1
        for (uint32_t i = 0; i < n; ++i) if ((a_val >> i)&1) idx |= uint64_t{1} << (1u+i);
        for (uint32_t i = 0; i < n; ++i) if ((b_val >> i)&1) idx |= uint64_t{1} << (1u+n+i);
        s.load_basis(idx);

        uint32_t ctrl = 0u;
        uint32_t a_idxs[4] = {1u, 2u, 3u, 4u};
        uint32_t b_idxs[4] = {5u, 6u, 7u, 8u};
        uint32_t carry_out = 9u;
        sturm::v2::AncillaManager mgr(s, 10u);
        sturm::v2::WhenLift wl(s, &mgr);

        wl.push_control(ctrl);
        sturm::v2::lib_add_cuccaro_when(wl, s, mgr, a_idxs, b_idxs, carry_out, n);
        wl.pop_control();

        uint32_t expected = (a_val + b_val) & 0xFu;
        uint32_t got = read_reg(s, 1u+n, n);
        if (got != expected) {
            std::printf("FAIL ctrl=1 add: expected %u got %u\n", expected, got);
            assert(false);
        }
        assert(read_qubit(s, carry_out) == 0u && "5+7=12 no overflow");
        std::puts("  PASS: controlled ADD ctrl=1 — b = a+b");
    }

    // ── ctrl=1, overflow case: a=12, b=11 ───────────────────────────────────
    {
        uint32_t total = 1u + n + n + 1u + n + 4u;
        if (total > 17u) total = 17u;
        sturm::v2::SimState s;
        s.allocate(total);

        uint32_t a_val = 12u, b_val = 11u;
        uint64_t idx = 1u; // ctrl=1
        for (uint32_t i = 0; i < n; ++i) if ((a_val >> i)&1) idx |= uint64_t{1} << (1u+i);
        for (uint32_t i = 0; i < n; ++i) if ((b_val >> i)&1) idx |= uint64_t{1} << (1u+n+i);
        s.load_basis(idx);

        uint32_t ctrl = 0u;
        uint32_t a_idxs[4] = {1u, 2u, 3u, 4u};
        uint32_t b_idxs[4] = {5u, 6u, 7u, 8u};
        uint32_t carry_out = 9u;
        sturm::v2::AncillaManager mgr(s, 10u);
        sturm::v2::WhenLift wl(s, &mgr);

        wl.push_control(ctrl);
        sturm::v2::lib_add_cuccaro_when(wl, s, mgr, a_idxs, b_idxs, carry_out, n);
        wl.pop_control();

        assert(read_reg(s, 1u+n, n) == 7u  && "12+11 mod16=7");
        assert(read_qubit(s, carry_out) == 1u && "12+11 overflows");
        std::puts("  PASS: controlled ADD ctrl=1, overflow: 12+11=23 -> sum=7, carry=1");
    }
}

int main() {
    std::puts("=== test_add_cuccaro ===");
    test_add_exhaustive_4bit();
    test_add_no_carry_out();
    test_add_overflow();
    test_add_identity();
    test_add_controlled();
    std::puts("ALL PASS");
    return 0;
}
