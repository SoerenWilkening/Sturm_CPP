// test_mul_mod_dsl_oneshot_adjoint.cpp -- sturm-7cix Beat C: lib_mul_mod_dsl
//                                          oneshot adjoint round-trip tests.
//
// Forward (lib_mul_mod_dsl_oneshot) followed by adjoint
// (__lib_mul_mod_dsl_oneshot_adj) must zero r, preserve a/b/n, and return
// the QubitPool live-count to its pre-call value, for every (a, b, n)
// input the forward sweep covers.  Mirrors the forward test's coverage
// expansion in sturm-4oot.4: W=2 exhaustive over n in {1,2,3} (n=4 at
// W=2 doesn't fit in the 2-bit modulus register), W=3 exhaustive over
// even n in {4, 6} (n=8 hoisted to W=4 for the same reason), W=3
// random odd-n sweep, W=4 exhaustive at n=8, plus W=4 and W=5 random
// spot checks at even n in {10, 12, 14}.
//
// Mirrors `tests/lib/test_double_mod_dsl_adjoint.cpp` adapted for the
// mul_mod oneshot helper.  Also exercises the
// `invert<&lib_mul_mod_dsl_oneshot<BitProxy>>()` resolution against the
// real backend (the static_assert on the resolved adj pointer stops a
// regression in the STURM_REGISTER_ADJOINT plumbing).
//
// CRUCIAL: the dispatcher in `lib_mul_mod_dsl_adj.hpp` reads
// `n_bits[0]`'s classical-tracked value to pick the oneshot vs. chain
// adjoint.  These tests therefore call the oneshot helpers DIRECTLY to
// pin the adjoint's correctness independent of the dispatcher.
//
// sturm-scin: forward+adjoint round-trip cases moved to a shared
// APPEND+classical-replay driver.  Forward IR and adjoint IR are both
// captured into the SAME ctx->ir, then replayed as a single bit-flip
// program over the classical bit-vector.  Final state must have r ==
// 0, a/b/n unchanged, and every ancilla qubit back to 0.  Per-case
// cost drops from O(2^n_orkan * |IR|) to O(|IR|).  A single W=2
// SIMULATE smoke case is retained for end-to-end statevector coverage.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include "classical_replay.hpp"  // sturm-scin: APPEND+replay helpers

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

