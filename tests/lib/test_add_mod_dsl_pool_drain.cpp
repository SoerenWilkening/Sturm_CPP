// test_add_mod_dsl_pool_drain.cpp -- sturm-yh3d.7 P1.7 add-mod-dsl 1.7
//                                     pool live-count round-trip.
//
// Plan §3.3 row 1.7 wording: "QubitPool::instance().live_count() returns to
// its pre-call value after every Beat-1.3 input — i.e. all transient
// ancillas are released. LIFO release correct."
//
// Mirrors the dedicated-test style of `tests/backend/test_lossy_ancilla_cleaned.cpp`
// (sturm-qoq0): every (a, b, n) input that beat 1.3 covered (the exhaustive
// W=2 sweep) is run through `lib_add_mod_dsl<W>` inside its own scope, and
// the post-call `QubitPool::instance().in_use()` is asserted equal to the
// pre-call value.  `in_use()` is the runtime equivalent of "live_count" for
// this codebase (see qubit_pool.hpp lines 100-103) — the pool exposes
// `in_use()` directly; "live count" is the spec-level synonym.
//
// Why a separate test from beats 1.2-1.5
// --------------------------------------
// The existing `test_add_mod_dsl.cpp` and `test_add_mod_dsl_adjoint.cpp`
// already include in_use() round-trip checks alongside the correctness
// asserts; this beat 1.7 dedicates a test whose ONLY pin is the LIFO
// release contract.  When a future regression breaks the release path
// (e.g. a missing `QubitPool::release(...)` in the algorithm tail, or a
// non-LIFO release order that happens to balance counts but corrupts the
// free-list ordering relative to allocation), this test is the first
// signal — beats 1.2-1.5 would still detect a counter mismatch, but they
// would also fail on correctness/adjoint asserts and the LIFO-specific
// signal would be lost in noise.  Mirrors how `test_lossy_ancilla_cleaned`
// pins drainage of the lossy-op runtime tails independently of
// `test_lossy_ancilla_*` correctness fixtures.
//
// LIFO release verification technique
// -----------------------------------
// LIFO release is what keeps the free-list reuse pattern stable: indices
// allocated last are released first, so the next allocate() returns the
// most-recently-released index.  We pin this by asserting that immediately
// after a `lib_add_mod_dsl` call returns, an `allocate()` followed by a
// `release()` reuses the index the algorithm released LAST during its
// internal LIFO unwind.  The algorithm releases in this order (see
// add_mod_dsl.hpp lines 197-202): carry_anc, lt_flag, n_pad, then s[n], …,
// s[0] — so the last release is `s[0]`'s index, which equals `s_idx[0]`
// (the first ancilla allocated, i.e. `pre_in_use`).  After the call, the
// next pool `allocate()` must therefore return that same index.  Combined
// with the `in_use() == pre_in_use` assert, this pins both
// (a) every transient ancilla was released, and
// (b) the release order was LIFO.
//
// Coverage shape
// --------------
// The 14-case W=2 sweep matches beat 1.3 exactly (all (a, b) with a, b in
// [0, n) for n in [1, 4)).  Per plan §3.3 row 1.7, the assertion is "every
// Beat-1.3 input"; running 14 lib_add_mod_dsl calls inside fresh scopes is
// enough to catch any input-data-dependent leak.  The algorithm's
// allocation footprint is data-independent (every branch allocates the
// same s/n_pad/lt_flag/carry_anc set), so 14 distinct (a, b, n) inputs
// exercise the fully-controlled add-back branch (lt_flag=1) and the no-op
// branch (lt_flag=0) without further parameterisation.
//
// Budget: <= 250 LoC.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/add_mod_dsl.hpp"
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
// Sized at the kMaxQubits=17 cap (matches test_add_mod_dsl beat 1.2/1.3
// W=2 sweep).  Leaves enough headroom for the algorithm's transient
// W + 6 ancilla footprint above 4·W = 8 input qubits.
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

