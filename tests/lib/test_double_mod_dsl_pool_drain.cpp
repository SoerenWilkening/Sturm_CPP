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
// the in-place doubling primitive.  As of sturm-4oot.2 the W=2 sweep
// covers n in {1, 2, 3} (odd-only restriction lifted under
// sturm-4oot.1) plus an even-n W=3 case (n=4) via a bypass-cap simulator
// harness and an even-n W=4 case (n=8) via the classical-trace harness;
// the algorithm's allocation footprint is parity-blind, so the LIFO
// release contract must hold uniformly.  Each (x, n) input is run
// inside its own scope and the post-call in_use() is asserted equal to
// the pre-call value, plus a LIFO-recycle witness.
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
// W=2 sweep covers n in {1, 2, 3} (1 + 2 + 3 = 6 cases) — odd-only
// restriction lifted under sturm-4oot.1, even n=2 added under
// sturm-4oot.2.  W=3 even-n sweep covers n in {2, 4, 6} (12 cases) via
// a bypass-cap simulator harness; W=4 even-n at n=8 (8 cases) via the
// classical-trace harness.  This matches the W=2/W=3 forward sweep in
// test_double_mod_dsl.cpp.  Per the issue brief, the algorithm's
// allocation footprint is data-independent (every branch allocates the
// same n_pad/carry_anc set), so this coverage is enough to catch any
// input-data-dependent leak.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/double_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
        bridge.allocate(n_q);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE);
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
// Pre: x < n.  No parity restriction (sturm-4oot.1 lifted it).
static void run_pool_drain_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
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

// ── W=3 even-n pool-drain helper (bypass-cap simulator) ─────────────────
// Mirrors run_pool_drain_case at W=3 width.  Uses orkan::allocate(...)
// directly to bypass kMaxQubits=17 (W=3 peak ~16-20 qubits).
static constexpr std::size_t W3_drain         = 3u;
static constexpr uint32_t    n_orkan_w3_drain = 21u;

struct RegX3 {
    std::array<int, W3_drain + 1u>             qi;
    std::array<sturm::qbool, W3_drain + 1u>    owners;
    std::array<sturm::BitProxy, W3_drain + 1u> bits;
};

struct RegN3 {
    std::array<int, W3_drain>              qi;
    std::array<sturm::qbool, W3_drain>     owners;
    std::array<sturm::BitProxy, W3_drain>  bits;
};

static RegX3 make_reg_x3(int base, uint32_t val, orkan::state_t& sv) {
    RegX3 r;
    for (std::size_t i = 0; i < W3_drain + 1u; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if (i < W3_drain && ((val >> i) & 1u))
            orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3_drain + 1u; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static RegN3 make_reg_n3(int base, uint32_t val, orkan::state_t& sv) {
    RegN3 r;
    for (std::size_t i = 0; i < W3_drain; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3_drain; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static void run_pool_drain_case_w3(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << W3_drain));
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W3_drain + 1u) + W3_drain + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    {
        sturm::OrkanBridge bridge;
        orkan::allocate(bridge.state(), n_orkan_w3_drain);
        sturm_backend_context_t* ctx =
            sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        sturm_backend_context_t* prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
        orkan::state_t& sv = bridge.state();

        RegX3 x  = make_reg_x3(0, x_val, sv);
        RegN3 n  = make_reg_n3(static_cast<int>(W3_drain + 1u), n_val, sv);
        RegLT lt = make_reg_lt(static_cast<int>(W3_drain + 1u + W3_drain));

        sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(),
                                                    n.bits.data(),
                                                    W3_drain, lt.bit);

        const int post_in_use = sturm::QubitPool::instance().in_use();
        if (post_in_use != pre_in_use) {
            std::fprintf(stderr,
                         "  FAIL (W=3): (x=%u, n=%u) pool leaked %d qubits\n",
                         x_val, n_val, post_in_use - pre_in_use);
        }
        assert(post_in_use == pre_in_use
               && "W=3: lib_double_mod_dsl must drain every transient");

        const uint32_t expect_x  = (2u * x_val) % n_val;
        const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
        assert(read_reg(sv, x.qi.data(), W3_drain, n_orkan_w3_drain)
                   == expect_x
               && "W=3 drain: x_bits == (2x) mod n");
        assert(read_reg(sv, x.qi.data() + W3_drain, 1u, n_orkan_w3_drain)
                   == 0u
               && "W=3 drain: x_bits[W] returned to |0>");
        assert(read_reg(sv, &lt.qi, 1u, n_orkan_w3_drain) == expect_lt
               && "W=3 drain: lt_flag_out correct");

        const int next_idx = sturm::QubitPool::instance().allocate();
        assert(next_idx == pre_in_use
               && "W=3: post-call allocate() must reuse LIFO-recycled "
                  "index == pre_in_use");
        sturm::QubitPool::instance().release(next_idx);

        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── W=4 even-n pool-drain helper (classical trace harness) ──────────────
// sturm-4oot.2: drive lib_double_mod_dsl<W=4> in APPEND mode for n=8 and
// pin the LIFO drain contract.  Pool semantics are execution-mode-
// independent (the pool is a process-level allocator), so this remains
// a meaningful drain test.
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

template <std::size_t Wn>
static void run_pool_drain_case_trace(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = static_cast<uint32_t>(Wn) + 1u
                              + static_cast<uint32_t>(Wn) + 1u;
    int qi_x[Wn + 1u], qi_n[Wn], qi_lt;
    for (std::size_t i = 0; i < Wn + 1u; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    qi_lt = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));
    {
        sturm::qbool x_own[Wn + 1u], n_own[Wn], lt_own;
        sturm::BitProxy x_bits[Wn + 1u], n_bits[Wn], lt_bit;
        for (std::size_t i = 0; i < Wn + 1u; ++i) {
            x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
            x_bits[i] = sturm::BitProxy(x_own[i]);
        }
        for (std::size_t i = 0; i < Wn; ++i) {
            n_own[i]  = sturm::qbool::make_non_owning(qi_n[i]);
            n_bits[i] = sturm::BitProxy(n_own[i]);
        }
        lt_own = sturm::qbool::make_non_owning(qi_lt);
        lt_bit = sturm::BitProxy(lt_own);

        sturm_backend_context_t* ctx =
            sturm_backend_create(STURM_MODE_APPEND);
        assert(ctx);
        sturm_backend_context_t* prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);

        sturm::lib_double_mod_dsl<sturm::BitProxy>(x_bits, n_bits, Wn, lt_bit);

        // (1) Pool drain.
        const int post_in_use = sturm::QubitPool::instance().in_use();
        if (post_in_use != pre_in_use) {
            std::fprintf(stderr,
                         "  FAIL (trace W=%zu): (x=%u, n=%u) leaked %d\n",
                         Wn, x_val, n_val, post_in_use - pre_in_use);
        }
        assert(post_in_use == pre_in_use
               && "trace drain: lib_double_mod_dsl must drain transients");

        // (2) Sanity replay.
        const int high_water = sturm::QubitPool::instance().high_water();
        std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
        for (std::size_t i = 0; i < Wn; ++i) {
            if ((x_val >> i) & 1u) bits[static_cast<std::size_t>(qi_x[i])] = 1u;
            if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        }
        for (std::size_t i = 0; i < ctx->ir.size(); ++i)
            apply_gate_classical(bits, ctx->ir.at(i));
        const uint32_t expect_x  = (2u * x_val) % n_val;
        const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
        uint32_t got_x = 0u;
        for (std::size_t k = 0; k < Wn; ++k)
            if (bits[static_cast<std::size_t>(qi_x[k])])
                got_x |= (1u << k);
        assert(got_x == expect_x && "trace drain sanity: x_bits == (2x) mod n");
        assert(bits[static_cast<std::size_t>(qi_x[Wn])] == 0u
               && "trace drain sanity: x_bits[Wn] returned to |0>");
        assert(bits[static_cast<std::size_t>(qi_lt)] == expect_lt
               && "trace drain sanity: lt_flag_out correct");

        // (3) LIFO witness.
        const int next_idx = sturm::QubitPool::instance().allocate();
        assert(next_idx == pre_in_use
               && "trace drain: post-call allocate() must reuse LIFO "
                  "index == pre_in_use");
        sturm::QubitPool::instance().release(next_idx);

        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::QubitPool::instance().release(qi_lt);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_x[i]);
}

