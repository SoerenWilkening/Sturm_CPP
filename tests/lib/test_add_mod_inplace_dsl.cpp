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

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>

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

// ── sturm-8lnp.4 — W=3 random sweep harness ──────────────────────────────
// W=3 peak ≈ 3·W + 1 + constant 5 = 15 live qubits, well inside orkan's
// 30-qubit ceiling.  We bypass kMaxQubits=17 cap by calling orkan::allocate
// directly (matches the W=3 sweep workaround in test_add_mod_dsl.cpp).
static constexpr std::size_t W3        = 3u;
static constexpr uint32_t    n_orkan_w3 = 21u;

struct RegA3 {
    std::array<int, W3>              qi;
    std::array<sturm::qbool, W3>     owners;
    std::array<sturm::BitProxy, W3>  bits;
};

struct RegDest3 {
    std::array<int, W3 + 1u>             qi;
    std::array<sturm::qbool, W3 + 1u>    owners;
    std::array<sturm::BitProxy, W3 + 1u> bits;
};

struct RegN3 {
    std::array<int, W3>              qi;
    std::array<sturm::qbool, W3>     owners;
    std::array<sturm::BitProxy, W3>  bits;
};

static RegA3 make_reg_a3(int base, uint32_t val, orkan::state_t& sv) {
    RegA3 r;
    for (std::size_t i = 0; i < W3; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static RegDest3 make_reg_dest3(int base, uint32_t val, orkan::state_t& sv) {
    RegDest3 r;
    for (std::size_t i = 0; i < W3 + 1u; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if (i < W3 && ((val >> i) & 1u))
            orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3 + 1u; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static RegN3 make_reg_n3(int base, uint32_t val, orkan::state_t& sv) {
    RegN3 r;
    for (std::size_t i = 0; i < W3; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

// sturm-8lnp.4 driver — mirrors run_classical_case but sized for W=3 and
// uses the bypass-cap simulator.  Asserts dest == (dest_old + a) mod n,
// inputs unchanged, pool live-count clean.
static void run_classical_case_w3(uint32_t a_val, uint32_t dest_val,
                                  uint32_t n_val) {
    assert(a_val    < n_val && "test precondition: a < n");
    assert(dest_val < n_val && "test precondition: dest_old < n");
    assert(n_val < (1u << W3) && "test precondition: n fits in W=3 bits");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 3u * static_cast<uint32_t>(W3) + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    sturm::OrkanBridge bridge;
    orkan::allocate(bridge.state(), n_orkan_w3);  // bypass kMaxQubits=17 cap
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_SIMULATE, 64u);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);
    orkan::state_t& sv = bridge.state();
    RegA3    a    = make_reg_a3(0,
                                a_val,    sv);
    RegDest3 dest = make_reg_dest3(static_cast<int>(W3),
                                   dest_val, sv);
    RegN3    n    = make_reg_n3(static_cast<int>(W3 + (W3 + 1u)),
                                n_val,    sv);
    sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a.bits.data(),
                                                    dest.bits.data(),
                                                    n.bits.data(), W3);
    const uint32_t expect_dest = (dest_val + a_val) % n_val;
    assert(read_reg(sv, a.qi.data(), W3, n_orkan_w3) == a_val
           && "W=3: a unchanged");
    assert(read_reg(sv, dest.qi.data(), W3, n_orkan_w3) == expect_dest
           && "W=3: dest_bits[0..W-1] == (dest_old + a) mod n");
    assert(read_reg(sv, dest.qi.data() + W3, 1u, n_orkan_w3) == 0u
           && "W=3: dest_bits[W] returned to |0>");
    assert(read_reg(sv, n.qi.data(), W3, n_orkan_w3) == n_val
           && "W=3: n unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "W=3: pool live-count returns to pre-call value");
    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
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
                "(all (a, dest_old) in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t dest_val = 0u; dest_val < n_val; ++dest_val) {
            for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
                run_classical_case(a_val, dest_val, n_val);
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
                "(%zu cases, seed=%u, n in [1, 8)):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val    = ab_dist(rng);
        uint32_t dest_val = ab_dist(rng);
        run_classical_case_w3(a_val, dest_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (dest_old + a) mod n matches "
                "classical reference\n", kW3Cases);

    std::printf("All sturm-8lnp Beat A forward tests passed.\n");
    return 0;
}
