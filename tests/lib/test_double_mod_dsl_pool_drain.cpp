// test_double_mod_dsl_pool_drain.cpp -- Beat B (sturm-wdas) pool live-count
//                                         round-trip / LIFO release test,
//                                         post sturm-4oot.1 API rewrite.
//
// `QubitPool::instance().in_use()` must return to its pre-call value after
// every Beat-`sturm-wdas` input — i.e. all transient ancillas are released.
// LIFO release order must be correct (the next allocate after the call
// reuses the most-recently-released index).
//
// Mirrors `tests/lib/test_add_mod_dsl_pool_drain.cpp` (sturm-yh3d.7) for
// the in-place doubling primitive.  The W=2 sweep restricts to odd n
// (n in {1, 3}) because the issue scope here is the API rewrite only;
// sturm-4oot.2 extends to even n.  Each (x, n) input is run inside its
// own scope and the post-call in_use() is asserted equal to the pre-call
// value, plus a LIFO-recycle witness.
//
// LIFO release verification technique
// -----------------------------------
// Under sturm-4oot.1, the forward primitive releases ancillas in this
// order (see double_mod_dsl.hpp step (9)): carry_anc, n_pad.  The LAST
// release is `n_pad`'s index, which equals `pre_in_use` (the first
// index allocated after the input registers).  After the call, the
// next pool `allocate()` must therefore return `pre_in_use`.  Combined
// with the `in_use() == pre_in_use` assert, this pins both
//   (a) every transient ancilla was released, and
//   (b) the release order was LIFO.
// The previously-internal `lt_flag` is no longer in this LIFO chain
// (sturm-4oot.1 externalised it as `lt_flag_out`).
//
// Coverage shape
// --------------
// W=2 odd-n sweep covers n in {1, 3} (1 + 3 = 4 cases).  This matches
// the W=2 forward sweep in test_double_mod_dsl.cpp.  Per the issue brief,
// running 4 lib_double_mod_dsl calls inside fresh scopes is enough to
// catch any input-data-dependent leak.  The algorithm's allocation
// footprint is data-independent (every branch allocates the same
// n_pad/carry_anc set).

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
static constexpr std::size_t W = 2;
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
        bridge.allocate(n_q);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

static uint32_t read_reg(orkan::state_t& sv, const int* qi, uint32_t n,
                         uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t v = 0u;
            for (uint32_t k = 0; k < n; ++k)
                if (qi[k] >= 0)
                    v |= (static_cast<uint32_t>((s >> qi[k]) & 1u) << k);
            return v;
        }
    }
    return 0u;
}

struct RegX {
    std::array<int, W + 1u>             qi;
    std::array<sturm::qbool, W + 1u>    owners;
    std::array<sturm::BitProxy, W + 1u> bits;
};

struct RegN {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

struct RegLT {
    int                qi;
    sturm::qbool       own;
    sturm::BitProxy    bit;
};

static RegX make_reg_x(int base, uint32_t val, orkan::state_t& sv) {
    RegX r;
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

static RegN make_reg_n(int base, uint32_t val, orkan::state_t& sv) {
    RegN r;
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

static RegLT make_reg_lt(int qi_base) {
    RegLT r;
    r.qi  = qi_base;
    r.own = sturm::qbool::make_non_owning(qi_base);
    r.bit = sturm::BitProxy(r.own);
    return r;
}

// Run lib_double_mod_dsl<W> for one (x, n) input inside its own scope, and
// pin the post-call pool live-count and LIFO-release contract:
//   1. pool drain: post_in_use == pre_in_use (every transient released)
//   2. peek-and-release the next allocate() returns pre_in_use, witnessing
//      LIFO recycling order.
// Pre: x < n, n odd.
static void run_pool_drain_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
    assert((n_val & 1u) == 1u && "test precondition: n is odd");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    {
        // Inner scope: SimCtx, the two Regs, the lt_flag_out, and every
        // Bit/qbool view they own go out of scope at the closing brace.
        // We measure the pool state INSIDE this scope (right after the
        // call), which is the strongest assertion: the algorithm itself
        // must drain its transients before returning, not rely on
        // caller-side destructor cleanup.
        SimCtx sc{n_orkan, 128u};
        RegX x   = make_reg_x(0, x_val, sc.sv());
        RegN n   = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());
        RegLT lt = make_reg_lt(static_cast<int>(W + 1u + W));

        sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(),
                                                    n.bits.data(), W, lt.bit);

        // (1) Pool live-count returns to its pre-call value.
        const int post_in_use = sturm::QubitPool::instance().in_use();
        if (post_in_use != pre_in_use) {
            std::fprintf(stderr,
                         "  FAIL: (x=%u, n=%u) pool leaked %d qubits "
                         "(pre=%d, post=%d)\n",
                         x_val, n_val, post_in_use - pre_in_use,
                         pre_in_use, post_in_use);
        }
        assert(post_in_use == pre_in_use
               && "lib_double_mod_dsl must drain every transient ancilla");

        // (2) Sanity: correctness alongside the drain pin (including
        //     the new lt_flag_out contract).
        const uint32_t expect_x  = (2u * x_val) % n_val;
        const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
        uint32_t x_low = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
        uint32_t x_top = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
        uint32_t lt_sv = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
        assert(x_low == expect_x
               && "drain test sanity: algorithm produced wrong x_bits");
        assert(x_top == 0u
               && "drain test sanity: x_bits[W] not |0> after call");
        assert(lt_sv == expect_lt
               && "drain test sanity: lt_flag_out wrong");

        // (3) LIFO-release witness: the next allocate() must return
        //     pre_in_use (the first index allocated after the n_reg
        //     reserved inputs).  Any non-LIFO release order would have
        //     left a different free-list ordering and `next` would land
        //     on a higher index.
        const int next_idx = sturm::QubitPool::instance().allocate();
        assert(next_idx == pre_in_use
               && "post-call allocate() must reuse LIFO-recycled index "
                  "== pre_in_use, witnessing LIFO release order");
        sturm::QubitPool::instance().release(next_idx);
    }
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-wdas/sturm-4oot.1 double-mod-dsl: pool live-count "
                "round-trip (W=2 exhaustive sweep, odd n in [1, 4): "
                "n in {1, 3}, all x in [0, n)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); n_val += 2u) {  // odd-only
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_pool_drain_case(x_val, n_val);
            ++cases_run;
        }
    }
    // odd n in [1, 4): n=1 → 1 case; n=3 → 3 cases.  Total = 4.
    assert(cases_run == 4u && "pool-drain sweep covered every (x, n) "
                              "with x < n and n odd");
    std::printf("  PASS: %zu W=2 cases, every transient ancilla released "
                "LIFO, pool drain == pre_in_use\n", cases_run);

    std::printf("All sturm-wdas/sturm-4oot.1 double-mod-dsl pool-drain tests "
                "passed.\n");
    return 0;
}
