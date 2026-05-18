// test_double_mod_dsl_adjoint.cpp -- Beat B (sturm-wdas) adjoint round-trip,
//                                       post sturm-4oot.1 API rewrite.
//
// Forward `lib_double_mod_dsl(x, n_bits, W, lt_flag_out)` followed by
// `__lib_double_mod_dsl_adj(x, n_bits, W, lt_flag_out)` must return
// `x_bits` to its original value AND consume `lt_flag_out` back to |0>
// for every (x, n) input that the forward beat covers.  Under
// sturm-4oot.2 the sweep is extended to even `n`: W=2 exhaustive over
// n in {1, 2, 3} (n=4 doesn't fit and is hoisted to W=3); W=3 even-n
// exhaustive over n in {2, 4, 6} via the classical-trace replay
// harness; W=4 exhaustive at n=8 and W=4/W=5 random spot checks at
// n in {10, 12, 14} via the same trace harness.  `n_bits` must remain
// unchanged across the full forward+adjoint pair, and
// `QubitPool::in_use()` must return to its pre-call value (no leaked
// ancillas).
//
// The (n+1)/2 case (n=3, x=2) is the previous odd-only counterexample
// that breaks any naive "halve via shift-right" interpretation; it is
// covered both by the W=2 sweep and the explicit design-note-3 call
// below (the latter retained as a SIMULATE smoke for end-to-end
// statevector coverage of the adjoint round-trip).  Even-n inputs
// round-trip without parity restriction.
//
// Under sturm-4oot.1, the lt_flag bit is caller-owned: forward XORs
// `(2x_orig < n_value)` into `lt_flag_out`, adjoint reads it to reverse
// the doubling, leaving `lt_flag_out = 0` on exit.  Tests cover both
// the |0>-pre flow (clean write / clean uncompute) and an XOR-into pre
// (lt_flag_out_pre = 1 → adjoint must consume both the pre and the
// witness, leaving 0 since 1 XOR 1 = 0 only when the witness == 1; for
// witness == 0, the adjoint leaves 1 and the round-trip fails — so the
// XOR-into round-trip is conditional on |0>-pre, exactly as the
// forward+adjoint contract requires).
//
// sturm-a3e9: the W=2 and W=3 sweeps (previously SIMULATE-based) now
// run through the shared APPEND+classical-replay harness from
// tests/lib/classical_replay.hpp, matching the migration that
// sturm-scin applied to the modular-arith forward tests.  Per-case
// cost drops from O(2^n_orkan * |IR|) to O(|IR|), bringing aggregate
// wall-clock from ~24 s to <1 s.  One SIMULATE smoke (the design-
// note-3 (x=2, n=3) case) is retained for end-to-end statevector
// coverage of `invert<&lib_double_mod_dsl<BitProxy>>()`.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/double_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
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
#include <random>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W  = 2u;
static constexpr std::size_t W3 = 3u;
// SIMULATE smoke for the design-note-3 case sizes the orkan state at the
// kMaxQubits=17 cap (matches forward beat).  Forward+adjoint run
// back-to-back inside one SimCtx; LIFO release between calls means peak
// live qubits never exceed the forward's peak.
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

