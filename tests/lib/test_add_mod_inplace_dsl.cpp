// test_add_mod_inplace_dsl.cpp -- sturm-8lnp Beat A forward tests:
//                                   n==0 no-op, single classical case,
//                                   W=2 exhaustive sweep, W=3 random sweep.
//
// Mirrors `tests/lib/test_add_mod_dsl.cpp` (sturm-yh3d.{1,2,3,4}) and
// `tests/lib/test_double_mod_dsl.cpp` (sturm-wdas.{1,2,3,4}) adapted for
// in-place modular addition on a (W+1)-qubit dest register.  The forward
// primitive's signature is `lib_add_mod_inplace_dsl(a_bits, dest_bits,
// n_bits, n)` -- dest is W+1 qubits wide (top bit = overflow slot, must
// enter |0>); `dest_bits[0..W-1]` holds the running value `dest_old` in
// [0, n_value) and is updated in-place to `(dest_old + a) mod n_value`.
//
// Coverage shape (mirrors add_mod_dsl beats 1.1-1.4):
//   - sturm-8lnp.1: n==0 short-circuits, leaves dest, a, n unchanged.
//   - sturm-8lnp.2: full algorithm, single classical case.
//   - sturm-8lnp.3: W=2 exhaustive sweep over (a, dest_old, n) with
//                   PRD §5 precondition `a, dest_old < n` and `n >= 1`
//                   (14 cases total).
//   - sturm-8lnp.4: W=3 random sweep (50 cases, fixed std::mt19937 seed=42)
//                   of (a, dest_old, n) with a, dest_old < n and n in
//                   [1, 2^3=8) vs. the classical reference.
//
// Each call asserts:
//   - dest_bits[0..W-1] == (dest_old + a) mod n_value,
//   - dest_bits[W] == 0 (overflow slot returned to |0>),
//   - a_bits, n_bits unchanged (reversibility of inputs),
//   - QubitPool::in_use() returns to its pre-call value.
//
// At W=3 the algorithm peaks at ~14 live qubits (3*W + 1 input regs +
// constant 5 ancillas + transients).  Stays well within orkan's 30-qubit
// ceiling.  W=2 fits inside the kMaxQubits=17 cap; W=3 bypasses the cap by
// calling `orkan::allocate(bridge.state(), n_orkan_w3)` directly (matches
// the workaround in test_add_mod_dsl.cpp / test_double_mod_dsl.cpp).

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

