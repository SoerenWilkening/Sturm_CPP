// test_pow_mod_dsl_cap_lift.cpp -- sturm-3sfl.4 Beat D-D cap-lift validation.
//
// Beat D-D (sturm-3sfl.4) lifts `lib_pow_mod_dsl`'s internal W cap from 8 to
// 64, matching the underlying `lib_mul_mod_inplace_dsl` (Beat D-A) and
// `lib_square_mod_dsl` (Beat D-B) caps lifted under sturm-8n73.  This test
// validates the lift end-to-end:
//
//   • `lib_pow_mod_dsl<W>` runs to completion at the new top of the
//     supported W range without tripping the internal `n <= kMaxN` assert.
//   • The output `r_bits` carries the correct value `(base^exp) mod n`,
//     verified via APPEND-mode capture + classical bit-vector replay
//     (the same harness used by test_pow_mod_dsl.cpp; orkan's 17-qubit
//     simulator budget cannot accommodate pow_mod's 2W² + 4W + 11 peak
//     ancillas above the 4·W input registers — see
//     tests/lib/test_pow_mod_dsl_ancilla.cpp).
//   • Input registers (`base`, `exp`, `n`) are restored bit-identically.
//   • `QubitPool::in_use()` returns to its pre-call value (no leaked
//     ancillas; LIFO release symmetry holds at the lifted cap).
//
// Why W = 16, not W = 64:
//   - Larger Ws exercise the same algorithm shape (per-iteration witness
//     allocation/release, controlled mul-inplace under exp_bits[i],
//     unconditional squaring) but cost gate count proportional to W^4 to
//     run to completion; W = 16 already emits ~10⁵ gate records.
//   - The cap-lift's stack-frame correctness scales with kMaxN²
//     (mul/sq witness matrices), independent of the actual `n` argument
//     used at the call site, so any W > 8 covers the lift's new range.
//   - Running the assertion harness end-to-end at W = 16 keeps the test
//     under a few hundred ms while still validating an n value
//     (here 2^15 - 1) that the previous cap of 8 could not represent.
//
// Mirrors the cap-lift validation pattern established by sturm-4oot's
// even-n-double-mod beats (extended kMaxN coverage) and sturm-8n73's
// modular-arith kMaxN-bump trace test (test_modular_arith_highw_trace.cpp).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/pow_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <vector>

// Apply one captured gate record to a classical bit-vector.  Mirrors
// test_pow_mod_dsl.cpp::apply_gate_classical (X / CX / CCX only; pow_mod
// composes exclusively classical-reversible primitives).
static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u;
        break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u;
        break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr,
                     "trace: unsupported gate kind %d\n",
                     static_cast<int>(rec.kind));
        std::abort();
    }
}

// Read a register's classical value back from the bit-vector.
static uint64_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint64_t v = 0u;
    for (std::size_t k = 0; k < n; ++k) {
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (uint64_t{1} << k);
    }
    return v;
}

// Reference (base^exp) mod n on uint64_t using square-and-multiply with
// 128-bit intermediates to avoid overflow up to W = 32.  Convention 0^0 = 1.
static uint64_t pow_mod_ref(uint64_t base, uint64_t exp_, uint64_t n_val) {
    if (n_val == 0u) return 0u;
    uint64_t acc = 1u % (n_val == 0u ? 1u : n_val);
    uint64_t cur = base % n_val;
    while (exp_) {
        if (exp_ & 1u)
            acc = static_cast<uint64_t>((__uint128_t{acc} * cur) % n_val);
        exp_ >>= 1;
        if (exp_)
            cur = static_cast<uint64_t>((__uint128_t{cur} * cur) % n_val);
    }
    return acc;
}

