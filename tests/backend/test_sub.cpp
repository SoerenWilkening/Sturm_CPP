// test_sub.cpp — M6 (PRD v2): exhaustive 4-bit SUB truth table.
//
// Tests:
//   1. test_sub_exhaustive_4bit — all 256 inputs (b -= a mod 16).
//   2. test_sub_identity        — b -= 0 leaves b unchanged.
//   3. test_sub_underflow       — b < a: wrap-around documented.
//
// Harness: plain assert + printf.

#include "sturm/lib/sub.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── Helpers ───────────────────────────────────────────────────────────────────

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

static uint32_t read_reg(const sturm::v2::SimState& s,
                         uint32_t first_qubit, uint32_t n) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        val |= (read_qubit(s, first_qubit + i) << i);
    }
    return val;
}

// Build a state with layout:
//   q[0..n-1]  = a
//   q[n..2n-1] = b
//   q[2n]      = borrow_out (ancilla, starts |0>)
//   q[2n+1..]  = ancilla pool
static sturm::v2::SimState make_sub_state(uint32_t n,
                                          uint32_t a_val, uint32_t b_val) {
    uint32_t total = n + n + 1u + n + 2u;
    if (total > 17u) total = 17u;

    sturm::v2::SimState s;
    s.allocate(total);

    uint64_t idx = 0u;
    for (uint32_t i = 0; i < n; ++i)
        if ((a_val >> i) & 1u) idx |= (uint64_t{1} << i);
    for (uint32_t i = 0; i < n; ++i)
        if ((b_val >> i) & 1u) idx |= (uint64_t{1} << (n + i));
    s.load_basis(idx);
    return s;
}

// ── test_sub_exhaustive_4bit ──────────────────────────────────────────────────
// b -= a mod 16 for all a,b in [0..15].
// The borrow_out qubit captures whether b < a (underflow).
static void test_sub_exhaustive_4bit() {
    const uint32_t n = 4u;
    uint32_t pass_count = 0;

    for (uint32_t a_val = 0; a_val < 16u; ++a_val) {
        for (uint32_t b_val = 0; b_val < 16u; ++b_val) {
            auto s = make_sub_state(n, a_val, b_val);
            uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
            uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
            uint32_t borrow_out = 8u;

            sturm::v2::AncillaManager mgr(s, 9u);
            sturm::v2::lib_sub(s, mgr, a_idxs, b_idxs, borrow_out, n);

            uint32_t got_a      = read_reg(s, 0u, n);
            uint32_t got_b      = read_reg(s, n, n);
            uint32_t got_borrow = read_qubit(s, borrow_out);

            // Expected: (b - a) mod 16; borrow = 1 iff b < a
            uint32_t expected_diff   = (b_val - a_val) & 0xFu;
            uint32_t expected_borrow = (b_val < a_val) ? 1u : 0u;

            if (got_a != a_val) {
                std::printf("FAIL sub: a=%u b=%u: a changed to %u\n", a_val, b_val, got_a);
                assert(false);
            }
            if (got_b != expected_diff) {
                std::printf("FAIL sub: a=%u b=%u: diff expected=%u got=%u\n",
                            a_val, b_val, expected_diff, got_b);
                assert(false);
            }
            if (got_borrow != expected_borrow) {
                std::printf("FAIL sub borrow: a=%u b=%u: borrow expected=%u got=%u\n",
                            a_val, b_val, expected_borrow, got_borrow);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL sub: a=%u b=%u: %u ancillas leaked\n",
                            a_val, b_val, mgr.num_in_use());
                assert(false);
            }
            ++pass_count;
        }
    }
    std::printf("  PASS: exhaustive 4-bit sub (%u cases)\n", pass_count);
}

// ── test_sub_identity ─────────────────────────────────────────────────────────
// b -= 0 leaves b unchanged, borrow=0.
static void test_sub_identity() {
    const uint32_t n = 4u;
    for (uint32_t b_val = 0; b_val < 16u; ++b_val) {
        auto s = make_sub_state(n, 0u, b_val);
        uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
        uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
        uint32_t borrow_out = 8u;

        sturm::v2::AncillaManager mgr(s, 9u);
        sturm::v2::lib_sub(s, mgr, a_idxs, b_idxs, borrow_out, n);

        assert(read_reg(s, n, n) == b_val && "b -= 0 must leave b unchanged");
        assert(read_qubit(s, borrow_out) == 0u && "no borrow for b-0");
    }
    std::puts("  PASS: b -= 0 identity (all 4-bit b)");
}

// ── test_sub_underflow ────────────────────────────────────────────────────────
// 3 - 7 = -4 = 12 (mod 16) with borrow=1.
// Documents underflow behaviour.
static void test_sub_underflow() {
    const uint32_t n = 4u;
    auto s = make_sub_state(n, 7u, 3u);  // b=3, a=7 => 3-7
    uint32_t a_idxs[4] = {0u, 1u, 2u, 3u};
    uint32_t b_idxs[4] = {4u, 5u, 6u, 7u};
    uint32_t borrow_out = 8u;

    sturm::v2::AncillaManager mgr(s, 9u);
    sturm::v2::lib_sub(s, mgr, a_idxs, b_idxs, borrow_out, n);

    assert(read_reg(s, n, n) == 12u && "3-7 mod 16 = 12");
    assert(read_qubit(s, borrow_out) == 1u && "3-7 underflows");
    std::puts("  PASS: 3-7=12 (mod16), borrow=1 (underflow documented)");
}

int main() {
    std::puts("=== test_sub ===");
    test_sub_exhaustive_4bit();
    test_sub_identity();
    test_sub_underflow();
    std::puts("ALL PASS");
    return 0;
}
