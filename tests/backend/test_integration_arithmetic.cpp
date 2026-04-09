// test_integration_arithmetic.cpp — M10 (PRD v2): Integration test: c = (a+5)*b
//
// Exercises ADD + MUL + move in one composite expression.
//
// Algorithm:
//   1. Load `a` and constant 1 (= 5 mod 4, since registers are 2 bits)
//      into separate 2-bit registers.
//   2. Compute sum = a + const5 in-place (Cuccaro adder), carry discarded.
//   3. Compute prod = sum * b out-of-place (lib_mul), result is 4 bits.
//   4. Verify prod == (a + const_val) * b (all arithmetic mod 2^n).
//   5. Assert ancilla pool is clean after each call.
//
// Qubit budget (n=2 bits):
//   a         q[0..1]   2 bits
//   const5    q[2..3]   2 bits  (encodes 5 mod 4 = 1 for 2-bit registers)
//   carry     q[4]      1 bit   (Cuccaro carry-out)
//   b         q[5..6]   2 bits
//   prod      q[7..10]  4 bits  (2n product)
//   ancilla pool q[11..16]  6 slots
//   Total: 17 qubits (within SimState::kMaxQubits cap)
//
// Note: Because a and const5 are 2-bit, the addition is mod 4.
//       5 mod 4 = 1, so the constant loaded is 1.  The test
//       verifies: prod == (a_val + 1) mod 4  *  b_val  (mod 16).
//
// For "c = (a+5)*b" semantics over wider registers, this 2-bit version
// is the minimum-width smoke test; wider versions would consume more qubits
// than the 17-qubit cap allows.
//
// Multiple input pairs are tested to demonstrate correctness across inputs.
// Harness: plain assert + printf (project convention).

#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/lib/mul.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double   kTol     = 1e-9;
static constexpr uint32_t N        = 2u;   // bits per narrow register
static constexpr uint32_t CONST_N  = 1u;   // 5 mod 2^N = 5 mod 4 = 1

// Qubit index layout (matching the budget above).
static constexpr uint32_t kAIdx[N]  = {0u, 1u};       // a
static constexpr uint32_t kC5Idx[N] = {2u, 3u};       // const 5 mod 4 = 1
static constexpr uint32_t kCarry    = 4u;              // carry-out of ADD
static constexpr uint32_t kBIdx[N]  = {5u, 6u};       // b
static constexpr uint32_t kPIdx[2*N]= {7u,8u,9u,10u}; // product (4 bits)
static constexpr uint32_t kAncStart = 11u;
static constexpr uint32_t kTotal    = 17u;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t read_reg(const sturm::v2::SimState& s,
                         const uint32_t* idxs, uint32_t n) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint64_t i = 0u; i < dim; ++i) {
        if (std::norm(s.amplitude(i)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t b = 0u; b < n; ++b)
                val |= static_cast<uint32_t>((i >> idxs[b]) & 1u) << b;
            return val;
        }
    }
    return 0u;
}

// Load a_val into q[0..N-1] and b_val into q[5..6].
// const 5 mod 4 = 1 is loaded into q[2..3] (bit 0 = 1, bit 1 = 0).
static sturm::v2::SimState make_state(uint32_t a_val, uint32_t b_val) {
    sturm::v2::SimState s;
    s.allocate(kTotal);

    uint64_t idx = 0u;
    // Encode a
    for (uint32_t i = 0u; i < N; ++i)
        if ((a_val >> i) & 1u) idx |= uint64_t{1} << kAIdx[i];
    // Encode const 5 mod 4 = 1 → bit 0 set in kC5Idx
    idx |= uint64_t{1} << kC5Idx[0];  // 1 = 0b01
    // Encode b
    for (uint32_t i = 0u; i < N; ++i)
        if ((b_val >> i) & 1u) idx |= uint64_t{1} << kBIdx[i];

    s.load_basis(idx);
    return s;
}

// ── test_add_mul_composite ────────────────────────────────────────────────────
//
// For each (a, b) pair in 0..3 × 0..3:
//   1. Add const5 (=1) into a-register in-place.  b unchanged.
//   2. Multiply sum × b → product register.
//   3. Verify product == (a + 1) mod 4  ×  b  (all mod arithmetic).
//   4. Verify a-register holds (a + 1) mod 4 after the add.
//   5. Verify ancilla pool is clean.
//
// Qubit budget note (from header):
//   Data:  a(2) + const(2) + carry(1) + b(2) + prod(4) = 11 qubits
//   Pool:  17 − 11 = 6 ancilla slots
//   Peak usage during lib_mul(n=2) with 1 WHEN control:
//     Cuccaro carry_anc(1) + WhenLift fold_anc (0, since 1 ctrl needs no fold) = 1
//   6 pool slots >> 1 peak ✓