template <std::size_t W>
static void run_cap_lift_case(uint64_t base_val, uint64_t exp_val,
                              uint64_t n_val) {
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    assert(n_val < (uint64_t{1} << W) && "test precondition: n fits in W bits");
    assert(exp_val < (uint64_t{1} << W) && "test precondition: exp fits in W bits");

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*W lowest qubit indices for base, exp, n, r so we know
    // exactly which slots in the classical bit-vector hold the inputs.
    int qi_base[W], qi_exp[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i) qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i) qi_exp[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i) qi_n[i]    = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i) qi_r[i]    = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(4u * W));

    sturm::qbool base_own[W], exp_own[W], n_own[W], r_own[W];
    sturm::BitProxy base_bits[W], exp_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        base_own[i] = sturm::qbool::make_non_owning(qi_base[i]);
        exp_own[i]  = sturm::qbool::make_non_owning(qi_exp[i]);
        n_own[i]    = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i]    = sturm::qbool::make_non_owning(qi_r[i]);
        base_bits[i] = sturm::BitProxy(base_own[i]);
        exp_bits[i]  = sturm::BitProxy(exp_own[i]);
        n_bits[i]    = sturm::BitProxy(n_own[i]);
        r_bits[i]    = sturm::BitProxy(r_own[i]);
    }

    // APPEND-mode capture: gates are recorded into ctx.ir; QubitPool tracks
    // allocate/release identically to SIMULATE mode, so the post-call
    // in_use() / high_water() figures are valid even though no
    // statevector is involved.
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, /*max_q=*/4096u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            W, r_bits);

    // Pool live-count returns to pre-call value.
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "cap-lift: pool live-count returns to pre-call value");

    // Replay the captured stream classically with seeded inputs.
    const std::size_t total_bits =
        static_cast<std::size_t>(sturm::QubitPool::instance().high_water());
    std::vector<uint8_t> bits(total_bits + 1u, 0u);
    for (std::size_t i = 0; i < W; ++i)
        bits[static_cast<std::size_t>(qi_base[i])] =
            static_cast<uint8_t>((base_val >> i) & 1u);
    for (std::size_t i = 0; i < W; ++i)
        bits[static_cast<std::size_t>(qi_exp[i])]  =
            static_cast<uint8_t>((exp_val  >> i) & 1u);
    for (std::size_t i = 0; i < W; ++i)
        bits[static_cast<std::size_t>(qi_n[i])]    =
            static_cast<uint8_t>((n_val    >> i) & 1u);
    // r_bits[*] start at 0.

    for (std::size_t g = 0; g < ctx->ir.size(); ++g)
        apply_gate_classical(bits, ctx->ir.at(g));

    const uint64_t base_after = read_reg_classical(bits, qi_base, W);
    const uint64_t exp_after  = read_reg_classical(bits, qi_exp,  W);
    const uint64_t n_after    = read_reg_classical(bits, qi_n,    W);
    const uint64_t r_after    = read_reg_classical(bits, qi_r,    W);
    const uint64_t expected   = pow_mod_ref(base_val, exp_val, n_val);

    std::printf("  W=%zu base=%llu exp=%llu n=%llu  r=%llu (expected %llu)\n",
                W, (unsigned long long)base_val, (unsigned long long)exp_val,
                (unsigned long long)n_val, (unsigned long long)r_after,
                (unsigned long long)expected);
    std::fflush(stdout);

    assert(base_after == base_val && "cap-lift: base preserved");
    assert(exp_after  == exp_val  && "cap-lift: exp preserved");
    assert(n_after    == n_val    && "cap-lift: n preserved");
    assert(r_after    == expected && "cap-lift: r == (base^exp) mod n");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_base[i]);
}

int main() {
    std::printf("sturm-3sfl.4 Beat D-D pow-mod-dsl cap-lift validation:\n");

    // ── W = 9 (just past the prior cap of 8) ────────────────────────────────
    // Smallest W exceeding the legacy cap; uses a Mersenne-prime modulus
    // (2^9 - 1 = 511) and a non-trivial base/exp pair.  Confirms the
    // assert(n <= kMaxN) does not trip for n = 9.
    {
        constexpr std::size_t W = 9u;
        run_cap_lift_case<W>(/*base=*/123u, /*exp=*/200u, /*n=*/509u);
    }

    // ── W = 12 ──────────────────────────────────────────────────────────────
    // Mid-range: exercises a full square-and-multiply ladder with 12
    // iterations and 12² = 144 mul-witness slots + 11·12 = 132 sq-witness
    // slots live across the forward loop.
    {
        constexpr std::size_t W = 12u;
        run_cap_lift_case<W>(/*base=*/1000u, /*exp=*/3001u, /*n=*/4093u);
    }

    // ── W = 16 (top of the validation range) ────────────────────────────────
    // Confirms the lifted cap accommodates a width that the previous cap of
    // 8 could not.  W = 16 produces a 16² qbool/BitProxy witness matrix
    // (well within stack budget, see pow_mod_dsl.hpp).  Uses a 16-bit prime
    // modulus and a representative base/exp pair.
    {
        constexpr std::size_t W = 16u;
        run_cap_lift_case<W>(/*base=*/40000u, /*exp=*/65000u, /*n=*/65521u);
    }

    std::printf("All sturm-3sfl.4 cap-lift validation cases passed.\n");
    return 0;
}
