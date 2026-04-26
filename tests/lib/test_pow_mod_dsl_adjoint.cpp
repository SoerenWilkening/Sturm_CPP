// test_pow_mod_dsl_adjoint.cpp -- sturm-a5te.6 P3.6 pow-mod-dsl 3.6 adjoint
//                                  round-trip.
//
// Plan §5.3 beat 3.6: forward `lib_pow_mod_dsl(base, exp, n, W, r)` followed
// by `__lib_pow_mod_dsl_adj(base, exp, n, W, r)` must return `r` to |0> for
// every (base, exp, n) input that beats 3.4 / 3.5 cover.  Inputs `base, exp,
// n` must remain unchanged across the full forward+adjoint pair, and
// `QubitPool::in_use()` must return to its pre-call value (no leaked
// ancillas).
//
// Mirrors `tests/lib/test_mul_mod_dsl_adjoint.cpp` (sturm-kubb.5) adapted
// for chain-style pow_mod (sturm-a5te.{2,3,4,5}).  Per the issue brief and
// the broader chain-style pattern, the simulator is infeasible at W=2+ for
// pow_mod given chain mul_mod composition (chain pow_mod stacks W copies of
// W-bit sq_chain[i] plus W+1 copies of W-bit acc_chain[i] and recursively
// allocates chain-style mul_mod's internal 2·W² ancillas; peak qubit count
// vastly exceeds orkan's 30-qubit ceiling at W=2 and beyond).  We use the
// APPEND-mode classical-trace harness (the same pattern test_pow_mod_dsl.cpp
// uses for beats 3.2-3.5 and test_mul_mod_dsl_adjoint.cpp uses for the W=3
// leg).
//
// Coverage:
//   - W=2 exhaustive sweep over every (base, exp, n) with base ∈ [0, n),
//     exp ∈ [0, 2^W), n ∈ [1, 2^W).  Total: n=1 → 4; n=2 → 8; n=3 → 12;
//     grand total 24 cases.
//   - W=3 random sweep: 50 cases, fixed `std::mt19937(42)` seed (matches
//     beat 3.5 / mul-mod beat 2.4).
//
// Each case asserts:
//   - forward portion sets r == (base^exp) mod n,
//   - adjoint portion returns r to 0,
//   - base, exp, n preserved across the full forward+adjoint pair,
//   - every ancilla bit is back to 0 after the adjoint,
//   - QubitPool::in_use() returns to its pre-call value.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/pow_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

static constexpr std::size_t W  = 2u;
static constexpr std::size_t W3 = 3u;

// ── classical-trace harness shared by W=2 sweep and W=3 sweep ─────────────
static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u; break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u; break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr, "trace: unsupported gate kind %d\n",
                     static_cast<int>(rec.kind));
        std::abort();
    }
}

static uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k)
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    return v;
}

// Reference: (base^exp) mod n via repeated unsigned multiplication
// (uint64_t guards against overflow at small values).  Convention 0^0 = 1.
static uint32_t pow_mod_ref(uint32_t base_val, uint32_t exp_val,
                            uint32_t n_val) {
    uint64_t r = 1u;
    for (uint32_t k = 0; k < exp_val; ++k)
        r = (r * static_cast<uint64_t>(base_val))
            % static_cast<uint64_t>(n_val);
    return static_cast<uint32_t>(r);
}

