// test_add_mod_inplace_dsl_adjoint.cpp -- sturm-8lnp Beat A adjoint
//                                          round-trip test.
//
// Forward `lib_add_mod_inplace_dsl(a, dest, n, W)` followed by
// `__lib_add_mod_inplace_dsl_adj(a, dest, n, W)` must restore `dest_bits`
// to its pre-forward value for every (a, dest_old, n) input the W=2
// exhaustive sweep covers.  Inputs `a, n` must remain unchanged across
// the full forward+adjoint pair, and `QubitPool::in_use()` must return
// to its pre-call value (no leaked ancillas).
//
// Mirrors `tests/lib/test_add_mod_dsl_adjoint.cpp`'s pattern:
// (1) drive the forward, (2) verify outputs match the classical reference,
// (3) resolve `invert<&lib_add_mod_inplace_dsl<BitProxy>>()` (compile-time
//     check that STURM_REGISTER_ADJOINT wired the pair),
// (4) call the adjoint, (5) assert dest is back to its pre-forward value
//     and a/n are intact.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/add_mod_inplace_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
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
// Sized at the kMaxQubits=17 cap (matches test_add_mod_inplace_dsl forward
// sweep).  Forward+adjoint runs back-to-back inside one SimCtx; LIFO
// release between calls means peak live qubits never exceed the forward's
// peak.
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

// Forward + adjoint round-trip for a single classical (a, dest_old, n).
//
// Precondition: a < n, dest_old < n, n >= 1.  Asserts:
//   - forward sets dest_bits[0..W-1] to (dest_old + a) mod n with
//     dest_bits[W] == 0,
//   - adjoint restores dest_bits[0..W-1] to dest_old with dest_bits[W] == 0,
//   - a, n preserved across both,
//   - QubitPool::in_use() returns to its pre-call value at the end.
static void run_roundtrip_case(uint32_t a_val, uint32_t dest_val,
                               uint32_t n_val) {
    assert(a_val    < n_val && "test precondition: a < n");
    assert(dest_val < n_val && "test precondition: dest_old < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 3u * W + 1u;  // a (W) + dest (W+1) + n (W)
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 128u};
    RegA    a    = make_reg_a(0,                              a_val,    sc.sv());
    RegDest dest = make_reg_dest(static_cast<int>(W),         dest_val, sc.sv());
    RegN    n    = make_reg_n(static_cast<int>(W + (W + 1u)), n_val,    sc.sv());

    sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a.bits.data(),
                                                    dest.bits.data(),
                                                    n.bits.data(), W);

    const uint32_t expect_dest = (dest_val + a_val) % n_val;
    uint32_t a_sv        = read_reg(sc.sv(), a.qi.data(),        W,  n_orkan);
    uint32_t dest_low_sv = read_reg(sc.sv(), dest.qi.data(),     W,  n_orkan);
    uint32_t dest_top_sv = read_reg(sc.sv(), dest.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv        = read_reg(sc.sv(), n.qi.data(),        W,  n_orkan);
    assert(a_sv        == a_val       && "forward: a preserved");
    assert(dest_low_sv == expect_dest && "forward: dest == (dest_old + a) mod n");
    assert(dest_top_sv == 0u          && "forward: dest_bits[W] returned to |0>");
    assert(n_sv        == n_val       && "forward: n preserved");

    // Compile-time wiring check: invert<&lib_add_mod_inplace_dsl<BitProxy>>()
    // must resolve to the registered adjoint (STURM_REGISTER_ADJOINT in
    // add_mod_inplace_dsl_adj.hpp).
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_add_mod_inplace_dsl<BitProxy>>() must resolve "
                  "to registered adjoint");
    adj_ptr(a.bits.data(), dest.bits.data(), n.bits.data(), W);

    a_sv        = read_reg(sc.sv(), a.qi.data(),        W,  n_orkan);
    dest_low_sv = read_reg(sc.sv(), dest.qi.data(),     W,  n_orkan);
    dest_top_sv = read_reg(sc.sv(), dest.qi.data() + W, 1u, n_orkan);
    n_sv        = read_reg(sc.sv(), n.qi.data(),        W,  n_orkan);
    assert(a_sv        == a_val    && "adjoint: a preserved");
    assert(dest_low_sv == dest_val && "adjoint: dest restored to dest_old");
    assert(dest_top_sv == 0u       && "adjoint: dest_bits[W] still |0>");
    assert(n_sv        == n_val    && "adjoint: n preserved");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-8lnp Beat A: adjoint round-trip "
                "(W=2 exhaustive sweep, all (a, dest_old) in [0, n) for n "
                "in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t dest_val = 0u; dest_val < n_val; ++dest_val) {
            for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
                run_roundtrip_case(a_val, dest_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4 cases; n=3 → 9 cases; total = 14 cases.
    assert(cases_run == 14u && "W=2 sweep covered every (a, dest_old, n) "
                                "with a, dest_old < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases (forward + adjoint restores dest, "
                "preserves a/n, no leaked ancillas)\n", cases_run);

    std::printf("All sturm-8lnp Beat A adjoint tests passed.\n");
    return 0;
}
