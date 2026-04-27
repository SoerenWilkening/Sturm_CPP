// test_add_mod_dsl_adjoint.cpp -- sturm-yh3d.5 P1.5 add-mod-dsl 1.5
//                                  adjoint round-trip.
//
// Plan §3.3 beat 1.5: forward `lib_add_mod_dsl(a, b, n, W, r)` followed by
// `__lib_add_mod_dsl_adj(a, b, n, W, r)` must return `r` to |0> for every
// (a, b, n) input that beat 1.3 covers (the exhaustive W=2 sweep over all
// `a, b < n`, `n >= 1`).  Inputs `a, b, n` must remain unchanged across
// the full forward+adjoint pair, and `QubitPool::in_use()` must return to
// its pre-call value (no leaked ancillas).
//
// Mirrors `tests/lib/test_div_mod_dsl_adjoint.cpp`'s pattern:
// (1) drive the forward, (2) verify outputs match the classical reference,
// (3) resolve `invert<&lib_add_mod_dsl<BitProxy>>()` (compile-time check
//     that STURM_REGISTER_ADJOINT wired the pair),
// (4) call the adjoint, (5) assert r is back to 0 and inputs are intact.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/add_mod_dsl.hpp"
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
// Sized at the kMaxQubits=17 cap (matches test_add_mod_dsl beat 1.2/1.3
// forward sweep).  Forward+adjoint runs back-to-back inside one SimCtx;
// LIFO release between calls means peak live qubits never exceed the
// forward's peak.
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

// Forward + adjoint round-trip for a single classical (a, b, n) input.
//
// Precondition: a, b < n, n >= 1.  Asserts:
//   - forward sets r to (a + b) mod n,
//   - adjoint zeros r,
//   - a, b, n preserved across both,
//   - QubitPool::in_use() returns to its pre-call value at the end.
static void run_roundtrip_case(uint32_t a_val, uint32_t b_val,
                               uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // a, b, n, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 128u};
    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     0u,    sc.sv());

    sturm::lib_add_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), W,
                                            r.bits.data());

    const uint32_t expect_r = (a_val + b_val) % n_val;
    uint32_t a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    uint32_t n_sv = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t r_sv = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    assert(a_sv == a_val && "forward: a preserved");
    assert(b_sv == b_val && "forward: b preserved");
    assert(n_sv == n_val && "forward: n preserved");
    assert(r_sv == expect_r && "forward: r == (a+b) mod n");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_add_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_add_mod_dsl<BitProxy>>() must resolve to "
                  "registered adjoint");
    adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), W,
            r.bits.data());

    a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    n_sv = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    r_sv = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    assert(a_sv == a_val && "adjoint: a preserved");
    assert(b_sv == b_val && "adjoint: b preserved");
    assert(n_sv == n_val && "adjoint: n preserved");
    assert(r_sv == 0u    && "adjoint: r ancilla returned to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-yh3d.5 P1.5 add-mod-dsl: adjoint round-trip "
                "(W=2 exhaustive sweep, all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_roundtrip_case(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4 cases; n=3 → 9 cases; total = 14 cases.
    assert(cases_run == 14u && "W=2 sweep covered every (a, b, n) "
                                "with a, b < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases (forward + adjoint zeros r, "
                "preserves a/b/n, no leaked ancillas)\n", cases_run);

    std::printf("All sturm-yh3d.5 tests passed.\n");
    return 0;
}