struct Reg {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

static Reg make_reg(int base, uint32_t val, orkan::state_t& sv) {
    Reg r;
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

// Run lib_add_mod_dsl<W> for one (a, b, n) input inside its own scope, and
// pin the post-call pool live-count and LIFO-release contract:
//   1. pool_drain   == pre_in_use   (every transient ancilla released)
//   2. peek-and-release the next allocate() returns an index <= pre's last
//      reserved index, witnessing LIFO recycling order.
//
// The scope-exit semantics of `SimCtx` are exercised exactly as the
// `run_block_and_assert_drained` lambda body in test_lossy_ancilla_cleaned:
// the inner block destructs all qbool-owners and then we measure.
static void run_pool_drain_case(uint32_t a_val, uint32_t b_val,
                                uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // a, b, n, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    {
        // Inner C++ scope: SimCtx, the four Regs, and every Bit/qbool view
        // they own go out of scope at the closing brace.  Per the dedicated
        // pool-drain pattern, we measure the pool state INSIDE this scope
        // (right after the call), which is the strongest assertion: the
        // algorithm itself must drain its transients before returning, not
        // rely on caller-side destructor cleanup.
        SimCtx sc{n_orkan, 128u};
        Reg a = make_reg(0,         a_val, sc.sv());
        Reg b = make_reg(W,         b_val, sc.sv());
        Reg n = make_reg(2 * W,     n_val, sc.sv());
        Reg r = make_reg(3 * W,     0u,    sc.sv());

        sturm::lib_add_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                                n.bits.data(), W,
                                                r.bits.data());

        // (1) Pool live-count returns to its pre-call value: every transient
        //     ancilla allocated by the algorithm has been released back.
        const int post_in_use = sturm::QubitPool::instance().in_use();
        if (post_in_use != pre_in_use) {
            std::fprintf(stderr,
                         "  FAIL: (a=%u, b=%u, n=%u) pool leaked %d qubits "
                         "(pre=%d, post=%d)\n",
                         a_val, b_val, n_val, post_in_use - pre_in_use,
                         pre_in_use, post_in_use);
        }
        assert(post_in_use == pre_in_use
               && "lib_add_mod_dsl must drain every transient ancilla");

        // (2) Sanity check that the algorithm produced the right answer —
        //     a leak-clean call that returned the wrong value would still
        //     fool a counter-only assertion, so we keep the correctness
        //     pin alongside the drain pin (mirrors the
        //     `run_block_and_assert_drained` body's tacit reliance on the
        //     called op being correct).
        const uint32_t expect_r = (a_val + b_val) % n_val;
        uint32_t r_sv = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
        assert(r_sv == expect_r
               && "drain test sanity: algorithm produced wrong r");

        // (3) LIFO-release witness: the next allocate() must reuse one of
        //     the indices the algorithm released — which means the index
        //     must be strictly less than `pre_high_water + algorithm_peak`
        //     and at minimum equal to one of the recently-released slots
        //     in the free-list.  Concretely, after a fully-LIFO-clean call
        //     the next allocate() returns the LAST-released index, which
        //     is the FIRST allocated after pre_in_use (i.e. equal to
        //     pre_in_use, the next slot beyond the n_reg=8 reserved
        //     inputs).  Asserting `next == pre_in_use` is the strongest
        //     LIFO pin: any non-LIFO release order would have left a
        //     different ordering on the free-list and `next` would land
        //     on a higher index.
        const int next_idx = sturm::QubitPool::instance().allocate();
        assert(next_idx == pre_in_use
               && "post-call allocate() must reuse LIFO-recycled index "
                  "== pre_in_use, witnessing LIFO release order");
        sturm::QubitPool::instance().release(next_idx);
    }
    // After the scope, the input qbools (owners array) are destructed but
    // they were created via `qbool::make_non_owning(...)` and so do NOT
    // release their underlying indices on destruction.  The reserved[]
    // indices we allocated explicitly therefore remain in_use; we release
    // them by hand to keep the QubitPool clean for the next iteration.
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-yh3d.7 P1.7 add-mod-dsl: pool live-count round-trip "
                "(W=2 exhaustive sweep, all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_pool_drain_case(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 -> 1 case; n=2 -> 4 cases; n=3 -> 9 cases; total = 14 cases
    // (matches beat 1.3 exactly).
    assert(cases_run == 14u && "pool-drain sweep covered every (a, b, n) "
                                "with a, b < n and n >= 1 (matches beat 1.3)");
    std::printf("  PASS: %zu W=2 cases, every transient ancilla released "
                "LIFO, pool drain == pre_in_use\n", cases_run);

    std::printf("All sturm-yh3d.7 tests passed.\n");
    return 0;
}
