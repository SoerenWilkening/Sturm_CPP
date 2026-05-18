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
//
// sturm-a3e9: the WHEN-wrapped variant (which previously sized orkan
// at n_orkan_when=22 → 4M-amplitude state vector) now runs through the
// shared APPEND+classical-replay harness from tests/lib/
// classical_replay.hpp.  Pool semantics and the lift's IR shape are
// execution-mode-independent: the lift's `(outer & flag)` AND-fold,
// `WHEN(tmp)` body, and `uncompute_and(tmp, ...)` all emit X / CX / CCX
// records into ctx->ir under STURM_MODE_APPEND, just as they would
// execute against amplitudes under STURM_MODE_SIMULATE.  Per-case cost
// drops from O(2^n_orkan_when * |IR|) to O(|IR|), bringing wall-clock
// from ~15 s to <1 s.  One SIMULATE smoke (bare path, (1+1) mod 3) is
// retained for end-to-end statevector coverage of the bare lift branch.

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
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include "../lib/classical_replay.hpp"  // sturm-a3e9: shared APPEND+replay helper

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// Bare SIMULATE smoke sizes orkan at the kMaxQubits=17 cap.  The
// WHEN-wrapped path used to size orkan at 22 for the SIMULATE driver;
// under sturm-a3e9 it runs through APPEND+replay and no longer needs an
// orkan state vector.
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q) {
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

// SIMULATE-mode bare-path smoke: exercises the `WHEN(lt_flag_own)` else
// branch of the lift via the orkan state vector.  Retained as a single
// SIMULATE witness for end-to-end statevector coverage; the WHEN-wrapped
// cases (which used to drive a 22-qubit state vector per case) move to
// APPEND+replay below.
static void run_case_bare_sim(uint32_t a_val, uint32_t b_val, uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;
    int reserved[32];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan};

    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     0u,    sc.sv());

    // ── Forward ─────────────────────────────────────────────────────────
    sturm::lib_add_mod_dsl<sturm::BitProxy>(
        a.bits.data(), b.bits.data(), n.bits.data(), W, r.bits.data());

    const uint32_t expect_r = (a_val + b_val) % n_val;
    assert(read_reg(sc.sv(), a.qi.data(), W, n_orkan) == a_val && "fwd: a preserved");
    assert(read_reg(sc.sv(), b.qi.data(), W, n_orkan) == b_val && "fwd: b preserved");
    assert(read_reg(sc.sv(), n.qi.data(), W, n_orkan) == n_val && "fwd: n preserved");
    assert(read_reg(sc.sv(), r.qi.data(), W, n_orkan) == expect_r
           && "fwd: r == (a+b) mod n");

    // ── Adjoint round-trip ──────────────────────────────────────────────
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_add_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_add_mod_dsl<BitProxy>>() must resolve");
    adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), W, r.bits.data());

    assert(read_reg(sc.sv(), a.qi.data(), W, n_orkan) == a_val && "adj: a preserved");
    assert(read_reg(sc.sv(), b.qi.data(), W, n_orkan) == b_val && "adj: b preserved");
    assert(read_reg(sc.sv(), n.qi.data(), W, n_orkan) == n_val && "adj: n preserved");
    assert(read_reg(sc.sv(), r.qi.data(), W, n_orkan) == 0u && "adj: r returned to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "bare-path lift: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── APPEND+replay driver (sturm-a3e9) ────────────────────────────────────
//
// Runs forward + `invert<>()`-resolved adjoint inside a single
// STURM_MODE_APPEND context, optionally wrapped in `WHEN(c_owner)` with
// super_mask=1 so WhenGuard takes the superposed branch and pushes c onto
// ctx->control_stack.  Under that push, the lift's `WhenGuard::
// active_control()` returns &c, exercising the `(outer & lt_flag_own)`
// AND-fold branch.  Replay the captured IR over a classical bit-vector
// and assert:
//   - r returns to |0> after the adjoint (round-trip),
//   - a, b, n preserved across the full round-trip,
//   - c preserved (when wrapped),
//   - every ancilla bit returns to 0 (algorithm cleans up after itself),
//   - QubitPool::in_use() returns to its pre-call value (LIFO drain).
//
// The lift's AND-fold + `WHEN(tmp)` body + `uncompute_and(tmp, ...)`
// trailer all emit X / CX / CCX records — classical-reversible — so
// `apply_gate_classical` (in classical_replay.hpp) is honest for the
// full captured IR.
static void run_case_replay(uint32_t a_val, uint32_t b_val, uint32_t n_val,
                            bool wrap_in_when) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W + (wrap_in_when ? 1u : 0u);
    int reserved[16];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    // Build BitProxy / qbool views over the reserved indices.  No SimCtx,
    // no orkan state vector: bit values live in the replay vector below.
    const int a_base    = 0;
    const int b_base    = static_cast<int>(W);
    const int n_base    = static_cast<int>(2 * W);
    const int r_base    = static_cast<int>(3 * W);
    const int c_idx     = wrap_in_when ? static_cast<int>(4 * W) : -1;

    sturm::qbool a_own[W], b_own[W], n_own[W], r_own[W];
    sturm::BitProxy a_bits[W], b_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(a_base + static_cast<int>(i));
        b_own[i] = sturm::qbool::make_non_owning(b_base + static_cast<int>(i));
        n_own[i] = sturm::qbool::make_non_owning(n_base + static_cast<int>(i));
        r_own[i] = sturm::qbool::make_non_owning(r_base + static_cast<int>(i));
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    // Outer control `c`: value=1 keeps the body live; super_mask=1 forces
    // WhenGuard into the superposed branch so `active_control()` returns
    // &c inside the lift, exercising the `(outer & lt_flag_own)` AND-fold.
    sturm::qbool c_owner;
    if (wrap_in_when) {
        c_owner = sturm::qbool::make_non_owning(c_idx, /*val=*/1,
                                                /*mask=*/1ULL);
    }

    sturm::test_helpers::AppendContext app;

    // ── Forward ─────────────────────────────────────────────────────────
    if (wrap_in_when) {
        WHEN(c_owner) {
            sturm::lib_add_mod_dsl<sturm::BitProxy>(
                a_bits, b_bits, n_bits, W, r_bits);
        }
    } else {
        sturm::lib_add_mod_dsl<sturm::BitProxy>(
            a_bits, b_bits, n_bits, W, r_bits);
    }

    // ── Adjoint ─────────────────────────────────────────────────────────
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_add_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_add_mod_dsl<BitProxy>>() must resolve "
                  "in trace harness too");
    if (wrap_in_when) {
        WHEN(c_owner) {
            adj_ptr(a_bits, b_bits, n_bits, W, r_bits);
        }
    } else {
        adj_ptr(a_bits, b_bits, n_bits, W, r_bits);
    }

    // Pool drain pin (inside AppendContext scope so the lift's transient
    // ancillas are unambiguously released by the algorithm itself, not by
    // RAII at scope exit).
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "lift: pool live-count returns to pre-call value "
              "(uncompute_and returned the lift ancilla LIFO)");

    // Replay the captured IR over a classical bit-vector.
    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(a_base + i)] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(b_base + i)] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(n_base + i)] = 1u;
        // r starts at |0>; ancillas start at |0>.
    }
    if (wrap_in_when) {
        bits[static_cast<std::size_t>(c_idx)] = 1u;  // c starts at |1>
    }
    sturm::test_helpers::replay_ir(app.ctx()->ir, bits);

    // Assertions over the replayed bit-vector.
    auto read_qi_classical = [&](int base) {
        uint32_t v = 0u;
        for (std::size_t i = 0; i < W; ++i) {
            if (bits[static_cast<std::size_t>(base + i)]) v |= (1u << i);
        }
        return v;
    };
    assert(read_qi_classical(a_base) == a_val && "replay: a preserved");
    assert(read_qi_classical(b_base) == b_val && "replay: b preserved");
    assert(read_qi_classical(n_base) == n_val && "replay: n preserved");
    assert(read_qi_classical(r_base) == 0u
           && "replay: r returned to |0> after adjoint");
    if (wrap_in_when) {
        assert(bits[static_cast<std::size_t>(c_idx)] == 1u
               && "replay: outer control c preserved");
    }
    // All ancilla qubits beyond the reserved n_reg slots must return to 0.
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u
               && "replay: ancilla not cleaned across forward + adjoint");
    }

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-a3t4.2/sturm-a3e9: depth-1 lift pattern in "
                "lib_add_mod_dsl (W=%zu, with/without enclosing WHEN(c)):\n",
                W);

    // Two operand triples cover the lt_flag=1 (add-back live) and
    // lt_flag=0 (no add-back) branches of the algorithm.  (1+1) mod 3 =
    // 2 and 0 < 3 so add-back fires.  (2+2) mod 3 = 1, 4 >= 3 so the
    // unconditional subtract leaves s>=0 and lt_flag=0 (no add-back).
    struct Triple { uint32_t a; uint32_t b; uint32_t n; };
    constexpr Triple cases[] = {
        {1u, 1u, 3u},   // lt_flag=1 branch (s_old=2 < 3)
        {2u, 2u, 3u},   // lt_flag=0 branch (s_old=4 >= 3)
    };

    // SIMULATE bare-path smoke (one operand triple), retaining end-to-end
    // statevector coverage of the bare lift branch.
    run_case_bare_sim(cases[0].a, cases[0].b, cases[0].n);
    std::printf("  PASS: SIMULATE bare WHEN(lt_flag_own) "
                "(%u + %u) mod %u, forward+adjoint clean\n",
                cases[0].a, cases[0].b, cases[0].n);

    // APPEND+replay sweep: bare and WHEN-wrapped, both operand triples.
    for (const Triple& t : cases) {
        run_case_replay(t.a, t.b, t.n, /*wrap_in_when=*/false);
        std::printf("  PASS: replay bare WHEN(lt_flag_own) "
                    "(%u + %u) mod %u, forward+adjoint clean\n",
                    t.a, t.b, t.n);
        run_case_replay(t.a, t.b, t.n, /*wrap_in_when=*/true);
        std::printf("  PASS: replay WHEN(c) wrapping -> lift via "
                    "(c & lt_flag_own), (%u + %u) mod %u\n",
                    t.a, t.b, t.n);
    }

    std::printf("All sturm-a3t4.2/sturm-a3e9 lift-pattern tests passed.\n");
    return 0;
}
