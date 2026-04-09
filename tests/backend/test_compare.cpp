// test_compare.cpp — M8 (PRD v2): exhaustive comparison truth tables.
//
// Uses n=3-bit registers to stay within the 17-qubit cap.
// Qubit layout:
//   q[0..2]  = a  (3 bits)
//   q[3..5]  = b  (3 bits)
//   q[6]     = out (1 bit, starts |0>)
//   q[7..]   = ancilla pool (tree-of-ANDs needs up to ~4 ancilla)
//
// Total budget: 3 + 3 + 1 + 4 spare = 11 <= 17. ✓
//
// Tests:
//   1. test_eq_exhaustive  — EQ: out = (a == b).
//   2. test_lt_exhaustive  — LT: out = (a < b).
//   3. test_le_exhaustive  — LE: out = (a <= b).
//   4. test_gt_exhaustive  — GT: out = (a > b).
//   5. test_ge_exhaustive  — GE: out = (a >= b).
//   6. test_compare_ancilla_clean — no ancilla leaked after each call.
//
// Harness: plain assert + printf.

#include "sturm/lib/compare.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;
// 3-bit registers: exhaustive coverage validates all carry/borrow paths.
// n=3: 8 values × 8 values = 64 cases per op. Total qubits needed: 11.
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

// Layout: a=[0..N-1], b=[N..2N-1], out=[2N], anc=[2N+1..]
static sturm::v2::SimState make_state(uint32_t a_val, uint32_t b_val) {
    // 3+3+1 data + 8 ancilla = 15 <= 17
    uint32_t total = 2u * N + 1u + 8u;
    sturm::v2::SimState s;
    s.allocate(total);
    uint64_t idx = 0u;
    for (uint32_t i = 0; i < N; ++i) if ((a_val >> i) & 1u) idx |= uint64_t{1} << i;
    for (uint32_t i = 0; i < N; ++i) if ((b_val >> i) & 1u) idx |= uint64_t{1} << (N + i);
    s.load_basis(idx);
    return s;
}

// ── test_eq_exhaustive ────────────────────────────────────────────────────────

static void test_eq_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t out_idx   = 2u * N;  // q[6]

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 2u * N + 1u);

            sturm::v2::lib_EQ(s, mgr, a_idxs, b_idxs, out_idx, N);

            uint32_t got = read_qubit(s, out_idx);
            uint32_t exp = (a == b) ? 1u : 0u;

            if (got != exp) {
                std::printf("FAIL EQ: %u==%u expected=%u got=%u\n", a, b, exp, got);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL EQ: %u ancilla leaked (a=%u, b=%u)\n",
                            mgr.num_in_use(), a, b);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit EQ (%u cases)\n", N, pass);
}

// ── test_lt_exhaustive ────────────────────────────────────────────────────────

static void test_lt_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t out_idx   = 2u * N;

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 2u * N + 1u);

            sturm::v2::lib_LT(s, mgr, a_idxs, b_idxs, out_idx, N);

            uint32_t got = read_qubit(s, out_idx);
            uint32_t exp = (a < b) ? 1u : 0u;

            if (got != exp) {
                std::printf("FAIL LT: %u<%u expected=%u got=%u\n", a, b, exp, got);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL LT: %u ancilla leaked (a=%u, b=%u)\n",
                            mgr.num_in_use(), a, b);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit LT (%u cases)\n", N, pass);
}

// ── test_le_exhaustive ────────────────────────────────────────────────────────

static void test_le_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t out_idx   = 2u * N;

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 2u * N + 1u);

            sturm::v2::lib_LE(s, mgr, a_idxs, b_idxs, out_idx, N);

            uint32_t got = read_qubit(s, out_idx);
            uint32_t exp = (a <= b) ? 1u : 0u;

            if (got != exp) {
                std::printf("FAIL LE: %u<=%u expected=%u got=%u\n", a, b, exp, got);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL LE: %u ancilla leaked (a=%u, b=%u)\n",
                            mgr.num_in_use(), a, b);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit LE (%u cases)\n", N, pass);
}

// ── test_gt_exhaustive ────────────────────────────────────────────────────────

static void test_gt_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t out_idx   = 2u * N;

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 2u * N + 1u);

            sturm::v2::lib_GT(s, mgr, a_idxs, b_idxs, out_idx, N);

            uint32_t got = read_qubit(s, out_idx);
            uint32_t exp = (a > b) ? 1u : 0u;

            if (got != exp) {
                std::printf("FAIL GT: %u>%u expected=%u got=%u\n", a, b, exp, got);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL GT: %u ancilla leaked (a=%u, b=%u)\n",
                            mgr.num_in_use(), a, b);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit GT (%u cases)\n", N, pass);
}

// ── test_ge_exhaustive ────────────────────────────────────────────────────────

static void test_ge_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u, 2u};
    uint32_t b_idxs[N] = {3u, 4u, 5u};
    uint32_t out_idx   = 2u * N;

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 0; b < (1u << N); ++b) {
            auto s = make_state(a, b);
            sturm::v2::AncillaManager mgr(s, 2u * N + 1u);

            sturm::v2::lib_GE(s, mgr, a_idxs, b_idxs, out_idx, N);

            uint32_t got = read_qubit(s, out_idx);
            uint32_t exp = (a >= b) ? 1u : 0u;

            if (got != exp) {
                std::printf("FAIL GE: %u>=%u expected=%u got=%u\n", a, b, exp, got);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL GE: %u ancilla leaked (a=%u, b=%u)\n",
                            mgr.num_in_use(), a, b);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit GE (%u cases)\n", N, pass);
}

int main() {
    std::puts("=== test_compare ===");
    test_eq_exhaustive();
    test_lt_exhaustive();
    test_le_exhaustive();
    test_gt_exhaustive();
    test_ge_exhaustive();
    std::puts("ALL PASS");
    return 0;
}
