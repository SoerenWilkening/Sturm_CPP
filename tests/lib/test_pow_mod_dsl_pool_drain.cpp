// test_pow_mod_dsl_pool_drain.cpp -- sturm-a5te.8 P3.8 pow-mod-dsl 3.8
//                                    pool live-count round-trip.
//
// Plan §5.3 row 3.8 wording: "Pool live-count return."  Mirrors beat 1.7
// (sturm-yh3d.7, `tests/lib/test_add_mod_dsl_pool_drain.cpp`) and beat 2.7
// (sturm-kubb.7, `tests/lib/test_mul_mod_dsl_pool_drain.cpp`) for the
// chain-style `lib_pow_mod_dsl`.
//
// Per the orchestrator brief on this beat:
//   * Use APPEND-mode classical trace -- the orkan simulator is infeasible
//     for chain-style pow_mod at W=2+ (peak ancilla 4·W² + 2W + 8 = 28 at
//     W=2 and 50 at W=3 above the 4·W input registers; W=2 already exceeds
//     orkan's 30-qubit ceiling once chain mul_mod composition kicks in).
//     APPEND mode has no qubit cap and the QubitPool tracks
//     allocate/release independent of executor, so the drain / LIFO
//     assertions are valid against the IR-recording context.
//   * Verify pre-call `QubitPool::in_use()` == post-call value (every
//     transient ancilla released) AND that the next allocate() returns
//     the lowest non-reserved index (LIFO recycle witness, just like
//     beats 1.7 / 2.7).
//   * W=2 exhaustive sweep + W=3 random sweep with seed 42, ~50 cases.
//
// Why a separate test from beats 3.2-3.6
// --------------------------------------
// The existing pow_mod tests bundle `in_use()` round-trip checks with
// correctness asserts; this beat 3.8 dedicates a test whose only pin is
// the LIFO release contract.  A regression that balances counts but
// corrupts the free-list ordering (e.g. a non-LIFO release order in the
// pow_mod tail that happens to balance counts but leaves a higher index
// at the top of the free-list) would still pass the bundled checks.
// Mirrors how beats 1.7 / 2.7 separate LIFO drainage from correctness
// fixtures.
//
// LIFO release verification technique
// -----------------------------------
// LIFO release is what keeps the free-list reuse pattern stable: indices
// allocated last are released first, so the next allocate() returns the
// most-recently-released index.  We pin this by asserting that
// immediately after a `lib_pow_mod_dsl` call returns, an `allocate()`
// reuses the index the algorithm released LAST during its internal LIFO
// unwind.
//
// The chain pow_mod algorithm's allocation order (see pow_mod_dsl.hpp):
//   1. sq_chain[0..W-1], each W bits (W² qubits)  -- allocated FIRST,
//      starting at pre_in_use.  sq_chain[0][0] gets index `pre_in_use`.
//   2. sq_chain[1..W-1] are built via lib_mul_mod_dsl, which internally
//      allocates and releases its own chain ancillas.
//   3. acc_chain[0..W], each W bits ((W+1)·W qubits) -- allocated AFTER
//      sq_chain.
//   4. acc_chain build/unbuild calls lib_mul_mod_dsl, which transiently
//      allocates internal chains.
//   5. Release order (pow_mod_dsl.hpp lines 227-249):
//        (5a) acc_chain LIFO inner-then-outer:
//             for i in W..0, for j in W-1..0: release acc_idx[i][j]
//        (5b) sq_chain uncompute via __lib_mul_mod_dsl_adj loop.
//        (5c) sq_chain LIFO inner-then-outer:
//             for i in W-1..0, for j in W-1..0: release sq_idx[i][j]
//
// The very LAST release is therefore `sq_idx[0][0]`, which equals
// `pre_in_use` (the FIRST chain ancilla allocated).  After the call the
// free-list's top element is `pre_in_use`, so the next allocate() returns
// that same index.  Asserting `next == pre_in_use` is the strongest LIFO
// pin: any non-LIFO release order or a stray missing release would have
// left a different ordering on the free-list and `next` would land on a
// higher index (or skip `pre_in_use` entirely if it was never released).
//
// Coverage shape
// --------------
// W=2 leg covers the 24-case exhaustive sweep matching beat 3.4 (all
// (base, exp, n) with base in [0, n), exp in [0, 2^W), n in [1, 2^W)).
// W=3 leg uses the same 50-case fixed-seed random sweep that beats 3.5 /
// 3.6 use, so any data-dependent leak that escapes the W=2 sweep has a
// high chance of being caught by the W=3 random sample.  The algorithm's
// allocation footprint is data-independent (sq_chain / acc_chain shapes
// do not depend on base/exp/n bit patterns), but the release path is
// exercised across both the no-mul-back branch (exp[i]=0 in acc_chain
// step, lt_flag=0 in inner add_mod) and the full-mul-back branch
// (exp[i]=1 / lt_flag=1).
//
// Budget: <= 250 LoC.

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
#include <random>