#include "classical_replay.hpp"  // sturm-scin: APPEND+replay helper

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// W=2: a_bits has W = 2 slots, dest_bits has W+1 = 3 slots, n_bits has W = 2
// slots → 7 input qubits.  Plus n_pad + lt_flag + carry_anc + inner adder
// transients ≈ 5 ancillas → ~12 live qubits at peak.  Sizing the simulator
// at the kMaxQubits=17 cap leaves headroom and matches the add_mod_dsl test
// family.
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 64u) {
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

// ── W=2 register helpers (W slots for a_bits, W+1 for dest_bits, W for n_bits).
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
        // Bit W (overflow slot) starts |0> — never flipped at init.
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

// sturm-8lnp.1: n==0 short-circuits, leaves dest, a, n unchanged.
static void run_n_zero_case(uint32_t a_val, uint32_t dest_val,
                            uint32_t n_val) {
    sturm::QubitPool::instance().reset_for_testing();
    // a (W) + dest (W+1) + n (W) = 3W + 1 inputs.
    const uint32_t n_reg = 3u * W + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 64u};
    RegA    a    = make_reg_a(0,                                   a_val,    sc.sv());
    RegDest dest = make_reg_dest(static_cast<int>(W),              dest_val, sc.sv());
    RegN    n    = make_reg_n(static_cast<int>(W + (W + 1u)),      n_val,    sc.sv());

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a.bits.data(),
                                                    dest.bits.data(),
                                                    n.bits.data(), /*n=*/0u);

    uint32_t a_sv        = read_reg(sc.sv(), a.qi.data(),        W,  n_orkan);
    uint32_t dest_low_sv = read_reg(sc.sv(), dest.qi.data(),     W,  n_orkan);
    uint32_t dest_top_sv = read_reg(sc.sv(), dest.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv        = read_reg(sc.sv(), n.qi.data(),        W,  n_orkan);
    assert(a_sv        == a_val    && "n==0: a register unchanged");
    assert(dest_low_sv == dest_val && "n==0: dest_bits[0..W-1] unchanged");
    assert(dest_top_sv == 0u       && "n==0: dest_bits[W] still |0>");
    assert(n_sv        == n_val    && "n==0: n register unchanged");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// sturm-8lnp.2/.3: full algorithm, single (a, dest_old, n).  Asserts:
//   - dest_bits[0..W-1] == (dest_old + a) mod n,
//   - dest_bits[W] == 0 (overflow slot returned to |0>),
//   - a_bits, n_bits unchanged,
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
// Pre: a, dest_old < n, n >= 1.
static void run_classical_case(uint32_t a_val, uint32_t dest_val,
                               uint32_t n_val) {
    assert(a_val    < n_val && "test precondition: a < n");
    assert(dest_val < n_val && "test precondition: dest_old < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 3u * W + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u};
    RegA    a    = make_reg_a(0,                                   a_val,    sc.sv());
    RegDest dest = make_reg_dest(static_cast<int>(W),              dest_val, sc.sv());
    RegN    n    = make_reg_n(static_cast<int>(W + (W + 1u)),      n_val,    sc.sv());

    sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a.bits.data(),
                                                    dest.bits.data(),
                                                    n.bits.data(), W);

    const uint32_t expect_dest = (dest_val + a_val) % n_val;
    uint32_t a_sv        = read_reg(sc.sv(), a.qi.data(),        W,  n_orkan);
    uint32_t dest_low_sv = read_reg(sc.sv(), dest.qi.data(),     W,  n_orkan);
    uint32_t dest_top_sv = read_reg(sc.sv(), dest.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv        = read_reg(sc.sv(), n.qi.data(),        W,  n_orkan);
    assert(a_sv        == a_val       && "forward: a register unchanged");
    assert(dest_low_sv == expect_dest && "forward: dest == (dest_old + a) mod n");
    assert(dest_top_sv == 0u          && "forward: dest_bits[W] returned to |0>");
    assert(n_sv        == n_val       && "forward: n register unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── sturm-scin — APPEND+classical-replay driver for add-mod-inplace ──────
//
// Previously: Beat .3 ran the W=2 exhaustive sweep (14 cases) under
// STURM_MODE_SIMULATE with n_orkan=17, and Beat .4 ran the W=3 random
// sweep (50 cases) under STURM_MODE_SIMULATE with n_orkan_w3=21
// (2^21 ≈ 2M amplitudes per case + per-case read_reg scan).  Aggregate
// runtime: ~132 s.
//
// Now: both sweeps capture the algorithm's X / CX / CCX gate stream in
// STURM_MODE_APPEND and replay it as a classical bit-flip program over
// a `std::vector<uint8_t>` sized to QubitPool::high_water().  Per-case
// cost drops to O(|IR|).  The SIMULATE-mode smoke for the algorithm at
// W=2 is retained via `run_classical_case` (Beat .2) and the n==0
// short-circuit smokes via `run_n_zero_case` (Beat .1).
//
// Asserts (same contract as the prior SIMULATE-based drivers):
//   - dest_bits[0..W_VAL-1] == (dest_old + a) mod n,
//   - dest_bits[W_VAL] == 0 (overflow slot returned to |0>),
//   - a, n registers unchanged (input reversibility),
//   - every ancilla bit (qubits beyond the 3*W_VAL + 1 input slots) is
//     back to 0 (algorithm cleans up after itself),
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas).

static constexpr std::size_t W3 = 3u;

template <std::size_t W_VAL>
static void run_replay_case_inplace(uint32_t a_val, uint32_t dest_val,
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

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W_VAL; ++i) {
        if ((a_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])]    = 1u;
        if ((dest_val >> i) & 1u) bits[static_cast<std::size_t>(qi_dest[i])] = 1u;
        if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
    }
    // dest_bits[W_VAL] (overflow slot) starts |0> (already zero).
    sturm::test_helpers::replay_ir(app.ctx()->ir, bits);

    const uint32_t expect_dest = (dest_val + a_val) % n_val;
    using sturm::test_helpers::read_reg_classical;
    assert(read_reg_classical(bits, qi_a, W_VAL) == a_val
           && "replay: a register unchanged");
    assert(read_reg_classical(bits, qi_dest, W_VAL) == expect_dest
           && "replay: dest_bits[0..W_VAL-1] == (dest_old + a) mod n");
    assert(bits[static_cast<std::size_t>(qi_dest[W_VAL])] == 0u
           && "replay: dest_bits[W_VAL] (overflow slot) returned to |0>");
    assert(read_reg_classical(bits, qi_n, W_VAL) == n_val
           && "replay: n register unchanged");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "replay: ancilla bit not cleaned up");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "replay: pool live-count returns to pre-call value");

    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W_VAL + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_dest[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    std::printf("sturm-8lnp.1 add-mod-inplace-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*a=*/1u, /*dest=*/2u, /*n=*/3u);
    std::puts("  PASS: n==0 with dest=2, a=1 leaves dest at 2");
    run_n_zero_case(/*a=*/0u, /*dest=*/0u, /*n=*/0u);
    std::puts("  PASS: n==0 with all-zero inputs leaves dest at 0");
    run_n_zero_case(/*a=*/2u, /*dest=*/3u, /*n=*/3u);
    std::puts("  PASS: n==0 with dest=3, a=2 leaves dest at 3");

    std::printf("sturm-8lnp.2 add-mod-inplace-dsl: single classical case:\n");
    run_classical_case(/*a=*/1u, /*dest=*/1u, /*n=*/3u);
    std::puts("  PASS: dest := (1 + 1) mod 3 == 2");

    std::printf("sturm-8lnp.3 add-mod-inplace-dsl: W=2 exhaustive sweep "
                "(all (a, dest_old) in [0, n) for n in [1, 4), "
                "APPEND+replay):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t dest_val = 0u; dest_val < n_val; ++dest_val) {
            for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
                run_replay_case_inplace<W>(a_val, dest_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4 cases; n=3 → 9 cases; total = 14 cases.
    assert(cases_run == 14u && "W=2 sweep covered every (a, dest_old, n) "
                                "with a, dest_old < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases covering every (a, dest_old, n) "
                "with a, dest_old < n, n >= 1\n", cases_run);

    // ── sturm-8lnp.4 — W=3 random sweep (50 cases, fixed seed=42) ────────
    constexpr uint32_t    kW3Seed  = 42u;  // plan §12 risk mitigation
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-8lnp.4 add-mod-inplace-dsl: W=3 random sweep "
                "(%zu cases, seed=%u, n in [1, 8), APPEND+replay):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val    = ab_dist(rng);
        uint32_t dest_val = ab_dist(rng);
        run_replay_case_inplace<W3>(a_val, dest_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (dest_old + a) mod n matches "
                "classical reference\n", kW3Cases);

    // sturm-8n73: high-W trace spot checks validating kMaxN > 32 works.
    // add_mod_inplace peak is ~3W+5; sturm-5jta dropped the legacy
    // compile-time pool cap, so any W is supported (the pool grows on
    // demand).  W=64 path is exercised by the dedicated
    // test_modular_arith_highw_trace target.
    constexpr std::size_t W8_hi  = 8u;
    constexpr std::size_t W16_hi = 16u;

    std::printf("sturm-8n73 add-mod-inplace-dsl: W=8 random trace spot "
                "checks (10 cases, seed=8073):\n");
    {
        constexpr uint32_t kSeed = 8073u;
        constexpr std::size_t kCases = 10u;
        std::mt19937 rng8(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u,
                                                        (1u << W8_hi) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng8);
            std::uniform_int_distribution<uint32_t> ab(0u, n_val - 1u);
            uint32_t a_val    = ab(rng8);
            uint32_t dest_val = ab(rng8);
            run_replay_case_inplace<W8_hi>(a_val, dest_val, n_val);
        }
        std::printf("  PASS: %zu W=8 add-mod-inplace trace spot checks\n",
                    kCases);
    }

    std::printf("sturm-8n73 add-mod-inplace-dsl: W=16 random trace spot "
                "checks (5 cases, seed=8074):\n");
    {
        constexpr uint32_t kSeed = 8074u;
        constexpr std::size_t kCases = 5u;
        std::mt19937 rng16(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u,
                                                        (1u << W16_hi) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng16);
            std::uniform_int_distribution<uint32_t> ab(0u, n_val - 1u);
            uint32_t a_val    = ab(rng16);
            uint32_t dest_val = ab(rng16);
            run_replay_case_inplace<W16_hi>(a_val, dest_val, n_val);
        }
        std::printf("  PASS: %zu W=16 add-mod-inplace trace spot checks\n",
                    kCases);
    }

    std::printf("All sturm-8lnp Beat A forward tests passed.\n");
    return 0;
}
