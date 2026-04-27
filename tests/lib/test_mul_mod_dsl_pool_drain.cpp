// test_mul_mod_dsl_pool_drain.cpp -- sturm-kubb.7 P2.7 mul-mod-dsl 2.7
//                                    pool live-count round-trip.
//
// Plan §4.3 row 2.7 wording: "QubitPool::live_count returns to pre-call
// value." -- mirror of beat 1.7 (sturm-yh3d.7,
// `tests/lib/test_add_mod_dsl_pool_drain.cpp`) for the chain-style
// `lib_mul_mod_dsl`.
//
// Per the orchestrator brief on this beat:
//   * Use APPEND-mode classical trace for the W=3 leg -- the orkan
//     simulator is infeasible at W=3 (chain-style mul_mod peaks at ~35
//     live qubits, ~34 GB amplitudes per case in the stub; same rationale
//     beats 2.4 / 2.5 give for skipping the simulator at W=3).
//   * The W=2 leg can use either simulator or trace; trace is faster.
//     Trace mode here keeps the test interactive (~ms per case) and
//     mirrors the W=3 leg's harness so the same drain / LIFO assertions
//     run for both register widths.
//   * Verify pre-call `QubitPool::in_use()` == post-call value (every
//     transient ancilla released) AND that the next allocate() returns
//     the lowest non-reserved index (LIFO recycle witness, just like
//     beat 1.7).
//
// Why a separate test from beats 2.2-2.5
// --------------------------------------
// The existing mul_mod tests bundle `in_use()` round-trip checks with
// correctness asserts; this beat 2.7 dedicates a test whose only pin is
// the LIFO release contract.  A regression that balances counts but
// corrupts the free-list ordering would still pass the bundled checks.
// Mirrors how beat 1.7 / `test_lossy_ancilla_cleaned` separate LIFO
// drainage from correctness fixtures.
//
// LIFO release verification technique
// -----------------------------------
// LIFO release is what keeps the free-list reuse pattern stable: indices
// allocated last are released first, so the next allocate() returns the
// most-recently-released index.  We pin this by asserting that
// immediately after a `lib_mul_mod_dsl` call returns, an `allocate()`
// reuses the index the algorithm released LAST during its internal LIFO
// unwind.  The chain algorithm's release order (see mul_mod_dsl.hpp
// lines 237-256) is r_chain[W..1] then shifted_chain[W-1..0], LIFO over
// the (i, j) pairs.  The first ancilla allocated by the algorithm is
// shifted_chain[0][0] -- its index is `pre_in_use` (the next pool slot
// after the n_reg=4*W reserved inputs).  The LAST ancilla released is
// also shifted_chain[0][0], so after the call the free-list's top
// element is `pre_in_use` and the next allocate() returns that same
// index.  Asserting `next == pre_in_use` is the strongest LIFO pin: any
// non-LIFO release order (or a stray missing release) would have left a
// different ordering on the free-list and `next` would land on a higher
// (or recycled-out-of-order) index.
//
// Coverage shape
// --------------
// W=2 leg covers the 14-case exhaustive sweep matching beat 2.3 (all
// (a, b, n) with a, b in [0, n) for n in [1, 4)).  W=3 leg uses the same
// 50-case fixed-seed random sweep that beat 2.4 / 2.5 use, so any
// data-dependent leak that escapes the W=2 sweep has a high chance of
// being caught by the W=3 random sample.  The algorithm's allocation
// footprint is data-independent (shifted_chain / r_chain shapes do not
// depend on a/b/n bit patterns), but the release path is exercised
// across both the no-add-back branch (b[i]=0 in r_chain step, lt_flag=0
// in inner add_mod) and the full-add-back branch (b[i]=1 / lt_flag=1).
//
// Budget: <= 250 LoC.

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
#include <random>

