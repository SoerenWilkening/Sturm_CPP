// test_mul_mod_dsl_oneshot.cpp -- sturm-7cix Beat C: lib_mul_mod_dsl_oneshot
//                                   forward path tests (O(W) ancilla via
//                                   the in-place add-mod + doubling primitives).
//
// Coverage (sturm-4oot.4 extends sturm-kubb's chain plan to ALL n):
//   * n==0 short-circuit (no-op, no allocation)
//   * single classical (a=2, b=2, n=3) case driven through orkan (witness
//     against the real backend, not just a bit-flip program).
//   * W=2 exhaustive trace sweep over all (a, b) for n in {1, 2, 3}
//     (covers the new even-n case n=2 — sturm-4oot.4; n=4 needs W>=3
//     to encode in the modulus register, so it is exercised at W=3)
//   * W=3 exhaustive trace sweep over all (a, b) for even n in {4, 6}
//     (sturm-4oot.4 even-n exhaustive at W=3; n=8 exercised at W=4
//     since it doesn't fit in a 3-bit modulus register) plus the
//     legacy odd-n random sweep (50 cases, fixed seed=42, n in
//     {1, 3, 5, 7})
//   * W=4 exhaustive even-n trace sweep at n=8 (sturm-4oot.4)
//   * W=4 random spot checks at n=10, 12, 14 (sturm-4oot.4 even-n)
//   * W=5 random spot checks at n=10, 12, 14 (sturm-4oot.4 even-n)
//   * squaring-aliasing case (a == b passed to lib_mul_mod_dsl_oneshot —
//     used by lib_pow_mod_dsl), now covering n in {1, 2, 3} at W=2
//
// Most legs use the APPEND-mode classical-trace harness (no orkan
// statevector simulation): the oneshot algorithm is built entirely from
// classical-reversible gates, so a bit-flip replay over the captured IR
// is a faithful reference and avoids the O(2^N) per-gate cost the
// simulator pays.  One W=2 simulator-driven case is kept as an end-to-end
// witness for the unitary-semantics path (matches sturm-kubb's strategy
// in test_mul_mod_dsl.cpp / test_mul_mod_dsl_adjoint.cpp).
//
// CRUCIAL: the dispatch in `lib_mul_mod_dsl` (mul_mod_dsl.hpp) reads
// `n_bits[0]`'s classical-tracked value to pick the oneshot vs. chain
// path.  These tests therefore seed the qbool's `.value` field via the
// 3-arg `qbool::make_non_owning(idx, val, mask)` factory — the 1-arg
// factory used by sturm-kubb's existing tests leaves `.value=0` and
// would route here back to the chain implementation, defeating the
// purpose of these tests.  We call `lib_mul_mod_dsl_oneshot` DIRECTLY
// to avoid the dispatch entirely; this is the cleanest way to pin the
// oneshot's correctness independent of the dispatcher.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include "classical_replay.hpp"  // sturm-scin: shared APPEND+replay helpers

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
    // For a, b, r (no classical-value hint needed by the dispatch; safe
    // to keep .value=0 because a/b/r are operand registers, not modulus).
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
    // Modulus register: seed BOTH the simulator state (via apply_x) AND
    // the qbool's classical .value field (via the 3-arg make_non_owning),
    // so the dispatch in `lib_mul_mod_dsl` can read the classical odd-n
    // hint.  This is the same factory call `qint_modular.hpp`'s
    // `make_proxy_quad` uses for the public `sturm::mul_mod` wrapper.
    Reg<W> r;
    for (std::size_t i = 0; i < W; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(
            r.qi[i],
            /*val=*/static_cast<int64_t>((val >> i) & 1u),
            /*mask=*/0ULL);  // classical (super_mask=0)
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

// ── n==0 no-op test ──────────────────────────────────────────────────────────
static void run_n_zero_oneshot() {
    constexpr std::size_t W = 2u;
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int reserved[16];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{17u, 64u};
    Reg<W> a = make_reg_blank<W>(0,         /*val=*/1u, sc.sv());
    Reg<W> b = make_reg_blank<W>(W,         /*val=*/2u, sc.sv());
    Reg<W> n = make_reg_n_seeded<W>(2 * W,  /*val=*/3u, sc.sv());
    Reg<W> r = make_reg_blank<W>(3 * W,     /*val=*/0u, sc.sv());

    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                                    n.bits.data(), /*n=*/0u,
                                                    r.bits.data());

    assert(read_reg(sc.sv(), a.qi.data(), W, 17u) == 1u);
    assert(read_reg(sc.sv(), b.qi.data(), W, 17u) == 2u);
    assert(read_reg(sc.sv(), n.qi.data(), W, 17u) == 3u);
    assert(read_reg(sc.sv(), r.qi.data(), W, 17u) == 0u);
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "n==0 oneshot: pool live-count unchanged");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace harness (APPEND mode + bit-vector replay) ────────────────
//
// sturm-scin: the prior single-classical-case SIMULATE smoke
// (`run_oneshot_classical_case<W=2>(2,2,3, n_orkan=25, bypass)`) was the
// dominant cost in this test (~115 s on a 2^25-amplitude orkan state
// vector).  Algorithm correctness for non-zero (a, b) is exhaustively
// validated by the APPEND+replay sweeps below; the run_n_zero_oneshot
// SIMULATE smoke retains end-to-end statevector coverage of the n==0
// short-circuit.  The local apply_gate_classical / read_reg_classical
// helpers also moved to tests/lib/classical_replay.hpp.