static constexpr double kTol = 1e-9;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 64u, bool bypass = false) {
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

template <std::size_t W>
struct Reg {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

template <std::size_t W>
static Reg<W> make_reg_blank(int base, uint32_t val, orkan::state_t& sv) {
    Reg<W> r;
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

template <std::size_t W>
static Reg<W> make_reg_n_seeded(int base, uint32_t val, orkan::state_t& sv) {
    Reg<W> r;
    for (std::size_t i = 0; i < W; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(
            r.qi[i],
            /*val=*/static_cast<int64_t>((val >> i) & 1u),
            /*mask=*/0ULL);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

// SIMULATE smoke: n==0 short-circuit round-trip.  forward + adjoint with
// n==0 must both no-op, leaving a, b, n, r bit-identical to their inputs
// and the QubitPool live-count unchanged (no ancillas allocated by
// either pass).  Uses kMaxQubits=17 so the state vector stays small;
// the n==0 path doesn't materialise any ancillas, so any value of
// n_orkan that covers 4*W = 8 input qubits is fine.
//
// sturm-scin: the previous W=2 full-round-trip SIMULATE smoke at
// n_orkan=25 (2^25 ≈ 33M amplitudes per gate, twice — forward + adjoint)
// cost ~230 s; the APPEND+replay W=2 (2, 2, 3) trace case (covered by
// the exhaustive W=2 sweep below) is the algorithm-level witness, so
// the SIMULATE smoke shrinks to the n==0 no-op-pair path for cheap
// end-to-end statevector coverage of the dispatch + adjoint resolution.
template <std::size_t W>
static void run_n_zero_roundtrip_sim() {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int reserved[64];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{17u, 64u};
    Reg<W> a = make_reg_blank<W>(0,         /*val=*/1u, sc.sv());
    Reg<W> b = make_reg_blank<W>(W,         /*val=*/2u, sc.sv());
    Reg<W> n = make_reg_n_seeded<W>(2 * W,  /*val=*/3u, sc.sv());
    Reg<W> r = make_reg_blank<W>(3 * W,     /*val=*/0u, sc.sv());

    // Forward with width n == 0; oneshot short-circuits.
    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                                    n.bits.data(), /*n=*/0u,
                                                    r.bits.data());

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl_oneshot<BitProxy>>() must resolve");
    adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), /*n=*/0u,
            r.bits.data());

    assert(read_reg(sc.sv(), a.qi.data(), W, 17u) == 1u
           && "n==0 round-trip: a unchanged");
    assert(read_reg(sc.sv(), b.qi.data(), W, 17u) == 2u
           && "n==0 round-trip: b unchanged");
    assert(read_reg(sc.sv(), n.qi.data(), W, 17u) == 3u
           && "n==0 round-trip: n unchanged");
    assert(read_reg(sc.sv(), r.qi.data(), W, 17u) == 0u
           && "n==0 round-trip: r unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "n==0 round-trip: pool live-count unchanged");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── APPEND+classical-replay round-trip driver (sturm-scin) ─────────────────
//
// Captures both the forward and the adjoint IR into the SAME APPEND-mode
// ctx->ir (the AppendContext stays installed across both calls), then
// replays the entire IR as a deterministic bit-flip program over a
// classical bit-vector.  Asserts that after the full round-trip:
//   - a, b, n registers are unchanged (reversibility of inputs),
//   - r register returned to |0> (forward+adjoint identity on output),
//   - every ancilla qubit (q >= 4*W_VAL) is back to 0 (clean tear-down),
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas across the forward/adjoint pair).
//
// CRUCIAL: the n register's qbool must be seeded with the classical
// `.value` via the 3-arg `qbool::make_non_owning(qi, val, mask)` factory.
// The 1-arg version leaves `.value=0` and routes the dispatcher to the
// chain implementation; here we call the oneshot helper directly, but
// the helper itself peeks at `n_bits[0].value` to drive the doubling
// primitive, so the classical hint must still be threaded through.

template <std::size_t W_VAL>
static void run_replay_case_oneshot_adjoint(uint32_t a_val, uint32_t b_val,
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
        // Seed n.value classically so the oneshot's internal dispatch
        // sees the classical odd/even hint.
        n_own[i] = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u),
            /*mask=*/0ULL);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    // Capture forward + adjoint into the SAME ctx->ir so replay walks
    // the round-trip as one continuous bit-flip program.
    sturm::test_helpers::AppendContext app;

    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                    W_VAL, r_bits);

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl_oneshot<BitProxy>>() must resolve");
    adj_ptr(a_bits, b_bits, n_bits, W_VAL, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W_VAL; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        // r starts at |0>.
    }
    sturm::test_helpers::replay_ir(app.ctx()->ir, bits);

    using sturm::test_helpers::read_reg_classical;
    assert(read_reg_classical(bits, qi_a, W_VAL) == a_val
           && "replay round-trip: a unchanged");
    assert(read_reg_classical(bits, qi_b, W_VAL) == b_val
           && "replay round-trip: b unchanged");
    assert(read_reg_classical(bits, qi_n, W_VAL) == n_val
           && "replay round-trip: n unchanged");
    assert(read_reg_classical(bits, qi_r, W_VAL) == 0u
           && "replay round-trip: r returned to |0>");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "replay round-trip: ancilla not cleaned");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "replay round-trip: pool live-count restored");

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
    constexpr std::size_t W2 = 2u;
    constexpr std::size_t W3 = 3u;

    std::printf("sturm-7cix Beat C oneshot: SIMULATE n==0 round-trip smoke "
                "(W=2):\n");
    run_n_zero_roundtrip_sim<W2>();
    std::puts("  PASS: orkan-simulator forward+adjoint round-trip with n==0");

    std::printf("sturm-7cix Beat C oneshot: W=2 exhaustive replay round-trip "
                "(n in {1, 2, 3}, sturm-4oot.4 even-n n=2 included):\n");
    std::size_t cases_w2 = 0u;
    // n_val < (1u << Wn) = 4 at W=2; n=4 exercised at W=3 below.
    for (uint32_t n_val : {1u, 2u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_replay_case_oneshot_adjoint<W2>(a_val, b_val, n_val);
                ++cases_w2;
            }
        }
    }
    // 1 + 4 + 9 = 14.
    assert(cases_w2 == 14u);
    std::printf("  PASS: %zu W=2 oneshot round-trips\n", cases_w2);

    // sturm-4oot.4: W=3 exhaustive even-n round-trip (n in {4, 6}; n=8
    // doesn't fit in a 3-bit register, exercised at W=4 below).
    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=3 exhaustive replay "
                "round-trip (even n in {4, 6}):\n");
    std::size_t cases_w3_even = 0u;
    for (uint32_t n_val : {4u, 6u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_replay_case_oneshot_adjoint<W3>(a_val, b_val, n_val);
                ++cases_w3_even;
            }
        }
    }
    assert(cases_w3_even == 52u);
    std::printf("  PASS: %zu W=3 even-n oneshot round-trips\n",
                cases_w3_even);

    std::printf("sturm-7cix Beat C oneshot: W=3 random replay round-trip "
                "(50 cases, seed=42):\n");
    constexpr uint32_t kW3Seed = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::mt19937 rng(kW3Seed);
    std::array<uint32_t, 4> odd_ns = {1u, 3u, 5u, 7u};
    std::uniform_int_distribution<uint32_t> n_idx(0u, 3u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = odd_ns[n_idx(rng)];
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_replay_case_oneshot_adjoint<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 oneshot round-trips\n", kW3Cases);

    // sturm-4oot.4: W=4 and W=5 random spot checks at even n in {10, 12, 14}.
    constexpr std::size_t W4 = 4u;
    constexpr std::size_t W5 = 5u;
    std::array<uint32_t, 3> even_ns_4_5 = {10u, 12u, 14u};

    // sturm-4oot.4: W=4 exhaustive round-trip at even n=8 (hoisted from
    // the issue's "W=3: n=4, 6, 8" so the modulus fits in the register).
    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=4 exhaustive replay "
                "round-trip (even n=8):\n");
    {
        constexpr uint32_t n_val = 8u;
        std::size_t cases_w4_n8 = 0u;
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_replay_case_oneshot_adjoint<W4>(a_val, b_val, n_val);
                ++cases_w4_n8;
            }
        }
        assert(cases_w4_n8 == 64u);
        std::printf("  PASS: %zu W=4 oneshot round-trips at n=8\n",
                    cases_w4_n8);
    }

    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=4 random round-trip spot "
                "checks (20 cases, seed=4204, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4204u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng4(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx_4(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx_4(rng4)];
            std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
            uint32_t a_val = ab_dist(rng4);
            uint32_t b_val = ab_dist(rng4);
            run_replay_case_oneshot_adjoint<W4>(a_val, b_val, n_val);
        }
        std::printf("  PASS: %zu W=4 even-n oneshot round-trip spot checks\n",
                    kCases);
    }

    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=5 random round-trip spot "
                "checks (20 cases, seed=4205, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4205u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng5(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx_5(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx_5(rng5)];
            std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
            uint32_t a_val = ab_dist(rng5);
            uint32_t b_val = ab_dist(rng5);
            run_replay_case_oneshot_adjoint<W5>(a_val, b_val, n_val);
        }
        std::printf("  PASS: %zu W=5 even-n oneshot round-trip spot checks\n",
                    kCases);
    }

    std::printf("All sturm-7cix Beat C oneshot adjoint tests passed.\n");
    return 0;
}
