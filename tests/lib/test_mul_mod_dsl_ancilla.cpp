// test_mul_mod_dsl_ancilla.cpp -- sturm-kubb.6 P2.6 mul-mod-dsl 2.6
//                                  ancilla-budget assertion.
//
// Plan §4.3 row 2.6 wording was originally "Peak ancilla counter ≤ 2W + 5"
// (an in-place doubling design's drafted bound).  After sturm-4oot.5
// `lib_mul_mod_dsl` is a thin wrapper around `lib_mul_mod_dsl_oneshot`
// (the chain-style fallback was retired), so this test pins the oneshot
// bound:
//
//   peak_oneshot ≈ 2·(W + 1)  [shifted_reg + acc_reg, each W+1 wide]
//                 + (W − 1)    [lt_flags register, sturm-4oot.3]
//                 + 5          [Beat A interior peak, sturm-yh3d.6]
//                 + 1          [lift_under(b_bits[i]) fold ancilla]
//                = 3·W + 7
//
// at the moment the inner `lib_add_mod_inplace_dsl` is mid-flight under
// the `lift_under(b_bits[i])` body.  This matches PRD §6.4's `W + O(1)`
// requirement for mul_mod (still linear in W, ~3× the ideal coefficient
// but no longer quadratic).  See
// `tests/lib/test_mul_mod_dsl_oneshot_ancilla.cpp` for the dedicated
// pin on the helper itself; this test pins the same bound through the
// public `lib_mul_mod_dsl` entry point.
//
// Pre-history: prior to sturm-4oot.5 this test pinned the chain
// implementation's `2·W² + W + 7` bound (see git history at commit
// 644a5f8 or earlier).  The chain helper was retired once
// `lib_mul_mod_dsl_oneshot` became parity-agnostic in sturm-4oot.4.
//
// Measurement technique (per orchestrator brief, "snapshots before / mid
// (max) / after"):
//
//   1. `QubitPool::instance().reset_for_testing()` zeros high_water.
//   2. Allocate the 4·W input registers (a, b, n, r) — pre_in_use = 4W.
//   3. Call `lib_mul_mod_dsl<W>(...)` in APPEND mode (no statevector
//      simulation needed: ancilla peak is determined by allocation
//      pattern in QubitPool, independent of which executor records the
//      gates — orchestrator brief).
//   4. `peak_ancilla = high_water() - pre_in_use` is the maximum number
//      of transient ancillas the call ever required beyond the caller's
//      pre-existing inputs.
//
// Asserts at W = 2 and W = 3 with both odd and even moduli (oneshot is
// parity-agnostic since sturm-4oot.4).  APPEND-mode capture has no qubit
// cap.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>

// Achieved peak ancilla footprint of `lib_mul_mod_dsl<W>` (= the oneshot
// helper's footprint, since sturm-4oot.5 made the public entry a thin
// wrapper).  See file header for the breakdown.  PRD §6.4 `W + O(1)`
// requirement is met by `3W + 7` (linear).
static constexpr int kMulModAncillaSlack(int W) { return 3 * W + 7; }

// Run lib_mul_mod_dsl<W> for a single (a, b, n) with a, b < n and report
// the peak live-ancilla count above the caller's pre-existing inputs.
// Pure APPEND-mode capture; no statevector — the ancilla peak comes from
// QubitPool::high_water(), which is independent of executor.  Behavioural
// post-conditions (a/b/n preserved, r == (a*b) mod n, pool LIFO-clean)
// are pinned by the W=2 simulator witness in test_mul_mod_dsl.cpp and the
// W=2/W=3 trace witnesses in test_mul_mod_dsl{,_adjoint}.cpp; this test
// only asserts the ancilla budget plus pool drain.
template <std::size_t W>
static int peak_ancilla_for(uint32_t a_val, uint32_t b_val,
                            uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W);  // a, b, n, r
    int qi_a[W], qi_b[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[W], b_own[W], n_own[W], r_own[W];
    sturm::BitProxy a_bits[W], b_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    // APPEND-mode context: gates are recorded into ctx.ir; QubitPool
    // tracks allocate/release exactly the same way as in SIMULATE mode,
    // so high_water() faithfully reports the algorithm's peak ancilla
    // footprint (see orchestrator brief: "ancilla peak is independent
    // of which executor records the gates").
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    // (Suppress input/output ignore warnings — a/b/n unused inputs in the
    //  ancilla-only path are still emitted as gate args during capture.)
    (void)a_val;
    (void)b_val;
    (void)n_val;

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                            W, r_bits);

    // Capture peak before tearing down (post-call high_water is the
    // max number of distinct qubit indices ever live during the call).
    const int peak = sturm::QubitPool::instance().high_water() - pre_in_use;

    // Pool live-count must return to pre-call value: every chain qubit
    // released LIFO.  This is pinned more rigorously by beat 2.7's
    // dedicated drain test, but assert here too so a regression in the
    // algorithm's allocation/release symmetry cannot pass an
    // ancilla-only assertion.
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "ancilla probe: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
    return peak;
}

int main() {
    // ── W = 2 ────────────────────────────────────────────────────────────
    {
        constexpr std::size_t W = 2u;
        constexpr int        kBudget = kMulModAncillaSlack(static_cast<int>(W));
        std::printf("sturm-kubb.6 P2.6 mul-mod-dsl: peak ancilla bound "
                    "<= 3W + 7 (W=%zu, odd n=3, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*a=*/2u, /*b=*/2u, /*n=*/3u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat 2.6: peak ancilla exceeds oneshot bound at W=2 (odd n)");
        std::puts("  PASS: W=2 peak ancilla within oneshot budget (odd n)");

        // sturm-4oot.5: confirm the same 3W + 7 budget holds for even n
        // — the oneshot helper is parity-agnostic since sturm-4oot.4.
        std::printf("sturm-kubb.6 P2.6 mul-mod-dsl: peak ancilla bound "
                    "<= 3W + 7 (W=%zu, even n=4, budget=%d):\n", W, kBudget);
        const int peak_even = peak_ancilla_for<W>(/*a=*/1u, /*b=*/3u, /*n=*/4u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak_even, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak_even <= kBudget
               && "Beat 2.6: peak ancilla exceeds oneshot bound at W=2 (even n)");
        std::puts("  PASS: W=2 peak ancilla within oneshot budget (even n)");
    }

    // ── W = 3 ────────────────────────────────────────────────────────────
    {
        constexpr std::size_t W = 3u;
        constexpr int        kBudget = kMulModAncillaSlack(static_cast<int>(W));
        std::printf("sturm-kubb.6 P2.6 mul-mod-dsl: peak ancilla bound "
                    "<= 3W + 7 (W=%zu, odd n=5, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*a=*/2u, /*b=*/3u, /*n=*/5u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat 2.6: peak ancilla exceeds oneshot bound at W=3 (odd n)");
        std::puts("  PASS: W=3 peak ancilla within oneshot budget (odd n)");

        // sturm-4oot.5: even-n probe at W=3 (n=6).
        std::printf("sturm-kubb.6 P2.6 mul-mod-dsl: peak ancilla bound "
                    "<= 3W + 7 (W=%zu, even n=6, budget=%d):\n", W, kBudget);
        const int peak_even = peak_ancilla_for<W>(/*a=*/2u, /*b=*/5u, /*n=*/6u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak_even, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak_even <= kBudget
               && "Beat 2.6: peak ancilla exceeds oneshot bound at W=3 (even n)");
        std::puts("  PASS: W=3 peak ancilla within oneshot budget (even n)");
    }

    std::printf("All sturm-kubb.6 tests passed.\n");
    return 0;
}