template <std::size_t Wn>
static void run_oneshot_trace_case(uint32_t a_val, uint32_t b_val,
                                   uint32_t n_val, bool squaring = false) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << Wn));
    // sturm-4oot.4: n parity-agnostic; the inner doubling primitive
    // (sturm-4oot.1) and the (W-1)-bit lt_flags register threaded
    // through this layer (sturm-4oot.3) absorb the witness for even n.
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg_full = 4u * static_cast<uint32_t>(Wn);
    constexpr uint32_t n_reg_sq   = 3u * static_cast<uint32_t>(Wn);
    const uint32_t n_reg = squaring ? n_reg_sq : n_reg_full;
    int qi_a[Wn], qi_b[Wn], qi_n[Wn], qi_r[Wn];
    for (std::size_t i = 0; i < Wn; ++i) qi_a[i] = sturm::QubitPool::instance().allocate();
    if (!squaring) {
        for (std::size_t i = 0; i < Wn; ++i) qi_b[i] = sturm::QubitPool::instance().allocate();
    } else {
        // Squaring: alias b onto a (no extra allocation).
        for (std::size_t i = 0; i < Wn; ++i) qi_b[i] = qi_a[i];
    }
    for (std::size_t i = 0; i < Wn; ++i) qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[Wn], b_own[Wn], n_own[Wn], r_own[Wn];
    sturm::BitProxy a_bits[Wn], b_bits[Wn], n_bits[Wn], r_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        if (!squaring) {
            b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        }
        // Seed n.value classically so dispatch finds the odd-n hint.
        n_own[i] = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u),
            /*mask=*/0ULL);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = squaring ? sturm::BitProxy(a_own[i]) : sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                    Wn, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if (!squaring && ((b_val >> i) & 1u)) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    sturm::test_helpers::replay_ir(ctx->ir, bits);

    const uint32_t expect_r = (a_val * b_val) % n_val;
    using sturm::test_helpers::read_reg_classical;
    assert(read_reg_classical(bits, qi_a, Wn) == a_val
           && "oneshot trace: a unchanged");
    if (!squaring) {
        assert(read_reg_classical(bits, qi_b, Wn) == b_val
               && "oneshot trace: b unchanged");
    }
    assert(read_reg_classical(bits, qi_n, Wn) == n_val
           && "oneshot trace: n unchanged");
    assert(read_reg_classical(bits, qi_r, Wn) == expect_r
           && "oneshot trace: r == (a*b) mod n");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "oneshot trace: ancilla not cleaned");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "oneshot trace: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    if (!squaring) {
        for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_b[i]);
    }
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    constexpr std::size_t W2 = 2u;
    constexpr std::size_t W3 = 3u;

    std::printf("sturm-7cix Beat C oneshot: n==0 short-circuit:\n");
    run_n_zero_oneshot();
    std::puts("  PASS: n==0 leaves a/b/n/r unchanged, no allocation");

    // sturm-scin: the W=2 single-case orkan-simulator witness was retired
    // (it cost ~115 s on a 2^25-amplitude state vector).  The exhaustive
    // APPEND+replay sweeps below cover correctness; the n==0
    // short-circuit smoke above retains end-to-end SIMULATE coverage.

    std::printf("sturm-7cix Beat C oneshot: W=2 exhaustive trace sweep "
                "(n in {1, 2, 3}, sturm-4oot.4 even-n n=2 included):\n");
    std::size_t cases_w2 = 0u;
    // n must satisfy n_val < (1u << Wn) = 4 at W=2.  n=4 is exercised
    // at W=3 in the next sweep.
    for (uint32_t n_val : {1u, 2u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_oneshot_trace_case<W2>(a_val, b_val, n_val);
                ++cases_w2;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4; n=3 → 9; total = 14.
    assert(cases_w2 == 14u && "W=2 sweep covered all (a, b) for n in {1, 2, 3}");
    std::printf("  PASS: %zu W=2 oneshot trace cases\n", cases_w2);

    std::printf("sturm-7cix Beat C oneshot: squaring-aliasing trace cases "
                "(a == b at W=2, n in {1, 2, 3}):\n");
    for (uint32_t n_val : {1u, 2u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            run_oneshot_trace_case<W2>(a_val, a_val, n_val, /*squaring=*/true);
        }
    }
    std::puts("  PASS: oneshot squaring-aliasing handles a == b correctly");

    // sturm-4oot.4: W=3 exhaustive even-n sweep (n in {4, 6}; n=8
    // doesn't fit in a 3-bit modulus register, exercised at W=4 below).
    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=3 exhaustive trace sweep "
                "(even n in {4, 6}):\n");
    std::size_t cases_w3_even = 0u;
    for (uint32_t n_val : {4u, 6u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_oneshot_trace_case<W3>(a_val, b_val, n_val);
                ++cases_w3_even;
            }
        }
    }
    // 16 + 36 = 52.
    assert(cases_w3_even == 52u
           && "W=3 even-n sweep covered all (a, b) for n in {4, 6}");
    std::printf("  PASS: %zu W=3 even-n oneshot trace cases\n",
                cases_w3_even);

    std::printf("sturm-7cix Beat C oneshot: W=3 random trace sweep "
                "(50 cases, seed=42, odd n in {1, 3, 5, 7}):\n");
    constexpr uint32_t kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::mt19937 rng(kW3Seed);
    std::array<uint32_t, 4> odd_ns = {1u, 3u, 5u, 7u};
    std::uniform_int_distribution<uint32_t> n_idx(0u, 3u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = odd_ns[n_idx(rng)];
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_oneshot_trace_case<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 oneshot trace cases\n", kW3Cases);

    // sturm-4oot.4: W=4 and W=5 random spot checks at even n in {10, 12, 14}.
    constexpr std::size_t W4 = 4u;
    constexpr std::size_t W5 = 5u;
    std::array<uint32_t, 3> even_ns_4_5 = {10u, 12u, 14u};

    // sturm-4oot.4: W=4 exhaustive even-n sweep at n=8 (the third entry
    // from the issue's "W=3: n=4, 6, 8 exhaustive" list, hoisted to W=4
    // so the modulus fits in the bit-register).
    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=4 exhaustive trace sweep "
                "(even n=8):\n");
    {
        constexpr uint32_t n_val = 8u;
        std::size_t cases_w4_n8 = 0u;
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_oneshot_trace_case<W4>(a_val, b_val, n_val);
                ++cases_w4_n8;
            }
        }
        assert(cases_w4_n8 == 64u);
        std::printf("  PASS: %zu W=4 oneshot trace cases at n=8\n",
                    cases_w4_n8);
    }

    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=4 random trace spot checks "
                "(20 cases, seed=4204, even n in {10, 12, 14}):\n");
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
            run_oneshot_trace_case<W4>(a_val, b_val, n_val);
        }
        std::printf("  PASS: %zu W=4 even-n oneshot trace spot checks\n",
                    kCases);
    }

    std::printf("sturm-7cix/sturm-4oot.4 oneshot: W=5 random trace spot checks "
                "(20 cases, seed=4205, even n in {10, 12, 14}):\n");
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
            run_oneshot_trace_case<W5>(a_val, b_val, n_val);
        }
        std::printf("  PASS: %zu W=5 even-n oneshot trace spot checks\n",
                    kCases);
    }

    // sturm-8n73: high-W trace spot checks validating that kMaxN > 32
    // works.  Peak live ancillas scale as 7W+7; sturm-5jta dropped the
    // legacy compile-time pool cap, so any W is supported (the pool
    // grows on demand).  The W=64 path is exercised separately by
    // test_modular_arith_highw_trace.
    constexpr std::size_t W8  = 8u;
    constexpr std::size_t W16 = 16u;

    std::printf("sturm-8n73 oneshot: W=8 random trace spot checks "
                "(10 cases, seed=8073):\n");
    {
        constexpr uint32_t kSeed = 8073u;
        constexpr std::size_t kCases = 10u;
        std::mt19937 rng8(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u, (1u << W8) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng8);
            std::uniform_int_distribution<uint32_t> ab(0u, n_val - 1u);
            uint32_t a_val = ab(rng8);
            uint32_t b_val = ab(rng8);
            run_oneshot_trace_case<W8>(a_val, b_val, n_val);
        }
        std::printf("  PASS: %zu W=8 oneshot trace spot checks\n", kCases);
    }

    std::printf("sturm-8n73 oneshot: W=16 random trace spot checks "
                "(5 cases, seed=8074):\n");
    {
        constexpr uint32_t kSeed = 8074u;
        constexpr std::size_t kCases = 5u;
        std::mt19937 rng16(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u,
                                                        (1u << W16) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng16);
            std::uniform_int_distribution<uint32_t> ab(0u, n_val - 1u);
            uint32_t a_val = ab(rng16);
            uint32_t b_val = ab(rng16);
            run_oneshot_trace_case<W16>(a_val, b_val, n_val);
        }
        std::printf("  PASS: %zu W=16 oneshot trace spot checks\n", kCases);
    }

    std::printf("All sturm-7cix Beat C oneshot forward tests passed.\n");
    return 0;
}