// Generic forward+adjoint round-trip via APPEND-mode capture + classical
// bit-vector replay.  Asserts forward portion sets r == (base^exp) mod n,
// adjoint portion returns r to 0 with base/exp/n preserved, every ancilla
// bit is back to 0, and pool live-count returns to pre-call value.
template <std::size_t Wn>
static void run_roundtrip_case_trace(uint32_t base_val, uint32_t exp_val,
                                     uint32_t n_val) {
    assert(n_val >= 1u && "test precondition: n >= 1 (n==0 is no-op path)");
    assert(n_val < (1u << Wn) && "test precondition: n fits in Wn bits");
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    assert(exp_val < (1u << Wn) && "test precondition: exp fits in Wn bits");

    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(Wn);
    int qi_base[Wn], qi_exp[Wn], qi_n[Wn], qi_r[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_exp[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i]    = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_r[i]    = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool base_own[Wn], exp_own[Wn], n_own[Wn], r_own[Wn];
    sturm::BitProxy base_bits[Wn], exp_bits[Wn], n_bits[Wn], r_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        base_own[i] = sturm::qbool::make_non_owning(qi_base[i]);
        exp_own[i]  = sturm::qbool::make_non_owning(qi_exp[i]);
        n_own[i]    = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i]    = sturm::qbool::make_non_owning(qi_r[i]);
        base_bits[i] = sturm::BitProxy(base_own[i]);
        exp_bits[i]  = sturm::BitProxy(exp_own[i]);
        n_bits[i]    = sturm::BitProxy(n_own[i]);
        r_bits[i]    = sturm::BitProxy(r_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    // Forward: r = (base^exp) mod n.
    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            Wn, r_bits);
    const std::size_t fwd_gate_count = ctx->ir.size();

    // Adjoint resolved through invert<>().
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_pow_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_pow_mod_dsl<BitProxy>>() must resolve");
    adj_ptr(base_bits, exp_bits, n_bits, Wn, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();

    // Replay: seed inputs, then run forward portion, then run adjoint portion.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((base_val >> i) & 1u) bits[static_cast<std::size_t>(qi_base[i])] = 1u;
        if ((exp_val  >> i) & 1u) bits[static_cast<std::size_t>(qi_exp[i])]  = 1u;
        if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
        // r starts at |0> per PRD §5 precondition.
    }

    // Replay forward portion.
    for (std::size_t i = 0; i < fwd_gate_count; ++i)
        apply_gate_classical(bits, ctx->ir.at(i));
    const uint32_t expect_r = pow_mod_ref(base_val, exp_val, n_val);
    assert(read_reg_classical(bits, qi_r, Wn) == expect_r &&
           "trace: forward portion sets r == (base^exp) mod n");

    // Replay adjoint portion.
    for (std::size_t i = fwd_gate_count; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));
    assert(read_reg_classical(bits, qi_base, Wn) == base_val &&
           "trace: base register unchanged across forward+adjoint");
    assert(read_reg_classical(bits, qi_exp, Wn) == exp_val &&
           "trace: exp register unchanged across forward+adjoint");
    assert(read_reg_classical(bits, qi_n, Wn) == n_val &&
           "trace: n register unchanged across forward+adjoint");
    assert(read_reg_classical(bits, qi_r, Wn) == 0u &&
           "trace: r returned to |0> after adjoint");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "trace: ancilla bit not cleaned up");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use &&
           "trace: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_base[i]);
}

int main() {
    std::printf("sturm-a5te.6 P3.6 pow-mod-dsl: adjoint round-trip "
                "(W=2 exhaustive trace, all base, exp, n with "
                "base in [0, n), exp in [0, 4), n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t base_val = 0u; base_val < n_val; ++base_val) {
            for (uint32_t exp_val = 0u; exp_val < (1u << W); ++exp_val) {
                run_roundtrip_case_trace<W>(base_val, exp_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1×4 = 4; n=2 → 2×4 = 8; n=3 → 3×4 = 12; total = 24.
    assert(cases_run == 24u);
    std::printf("  PASS: %zu W=2 cases (forward + adjoint zeros r, "
                "preserves base/exp/n, no leaked ancillas)\n", cases_run);

    // W=3 random sweep — classical-trace mode (simulator infeasible at W=3+).
    constexpr uint32_t    kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-a5te.6 P3.6 pow-mod-dsl: adjoint round-trip "
                "(W=3 random sweep, %zu cases, seed=%u, classical trace):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    std::uniform_int_distribution<uint32_t> exp_dist(0u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> base_dist(0u, n_val - 1u);
        uint32_t base_val = base_dist(rng);
        uint32_t exp_val  = exp_dist(rng);
        run_roundtrip_case_trace<W3>(base_val, exp_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (forward + adjoint round-trip "
                "in trace mode)\n", kW3Cases);

    std::printf("All sturm-a5te.6 tests passed.\n");
    return 0;
}