static void test_add_mul_composite() {
    uint32_t pass = 0u;
    const uint32_t mask4  = (1u << N) - 1u;  // mod 4
    const uint32_t mask16 = (1u << (2u*N)) - 1u;  // mod 16

    for (uint32_t av = 0u; av < (1u << N); ++av) {
        for (uint32_t bv = 0u; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, kAncStart);

            // ── Step 1: sum = a + const5 (in-place, Cuccaro) ─────────────────
            // a_idxs = kAIdx (the a register, which gets overwritten with sum)
            // b_idxs = kC5Idx (the const5 register, left unchanged)
            // carry_out = kCarry
            uint32_t a_idxs_mut[N] = {kAIdx[0], kAIdx[1]};
            uint32_t c5_idxs[N]    = {kC5Idx[0], kC5Idx[1]};
            sturm::v2::lib_add_cuccaro(s, mgr, c5_idxs, a_idxs_mut, kCarry, N);

            uint32_t sum_got  = read_reg(s, a_idxs_mut, N);
            uint32_t sum_exp  = (av + CONST_N) & mask4;
            assert(sum_got == sum_exp && "ADD step: sum != a + 1");
            assert(mgr.num_in_use() == 0u && "ADD step: ancilla leaked");

            // ── Step 2: prod = sum × b (out-of-place, lib_mul) ───────────────
            uint32_t prod_idxs[2*N] = {kPIdx[0], kPIdx[1], kPIdx[2], kPIdx[3]};
            uint32_t b_idxs_mut[N]  = {kBIdx[0], kBIdx[1]};
            sturm::v2::lib_mul(s, mgr, a_idxs_mut, b_idxs_mut, prod_idxs, N);

            uint32_t prod_got = read_reg(s, prod_idxs, 2u*N);
            uint32_t prod_exp = (sum_exp * bv) & mask16;
            if (prod_got != prod_exp) {
                std::printf("FAIL arithmetic: a=%u b=%u sum=%u*b=%u expected=%u got=%u\n",
                            av, bv, sum_exp, bv, prod_exp, prod_got);
                assert(false);
            }

            // ── Step 3: verify inputs unchanged ──────────────────────────────
            uint32_t b_check = read_reg(s, b_idxs_mut, N);
            assert(b_check == bv && "b was modified during mul");

            // ── Step 4: ancilla pool is clean ─────────────────────────────────
            assert(mgr.num_in_use() == 0u && "MUL step: ancilla leaked");

            ++pass;
        }
    }
    std::printf("  PASS: test_add_mul_composite — c=(a+5)*b, %u cases\n"
                "        Qubit budget: a(2)+const(2)+carry(1)+b(2)+prod(4)"
                "+pool(6) = %u total\n",
                pass, kTotal);
}

// ── test_add_mul_multiple_inputs ──────────────────────────────────────────────
//
// Spot-check a selection of (a, b) pairs with the larger constant encoded
// as a 2-bit value to further exercise the ADD → MUL pipeline.
// Includes cases where sum overflows (carry-out set) to verify carry
// does not corrupt b or the product.

static void test_add_mul_carry_behavior() {
    // Pairs where carry-out is set: a=3, const=1 → sum=4 ≡ 0 (mod 4), carry=1.
    // prod = 0 * b = 0 for any b.
    const uint32_t av = 3u, bv = 3u;
    auto s = make_state(av, bv);
    sturm::v2::AncillaManager mgr(s, kAncStart);

    uint32_t a_idxs_mut[N] = {kAIdx[0], kAIdx[1]};
    uint32_t c5_idxs[N]    = {kC5Idx[0], kC5Idx[1]};
    sturm::v2::lib_add_cuccaro(s, mgr, c5_idxs, a_idxs_mut, kCarry, N);
    assert(mgr.num_in_use() == 0u);

    uint32_t sum_got = read_reg(s, a_idxs_mut, N);
    assert(sum_got == 0u && "carry case: 3+1 mod 4 should be 0");

    // carry qubit should be 1 (overflow occurred)
    const uint32_t carry_q[1] = {kCarry};
    uint32_t carry_val = read_reg(s, carry_q, 1u);
    assert(carry_val == 1u && "carry case: carry-out should be 1");

    uint32_t prod_idxs[2*N] = {kPIdx[0], kPIdx[1], kPIdx[2], kPIdx[3]};
    uint32_t b_idxs_mut[N]  = {kBIdx[0], kBIdx[1]};
    sturm::v2::lib_mul(s, mgr, a_idxs_mut, b_idxs_mut, prod_idxs, N);

    uint32_t prod_got = read_reg(s, prod_idxs, 2u*N);
    assert(prod_got == 0u && "carry case: 0 * 3 = 0");
    assert(mgr.num_in_use() == 0u && "carry case: ancilla leaked after mul");

    std::printf("  PASS: test_add_mul_carry_behavior — overflow + ancilla clean\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::puts("=== M10 test_integration_arithmetic: c = (a+5)*b ===");
    std::printf("  Qubit budget: N=%u bits, total=%u qubits (cap=17)\n", N, kTotal);
    test_add_mul_composite();
    test_add_mul_carry_behavior();
    std::puts("ALL PASS");
    return 0;
}
