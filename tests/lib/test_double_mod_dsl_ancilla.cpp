// test_double_mod_dsl_ancilla.cpp -- Beat B (sturm-wdas) ancilla budget.
//
// The forward `lib_double_mod_dsl<W>` peak ancilla footprint above the
// `(W+1) + W = 2W+1` input qubits is bounded by a constant: there is no
// per-bit scratch chain (unlike chain-style mul_mod / pow_mod), and the
// shifted view `s_view` is a pure relabel of x_bits — it allocates zero
// fresh qubits.  The auxiliary ancillas allocated directly inside the
// algorithm are:
//
//   • `n_pad` (1) — zero-padding qubit for the (W+1)-bit n_extended view.
//   • `lt_flag` (1) — comparison bit set by the trial-subtract.
//   • `carry_anc` (1) — overflow sink for the conditional add-back.
//
// Plus the transient ancillas that the inner adder primitives allocate
// during the conditional reduction:
//
//   • Each `lib_add_dsl` / `detail_div::lib_add_adj` call inside the
//     algorithm allocates its own 1-qubit carry_anc transient (see
//     adder_dsl.hpp line 117 / div_dsl_adj.hpp line 49).
//   • The `lift_under(lt_flag)` body inside step (5) of double_mod_dsl
//     pushes lt_flag as a control and then calls `lib_add_dsl`.  Inside
//     that call, `maj_dsl` / `uma_dsl` execute `c ^= (a & b)` which
//     dispatches through `emit_CCX_lifted`; with a non-empty control
//     stack, that primitive allocates a fold ancilla per CCX (qbool_ops.hpp
//     line 101) — the fold ancilla is released before the next CCX, but
//     it is live simultaneously with everything above.
//
// So the *measured* peak (high-water minus pre-call in_use) at any W is:
//   1 (n_pad) + 1 (lt_flag) + 1 (carry_anc)
//        + 1 (inner adder carry_anc) + 1 (CCX fold ancilla under lt_flag)
//   = 5
//
// This is **independent of W** — the algorithm allocates a constant
// number of ancillas above its inputs (the (W+1)-bit shifted view is a
// relabel of x_bits, not a fresh allocation, and the SWAP-based rotation
// at step 7 of the forward emits CNOT triplets without any further
// allocation).  PRD §6.4 `W + O(1)` is comfortably met.  This test pins
// the achieved bound (constant 5 above 2W+1 inputs) so any future
// regression in the algorithm's ancilla footprint trips immediately.
//
// Measurement technique mirrors test_add_mod_dsl_ancilla.cpp: snapshot
// QubitPool::high_water() after the call to get the maximum number of
// distinct qubit indices ever live during the run, then subtract
// pre-call in_use to isolate the algorithm's peak transient footprint.
//
// Asserts at W = 2 and W = 3.  W = 3 bypasses OrkanBridge::allocate's
// kMaxQubits=17 cap by calling `orkan::allocate(bridge.state(),
// n_orkan_w3)` directly — same workaround as the forward W=3 sweep.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/double_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// Achieved peak ancilla footprint of `lib_double_mod_dsl<W>`, in qubits
// above the (2W+1) input registers.  See the file-header analysis: the
// algorithm allocates n_pad (1) + lt_flag (1) + carry_anc (1) directly,
// plus the inner adder carry_anc (1) and the emit_CCX_lifted fold ancilla
// under lift_under(lt_flag) (1) at the algorithm's peak step.  Total = 5.
//
// PRD `W + O(1)` requirement is met (5 is independent of W).
static constexpr int kDoubleModAncillaBudget = 5;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u, bool bypass = false) {
        if (bypass) {
            orkan::allocate(bridge.state(), n_q);  // bypass kMaxQubits cap
        } else {
            bridge.allocate(n_q);
        }
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

template <std::size_t W>
struct RegXT {
    std::array<int, W + 1u>             qi;
    std::array<sturm::qbool, W + 1u>    owners;
    std::array<sturm::BitProxy, W + 1u> bits;
};

template <std::size_t W>
struct RegNT {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

template <std::size_t W>
static RegXT<W> make_reg_x(int base, uint32_t val, orkan::state_t& sv) {
    RegXT<W> r;
    for (std::size_t i = 0; i < W + 1u; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if (i < W && ((val >> i) & 1u))
            orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W + 1u; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

template <std::size_t W>
static RegNT<W> make_reg_n(int base, uint32_t val, orkan::state_t& sv) {
    RegNT<W> r;
    for (std::size_t i = 0; i < W; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

// Run lib_double_mod_dsl<W> for a single (x, n) with x < n and n odd, and
// report the peak live-ancilla count above the caller's pre-existing
// inputs.  The "mid-call max" snapshot is captured via QubitPool::high_water()
// — see file-header for the technique.  Also asserts the call's behavioural
// post-conditions (x_bits == (2x) mod n, x_bits[W] == 0, n preserved, pool
// LIFO-clean) so a regression in the algorithm cannot pass an ancilla-only
// assertion.
template <std::size_t W>
static int peak_ancilla_for(uint32_t x_val, uint32_t n_val,
                            uint32_t n_orkan, bool bypass_cap) {
    assert(x_val < n_val && "test precondition: x < n");
    assert((n_val & 1u) == 1u && "test precondition: n is odd");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W;
    int reserved[32];
    assert(n_reg <= 32u);
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    {
        SimCtx sc{n_orkan, 128u, bypass_cap};
        RegXT<W> x = make_reg_x<W>(0, x_val, sc.sv());
        RegNT<W> n = make_reg_n<W>(static_cast<int>(W + 1u), n_val, sc.sv());

        sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(),
                                                    n.bits.data(), W);

        // Sanity check: the call must produce the correct x_bits and leave
        // n_bits untouched, otherwise the ancilla number is meaningless.
        const uint32_t expect_x = (2u * x_val) % n_val;
        uint64_t dim = uint64_t{1} << n_orkan;
        auto read_low = [&](const int* qi) {
            for (uint64_t s = 0; s < dim; ++s) {
                if (std::norm(orkan::amplitude(sc.sv(), s)) > kTol) {
                    uint32_t v = 0u;
                    for (uint32_t k = 0; k < W; ++k)
                        v |= (static_cast<uint32_t>((s >> qi[k]) & 1u) << k);
                    return v;
                }
            }
            return 0u;
        };
        auto read_top = [&](const int* qi) {
            for (uint64_t s = 0; s < dim; ++s) {
                if (std::norm(orkan::amplitude(sc.sv(), s)) > kTol) {
                    return static_cast<uint32_t>((s >> qi[W]) & 1u);
                }
            }
            return 0u;
        };
        assert(read_low(x.qi.data()) == expect_x
               && "ancilla probe: x_bits == (2x) mod n");
        assert(read_top(x.qi.data()) == 0u
               && "ancilla probe: x_bits[W] returned to |0>");
        assert(read_low(n.qi.data()) == n_val
               && "ancilla probe: n_bits unchanged");
        assert(sturm::QubitPool::instance().in_use() == pre_in_use
               && "ancilla probe: pool live-count returns to pre-call value");
    }

    // After the call, high_water captures the maximum number of distinct
    // qubit indices ever issued during the run (transient sub-primitive
    // allocations included).  Subtracting the pre-call in_use leaves the
    // algorithm's own peak ancilla footprint.
    const int peak = sturm::QubitPool::instance().high_water() - pre_in_use;

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
    return peak;
}

int main() {
    // ── W = 2 ────────────────────────────────────────────────────────────
    {
        constexpr std::size_t W       = 2u;
        constexpr int        kBudget  = kDoubleModAncillaBudget;
        std::printf("sturm-wdas double-mod-dsl: peak ancilla bound "
                    "<= %d (W=%zu, budget=%d):\n",
                    kBudget, W, kBudget);
        const int peak = peak_ancilla_for<W>(/*x=*/1u, /*n=*/3u,
                                             /*n_orkan=*/17u,
                                             /*bypass_cap=*/false);
        std::printf("  measured peak = %d ancillas above %zu inputs\n",
                    peak, (W + 1u) + W);
        assert(peak <= kBudget
               && "Beat sturm-wdas: peak ancilla exceeds budget at W=2");
        std::puts("  PASS: W=2 peak ancilla within achieved budget");
    }

    // ── W = 3 ────────────────────────────────────────────────────────────
    {
        constexpr std::size_t W       = 3u;
        constexpr int        kBudget  = kDoubleModAncillaBudget;
        std::printf("sturm-wdas double-mod-dsl: peak ancilla bound "
                    "<= %d (W=%zu, budget=%d):\n",
                    kBudget, W, kBudget);
        const int peak = peak_ancilla_for<W>(/*x=*/2u, /*n=*/5u,
                                             /*n_orkan=*/21u,
                                             /*bypass_cap=*/true);
        std::printf("  measured peak = %d ancillas above %zu inputs\n",
                    peak, (W + 1u) + W);
        assert(peak <= kBudget
               && "Beat sturm-wdas: peak ancilla exceeds budget at W=3");
        std::puts("  PASS: W=3 peak ancilla within achieved budget");
    }

    std::printf("All sturm-wdas double-mod-dsl ancilla tests passed.\n");
    return 0;
}
