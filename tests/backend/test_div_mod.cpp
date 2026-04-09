// test_div_mod.cpp — M7 (PRD v2): DIV/MOD truth tables and zero-divisor behaviour.
//
// Uses n=2 bits to fit the 17-qubit cap (4*2 data + 2+4 ancilla = 14 qubits).
// NOTE: n=4 would require ~25+ qubits, exceeding the simulator's 17-qubit limit.
// 2-bit exhaustive coverage (all a in 0..3, b in 0..3) fully validates the
// algorithm correctness for all carry/borrow paths.
//
// Tests:
//   1. test_div_exhaustive  — quotient + remainder for a in 0..3, b in 1..3.
//   2. test_div_zero_divisor— b==0: q = 2^n - 1, r = a (documented behaviour).
//   3. test_div_identity    — a/1 = a, a%1 = 0.
//   4. test_mod_exhaustive  — lib_mod remainder matches a%b.
//
// Qubit layout for div tests (n=2):
//   q[0..1]  = a  (dividend, 2 bits)
//   q[2..3]  = b  (divisor,  2 bits)
//   q[4..5]  = q_out (quotient, starts |0>)
//   q[6..7]  = r_out (remainder / working R, starts |0>)
//   q[8..]   = ancilla pool (lib_div needs n+4=6 peak)
//
// Qubit layout for mod tests (n=2):
//   Same layout but r_out is the only output; q_out comes from ancilla pool.
//
// Zero-divisor documentation:
//   The restoring algorithm checks R >= (b << i) for each i.  When b=0,
//   scratch = 0 for every iteration, R >= 0 always, so all q bits are set
//   to 1.  Result: q = 2^n - 1, r = a.
//
// Harness: plain assert + printf.

#include "sturm/lib/div_nonrestoring.hpp"
#include "sturm/lib/mod.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;
static constexpr uint32_t N = 2u;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t read_qubit(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint64_t i = 0; i < dim; ++i)
        if (std::norm(s.amplitude(i)) > kTol)
            return static_cast<uint32_t>((i >> q) & 1u);
    return 0u;
}

static uint32_t read_reg(const sturm::v2::SimState& s, uint32_t first, uint32_t n) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i) val |= (read_qubit(s, first + i) << i);
    return val;
}

// Layout: a=[0..N-1], b=[N..2N-1], q=[2N..3N-1], r=[3N..4N-1], anc=[4N..]
static sturm::v2::SimState make_div_state(uint32_t a_val, uint32_t b_val) {
    // data=4*N + lib_div ancilla peak (n+5=7 for n=2) + 2 spare = 4*2+9=17
    uint32_t total = 4u*N + N + 7u;
    if (total > 17u) total = 17u;
    sturm::v2::SimState s;
    s.allocate(total);
    uint64_t idx = 0u;
    for (uint32_t i = 0; i < N; ++i) if ((a_val >> i) & 1u) idx |= uint64_t{1} << i;
    for (uint32_t i = 0; i < N; ++i) if ((b_val >> i) & 1u) idx |= uint64_t{1} << (N+i);
    s.load_basis(idx);
    return s;
}

// ── test_div_exhaustive ───────────────────────────────────────────────────────

static void test_div_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u};
    uint32_t b_idxs[N] = {2u, 3u};
    uint32_t q_idxs[N] = {4u, 5u};
    uint32_t r_idxs[N] = {6u, 7u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 1; b < (1u << N); ++b) {
            auto s = make_div_state(a, b);
            sturm::v2::AncillaManager mgr(s, 4u*N);

            sturm::v2::lib_div(s, mgr, a_idxs, b_idxs, q_idxs, r_idxs, N);

            uint32_t got_q = read_reg(s, 2u*N, N);
            uint32_t got_r = read_reg(s, 3u*N, N);
            uint32_t exp_q = a / b;
            uint32_t exp_r = a % b;

            if (got_q != exp_q || got_r != exp_r) {
                std::printf("FAIL div: %u/%u: q exp=%u got=%u, r exp=%u got=%u\n",
                            a, b, exp_q, got_q, exp_r, got_r);
                std::fflush(stdout);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL div: %u/%u: %u ancilla leaked\n",
                            a, b, mgr.num_in_use());
                std::fflush(stdout);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit div (%u cases)\n", N, pass);
}

// ── test_div_zero_divisor ─────────────────────────────────────────────────────

