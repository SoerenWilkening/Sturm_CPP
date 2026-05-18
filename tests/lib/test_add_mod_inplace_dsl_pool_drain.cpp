// test_add_mod_inplace_dsl_pool_drain.cpp -- sturm-8lnp Beat A pool
//                                              live-count round-trip /
//                                              LIFO release test.
//
// `QubitPool::instance().in_use()` (the runtime equivalent of "live_count")
// must return to its pre-call value after every input the W=2 exhaustive
// sweep covers — i.e. all transient ancillas are released and LIFO release
// order is correct.
//
// Mirrors the dedicated-test style of `tests/backend/test_lossy_ancilla_cleaned.cpp`
// (sturm-qoq0) and `tests/lib/test_add_mod_dsl_pool_drain.cpp` (sturm-yh3d.7):
// every (a, dest_old, n) input is run through `lib_add_mod_inplace_dsl<W>`
// inside its own scope, and the post-call `QubitPool::instance().in_use()`
// is asserted equal to the pre-call value.
//
// Why a separate test from the forward / adjoint / ancilla siblings
// -----------------------------------------------------------------
// The existing forward/adjoint tests already include in_use() round-trip
// checks alongside the correctness asserts; this dedicated drain test
// pins the LIFO release contract independently.  When a future regression
// breaks the release path (e.g. a missing `QubitPool::release(...)` in
// the algorithm tail, or a non-LIFO release order that happens to balance
// counts but corrupts the free-list ordering relative to allocation),
// this test is the first signal — the forward/adjoint tests would still
// detect a counter mismatch but the LIFO-specific signal would be lost
// in noise.  Mirrors how `test_lossy_ancilla_cleaned` pins drainage of
// the lossy-op runtime tails independently of `test_lossy_ancilla_*`
// correctness fixtures.
//
// LIFO release verification technique
// -----------------------------------
// LIFO release is what keeps the free-list reuse pattern stable: indices
// allocated last are released first, so the next allocate() returns the
// most-recently-released index.  We pin this by asserting that immediately
// after a `lib_add_mod_inplace_dsl` call returns, an `allocate()` reuses
// the index the algorithm released LAST during its internal LIFO unwind.
// The algorithm releases in this order (see add_mod_inplace_dsl.hpp's
// step (10) / its trailer): carry_anc, lt_flag, n_pad — so the last
// release is `n_pad`'s index.  Because n_pad was the FIRST ancilla
// allocated (right after the input n_reg=3W+1 reservations), its index
// equals `pre_in_use` = `n_reg`.  After the call, the next pool
// `allocate()` must therefore return that same index.  Combined with the
// `in_use() == pre_in_use` assert, this pins both:
//   (a) every transient ancilla was released, and
//   (b) the release order was LIFO.
//
// Coverage shape
// --------------
// The 14-case W=2 sweep matches the forward beats exactly (all (a,
// dest_old) with a, dest_old in [0, n) for n in [1, 4)).  The algorithm's
// allocation footprint is data-independent (every branch allocates the
// same n_pad/lt_flag/carry_anc set), so 14 distinct (a, dest_old, n)
// inputs exercise the fully-controlled add-back branch (lt_flag=1) and
// the no-op branch (lt_flag=0) without further parameterisation.
//
// sturm-a3e9: the W=2 exhaustive sweep now runs through the shared
// APPEND+classical-replay harness from tests/lib/classical_replay.hpp,
// matching the migration that sturm-scin applied to the forward test.
// Pool semantics are execution-mode-independent (the pool is a
// process-level allocator), so the drain / LIFO contract holds
// identically under APPEND.  Per-case cost drops from O(2^n_orkan *
// |IR|) to O(|IR|), bringing wall-clock from ~1 s to <1 s with
// headroom.  One SIMULATE smoke ((a=1, dest=1, n=3)) is retained for
// end-to-end statevector coverage of the drain contract alongside the
// algorithm's amplitudes.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/add_mod_inplace_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include "classical_replay.hpp"  // sturm-a3e9: shared APPEND+replay helper

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// Sized at the kMaxQubits=17 cap (matches forward W=2 sweep).  Leaves
// enough headroom for the algorithm's transient ~5 ancilla footprint
// above 3W+1 = 7 input qubits.
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

