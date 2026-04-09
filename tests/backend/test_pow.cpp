// test_pow.cpp — M8 (PRD v2): POW truth table via repeated squaring with lib_mul.
//
// Qubit budget analysis (n_base=2, n_exp=2, n_res=4, n_mul=2):
//   Data qubits: base(2) + exp(2) + res(4) = 8.
//   Ancilla pool: 17 - 8 = 9 slots.
//
//   Peak ancilla usage during lib_mul(n_mul=2) call:
//     power_q(2) + prod(4) + carry_anc(1) + c_and_anc1(1) + c_and_anc2(1) = 9.
//   Exactly fits the pool.  (c_and_anc1 and c_and_anc2 arise from the 4-ctrl
//   c_and_impl needed by lift_AND when WhenLift has 1 active control b[j].)
//
//   Test cases are limited to (base, exp) pairs where no intermediate power
//   value overflows 2 bits (i.e., base^(2^k) mod 4 is exact for all k):
//     base=0: power always 0 — all exponents valid.
//     base=1: power always 1 — all exponents valid.
//     base=2: exp=0 (result=1) and exp=1 (result=2) only;
//             squaring 2→4 overflows 2 bits for higher exponents.
//     base=3: exp=0 (result=1) and exp=1 (result=3) only;
//             squaring 3→9≡1 mod 4 produces incorrect intermediate values.
//
// Qubit layout:
//   q[0..1]   = base   (2 bits)
//   q[2..3]   = exp    (2 bits)
//   q[4..7]   = result (4 bits, starts |0>)
//   q[8..16]  = ancilla pool (9 slots)
//
// Harness: plain assert + printf.

#include "sturm/lib/pow.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

static constexpr uint32_t N_BASE = 2u;
static constexpr uint32_t N_EXP  = 2u;
static constexpr uint32_t N_RES  = 4u;
static constexpr uint32_t N_TOTAL = 17u;  // 8 data + 9 ancilla pool

static constexpr uint32_t kBaseStart   = 0u;
static constexpr uint32_t kExpStart    = N_BASE;
static constexpr uint32_t kResultStart = N_BASE + N_EXP;
static constexpr uint32_t kAncStart    = N_BASE + N_EXP + N_RES;  // = 8

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

static sturm::v2::SimState make_pow_state(uint32_t base_val, uint32_t exp_val) {
    sturm::v2::SimState s;
    s.allocate(N_TOTAL);
    uint64_t idx = 0u;
    for (uint32_t i = 0; i < N_BASE; ++i)
        if ((base_val >> i) & 1u) idx |= uint64_t{1} << (kBaseStart + i);
    for (uint32_t i = 0; i < N_EXP; ++i)
        if ((exp_val >> i) & 1u) idx |= uint64_t{1} << (kExpStart + i);
    s.load_basis(idx);
    return s;
}

// ── test_pow_small ────────────────────────────────────────────────────────────
// Tests (base, exp) pairs where all intermediate powers fit in 2 bits.
// See qubit budget comment for rationale on excluded cases.

static void test_pow_small() {
    struct Case { uint32_t base, exp, expected; };
    static const Case cases[] = {
        // base=0: power=0 throughout, any exp is safe.
        {0u, 0u, 1u},
        {0u, 1u, 0u},
        {0u, 2u, 0u},
        {0u, 3u, 0u},
        // base=1: power=1 throughout, any exp is safe.
        {1u, 0u, 1u},
        {1u, 1u, 1u},
        {1u, 2u, 1u},
        {1u, 3u, 1u},
        // base=2: only exp=0,1 — squaring 2→4 overflows 2-bit power register.
        {2u, 0u, 1u},
        {2u, 1u, 2u},
        // base=3: only exp=0,1 — squaring 3→9 mod 4 gives wrong intermediate.
        {3u, 0u, 1u},
        {3u, 1u, 3u},
    };
    static constexpr uint32_t kNumCases = sizeof(cases) / sizeof(cases[0]);

    uint32_t base_idxs[N_BASE] = {0u, 1u};
    uint32_t exp_idxs[N_EXP]  = {2u, 3u};
    uint32_t res_idxs[N_RES]  = {4u, 5u, 6u, 7u};

    uint32_t pass = 0u;
    for (uint32_t c = 0; c < kNumCases; ++c) {
        uint32_t base_val = cases[c].base;
        uint32_t exp_val  = cases[c].exp;
        uint32_t expected = cases[c].expected;

        auto s = make_pow_state(base_val, exp_val);
        sturm::v2::AncillaManager mgr(s, kAncStart);

        sturm::v2::lib_pow(s, mgr, base_idxs, exp_idxs, res_idxs,
                           N_BASE, N_EXP, N_RES);

        uint32_t got = read_reg(s, kResultStart, N_RES);
        if (got != expected) {
            std::printf("FAIL pow: %u^%u expected=%u got=%u\n",
                        base_val, exp_val, expected, got);
            assert(false);
        }

        // Input registers must be unchanged.
        uint32_t got_base = read_reg(s, kBaseStart, N_BASE);
        uint32_t got_exp  = read_reg(s, kExpStart,  N_EXP);
        if (got_base != base_val || got_exp != exp_val) {
            std::printf("FAIL pow: input changed: base %u->%u, exp %u->%u\n",
                        base_val, got_base, exp_val, got_exp);
            assert(false);
        }

        if (mgr.num_in_use() != 0u) {
            std::printf("FAIL pow: %u ancilla leaked (%u^%u)\n",
                        mgr.num_in_use(), base_val, exp_val);
            assert(false);
        }
        ++pass;
    }
    std::printf("  PASS: pow small cases (%u cases, n_base=%u, n_exp=%u, n_res=%u, total=%u qubits)\n",
                pass, N_BASE, N_EXP, N_RES, N_TOTAL);
}

int main() {
    std::puts("=== test_pow ===");
    test_pow_small();
    std::puts("ALL PASS");
    return 0;
}