int main() {
    std::printf("sturm-wdas/sturm-4oot.1/sturm-4oot.2 double-mod-dsl: "
                "pool live-count round-trip (W=2 exhaustive sweep, n in "
                "{1, 2, 3}, all x in [0, n); n=4 hoisted to W=3 below):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_pool_drain_case(x_val, n_val);
            ++cases_run;
        }
    }
    // n in [1, 4): n=1 → 1 case; n=2 → 2; n=3 → 3.  Total = 6.
    assert(cases_run == 6u && "pool-drain sweep covered every (x, n) "
                              "with x < n and n in {1, 2, 3}");
    std::printf("  PASS: %zu W=2 cases, every transient ancilla released "
                "LIFO, pool drain == pre_in_use\n", cases_run);

    // sturm-4oot.2 — W=3 even-n pool-drain sweep (n in {2, 4, 6}; n=8
    // doesn't fit in a 3-bit modulus register and is exercised at W=4
    // via the trace harness below).
    std::printf("sturm-4oot.2 double-mod-dsl: W=3 even-n pool-drain "
                "sweep (n in {2, 4, 6}, all x in [0, n)):\n");
    std::size_t cases_w3_even = 0u;
    for (uint32_t n_val : {2u, 4u, 6u}) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_pool_drain_case_w3(x_val, n_val);
            ++cases_w3_even;
        }
    }
    // n=2 → 2; n=4 → 4; n=6 → 6.  Total = 12.
    assert(cases_w3_even == 12u
           && "W=3 even-n pool-drain sweep covered every (x, n)");
    std::printf("  PASS: %zu W=3 even-n pool-drain cases\n", cases_w3_even);

    // sturm-4oot.2 — W=4 even-n pool-drain at n=8 via the classical-trace
    // harness, witnessing parity-blind drain at the W=4 width.
    std::printf("sturm-4oot.2 double-mod-dsl: W=4 even-n pool-drain "
                "(n=8, trace harness):\n");
    {
        constexpr uint32_t n_val = 8u;
        std::size_t cases_w4 = 0u;
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_pool_drain_case_trace<4u>(x_val, n_val);
            ++cases_w4;
        }
        assert(cases_w4 == 8u);
        std::printf("  PASS: %zu W=4 trace pool-drain cases at n=8\n",
                    cases_w4);
    }

    std::printf("All sturm-wdas/sturm-4oot.1/sturm-4oot.2 double-mod-dsl "
                "pool-drain tests passed.\n");
    return 0;
}