struct RegA {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

struct RegDest {
    std::array<int, W + 1u>             qi;
    std::array<sturm::qbool, W + 1u>    owners;
    std::array<sturm::BitProxy, W + 1u> bits;
};

struct RegN {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

static RegA make_reg_a(int base, uint32_t val, orkan::state_t& sv) {
    RegA r;
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

static RegDest make_reg_dest(int base, uint32_t val, orkan::state_t& sv) {
    RegDest r;
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

// SIMULATE-mode pool-drain smoke for (a=1, dest_old=1, n=3).  Retained
// as a single SIMULATE drain witness alongside the algorithm's amplitude
// evolution; the sweep coverage runs through the APPEND+replay driver
// below.
static void run_pool_drain_case_sim(uint32_t a_val, uint32_t dest_val,
                                    uint32_t n_val) {
    assert(a_val    < n_val && "test precondition: a < n");
    assert(dest_val < n_val && "test precondition: dest_old < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 3u * W + 1u;  // a (W) + dest (W+1) + n (W)
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    {
        SimCtx sc{n_orkan, 128u};
        RegA    a    = make_reg_a(0,                              a_val,    sc.sv());
        RegDest dest = make_reg_dest(static_cast<int>(W),         dest_val, sc.sv());
        RegN    n    = make_reg_n(static_cast<int>(W + (W + 1u)), n_val,    sc.sv());

        sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a.bits.data(),
                                                        dest.bits.data(),
                                                        n.bits.data(), W);

        const int post_in_use = sturm::QubitPool::instance().in_use();
        if (post_in_use != pre_in_use) {
            std::fprintf(stderr,
                         "  FAIL: (a=%u, dest=%u, n=%u) pool leaked %d "
                         "qubits (pre=%d, post=%d)\n",
                         a_val, dest_val, n_val, post_in_use - pre_in_use,
                         pre_in_use, post_in_use);
        }
        assert(post_in_use == pre_in_use
               && "lib_add_mod_inplace_dsl must drain every transient ancilla");

        const uint32_t expect_dest = (dest_val + a_val) % n_val;
        uint32_t dest_low = read_reg(sc.sv(), dest.qi.data(),     W,  n_orkan);
        uint32_t dest_top = read_reg(sc.sv(), dest.qi.data() + W, 1u, n_orkan);
        assert(dest_low == expect_dest
               && "drain test sanity: algorithm produced wrong dest");
        assert(dest_top == 0u
               && "drain test sanity: dest[W] not |0> after call");

        const int next_idx = sturm::QubitPool::instance().allocate();
        assert(next_idx == pre_in_use
               && "post-call allocate() must reuse LIFO-recycled index "
                  "== pre_in_use, witnessing LIFO release order");
        sturm::QubitPool::instance().release(next_idx);
    }
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── APPEND+replay pool-drain driver (sturm-a3e9) ─────────────────────────
//
// Runs lib_add_mod_inplace_dsl<W_VAL> inside a single STURM_MODE_APPEND
// context and pins the same three contracts as the SIMULATE smoke:
//   (1) post_in_use == pre_in_use   (drain),
//   (2) correctness via classical replay of the captured IR,
//   (3) next allocate() returns pre_in_use   (LIFO recycling).
//
// Pool semantics are execution-mode-independent (the pool is a
// process-level allocator), so this is a meaningful drain witness at
// every W_VAL.
template <std::size_t W_VAL>
static void run_replay_pool_drain_case(uint32_t a_val, uint32_t dest_val,
                                       uint32_t n_val) {
    assert(a_val    < n_val && "test precondition: a < n");
    assert(dest_val < n_val && "test precondition: dest_old < n");
    assert(n_val < (1u << W_VAL) && "test precondition: n fits in W_VAL bits");
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg_a    = static_cast<uint32_t>(W_VAL);
    constexpr uint32_t n_reg_dest = static_cast<uint32_t>(W_VAL) + 1u;
    constexpr uint32_t n_reg_n    = static_cast<uint32_t>(W_VAL);
    constexpr uint32_t n_reg      = n_reg_a + n_reg_dest + n_reg_n;
    int qi_a[W_VAL], qi_dest[W_VAL + 1u], qi_n[W_VAL];
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL + 1u; ++i)
        qi_dest[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));
    {
        sturm::qbool a_own[W_VAL], dest_own[W_VAL + 1u], n_own[W_VAL];
        sturm::BitProxy a_bits[W_VAL], dest_bits[W_VAL + 1u], n_bits[W_VAL];
        for (std::size_t i = 0; i < W_VAL; ++i) {
            a_own[i]  = sturm::qbool::make_non_owning(qi_a[i]);
            a_bits[i] = sturm::BitProxy(a_own[i]);
            n_own[i]  = sturm::qbool::make_non_owning(qi_n[i]);
            n_bits[i] = sturm::BitProxy(n_own[i]);
        }
        for (std::size_t i = 0; i < W_VAL + 1u; ++i) {
            dest_own[i]  = sturm::qbool::make_non_owning(qi_dest[i]);
            dest_bits[i] = sturm::BitProxy(dest_own[i]);
        }

        sturm::test_helpers::AppendContext app;

        sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a_bits, dest_bits,
                                                        n_bits, W_VAL);

        // (1) Pool drain.
        const int post_in_use = sturm::QubitPool::instance().in_use();
        if (post_in_use != pre_in_use) {
            std::fprintf(stderr,
                         "  FAIL (replay W=%zu): (a=%u, dest=%u, n=%u) "
                         "leaked %d\n",
                         W_VAL, a_val, dest_val, n_val,
                         post_in_use - pre_in_use);
        }
        assert(post_in_use == pre_in_use
               && "replay drain: lib_add_mod_inplace_dsl must drain transients");

        // (2) Sanity replay.
        const int high_water = sturm::QubitPool::instance().high_water();
        std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
        for (std::size_t i = 0; i < W_VAL; ++i) {
            if ((a_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])]    = 1u;
            if ((dest_val >> i) & 1u) bits[static_cast<std::size_t>(qi_dest[i])] = 1u;
            if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
        }
        sturm::test_helpers::replay_ir(app.ctx()->ir, bits);
        const uint32_t expect_dest = (dest_val + a_val) % n_val;
        using sturm::test_helpers::read_reg_classical;
        assert(read_reg_classical(bits, qi_a, W_VAL) == a_val
               && "replay drain sanity: a register unchanged");
        assert(read_reg_classical(bits, qi_dest, W_VAL) == expect_dest
               && "replay drain sanity: dest == (dest_old + a) mod n");
        assert(bits[static_cast<std::size_t>(qi_dest[W_VAL])] == 0u
               && "replay drain sanity: dest[W_VAL] returned to |0>");
        assert(read_reg_classical(bits, qi_n, W_VAL) == n_val
               && "replay drain sanity: n register unchanged");

        // (3) LIFO witness.
        const int next_idx = sturm::QubitPool::instance().allocate();
        assert(next_idx == pre_in_use
               && "replay drain: post-call allocate() must reuse LIFO "
                  "index == pre_in_use");
        sturm::QubitPool::instance().release(next_idx);
    }
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W_VAL + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_dest[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    // sturm-a3e9 — SIMULATE pool-drain smoke for (a=1, dest=1, n=3).
    // Retains a SIMULATE drain witness alongside the algorithm's
    // amplitude evolution; the W=2 sweep below moves to APPEND+replay.
    std::printf("sturm-8lnp Beat A: pool live-count round-trip "
                "(SIMULATE smoke: a=1, dest=1, n=3):\n");
    run_pool_drain_case_sim(/*a=*/1u, /*dest=*/1u, /*n=*/3u);
    std::puts("  PASS: SIMULATE drain on (1+1) mod 3 = 2 (in_use restored, "
              "LIFO recycle witness)");

    std::printf("sturm-8lnp/sturm-a3e9 Beat A: pool live-count round-trip "
                "(W=2 exhaustive sweep, all (a, dest_old) in [0, n) for n "
                "in [1, 4); APPEND+replay):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t dest_val = 0u; dest_val < n_val; ++dest_val) {
            for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
                run_replay_pool_drain_case<W>(a_val, dest_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 -> 1 case; n=2 -> 4 cases; n=3 -> 9 cases; total = 14 cases.
    assert(cases_run == 14u && "pool-drain sweep covered every (a, dest_old, "
                                "n) with a, dest_old < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases, every transient ancilla released "
                "LIFO, pool drain == pre_in_use\n", cases_run);

    std::printf("All sturm-8lnp/sturm-a3e9 Beat A pool-drain tests passed.\n");
    return 0;
}
