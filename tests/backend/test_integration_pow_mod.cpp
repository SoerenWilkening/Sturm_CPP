// test_integration_pow_mod.cpp — M10 (PRD v2): Integration test: pow(a,e) mod m
//
// Exercises the POW + MOD pipeline.
//
// Because lib_pow peaks at 9 ancilla slots (for n_mul=2) and lib_mod peaks at
// 10 ancilla slots (for n=2), the two operations cannot run simultaneously in
// a single 17-qubit statevector.  The integration strategy is therefore:
//
//   Phase 1 — pow(a, e) → pow_res in a 17-qubit statevector.
//     Layout: base(2) + exp(2) + pow_res(4) + pool(9) = 17 qubits.
//     lib_pow produces pow_res = a^e.  Pool clean after call.
//
//   Phase 2 — mod(pow_res_val, m) → r in a fresh 17-qubit statevector.
//     Layout: a(2) + b(2) + r_out(2) + pool(11) = 17 qubits.
//     lib_mod produces r = pow_res_val % m.  Pool clean after call.
//
//   Verification: r == (a^e) % m for all selected test cases.
//
// This two-phase approach faithfully mirrors a real program where the
// pow result (a classical bitstring on the output of phase 1) is handed
// to the mod subroutine as its dividend input.
//
// Qubit budget (per phase):
//   Phase 1: base(2)+exp(2)+pow_res(4)+pool(9) = 17 (matches test_pow.cpp)
//   Phase 2: a(2)+m(2)+r(2)+pool(11) = 17 (matches test_div_mod.cpp)
//
// Test cases are restricted to (base, exp) pairs where no intermediate
// power value overflows 2 bits (same constraint as test_pow.cpp):
//   base=0: all exponents.
//   base=1: all exponents.
//   base=2: exp=0 (→1), exp=1 (→2) only.
//   base=3: exp=0 (→1), exp=1 (→3) only.
//
// Harness: plain assert + printf (project convention).

#include "sturm/lib/pow.hpp"
#include "sturm/lib/mod.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double   kTol    = 1e-9;

// ── Phase 1 layout constants (matches test_pow.cpp) ──────────────────────────
static constexpr uint32_t kPowNBase   = 2u;
static constexpr uint32_t kPowNExp    = 2u;
static constexpr uint32_t kPowNRes    = 4u;
static constexpr uint32_t kPowTotal   = 17u;
static constexpr uint32_t kPowAncStart = kPowNBase + kPowNExp + kPowNRes; // 8

// ── Phase 2 layout constants (matches test_div_mod.cpp for lib_mod) ──────────
static constexpr uint32_t kModN       = 2u;
static constexpr uint32_t kModTotal   = 17u;
// a=[0..1], m=[2..3], r=[4..5], anc=[6..16] = 11 slots
static constexpr uint32_t kModAncStart = 3u * kModN; // 6

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t read_qubit(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint64_t i = 0u; i < dim; ++i)
        if (std::norm(s.amplitude(i)) > kTol)
            return static_cast<uint32_t>((i >> q) & 1u);
    return 0u;
}

static uint32_t read_reg(const sturm::v2::SimState& s, uint32_t first, uint32_t n) {
    uint32_t val = 0u;
    for (uint32_t i = 0u; i < n; ++i)
        val |= (read_qubit(s, first + i) << i);
    return val;
}

// ── run_pow ───────────────────────────────────────────────────────────────────
//
// Compute base^exp using lib_pow in a fresh 17-qubit statevector.
// Returns the n_res-bit result and verifies ancilla cleanliness.
static uint32_t run_pow(uint32_t base_val, uint32_t exp_val) {
    sturm::v2::SimState s;
    s.allocate(kPowTotal);

    // Encode base and exp.
    uint64_t idx = 0u;
    for (uint32_t i = 0u; i < kPowNBase; ++i)
        if ((base_val >> i) & 1u) idx |= uint64_t{1} << i;
    for (uint32_t i = 0u; i < kPowNExp; ++i)
        if ((exp_val >> i) & 1u) idx |= uint64_t{1} << (kPowNBase + i);
    s.load_basis(idx);

    sturm::v2::AncillaManager mgr(s, kPowAncStart);

    uint32_t base_idxs[kPowNBase] = {0u, 1u};
    uint32_t exp_idxs[kPowNExp]   = {2u, 3u};
    uint32_t res_idxs[kPowNRes]   = {4u, 5u, 6u, 7u};

    sturm::v2::lib_pow(s, mgr, base_idxs, exp_idxs, res_idxs,
                       kPowNBase, kPowNExp, kPowNRes);

    assert(mgr.num_in_use() == 0u && "run_pow: ancilla leaked");

    // Verify inputs unchanged.
    uint32_t got_base = read_reg(s, 0u, kPowNBase);
    uint32_t got_exp  = read_reg(s, kPowNBase, kPowNExp);
    assert(got_base == base_val && "run_pow: base changed");
    assert(got_exp  == exp_val  && "run_pow: exp changed");

    return read_reg(s, kPowNBase + kPowNExp, kPowNRes);
}

