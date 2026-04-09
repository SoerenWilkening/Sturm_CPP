// test_mul.cpp — M7 (PRD v2): MUL truth table + move path.
//
// Tests (n=3-bit inputs to fit the 17-qubit cap; n=4 would need ~25+ qubits):
//   1. test_mul_exhaustive  — all 64 products a*b for a,b in 0..7.
//   2. test_mul_by_zero     — a*0 = 0.
//   3. test_mul_by_one      — a*1 = a.
//   4. test_mul_move_path   — a*=b exercises lib_SWAP (move path).
//
// Qubit layout (n=3):
//   q[0..2]  = a  (3 bits)
//   q[3..5]  = b  (3 bits)
//   q[6..11] = product (6 bits, starts |0>)
//   q[12..]  = ancilla pool
//
// Harness: plain assert + printf.

#include "sturm/lib/mul.hpp"
#include "sturm/lib/swap.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;
static constexpr uint32_t N = 3u;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t read_qubit(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(s.amplitude(i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

static uint32_t read_reg(const sturm::v2::SimState& s, uint32_t first, uint32_t n) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i) val |= (read_qubit(s, first + i) << i);
    return val;
}

// Layout: a=[0..N-1], b=[N..2N-1], prod=[2N..4N-1], ancilla=[4N..]
static sturm::v2::SimState make_state(uint32_t a_val, uint32_t b_val) {
    uint32_t total = 4u*N + 5u;  // generous ancilla pool
    if (total > 17u) total = 17u;
    sturm::v2::SimState s;
    s.allocate(total);
    uint64_t idx = 0u;
    for (uint32_t i = 0; i < N; ++i) if ((a_val >> i) & 1u) idx |= uint64_t{1} << i;
    for (uint32_t i = 0; i < N; ++i) if ((b_val >> i) & 1u) idx |= uint64_t{1} << (N+i);
    s.load_basis(idx);
    return s;
}

// ── test_mul_exhaustive ───────────────────────────────────────────────────────

static void test_mul_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t prod_idxs[2*N] = {6u, 7u, 8u, 9u, 10u, 11u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 4u*N);

            sturm::v2::lib_mul(s, mgr, a_idxs, b_idxs, prod_idxs, N);

            uint32_t got = read_reg(s, 2u*N, 2u*N);
            uint32_t exp = a * b;

            if (got != exp) {
                std::printf("FAIL mul: %u*%u expected=%u got=%u\n", a, b, exp, got);
                assert(false);
            }
            if (read_reg(s, 0u, N) != a) {
                std::printf("FAIL mul: a=%u changed\n", a);
                assert(false);
            }
            if (read_reg(s, N,  N) != b) {
                std::printf("FAIL mul: b=%u changed\n", b);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL mul: %u ancilla leaked\n", mgr.num_in_use());
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit mul (%u cases)\n", N, pass);
}

// ── test_mul_by_zero ─────────────────────────────────────────────────────────

static void test_mul_by_zero() {
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t prod_idxs[2*N] = {6u, 7u, 8u, 9u, 10u, 11u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        auto s = make_state(a, 0u);
        sturm::v2::AncillaManager mgr(s, 4u*N);
        sturm::v2::lib_mul(s, mgr, a_idxs, b_idxs, prod_idxs, N);
        assert(read_reg(s, 2u*N, 2u*N) == 0u && "a*0=0");
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: a*0=0");
}

// ── test_mul_by_one ───────────────────────────────────────────────────────────

static void test_mul_by_one() {
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t prod_idxs[2*N] = {6u, 7u, 8u, 9u, 10u, 11u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        auto s = make_state(a, 1u);
        sturm::v2::AncillaManager mgr(s, 4u*N);
        sturm::v2::lib_mul(s, mgr, a_idxs, b_idxs, prod_idxs, N);
        assert(read_reg(s, 2u*N, 2u*N) == a && "a*1=a");
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: a*1=a");
}

// ── test_mul_move_path ────────────────────────────────────────────────────────
// Simulates a *= b: compute out-of-place, then lib_SWAP moves lower N bits
// of product into a's slot (index relabel, zero gates).

static void test_mul_move_path() {
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t prod_idxs[2*N] = {6u, 7u, 8u, 9u, 10u, 11u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 4u*N);

            sturm::v2::lib_mul(s, mgr, a_idxs, b_idxs, prod_idxs, N);

            // Move lower N bits of product into a slot via index relabel.
            uint32_t lower[N] = {prod_idxs[0], prod_idxs[1], prod_idxs[2]};
            uint32_t a_slot[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            sturm::v2::lib_SWAP(s, mgr, a_slot, lower, N);

            // a_slot now contains the product qubits (lower N bits).
            uint32_t got = read_reg(s, a_slot[0], N);
            uint32_t exp = (a * b) & ((1u << N) - 1u);
            if (got != exp) {
                std::printf("FAIL mul_move: %u*%u expected=%u got=%u\n",
                            a, b, exp, got);
                assert(false);
            }
        }
    }
    std::puts("  PASS: a*=b move path");
}

int main() {
    std::puts("=== test_mul ===");
    test_mul_exhaustive();
    test_mul_by_zero();
    test_mul_by_one();
    test_mul_move_path();
    std::puts("ALL PASS");
    return 0;
}