// SIMULATE-mode forward + adjoint round-trip for a single classical
// (x, n) input.  Retained as a smoke for end-to-end statevector
// coverage of `invert<&lib_double_mod_dsl<BitProxy>>()`; the sweep
// coverage runs through the APPEND+replay driver below.
//
// Precondition: x < n.  Asserts:
//   - forward sets x_bits[0..W-1] to (2 * x) mod n with x_bits[W] == 0,
//   - forward sets lt_flag_out to (2x < n)  (XOR-into a |0> pre),
//   - adjoint restores x_bits[0..W-1] to original x with x_bits[W] == 0,
//   - adjoint consumes lt_flag_out back to |0>,
//   - n_bits preserved across both,
//   - QubitPool::in_use() returns to its pre-call value at the end.
static void run_roundtrip_case_sim(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W + 1u;  // x (W+1), n (W), lt (1)
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 128u};
    RegX x   = make_reg_x(0, x_val, sc.sv());
    RegN n   = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());
    RegLT lt = make_reg_lt(static_cast<int>(W + 1u + W));

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                                W, lt.bit);

    const uint32_t expect_x  = (2u * x_val) % n_val;
    const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
    uint32_t x_low = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    uint32_t x_top = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv  = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t lt_sv = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(x_low == expect_x  && "forward: x_bits[0..W-1] == (2x) mod n");
    assert(x_top == 0u        && "forward: x_bits[W] returned to |0>");
    assert(n_sv  == n_val     && "forward: n_bits preserved");
    assert(lt_sv == expect_lt && "forward: lt_flag_out == (2x_orig < n)");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_double_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_double_mod_dsl<BitProxy>>() must resolve "
                  "to registered adjoint");
    adj_ptr(x.bits.data(), n.bits.data(), W, lt.bit);

    x_low = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    x_top = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
    n_sv  = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    lt_sv = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(x_low == x_val && "adjoint: x_bits[0..W-1] restored to original x");
    assert(x_top == 0u    && "adjoint: x_bits[W] still |0>");
    assert(n_sv  == n_val && "adjoint: n_bits preserved");
    assert(lt_sv == 0u    && "adjoint: lt_flag_out consumed back to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── APPEND+replay round-trip driver (sturm-a3e9) ─────────────────────────
//
// Drives forward + `invert<>()`-resolved adjoint inside a single
// STURM_MODE_APPEND context so both emissions append to the same ctx->ir.
// The captured IR is replayed in one pass over a classical bit-vector
// sized to QubitPool::high_water().  Asserts:
//   - x_bits[0..W_VAL-1] restored to original x,
//   - x_bits[W_VAL] (overflow slot) returned to |0>,
//   - n_bits preserved,
//   - lt_flag_out consumed back to |0>,
//   - every ancilla bit (qubits beyond the input slots) is back to 0,
//   - QubitPool::in_use() returns to its pre-call value.
//
// Per-case cost is O(|IR|) instead of O(2^n_orkan * |IR|); the W=2 / W=3
// sweeps collectively drop from ~24 s of SIMULATE work to a few ms.
template <std::size_t W_VAL>
static void run_replay_roundtrip_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << W_VAL));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = static_cast<uint32_t>(W_VAL) + 1u
                              + static_cast<uint32_t>(W_VAL) + 1u;
    int qi_x[W_VAL + 1u], qi_n[W_VAL], qi_lt;
    for (std::size_t i = 0; i < W_VAL + 1u; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    qi_lt = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool x_own[W_VAL + 1u], n_own[W_VAL], lt_own;
    sturm::BitProxy x_bits[W_VAL + 1u], n_bits[W_VAL], lt_bit;
    for (std::size_t i = 0; i < W_VAL + 1u; ++i) {
        x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
        x_bits[i] = sturm::BitProxy(x_own[i]);
    }
    for (std::size_t i = 0; i < W_VAL; ++i) {
        n_own[i]  = sturm::qbool::make_non_owning(qi_n[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
    }
    lt_own = sturm::qbool::make_non_owning(qi_lt);
    lt_bit = sturm::BitProxy(lt_own);

    sturm::test_helpers::AppendContext app;

    // Forward + adjoint both append into the same ctx->ir.
    sturm::lib_double_mod_dsl<sturm::BitProxy>(x_bits, n_bits, W_VAL, lt_bit);
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_double_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_double_mod_dsl<BitProxy>>() must resolve "
                  "in trace harness too");
    adj_ptr(x_bits, n_bits, W_VAL, lt_bit);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W_VAL; ++i) {
        if ((x_val >> i) & 1u) bits[static_cast<std::size_t>(qi_x[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    // x_bits[W_VAL] (overflow slot) and lt_bit start |0> (already zero).
    sturm::test_helpers::replay_ir(app.ctx()->ir, bits);

    using sturm::test_helpers::read_reg_classical;
    assert(read_reg_classical(bits, qi_x, W_VAL) == x_val
           && "replay round-trip: x_bits restored to original x");
    assert(bits[static_cast<std::size_t>(qi_x[W_VAL])] == 0u
           && "replay round-trip: x_bits[W_VAL] (overflow slot) still |0>");
    assert(read_reg_classical(bits, qi_n, W_VAL) == n_val
           && "replay round-trip: n_bits preserved");
    assert(bits[static_cast<std::size_t>(qi_lt)] == 0u
           && "replay round-trip: lt_flag_out consumed back to |0>");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u
               && "replay round-trip: ancilla not cleaned across f + adj");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "replay round-trip: pool live-count restored");

    sturm::QubitPool::instance().release(qi_lt);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W_VAL + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_x[i]);
}

int main() {
    // sturm-a3e9 — SIMULATE smoke for the design-note-3 case.  Retains
    // end-to-end statevector coverage of `invert<&lib_double_mod_dsl<
    // BitProxy>>()`: the (n+1)/2 case (n=3, x=2 → 2·2 mod 3 = 1, LSB=1)
    // is the input that breaks any naive "halve via shift-right"
    // interpretation of the forward and proves the adjoint here is the
    // gate-reverse of the forward (not a literal halving).  Coverage of
    // the W=2 / W=3 sweeps runs through the APPEND+replay driver below.
    std::printf("sturm-wdas double-mod-dsl: (n+1)/2 adjoint round-trip "
                "(design note #3 SIMULATE smoke):\n");
    run_roundtrip_case_sim(/*x=*/2u, /*n=*/3u);
    std::puts("  PASS: SIMULATE round-trip on x=2, n=3 (forward 2·2 mod "
              "3 = 1, LSB=1, lt_flag_out=0; adjoint restores x=2, "
              "lt_flag_out=0)");

    std::printf("sturm-wdas/sturm-4oot.1/sturm-4oot.2/sturm-a3e9 "
                "double-mod-dsl: adjoint round-trip (W=2 exhaustive sweep, "
                "n in {1, 2, 3}, all x in [0, n); APPEND+replay):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_replay_roundtrip_case<W>(x_val, n_val);
            ++cases_run;
        }
    }
    // n in [1, 4): n=1 → 1; n=2 → 2; n=3 → 3.  Total = 6.
    assert(cases_run == 6u && "W=2 sweep covered every (x, n) "
                              "with x < n and n in {1, 2, 3}");
    std::printf("  PASS: %zu W=2 cases (forward + adjoint restores x, "
                "consumes lt_flag_out, preserves n, no leaked ancillas)\n",
                cases_run);

    // sturm-4oot.2/sturm-a3e9 — W=3 even-n exhaustive round-trip sweep
    // (n in {2, 4, 6}; n=8 doesn't fit in a 3-bit modulus register and
    // is exercised at W=4 via the same trace driver below).
    std::printf("sturm-4oot.2/sturm-a3e9 double-mod-dsl: W=3 even-n "
                "exhaustive round-trip sweep (n in {2, 4, 6}, all x in "
                "[0, n); APPEND+replay):\n");
    std::size_t cases_w3_even = 0u;
    for (uint32_t n_val : {2u, 4u, 6u}) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_replay_roundtrip_case<W3>(x_val, n_val);
            ++cases_w3_even;
        }
    }
    // n=2 → 2; n=4 → 4; n=6 → 6.  Total = 12.
    assert(cases_w3_even == 12u
           && "W=3 even-n sweep covered every (x, n) with x < n and n even");
    std::printf("  PASS: %zu W=3 even-n round-trip cases\n", cases_w3_even);

    // sturm-4oot.2 — W=4 exhaustive trace round-trip at n=8 + W=4/W=5
    // random spot checks at n in {10, 12, 14}.  These already used the
    // APPEND-mode trace harness pre-sturm-a3e9; the migration here is
    // a pure rename to the shared driver.
    constexpr std::size_t W4 = 4u;
    constexpr std::size_t W5 = 5u;
    std::array<uint32_t, 3> even_ns_4_5 = {10u, 12u, 14u};

    std::printf("sturm-4oot.2 double-mod-dsl: W=4 exhaustive trace "
                "round-trip (even n=8):\n");
    {
        constexpr uint32_t n_val = 8u;
        std::size_t cases_w4_n8 = 0u;
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_replay_roundtrip_case<W4>(x_val, n_val);
            ++cases_w4_n8;
        }
        assert(cases_w4_n8 == 8u);
        std::printf("  PASS: %zu W=4 trace round-trip cases at n=8\n",
                    cases_w4_n8);
    }

    std::printf("sturm-4oot.2 double-mod-dsl: W=4 random trace round-trip "
                "spot checks (20 cases, seed=4214, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4214u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng4(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx(rng4)];
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng4);
            run_replay_roundtrip_case<W4>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=4 even-n trace round-trip spot checks\n",
                    kCases);
    }

    std::printf("sturm-4oot.2 double-mod-dsl: W=5 random trace round-trip "
                "spot checks (20 cases, seed=4215, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4215u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng5(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx(rng5)];
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng5);
            run_replay_roundtrip_case<W5>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=5 even-n trace round-trip spot checks\n",
                    kCases);
    }

    std::printf("All sturm-wdas/sturm-4oot.1/sturm-4oot.2/sturm-a3e9 "
                "double-mod-dsl adjoint tests passed.\n");
    return 0;
}
