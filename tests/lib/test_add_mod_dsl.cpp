// test_add_mod_dsl.cpp -- sturm-yh3d.{1,2,3,4} P1 beats 1.1, 1.2, 1.3, 1.4.
//
// Plan §3.3 beat 1.1: `lib_add_mod_dsl(... n=0 ...)` short-circuits and
// leaves r (and a, b, n) unchanged.
// Plan §3.3 beat 1.2: full algorithm produces r = (a+b) mod n for the
// single classical W=2 case (a=1, b=1, n=3) -> r=2.
// Plan §3.3 beat 1.3: exhaustive W=2 sweep over all (a, b, n) with
// PRD §5 precondition `a, b < n` and `n >= 1` (14 cases total).
// Plan §3.3 beat 1.4: W=3 random sweep (50 cases, fixed std::mt19937
// seed = 42 per plan §12 risk mitigation) of (a, b, n) with a, b in
// [0, n) and n in [1, 2^3=8) vs. the classical reference.
//
// Each call asserts r == (a+b) mod n, that a, b, n are unchanged
// (reversibility of inputs), and that QubitPool::in_use() returns to its
// pre-call value (no leaked ancillas).
//
// At W=3 the algorithm peaks at 21 live qubits (4*W = 12 input regs +
// (W+1) = 4 sum reg + n_pad + lt_flag + carry_anc + 1 transient inside
// lib_add_dsl/adj + 1 fold-ancilla emitted by emit_CCX_lifted under the
// WHEN(lt_flag) push in steps 7/11).  This exceeds the kMaxQubits=17 cap
// that OrkanBridge::allocate enforces, so the W=3 harness allocates the
// orkan::state_t directly via orkan::allocate(bridge.state(), 21) —
// within the orkan stub's 30-qubit ceiling and the qbool super_mask's
// 32-qubit limit.  Beats 1.5..1.7 expand coverage (adjoint round-trip,
// ancilla counter, pool live-count).

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/add_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
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
// Beat 1.2's algorithm needs a (W+1)-bit s register, an n_pad qubit, an
// lt_flag, a carry_anc, plus the transient ancillas inside the lib_add_*
// calls and their controlled-Toffoli folds.  Sizing the simulator at the
// hard 17-qubit cap (kMaxQubits) gives us the headroom every branch needs
// without forcing the budget to be re-tuned per beat.
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

// Beat 1.1: n==0 short-circuits, leaves r (and a, b, n) unchanged.
static void run_n_zero_case(uint32_t a_val, uint32_t b_val,
                            uint32_t n_val, uint32_t r_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // a, b, n, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 64u};
    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     r_val, sc.sv());

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_add_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), /*n=*/0u,
                                            r.bits.data());

    uint32_t a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    uint32_t n_sv = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t r_sv = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    assert(a_sv == a_val && "n==0: a register unchanged");
    assert(b_sv == b_val && "n==0: b register unchanged");
    assert(n_sv == n_val && "n==0: n register unchanged");
    assert(r_sv == r_val && "n==0: r register unchanged (no-op)");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// Beat 1.2: full algorithm, single classical case (a, b, n=3) -> r = (a+b) % n.
//
// Asserts:
//   - r register holds (a + b) mod n,
//   - a, b, n registers unchanged,
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
static void run_classical_case(uint32_t a_val, uint32_t b_val,
                               uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u};
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
    assert(a_sv == a_val && "forward: a register unchanged");
    assert(b_sv == b_val && "forward: b register unchanged");
    assert(n_sv == n_val && "forward: n register unchanged");
    assert(r_sv == expect_r && "forward: r == (a+b) mod n");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Beat 1.4 — W=3 random sweep harness ──────────────────────────────────
// W=3 peaks at 21 live qubits, exceeding OrkanBridge::allocate's
// kMaxQubits=17 cap; Beat 1.4 calls orkan::allocate directly on the
// bridge's state to bypass the cap (orkan stub permits up to 30 qubits).
static constexpr std::size_t W3        = 3u;
static constexpr uint32_t    n_orkan_w3 = 21u;

struct Reg3 {
    std::array<int, W3>             qi;
    std::array<sturm::qbool, W3>    owners;
    std::array<sturm::BitProxy, W3> bits;
};

static Reg3 make_reg3(int base, uint32_t val, orkan::state_t& sv) {
    Reg3 r;
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

// Beat 1.4 driver — mirrors run_classical_case but sized for W=3 and uses
// the bypass-cap simulator (orkan::allocate called directly on the bridge
// state).  Asserts r==(a+b) mod n, inputs unchanged, pool live-count clean.
static void run_classical_case_w3(uint32_t a_val, uint32_t b_val,
                                  uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << W3));
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W3);
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
    Reg3 a = make_reg3(0,                          a_val, sv);
    Reg3 b = make_reg3(static_cast<int>(W3),       b_val, sv);
    Reg3 n = make_reg3(static_cast<int>(2u * W3),  n_val, sv);
    Reg3 r = make_reg3(static_cast<int>(3u * W3),  0u,    sv);
    sturm::lib_add_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), W3,
                                            r.bits.data());
    const uint32_t expect_r = (a_val + b_val) % n_val;
    assert(read_reg(sv, a.qi.data(), W3, n_orkan_w3) == a_val
           && "W=3: a unchanged");
    assert(read_reg(sv, b.qi.data(), W3, n_orkan_w3) == b_val
           && "W=3: b unchanged");
    assert(read_reg(sv, n.qi.data(), W3, n_orkan_w3) == n_val
           && "W=3: n unchanged");
    assert(read_reg(sv, r.qi.data(), W3, n_orkan_w3) == expect_r
           && "W=3: r == (a+b) mod n");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "W=3: pool live-count returns to pre-call value");
    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-yh3d.1 P1.1 add-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*a=*/0u, /*b=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");

    std::printf("sturm-yh3d.2 P1.2 add-mod-dsl: single classical case:\n");
    run_classical_case(/*a=*/1u, /*b=*/1u, /*n=*/3u);
    std::puts("  PASS: (1 + 1) mod 3 == 2");

    std::printf("sturm-yh3d.3 P1.3 add-mod-dsl: W=2 exhaustive sweep "
                "(all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_classical_case(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4 cases; n=3 → 9 cases; total = 14 cases.
    assert(cases_run == 14u && "W=2 sweep covered every (a, b, n) "
                                "with a, b < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases covering every (a, b, n) "
                "with a, b < n, n >= 1\n", cases_run);

    // ── Beat 1.4 — W=3 random sweep (50 cases, fixed seed=42) ────────────
    constexpr uint32_t    kW3Seed  = 42u;  // plan §12 risk mitigation
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-yh3d.4 P1.4 add-mod-dsl: W=3 random sweep "
                "(%zu cases, seed=%u, n in [1, 8)):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_classical_case_w3(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (a + b) mod n matches "
                "classical reference\n", kW3Cases);

    std::printf("All sturm-yh3d.{1,2,3,4} tests passed.\n");
    return 0;
}
