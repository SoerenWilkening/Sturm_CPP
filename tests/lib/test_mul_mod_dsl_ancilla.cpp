// test_mul_mod_dsl_ancilla.cpp -- sturm-kubb.6 P2.6 mul-mod-dsl 2.6
//                                  ancilla-budget assertion.
//
// Plan §4.3 row 2.6 wording: "Peak ancilla counter ≤ 2W + 5."  The literal
// `2W + 5` count was drafted for an in-place doubling design (one W-bit
// `shifted` register + one W-bit `r` accumulator + add_mod interior).  The
// shipping `lib_mul_mod_dsl` uses the chain-style algorithm documented at
// the top of `include/sturm/lib/mul_mod_dsl.hpp`:
//
//   • `shifted_chain[0..W-1]`, each W bits   (W·W qubits)
//   • `r_chain[1..W]`,         each W bits   (W·W qubits, index 0 unused)
//
// plus the add_mod-interior peak when the innermost `lib_add_mod_dsl` call
// is on the stack at the chain step that holds the largest number of
// registers simultaneously.  Add-mod beat 1.6 (sturm-yh3d.6) pinned the
// add_mod-only peak at `W + 6`; called from inside the mul_mod chain with
// `b_bits[i]` already pushed as a control, the inner adder's `emit_X_lifted`
// reaches depth=3 (mul_mod's b_bits[i] + add_mod's lt_flag + emit_CCX_lifted's
// fold) and allocates one more ancilla via the depth==3 branch in
// include/sturm/qtypes/qbool_ops.hpp, taking the per-call peak to `W + 7`.
//
// So the algorithm's achieved peak ancilla footprint is
//
//   peak_chain  =  2·W·W + (W + 7)  =  2·W² + W + 7
//
// at the inner add_mod call inside the r_chain accumulator loop, far above
// the literal `2W + 5`.  This mirrors the situation add-mod beat 1.6 hit:
// the literal plan bound was undercounted relative to the actual algorithm.
// The PRD §6 bullet 4 requirement — `W + O(1)` for mul_mod — is NOT met by
// the chain implementation (it is `O(W²)`); plan §4.1 explicitly notes
// "peak ancilla at `2W + O(1)`" as the design intent under the in-place
// doubling alternative, and §12 says to revisit the Karatsuba alt
// (PRD §8 #1) if the bound is exceeded.
//
// Per the orchestrator brief: do NOT silently relax to 2W+5; instead pin
// the achieved bound here (regression guard) and let the autopilot surface
// the discrepancy to the user for plan amendment.
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
// Asserts at W = 2 and W = 3.  W = 3 chain-style peaks at ~35 live qubits,
// which would blow the orkan stub's 30-qubit ceiling; APPEND-mode capture
// has no qubit cap.  Mirrors the pattern test_mul_mod_dsl.cpp uses for
// beat 2.4's W=3 sweep.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>

// Achieved peak ancilla footprint of `lib_mul_mod_dsl<W>`, in qubits above
// the 4·W input registers.  Empirical peak (measured by this very test;
// see W=2 / W=3 main bodies below):
//
//   • W = 2:  17 qubits
//   • W = 3:  28 qubits
//
// fits the closed form `2·W² + W + 7`, which decomposes as
//
//   peak  =  W·W (shifted_chain[0..W-1])
//        +  W·W (r_chain[1..W])
//        +  W + 7 (add_mod-interior peak under one mul_mod control)
//
// where the `W + 7` add_mod-interior is the sturm-yh3d.6 `W + 6` plus one
// extra ancilla from emit_X_lifted at depth ≥ 3 (mul_mod pushes `b_bits[i]`,
// add_mod pushes `lt_flag`, and `emit_CCX_lifted` then pushes its fold
// ancilla, taking the live control depth to 3 at the inner adder's CCX
// gates — see include/sturm/qtypes/qbool_ops.hpp's emit_X_lifted depth==3
// branch which allocates one more ancilla).
//
// Plan §4.3 row 2.6 wording (`2W + 5`) was drafted before the chain
// implementation was settled — see PRD §8 #1 (Karatsuba alt) for the path
// to a tighter bound.  Documented as a discrepancy to the user.
static constexpr int kMulModAncillaSlack(int W) { return 2 * W * W + W + 7; }

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
        sturm_backend_create(STURM_MODE_APPEND, 64u);
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
    // Achieved bound = 2·W² + W + 6 = 2·4 + 2 + 6 = 16.  Pinned as the
    // regression guard for the chain-style algorithm.  See the file
    // header — plan §4.3 row 2.6's literal `2W + 5` is undercounted for
    // the chain implementation; this test pins the actual achieved peak.
    {
        constexpr std::size_t W = 2u;
        constexpr int        kBudget = kMulModAncillaSlack(static_cast<int>(W));
        std::printf("sturm-kubb.6 P2.6 mul-mod-dsl: peak ancilla bound "
                    "<= 2W^2 + W + 7 (W=%zu, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*a=*/2u, /*b=*/2u, /*n=*/3u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat 2.6: peak ancilla exceeds achieved chain bound at W=2");
        std::puts("  PASS: W=2 peak ancilla within achieved chain budget");
    }

    // ── W = 3 ────────────────────────────────────────────────────────────
    // Achieved bound = 2·W² + W + 6 = 2·9 + 3 + 6 = 27.  Chain-style
    // mul_mod peaks at ~35 live qubits at W=3 (which exceeds the orkan
    // stub's 30-qubit ceiling), but the APPEND-mode harness has no qubit
    // cap and the ancilla peak comes from QubitPool, independent of
    // executor — see file-header rationale.
    {
        constexpr std::size_t W = 3u;
        constexpr int        kBudget = kMulModAncillaSlack(static_cast<int>(W));
        std::printf("sturm-kubb.6 P2.6 mul-mod-dsl: peak ancilla bound "
                    "<= 2W^2 + W + 7 (W=%zu, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*a=*/2u, /*b=*/3u, /*n=*/5u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat 2.6: peak ancilla exceeds achieved chain bound at W=3");
        std::puts("  PASS: W=3 peak ancilla within achieved chain budget");
    }

    std::printf("All sturm-kubb.6 tests passed.\n");
    return 0;
}
