// test_pow_mod_dsl_ancilla.cpp -- sturm-a5te.7 P3.7 pow-mod-dsl 3.7
//                                  ancilla-budget assertion.
//
// Plan §5.3 row 3.7 wording: "Peak ancilla `≤ c·W` for small `c`."  PRD §6
// bullet 4: O(W) for pow_mod.
//
// Both targets were drafted under the assumption that pow_mod could be
// implemented with a single in-place `acc` register and a single `sq`
// register, with `acc := mul_mod(acc, sq, n)` and `sq := mul_mod(sq, sq, n)`.
// The shipping `lib_pow_mod_dsl` (sturm-a5te.{2..6}) uses chain-style
// repeated squaring on top of the chain-style `lib_mul_mod_dsl` because the
// in-place updates are blocked by `lib_add_dsl`'s distinct-operand
// precondition (the same reason mul_mod itself is chain-style — see
// `tests/lib/test_mul_mod_dsl_ancilla.cpp` for the W²-scaling pattern at
// one chain layer below).
//
// pow_mod's chain layer adds an extra outer dimension: the algorithm
// allocates
//
//   • `sq_chain[0..W-1]`,   each W bits   (W·W qubits)
//   • `acc_chain[0..W]`,    each W bits   ((W+1)·W qubits, index 0 is the
//                                          initial `1` accumulator)
//
// before driving the inner `lib_mul_mod_dsl` calls.  The peak step is the
// step-2b inner mul_mod call where all sq_chain + all acc_chain qubits
// are simultaneously live AND mul_mod's own internal chain is on the stack.
// At that moment the live ancilla footprint above the 4·W input registers
// is:
//
//     sq_chain                 :       W · W  =  W²
//     acc_chain[0..W]          : (W+1)·W      =  W² + W
//     mul_mod-internal peak    :   2·W² + W + 7
//                                  + (extra ancilla from emit_CCX_lifted /
//                                     emit_X_lifted under the deeper
//                                     control stack induced by pow_mod's
//                                     `exp_bits[i]` push — see below)
//
// pow_mod calls `lib_mul_mod_dsl` while `exp_bits[i]` is already pushed
// onto the control stack (see `pow_mod_dsl.hpp` step 2b, the
// `flip-then-control` idiom).  Inside the inner mul_mod that means
//   - mul_mod pushes `b_bits[j]`  (control depth = 2 incl. exp_bits[i])
//   - inner add_mod pushes `lt_flag` (control depth = 3)
//   - inner add_mod's `emit_CCX_lifted` allocates a fold ancilla and
//     pushes it (control depth = 4) before calling emit_X_lifted at the
//     adder's MAJ/UMA primitive_AND sites.  emit_X_lifted at depth=4
//     allocates one more ancilla via the depth>=4 branch in
//     include/sturm/qtypes/qbool_ops.hpp (lines 53-60).
//
// So the chain-style pow_mod peak ancilla footprint is bounded above by
// the structural composition above.  Per the orchestrator brief: this beat
// MEASURES the actual peak experimentally (by snapshotting
// `QubitPool::high_water()` after the call), derives the closed form, and
// PINS the achieved bound — exactly the same approach the add_mod beat 1.6
// (`tests/lib/test_add_mod_dsl_ancilla.cpp`) and mul_mod beat 2.6
// (`tests/lib/test_mul_mod_dsl_ancilla.cpp`) used when their literal plan
// bounds came in undercounted.  The peak does NOT meet the plan §5.3
// `c·W` / PRD §6 `O(W)` target — it is `O(W²)` (or worse, depending on
// how the chain ancillas layer); plan §5.1 already documents that the
// `O(W)` budget would only follow once `mul_mod` switches to the deferred
// Karatsuba design (PRD §8 #1).
//
// Measurement technique (mirrors mul-mod beat 2.6):
//
//   1. `QubitPool::instance().reset_for_testing()` zeros high_water.
//   2. Allocate the 4·W input registers (base, exp, n, r) — pre_in_use = 4W.
//   3. Call `lib_pow_mod_dsl<W>(...)` in APPEND mode (no statevector
//      simulation needed: ancilla peak is determined by allocation
//      pattern in QubitPool, independent of which executor records the
//      gates).
//   4. `peak_ancilla = high_water() - pre_in_use` is the maximum number
//      of transient ancillas the call ever required beyond the caller's
//      pre-existing inputs.
//
// Asserts at W = 2 and W = 3.  Both widths exceed orkan's 30-qubit
// ceiling for chain-style pow_mod, but APPEND-mode capture has no qubit
// cap.  Mirrors the pattern test_pow_mod_dsl_adjoint.cpp uses.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/pow_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>

