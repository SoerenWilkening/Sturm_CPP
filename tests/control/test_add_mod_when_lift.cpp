// test_add_mod_when_lift.cpp -- sturm-a3t4.2 P1
//
// Pins the depth-1 lift pattern in `lib_add_mod_dsl` (forward + adjoint).
// The forward and adjoint headers replaced the inner
// `push_control / lib_add_dsl / pop_control` triples with the inline
// lift:
//
//   if (qbool* outer = WhenGuard::active_control()) {
//       qbool tmp = (*outer) & lt_flag_own;
//       WHEN(tmp) { body(); }
//       sturm::uncompute_and(tmp, *outer, lt_flag_own);
//   } else {
//       WHEN(lt_flag_own) { body(); }
//   }
//
// Per the sturm-a3t4 epic ("depth-1 control stack invariant"), the inner
// body must execute under a single control qubit regardless of whether the
// caller wrapped the modular addition in an enclosing `WHEN(c)`.  This
// test verifies behavioural correctness in both branches with a minimal
// surface (one width, two operand triples, both with and without WHEN(c)).
//
// Coverage rationale (issue body): "modular-arithmetic test surface can be
// reduced -- keep just enough cases (e.g. 1-2 widths * a few operand
// triples * with/without WHEN) to verify correctness, not exhaustive
// coverage."  The exhaustive sweeps in tests/lib/test_add_mod_dsl* already
// cover correctness across (a, b, n); this test focuses on the new
// control-stack lift specifically.
//
// Method per (a, b, n) input:
//   1. Forward run: assert r == (a+b) mod n, inputs unchanged, pool clean.
//   2. Adjoint round-trip: assert r returns to |0>, inputs unchanged.
//   3. Repeat both inside a `WHEN(c)` scope where c is a classical-true
//      qbool the WhenGuard activates with super_mask=1 (so the lift takes
//      the `if (outer)` branch).  Setting c's classical value to 1 means
//      the controlled body must still execute and produce the same result
//      as the bare run.
//
// `c` is a classical-true qbool with super_mask forced to 1.  The
// classical-true value (1) keeps the body live; the super_mask=1 forces
// `WhenGuard` down the superposed branch so the lift sees a real control
// qbool from `active_control()`.  A purely classical-true outer would
// short-circuit the WhenGuard before `active_control()` is set, defeating
// the test's goal of exercising the `(outer & lt_flag_own)` AND-fold.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/add_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/control/when.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// Sized at the kMaxQubits=17 cap for the bare path; the WHEN-wrapped path
// allocates one extra outer-control qubit and one lift ancilla per
// controlled body, so it bypasses the cap via `orkan::allocate` directly.
static constexpr uint32_t n_orkan      = 17u;
static constexpr uint32_t n_orkan_when = 22u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    SimCtx(uint32_t n_q, bool bypass) {
        if (bypass) orkan::allocate(bridge.state(), n_q);
        else        bridge.allocate(n_q);
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

// Runs forward + adjoint round-trip on (a, b, n) under one of two modes:
//   wrap_in_when=false  -> exercises the `else` branch of the lift
//                          (WHEN(lt_flag_own)).
//   wrap_in_when=true   -> wraps the calls in WHEN(c) with c carrying
//                          super_mask=1 and value=1, so WhenGuard takes
//                          the superposed branch and `active_control()`
//                          returns &c — exercising the `if (outer)` lift
//                          branch (`tmp = c & lt_flag_own`).
static void run_case(uint32_t a_val, uint32_t b_val, uint32_t n_val,
                     bool wrap_in_when) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W + (wrap_in_when ? 1u : 0u);
    int reserved[32];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    const uint32_t n_q = wrap_in_when ? n_orkan_when : n_orkan;
    SimCtx sc{n_q, /*bypass=*/wrap_in_when};

    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     0u,    sc.sv());

    // Build the outer control `c` for the wrap branch.  Value=1 keeps the
    // body live; super_mask=1 forces WhenGuard into the superposed branch
    // so `active_control()` returns &c inside the lift.
    sturm::qbool c_owner;
    int c_idx = -1;
    if (wrap_in_when) {
        c_idx = 4 * W;  // qubit slot just above the four W-bit registers
        orkan::apply_x(sc.sv(), static_cast<uint32_t>(c_idx));  // |1>
        c_owner = sturm::qbool::make_non_owning(c_idx, /*val=*/1,
                                                /*mask=*/1ULL);
    }

    // ── Forward ─────────────────────────────────────────────────────────
    if (wrap_in_when) {
        WHEN(c_owner) {
            sturm::lib_add_mod_dsl<sturm::BitProxy>(
                a.bits.data(), b.bits.data(), n.bits.data(), W,
                r.bits.data());
        }
    } else {
        sturm::lib_add_mod_dsl<sturm::BitProxy>(
            a.bits.data(), b.bits.data(), n.bits.data(), W,
            r.bits.data());
    }

    const uint32_t expect_r = (a_val + b_val) % n_val;
    assert(read_reg(sc.sv(), a.qi.data(), W, n_q) == a_val && "fwd: a preserved");
    assert(read_reg(sc.sv(), b.qi.data(), W, n_q) == b_val && "fwd: b preserved");
    assert(read_reg(sc.sv(), n.qi.data(), W, n_q) == n_val && "fwd: n preserved");
    assert(read_reg(sc.sv(), r.qi.data(), W, n_q) == expect_r
           && "fwd: r == (a+b) mod n");

    // ── Adjoint round-trip ──────────────────────────────────────────────
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_add_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_add_mod_dsl<BitProxy>>() must resolve");
    if (wrap_in_when) {
        WHEN(c_owner) {
            adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), W,
                    r.bits.data());
        }
    } else {
        adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), W,
                r.bits.data());
    }

    assert(read_reg(sc.sv(), a.qi.data(), W, n_q) == a_val && "adj: a preserved");
    assert(read_reg(sc.sv(), b.qi.data(), W, n_q) == b_val && "adj: b preserved");
    assert(read_reg(sc.sv(), n.qi.data(), W, n_q) == n_val && "adj: n preserved");
    assert(read_reg(sc.sv(), r.qi.data(), W, n_q) == 0u && "adj: r returned to |0>");
    if (wrap_in_when) {
        assert(read_reg(sc.sv(), &c_idx, 1u, n_q) == 1u
               && "adj: outer control c preserved");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "lift: pool live-count returns to pre-call value "
              "(uncompute_and returned the lift ancilla LIFO)");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-a3t4.2: depth-1 lift pattern in lib_add_mod_dsl "
                "(W=%zu, with/without enclosing WHEN(c)):\n", W);

    // Two operand triples cover the lt_flag=1 (add-back live) and
    // lt_flag=0 (no add-back) branches of the algorithm.  (1+1) mod 3 =
    // 2 and 0 < 3 so add-back fires.  (2+2) mod 3 = 1, 4 >= 3 so the
    // unconditional subtract leaves s>=0 and lt_flag=0 (no add-back).
    struct Triple { uint32_t a; uint32_t b; uint32_t n; };
    constexpr Triple cases[] = {
        {1u, 1u, 3u},   // lt_flag=1 branch (s_old=2 < 3)
        {2u, 2u, 3u},   // lt_flag=0 branch (s_old=4 >= 3)
    };

    for (const Triple& t : cases) {
        run_case(t.a, t.b, t.n, /*wrap_in_when=*/false);
        std::printf("  PASS: bare WHEN(lt_flag_own) "
                    "(%u + %u) mod %u, forward+adjoint clean\n",
                    t.a, t.b, t.n);
        run_case(t.a, t.b, t.n, /*wrap_in_when=*/true);
        std::printf("  PASS: WHEN(c) wrapping -> lift via "
                    "(c & lt_flag_own), (%u + %u) mod %u\n",
                    t.a, t.b, t.n);
    }

    std::printf("All sturm-a3t4.2 lift-pattern tests passed.\n");
    return 0;
}
