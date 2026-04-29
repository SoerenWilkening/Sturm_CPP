// test_mul_mod_dsl_oneshot_ancilla.cpp -- sturm-7cix Beat C: peak-ancilla
//                                          budget assertion for the new
//                                          O(W) oneshot path.
//
// The oneshot algorithm allocates a (W+1)-bit shifted register, a
// (W+1)-bit accumulator, and a (W-1)-bit `lt_flags` register above the
// 4·W input registers, plus the ancillas the inner Beat-A and Beat-B
// primitives consume during their peak step.  Beat A's peak is
// constant 5; Beat B's peak is constant 4 (post sturm-4oot.1, after
// the internal `lt_flag` was externalised — see
// docs/design_even_n_double_mod.md §8 row 1).  The dominant cost is
// therefore the two (W+1)-qubit registers plus the (W-1)-bit
// `lt_flags`, yielding a peak above the 4·W inputs of:
//
//   peak_oneshot ≈ 2·(W + 1) [shifted + acc]
//                 + (W - 1)   [lt_flags register, sturm-4oot.3]
//                 + 5         [Beat A interior peak]
//                 + 1         [lift_under(b_bits[i]) fold ancilla]
//                = 3·W + 7
//
// at the moment the inner `lib_add_mod_inplace_dsl` is mid-flight under
// the `lift_under(b_bits[i])` body.  This is **independent of the chain
// implementation's `2W² + W + 7` peak** — the entire point of Beat C
// is to drop the asymptotic class from O(W²) to O(W).  See the file
// header in `include/sturm/detail/lib/mul_mod_dsl_oneshot.hpp` for the
// algorithm summary and ancilla breakdown.
//
// PRD §6.4 `W + O(1)` requirement is met by `3W + 7` (still linear).
//
// Pre-fix peak was `2W + 8` (with the doubling primitive's internal
// `lt_flag` ancilla); sturm-4oot.3 raises this layer's peak by `(W-1)`
// (the new lt_flags register at this layer) and lowers the inner
// double_mod peak by `1` (its own lt_flag externalised).  Net total
// across the two layers grows by `W − 2` qubits; in exchange the
// doubling primitive — and therefore this oneshot helper — becomes
// parity-agnostic, dropping the odd-n precondition.  See
// docs/design_even_n_double_mod.md §8 row 2.
//
// Measurement technique mirrors test_double_mod_dsl_ancilla.cpp:
// snapshot QubitPool::high_water() after the call and subtract pre-call
// in_use to isolate the algorithm's peak transient footprint.  Tests
// at W = 2 and W = 3 (both fit inside orkan's 30-qubit ceiling using
// the `bypass_cap` allocation path).

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

// Achieved peak ancilla footprint of `lib_mul_mod_dsl_oneshot<W>`, in
// qubits above the 4·W input registers.  See file-header analysis:
// 2·(W+1) [shifted + acc] + (W-1) [lt_flags register, sturm-4oot.3]
// + 5 [Beat A interior peak] + 1 [lift_under fold] = 3W + 7.
// PRD `W + O(1)` requirement is met (linear in W).
static constexpr int kOneshotAncillaSlack(int W) { return 3 * W + 7; }

template <std::size_t W>
static int peak_ancilla_oneshot_for(uint32_t a_val, uint32_t b_val,
                                    uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && (n_val & 1u) == 1u);
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W);
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
        // Seed n with classical odd hint so the dispatcher (if reached)
        // routes here.  The direct call below skips dispatch, but seeding
        // keeps the test consistent with the rest of the oneshot family.
        n_own[i] = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u),
            /*mask=*/0ULL);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    (void)a_val; (void)b_val; (void)n_val;
    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                    W, r_bits);

    const int peak = sturm::QubitPool::instance().high_water() - pre_in_use;
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "oneshot ancilla probe: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
    return peak;
}

int main() {
    {
        constexpr std::size_t W       = 2u;
        constexpr int        kBudget  = kOneshotAncillaSlack(static_cast<int>(W));
        std::printf("sturm-7cix/sturm-4oot.3 oneshot: peak ancilla bound "
                    "<= 3W + 7 (W=%zu, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_oneshot_for<W>(/*a=*/2u, /*b=*/2u,
                                                     /*n=*/3u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat C oneshot: peak ancilla exceeds budget at W=2");
        std::puts("  PASS: W=2 oneshot peak ancilla within budget");
    }

    {
        constexpr std::size_t W       = 3u;
        constexpr int        kBudget  = kOneshotAncillaSlack(static_cast<int>(W));
        std::printf("sturm-7cix/sturm-4oot.3 oneshot: peak ancilla bound "
                    "<= 3W + 7 (W=%zu, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_oneshot_for<W>(/*a=*/2u, /*b=*/3u,
                                                     /*n=*/5u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat C oneshot: peak ancilla exceeds budget at W=3");
        std::puts("  PASS: W=3 oneshot peak ancilla within budget");
    }

    std::printf("All sturm-7cix Beat C oneshot ancilla tests passed.\n");
    return 0;
}