// Achieved peak ancilla footprint of `lib_pow_mod_dsl<W>`, in qubits above
// the 4·W input registers.  The closed form below is derived structurally
// from the algorithm (sq_chain + acc_chain + inner mul_mod peak) and pinned
// against the empirically-measured peaks at W=2 / W=3.  Plan §5.3 row 3.7's
// `c·W` and PRD §6's `O(W)` are NOT met by the chain implementation
// — see the file header — so this bound is the regression guard, not
// a refinement of the plan target.
//
// Measured peaks (this test prints them on every run):
//   • W = 2:  28 qubits   (matches 4·W² + 2W + 8 = 28 exactly)
//   • W = 3:  50 qubits   (matches 4·W² + 2W + 8 = 50 exactly)
//
// The structural composition predicts:
//
//   sq_chain         :       W²        (W copies of W bits)
//   acc_chain[0..W]  :   W² + W        ((W+1) copies of W bits)
//   mul_mod-internal :  2·W² + W + 7   (sturm-kubb.6 measured bound)
//   extra fold lift  :       1         (one more emit_X_lifted ancilla
//                                       level: under pow_mod's
//                                       exp_bits[i] outer control, the
//                                       inner add_mod's emit_CCX_lifted +
//                                       lt_flag + b_bits[j] + exp_bits[i]
//                                       reaches depth 4 — qbool_ops.hpp
//                                       lines 53-60 allocate one extra
//                                       ancilla via the depth>=4 branch
//                                       on top of the depth-3 ancilla
//                                       that mul_mod's own bound already
//                                       includes)
//                       ─────────────
//                       4·W² + 2W + 8
//
// Both W=2 (28) and W=3 (50) hit the closed form exactly; the
// `4W² + 2W + 8` formula is therefore the tight achieved bound under
// chain-style pow_mod composition.
static constexpr int kPowModAncillaSlack(int W) {
    // 4·W² + 2W + 8 — closed form, derived structurally; see header.
    return 4 * W * W + 2 * W + 8;
}

// Run lib_pow_mod_dsl<W> for a single (base, exp, n) with base < n and
// report the peak live-ancilla count above the caller's pre-existing
// inputs.  Pure APPEND-mode capture; no statevector — the ancilla peak
// comes from QubitPool::high_water(), which is independent of executor.
// Behavioural post-conditions (base/exp/n preserved, r == (base^exp) mod n,
// pool LIFO-clean) are pinned by test_pow_mod_dsl{,_adjoint}.cpp; this test
// only asserts the ancilla budget plus pool drain.
template <std::size_t W>
static int peak_ancilla_for(uint32_t base_val, uint32_t exp_val,
                            uint32_t n_val) {
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W);  // base, exp, n, r
    int qi_base[W], qi_exp[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_exp[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i]    = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_r[i]    = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

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

    // APPEND-mode context: gates are recorded into ctx.ir; QubitPool
    // tracks allocate/release exactly the same way as in SIMULATE mode,
    // so high_water() faithfully reports the algorithm's peak ancilla
    // footprint.
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    // (Suppress input/output ignore warnings — base/exp/n unused in the
    //  ancilla-only path are still emitted as gate args during capture.)
    (void)base_val;
    (void)exp_val;
    (void)n_val;

    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            W, r_bits);

    // Capture peak before tearing down (post-call high_water is the
    // max number of distinct qubit indices ever live during the call).
    const int peak = sturm::QubitPool::instance().high_water() - pre_in_use;

    // Pool live-count must return to pre-call value: every chain qubit
    // released LIFO.  Pinned more rigorously by beat 3.8's dedicated drain
    // test; assert here too so a regression in the algorithm's
    // allocation/release symmetry cannot pass an ancilla-only assertion.
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "ancilla probe: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_base[i]);
    return peak;
}

int main() {
    // ── W = 2 ────────────────────────────────────────────────────────────
    // Closed-form bound = 4·W² + 2W + 8 = 16 + 4 + 8 = 28.  Pinned as the
    // regression guard for the chain-style algorithm.  See the file
    // header — plan §5.3 row 3.7's literal `c·W` is not achievable for
    // the chain implementation; this test pins the actual achieved peak.
    {
        constexpr std::size_t W = 2u;
        constexpr int        kBudget = kPowModAncillaSlack(static_cast<int>(W));
        std::printf("sturm-a5te.7 P3.7 pow-mod-dsl: peak ancilla bound "
                    "<= 4W^2 + 2W + 8 (W=%zu, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*base=*/2u, /*exp=*/3u,
                                              /*n=*/3u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat 3.7: peak ancilla exceeds achieved chain bound at W=2");
        std::puts("  PASS: W=2 peak ancilla within achieved chain budget");
    }

    // ── W = 3 ────────────────────────────────────────────────────────────
    // Closed-form bound = 4·W² + 2W + 8 = 36 + 6 + 8 = 50.  Chain-style
    // pow_mod peaks far above the orkan stub's 30-qubit ceiling at W=3,
    // but the APPEND-mode harness has no qubit cap and the ancilla peak
    // comes from QubitPool, independent of executor — see file-header
    // rationale.
    {
        constexpr std::size_t W = 3u;
        constexpr int        kBudget = kPowModAncillaSlack(static_cast<int>(W));
        std::printf("sturm-a5te.7 P3.7 pow-mod-dsl: peak ancilla bound "
                    "<= 4W^2 + 2W + 8 (W=%zu, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*base=*/2u, /*exp=*/3u,
                                              /*n=*/5u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat 3.7: peak ancilla exceeds achieved chain bound at W=3");
        std::puts("  PASS: W=3 peak ancilla within achieved chain budget");
    }

    std::printf("All sturm-a5te.7 tests passed.\n");
    return 0;
}