// ── run_mod ───────────────────────────────────────────────────────────────────
//
// Compute a_val mod m_val using lib_mod in a fresh 17-qubit statevector.
// Returns the n-bit remainder and verifies ancilla cleanliness.
static uint32_t run_mod(uint32_t a_val, uint32_t m_val) {
    sturm::v2::SimState s;
    s.allocate(kModTotal);

    // Encode a and m.
    uint64_t idx = 0u;
    for (uint32_t i = 0u; i < kModN; ++i)
        if ((a_val >> i) & 1u) idx |= uint64_t{1} << i;
    for (uint32_t i = 0u; i < kModN; ++i)
        if ((m_val >> i) & 1u) idx |= uint64_t{1} << (kModN + i);
    s.load_basis(idx);

    sturm::v2::AncillaManager mgr(s, kModAncStart);

    uint32_t a_idxs[kModN]   = {0u, 1u};
    uint32_t m_idxs[kModN]   = {2u, 3u};
    uint32_t r_idxs[kModN]   = {4u, 5u};

    sturm::v2::lib_mod(s, mgr, a_idxs, m_idxs, r_idxs, kModN);

    assert(mgr.num_in_use() == 0u && "run_mod: ancilla leaked");

    // Verify inputs unchanged.
    uint32_t got_a = read_reg(s, 0u, kModN);
    uint32_t got_m = read_reg(s, kModN, kModN);
    assert(got_a == (a_val & ((1u << kModN) - 1u)) && "run_mod: a changed");
    assert(got_m == m_val && "run_mod: m changed");

    return read_reg(s, 2u * kModN, kModN);
}

// ── test_pow_mod_chain ────────────────────────────────────────────────────────
//
// For each (base, exp, m) triple, verify that:
//   run_pow(base, exp) == base^exp
//   run_mod(base^exp, m) == (base^exp) % m
//
// Test cases chosen so intermediate powers fit in 2 bits (see header).
// m must be non-zero and ≤ 3 (2-bit divisor).

static void test_pow_mod_chain() {
    struct Case {
        uint32_t base, exp, m;
        uint32_t expected_pow;   // base^exp (full value before mod)
        uint32_t expected_mod;   // base^exp mod m
        const char* label;
    };

    static const Case cases[] = {
        // base=0
        {0u, 0u, 1u, 1u, 0u, "0^0 mod 1 = 0"},
        {0u, 1u, 3u, 0u, 0u, "0^1 mod 3 = 0"},
        {0u, 3u, 2u, 0u, 0u, "0^3 mod 2 = 0"},
        // base=1
        {1u, 0u, 3u, 1u, 1u, "1^0 mod 3 = 1"},
        {1u, 1u, 3u, 1u, 1u, "1^1 mod 3 = 1"},
        {1u, 3u, 3u, 1u, 1u, "1^3 mod 3 = 1"},
        // base=2 (only exp=0,1 safe; 2^2=4 overflows 2-bit result but fits 4-bit)
        {2u, 0u, 3u, 1u, 1u, "2^0 mod 3 = 1"},
        {2u, 1u, 3u, 2u, 2u, "2^1 mod 3 = 2"},
        {2u, 0u, 1u, 1u, 0u, "2^0 mod 1 = 0"},
        // base=3 (only exp=0,1 safe)
        {3u, 0u, 2u, 1u, 1u, "3^0 mod 2 = 1"},
        {3u, 1u, 2u, 3u, 1u, "3^1 mod 2 = 1"},
        {3u, 0u, 3u, 1u, 1u, "3^0 mod 3 = 1"},
        {3u, 1u, 3u, 3u, 0u, "3^1 mod 3 = 0"},
    };
    static constexpr uint32_t kNCases = sizeof(cases) / sizeof(cases[0]);

    const uint32_t mask2 = (1u << kModN) - 1u;

    uint32_t pass = 0u;
    for (uint32_t ci = 0u; ci < kNCases; ++ci) {
        const auto& tc = cases[ci];

        // Phase 1: compute pow.
        uint32_t pow_result = run_pow(tc.base, tc.exp);
        // The result register is 4 bits wide; compare against expected_pow.
        if (pow_result != tc.expected_pow) {
            std::printf("FAIL pow_mod [%s]: pow(%u,%u)=%u expected=%u\n",
                        tc.label, tc.base, tc.exp, pow_result, tc.expected_pow);
            assert(false);
        }

        // Phase 2: compute mod using lower kModN bits of pow result.
        // (Since expected_mod is stated for the full value, we check that the
        //  2-bit truncation of pow_result mod m equals expected_mod.)
        uint32_t a_mod_input = pow_result & mask2;
        uint32_t got_mod = run_mod(a_mod_input, tc.m);
        uint32_t exp_mod  = tc.expected_mod;

        if (got_mod != exp_mod) {
            std::printf("FAIL pow_mod [%s]: mod(%u,%u)=%u expected=%u\n",
                        tc.label, a_mod_input, tc.m, got_mod, exp_mod);
            assert(false);
        }

        ++pass;
        std::printf("  [%s] pow(%u,%u)=%u, mod(%u,%u)=%u ✓\n",
                    tc.label, tc.base, tc.exp, pow_result, a_mod_input, tc.m, got_mod);
    }
    std::printf("  PASS: test_pow_mod_chain — %u cases\n", pass);
}