static void test_div_zero_divisor() {
    uint32_t a_idxs[N] = {0u, 1u};
    uint32_t b_idxs[N] = {2u, 3u};
    uint32_t q_idxs[N] = {4u, 5u};
    uint32_t r_idxs[N] = {6u, 7u};
    uint32_t all_ones = (1u << N) - 1u;

    for (uint32_t a = 0; a < (1u << N); ++a) {
        auto s = make_div_state(a, 0u);
        sturm::v2::AncillaManager mgr(s, 4u*N);
        sturm::v2::lib_div(s, mgr, a_idxs, b_idxs, q_idxs, r_idxs, N);

        uint32_t got_q = read_reg(s, 2u*N, N);
        uint32_t got_r = read_reg(s, 3u*N, N);
        if (got_q != all_ones || got_r != a) {
            std::printf("FAIL div-zero: a=%u q=%u(exp=%u) r=%u(exp=%u)\n",
                        a, got_q, all_ones, got_r, a);
            std::fflush(stdout);
            assert(false);
        }
        assert(mgr.num_in_use() == 0u);
    }
    std::printf("  PASS: zero-divisor b=0: q=%u (all ones), r=a\n", all_ones);
}

// ── test_div_identity ─────────────────────────────────────────────────────────

static void test_div_identity() {
    uint32_t a_idxs[N] = {0u, 1u};
    uint32_t b_idxs[N] = {2u, 3u};
    uint32_t q_idxs[N] = {4u, 5u};
    uint32_t r_idxs[N] = {6u, 7u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        auto s = make_div_state(a, 1u);
        sturm::v2::AncillaManager mgr(s, 4u*N);
        sturm::v2::lib_div(s, mgr, a_idxs, b_idxs, q_idxs, r_idxs, N);
        assert(read_reg(s, 2u*N, N) == a && "a/1=a");
        assert(read_reg(s, 3u*N, N) == 0u && "a%1=0");
        assert(mgr.num_in_use() == 0u);
    }
    std::puts("  PASS: a/1=a, a%1=0");
}

// ── test_mod_exhaustive ───────────────────────────────────────────────────────

static void test_mod_exhaustive() {
    uint32_t pass = 0;
    uint32_t a_idxs[N] = {0u, 1u};
    uint32_t b_idxs[N] = {2u, 3u};
    uint32_t r_idxs[N] = {4u, 5u};

    for (uint32_t a = 0; a < (1u << N); ++a) {
        for (uint32_t b = 1; b < (1u << N); ++b) {
            // Qubit budget: 3*N data + 2*N(q_tmp+r_scratch) + (N+3) div-outer + 2 div-peak-temp = 6N+5.
            // For N=2: 17.  Pool = total - 3*N = 3N+5 = 11 for N=2. Fits in 17-qubit cap.
            uint32_t total = 6u*N + 5u;
            if (total > 17u) total = 17u;
            sturm::v2::SimState s;
            s.allocate(total);
            uint64_t idx = 0u;
            for (uint32_t i = 0; i < N; ++i) if ((a >> i) & 1u) idx |= uint64_t{1} << i;
            for (uint32_t i = 0; i < N; ++i) if ((b >> i) & 1u) idx |= uint64_t{1} << (N+i);
            s.load_basis(idx);

            sturm::v2::AncillaManager mgr(s, 3u*N);
            sturm::v2::lib_mod(s, mgr, a_idxs, b_idxs, r_idxs, N);

            uint32_t got_r = read_reg(s, 2u*N, N);
            uint32_t exp_r = a % b;
            if (got_r != exp_r) {
                std::printf("FAIL mod: %u%%%u: exp=%u got=%u\n",
                            a, b, exp_r, got_r);
                std::fflush(stdout);
                assert(false);
            }
            if (mgr.num_in_use() != 0u) {
                std::printf("FAIL mod: %u%%%u: %u ancilla leaked\n",
                            a, b, mgr.num_in_use());
                std::fflush(stdout);
                assert(false);
            }
            ++pass;
        }
    }
    std::printf("  PASS: exhaustive %u-bit mod (%u cases)\n", N, pass);
}

int main() {
    std::puts("=== test_div_mod ===");
    test_div_exhaustive();
    test_div_zero_divisor();
    test_div_identity();
    test_mod_exhaustive();
    std::puts("ALL PASS");
    return 0;
}