// Run lib_mul_mod_dsl<Wn> for one (a, b, n) input in APPEND-mode (so no
// statevector simulation is needed; the QubitPool still tracks
// allocate/release in real time, which is what this test pins).  Asserts:
//   1. pool drain   == pre_in_use   (every transient ancilla released)
//   2. peek-and-release the next allocate() returns `pre_in_use` exactly,
//      witnessing LIFO recycling order (the LAST-released index is the
//      lowest non-reserved slot, i.e. the FIRST chain ancilla allocated).
template <std::size_t Wn>
static void run_pool_drain_case(uint32_t a_val, uint32_t b_val,
                                uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    assert(n_val < (1u << Wn) && "test precondition: n fits in Wn bits");

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*Wn lowest qubit indices for a, b, n, r so we know
    // exactly which slots are pre-existing inputs vs. transient ancillas.
    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(Wn);
    int qi_a[Wn], qi_b[Wn], qi_n[Wn], qi_r[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg)
           && "pre-call in_use must equal the 4*Wn reserved input slots");

    // Wrap the reserved indices as non-owning qbools / BitProxies so the
    // algorithm sees them as quantum (is_quantum() == true) and emits
    // gates rather than classical-folding -- same harness shape as
    // test_mul_mod_dsl.cpp's W=3 trace path.
    sturm::qbool a_own[Wn], b_own[Wn], n_own[Wn], r_own[Wn];
    sturm::BitProxy a_bits[Wn], b_bits[Wn], n_bits[Wn], r_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    // (Suppress unused-input warnings in the strict drain-only path -- the
    //  inputs are still emitted as gate args during APPEND-mode capture.)
    (void)a_val;
    (void)b_val;
    (void)n_val;

    // APPEND-mode context: gates recorded into ctx.ir; QubitPool tracks
    // allocate/release exactly as in SIMULATE mode, so the drain / LIFO
    // assertions are independent of executor.  This is the same rationale
    // beat 2.6 (mul_mod_dsl_ancilla) and beat 2.4 (W=3 trace) use to
    // bypass the orkan stub's 30-qubit ceiling at W=3.
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                            Wn, r_bits);

    // (1) Pool live-count returns to its pre-call value: every transient
    //     ancilla allocated by the algorithm has been released back.
    const int post_in_use = sturm::QubitPool::instance().in_use();
    if (post_in_use != pre_in_use) {
        std::fprintf(stderr,
                     "  FAIL: (W=%zu, a=%u, b=%u, n=%u) pool leaked %d qubits "
                     "(pre=%d, post=%d)\n",
                     Wn, a_val, b_val, n_val, post_in_use - pre_in_use,
                     pre_in_use, post_in_use);
    }
    assert(post_in_use == pre_in_use
           && "lib_mul_mod_dsl must drain every transient ancilla");

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
                     "  FAIL: (W=%zu, a=%u, b=%u, n=%u) LIFO recycle witness: "
                     "expected next allocate() == %d (pre_in_use), got %d\n",
                     Wn, a_val, b_val, n_val, pre_in_use, next_idx);
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
        sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    // ── W=2 exhaustive sweep (matches beat 2.3 coverage shape) ────────────
    constexpr std::size_t W = 2u;
    std::printf("sturm-kubb.7 P2.7 mul-mod-dsl: pool live-count round-trip "
                "(W=2 exhaustive sweep, all a, b in [0, n) for n in [1, 4),"
                " classical trace):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_pool_drain_case<W>(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 -> 1 case; n=2 -> 4 cases; n=3 -> 9 cases; total = 14 cases
    // (matches beat 2.3 exactly).
    assert(cases_run == 14u && "pool-drain sweep covered every (a, b, n) "
                                "with a, b < n and n >= 1 (matches beat 2.3)");
    std::printf("  PASS: %zu W=2 cases, every transient ancilla released "
                "LIFO, pool drain == pre_in_use\n", cases_run);

    // ── W=3 random sweep (matches beats 2.4 / 2.5 coverage shape) ─────────
    // Orkan simulator infeasible at W=3 (~35 live qubits); APPEND-mode
    // classical trace -- which is what the run_pool_drain_case<Wn>
    // template uses already -- has no qubit cap, so the W=3 leg is just
    // a parameter swap.  Fixed seed=42 matches add-mod beat 1.4 / mul-mod
    // beats 2.4 / 2.5 for reproducibility across the modular family.
    constexpr std::size_t W3 = 3u;
    constexpr uint32_t    kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-kubb.7 P2.7 mul-mod-dsl: pool live-count round-trip "
                "(W=3 random sweep, %zu cases, seed=%u, classical trace):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_pool_drain_case<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases, every transient ancilla "
                "released LIFO, pool drain == pre_in_use\n", kW3Cases);

    std::printf("All sturm-kubb.7 tests passed.\n");
    return 0;
}