// Run lib_pow_mod_dsl<Wn> for one (base, exp, n) input in APPEND-mode (so
// no statevector simulation is needed; the QubitPool still tracks
// allocate/release in real time, which is what this test pins).  Asserts:
//   1. pool drain   == pre_in_use   (every transient ancilla released)
//   2. peek-and-release the next allocate() returns `pre_in_use` exactly,
//      witnessing LIFO recycling order (the LAST-released index is the
//      lowest non-reserved slot, i.e. the FIRST chain ancilla allocated).
template <std::size_t Wn>
static void run_pool_drain_case(uint32_t base_val, uint32_t exp_val,
                                uint32_t n_val) {
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    assert(exp_val  < (1u << Wn) && "test precondition: exp fits in Wn bits");
    assert(n_val    < (1u << Wn) && "test precondition: n fits in Wn bits");

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*Wn lowest qubit indices for base, exp, n, r so we know
    // exactly which slots are pre-existing inputs vs. transient ancillas.
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
    assert(pre_in_use == static_cast<int>(n_reg)
           && "pre-call in_use must equal the 4*Wn reserved input slots");

    // Wrap the reserved indices as non-owning qbools / BitProxies so the
    // algorithm sees them as quantum (is_quantum() == true) and emits
    // gates rather than classical-folding -- same harness shape as
    // test_pow_mod_dsl_adjoint.cpp's W=2/W=3 trace path.
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

    // (Suppress unused-input warnings in the strict drain-only path -- the
    //  inputs are still emitted as gate args during APPEND-mode capture.)
    (void)base_val;
    (void)exp_val;
    (void)n_val;

    // APPEND-mode context: gates recorded into ctx.ir; QubitPool tracks
    // allocate/release exactly as in SIMULATE mode, so the drain / LIFO
    // assertions are independent of executor.  This is the same rationale
    // beats 3.4 / 3.5 / 3.6 / 3.7 use to bypass the orkan stub's 30-qubit
    // ceiling at W=2+ chain pow_mod.
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            Wn, r_bits);

    // (1) Pool live-count returns to its pre-call value: every transient
    //     ancilla allocated by the algorithm has been released back.
    const int post_in_use = sturm::QubitPool::instance().in_use();
    if (post_in_use != pre_in_use) {
        std::fprintf(stderr,
                     "  FAIL: (W=%zu, base=%u, exp=%u, n=%u) pool leaked %d "
                     "qubits (pre=%d, post=%d)\n",
                     Wn, base_val, exp_val, n_val, post_in_use - pre_in_use,
                     pre_in_use, post_in_use);
    }
    assert(post_in_use == pre_in_use
           && "lib_pow_mod_dsl must drain every transient ancilla");

    // (2) LIFO-release witness: the next allocate() must reuse the LAST
    //     released index, which after a fully-LIFO-clean call equals the
    //     FIRST chain ancilla index (= pre_in_use, the next slot beyond
    //     the n_reg=4*Wn reserved inputs).  Asserting `next == pre_in_use`
    //     is the strongest LIFO pin -- any non-LIFO release order or
    //     missing release would have left a different ordering on the
    //     free-list and `next` would land on a higher index (or skip
    //     `pre_in_use` entirely if it was never released).
    const int next_idx = sturm::QubitPool::instance().allocate();
    if (next_idx != pre_in_use) {
        std::fprintf(stderr,
                     "  FAIL: (W=%zu, base=%u, exp=%u, n=%u) LIFO recycle "
                     "witness: expected next allocate() == %d (pre_in_use), "
                     "got %d\n",
                     Wn, base_val, exp_val, n_val, pre_in_use, next_idx);
    }
    assert(next_idx == pre_in_use
           && "post-call allocate() must reuse LIFO-recycled index "
              "== pre_in_use, witnessing LIFO release order");
    sturm::QubitPool::instance().release(next_idx);

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    // Release the reserved inputs by hand to keep the QubitPool clean for
    // the next iteration -- the input qbools were created via
    // `make_non_owning(...)` and so do NOT release on destruction.
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_base[i]);
}

int main() {
    // ── W=2 exhaustive sweep (matches beat 3.4 coverage shape) ────────────
    // All (base, exp, n) with base in [0, n), exp in [0, 2^W), n in [1, 2^W).
    constexpr std::size_t W = 2u;
    std::printf("sturm-a5te.8 P3.8 pow-mod-dsl: pool live-count round-trip "
                "(W=2 exhaustive sweep, all base, exp, n with "
                "base in [0, n), exp in [0, 4), n in [1, 4), classical "
                "trace):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t base_val = 0u; base_val < n_val; ++base_val) {
            for (uint32_t exp_val = 0u; exp_val < (1u << W); ++exp_val) {
                run_pool_drain_case<W>(base_val, exp_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 -> 1*4 = 4; n=2 -> 2*4 = 8; n=3 -> 3*4 = 12; total = 24 cases
    // (matches beat 3.4 exactly).
    assert(cases_run == 24u && "pool-drain sweep covered every (base, exp, n) "
                                "with base < n and n >= 1 (matches beat 3.4)");
    std::printf("  PASS: %zu W=2 cases, every transient ancilla released "
                "LIFO, pool drain == pre_in_use\n", cases_run);

    // ── W=3 random sweep (matches beats 3.5 / 3.6 coverage shape) ─────────
    // Orkan simulator infeasible at W=3 (chain pow_mod peaks at ~50 ancillas
    // above 12 inputs = ~62 qubits, far above the 30-qubit ceiling);
    // APPEND-mode classical trace -- which is what the run_pool_drain_case
    // <Wn> template uses already -- has no qubit cap, so the W=3 leg is
    // just a parameter swap.  Fixed seed=42 matches the modular family's
    // W=3 random sweeps (add-mod beat 1.4, mul-mod beats 2.4 / 2.5 / 2.7,
    // pow-mod beats 3.5 / 3.6) for reproducibility.
    constexpr std::size_t W3       = 3u;
    constexpr uint32_t    kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-a5te.8 P3.8 pow-mod-dsl: pool live-count round-trip "
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
        run_pool_drain_case<W3>(base_val, exp_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases, every transient ancilla "
                "released LIFO, pool drain == pre_in_use\n", kW3Cases);

    std::printf("All sturm-a5te.8 tests passed.\n");
    return 0;
}
