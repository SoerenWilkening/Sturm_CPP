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
#include "sturm/detail/lib/add_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

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

// ── Beats 1.3 / 1.4 — APPEND+classical-replay driver (sturm-scin) ────────
//
// Previously: Beat 1.3 ran the W=2 exhaustive sweep (14 cases) under
// STURM_MODE_SIMULATE with n_orkan=17, and Beat 1.4 ran the W=3 random
// sweep (50 cases) under STURM_MODE_SIMULATE with n_orkan_w3=21
// (2^21 ≈ 2M amplitudes per case + per-case read_reg scan).  Aggregate
// runtime: ~180 s.
//
// Now: both sweeps capture the algorithm's X / CX / CCX gate stream in
// STURM_MODE_APPEND and replay it as a classical bit-flip program over
// a `std::vector<uint8_t>` sized to QubitPool::high_water().  Per-case
// cost drops to O(|IR|), aggregate to a few ms.  The SIMULATE-mode
// smoke for the algorithm at W=2 is retained via `run_classical_case`
// (Beat 1.2).
//
// Asserts (same contract as the prior SIMULATE-based drivers):
//   - r register holds (a + b) mod n,
//   - a, b, n registers unchanged (input reversibility),
//   - every ancilla bit (qubits beyond the 4*W_VAL input slots) is back
//     to 0 (algorithm cleans up after itself),
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas).

static constexpr std::size_t W3 = 3u;

template <std::size_t W_VAL>
static void run_replay_case(uint32_t a_val, uint32_t b_val,
                            uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << W_VAL));

    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W_VAL);
    int qi_a[W_VAL], qi_b[W_VAL], qi_n[W_VAL], qi_r[W_VAL];
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[W_VAL], b_own[W_VAL], n_own[W_VAL], r_own[W_VAL];
    sturm::BitProxy a_bits[W_VAL], b_bits[W_VAL], n_bits[W_VAL], r_bits[W_VAL];
    for (std::size_t i = 0; i < W_VAL; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    sturm::test_helpers::AppendContext app;

    sturm::lib_add_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                            W_VAL, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W_VAL; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    sturm::test_helpers::replay_ir(app.ctx()->ir, bits);

    const uint32_t expect_r = (a_val + b_val) % n_val;
    using sturm::test_helpers::read_reg_classical;
    const uint32_t a_out = read_reg_classical(bits, qi_a, W_VAL);
    const uint32_t b_out = read_reg_classical(bits, qi_b, W_VAL);
    const uint32_t n_out = read_reg_classical(bits, qi_n, W_VAL);
    const uint32_t r_out = read_reg_classical(bits, qi_r, W_VAL);
    assert(a_out == a_val && "replay: a register unchanged");
    assert(b_out == b_val && "replay: b register unchanged");
    assert(n_out == n_val && "replay: n register unchanged");
    assert(r_out == expect_r && "replay: r == (a+b) mod n");

    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "replay: ancilla bit not cleaned up");
    }

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "replay: pool live-count returns to pre-call value");

    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
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
                "(all a, b in [0, n) for n in [1, 4), APPEND+replay):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_replay_case<W>(a_val, b_val, n_val);
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
                "(%zu cases, seed=%u, n in [1, 8), APPEND+replay):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_replay_case<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (a + b) mod n matches "
                "classical reference\n", kW3Cases);

    std::printf("All sturm-yh3d.{1,2,3,4} tests passed.\n");
    return 0;
}