// ── test_pow_ancilla_clean ────────────────────────────────────────────────────
//
// Verify lib_pow leaves the ancilla pool completely clean for multiple inputs.
// Checks mgr.num_in_use() == 0 and that all ancilla qubits are |0⟩ in the
// statevector.

static void test_pow_ancilla_clean() {
    struct Case { uint32_t base, exp; };
    static const Case cases[] = {
        {0u, 2u}, {1u, 3u}, {2u, 1u}, {3u, 0u}
    };
    static constexpr uint32_t kNCases = sizeof(cases) / sizeof(cases[0]);

    for (uint32_t ci = 0u; ci < kNCases; ++ci) {
        const auto& tc = cases[ci];

        sturm::v2::SimState s;
        s.allocate(kPowTotal);

        uint64_t idx = 0u;
        for (uint32_t i = 0u; i < kPowNBase; ++i)
            if ((tc.base >> i) & 1u) idx |= uint64_t{1} << i;
        for (uint32_t i = 0u; i < kPowNExp; ++i)
            if ((tc.exp >> i) & 1u) idx |= uint64_t{1} << (kPowNBase + i);
        s.load_basis(idx);

        sturm::v2::AncillaManager mgr(s, kPowAncStart);

        uint32_t base_idxs[kPowNBase] = {0u, 1u};
        uint32_t exp_idxs[kPowNExp]   = {2u, 3u};
        uint32_t res_idxs[kPowNRes]   = {4u, 5u, 6u, 7u};

        sturm::v2::lib_pow(s, mgr, base_idxs, exp_idxs, res_idxs,
                           kPowNBase, kPowNExp, kPowNRes);

        assert(mgr.num_in_use() == 0u && "pow_ancilla_clean: ancilla leaked");

        // Check all ancilla qubits are |0⟩.
        const uint64_t dim = uint64_t{1} << s.num_qubits();
        for (uint32_t q = kPowAncStart; q < kPowTotal; ++q) {
            uint64_t mask = uint64_t{1} << q;
            for (uint64_t sidx = 0u; sidx < dim; ++sidx) {
                if ((sidx & mask) != 0u) {
                    assert(std::norm(s.amplitude(sidx)) < kTol
                           && "pow_ancilla_clean: ancilla qubit not |0⟩");
                }
            }
        }
    }
    std::printf("  PASS: test_pow_ancilla_clean — %u cases, pool |0⟩ verified\n",
                kNCases);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::puts("=== M10 test_integration_pow_mod: pow(a,e) mod m ===");
    std::printf("  Phase 1 budget: base(%u)+exp(%u)+res(%u)+pool(%u)=%u qubits\n",
                kPowNBase, kPowNExp, kPowNRes,
                kPowTotal - kPowAncStart, kPowTotal);
    std::printf("  Phase 2 budget: a(%u)+m(%u)+r(%u)+pool(%u)=%u qubits\n",
                kModN, kModN, kModN,
                kModTotal - kModAncStart, kModTotal);
    test_pow_mod_chain();
    test_pow_ancilla_clean();
    std::puts("ALL PASS");
    return 0;
}
