// test_mul_mod_dsl_oneshot.cpp -- sturm-7cix Beat C: lib_mul_mod_dsl_oneshot
//                                   forward path tests (O(W) ancilla via
//                                   the in-place add-mod + doubling primitives).
//
// Coverage (mirrors sturm-kubb's chain test plan, restricted to odd n):
//   * n==0 short-circuit (no-op, no allocation)
//   * single classical (a=2, b=2, n=3) case driven through orkan (witness
//     against the real backend, not just a bit-flip program).
//   * W=2 exhaustive trace sweep over all (a, b) for ODD n in {1, 3}
//   * W=3 random sweep (50 cases, fixed seed=42, odd n in {1, 3, 5, 7})
//   * squaring-aliasing case (a == b passed to lib_mul_mod_dsl_oneshot —
//     used by lib_pow_mod_dsl)
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

// ── single classical case, simulator-driven (W=2, a=2, b=2, n=3) ─────────────
template <std::size_t W>
static void run_oneshot_classical_case(uint32_t a_val, uint32_t b_val,
                                       uint32_t n_val, uint32_t n_orkan,
                                       bool bypass_cap) {
    assert(a_val < n_val && b_val < n_val && (n_val & 1u) == 1u
           && "oneshot test: a, b in [0, n) and n odd");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int reserved[64];
    assert(n_reg <= 64u);
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u, bypass_cap};
    Reg<W> a = make_reg_blank<W>(0,         a_val, sc.sv());
    Reg<W> b = make_reg_blank<W>(W,         b_val, sc.sv());
    Reg<W> n = make_reg_n_seeded<W>(2 * W,  n_val, sc.sv());
    Reg<W> r = make_reg_blank<W>(3 * W,     0u,    sc.sv());

    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                                    n.bits.data(), W,
                                                    r.bits.data());

    const uint32_t expect_r = (a_val * b_val) % n_val;
    assert(read_reg(sc.sv(), a.qi.data(), W, n_orkan) == a_val
           && "oneshot: a register unchanged");
    assert(read_reg(sc.sv(), b.qi.data(), W, n_orkan) == b_val
           && "oneshot: b register unchanged");
    assert(read_reg(sc.sv(), n.qi.data(), W, n_orkan) == n_val
           && "oneshot: n register unchanged");
    assert(read_reg(sc.sv(), r.qi.data(), W, n_orkan) == expect_r
           && "oneshot: r == (a*b) mod n");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "oneshot: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace harness (APPEND mode + bit-vector replay) ────────────────
// Mirrors test_mul_mod_dsl.cpp's W=3 trace harness but seeds n with the
// 3-arg make_non_owning factory so the dispatch picks the oneshot path.
static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u; break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u; break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr, "trace: unsupported gate kind %d\n",
                     static_cast<int>(rec.kind));
        std::abort();
    }
}

static uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k)
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    return v;
}

template <std::size_t Wn>
static void run_oneshot_trace_case(uint32_t a_val, uint32_t b_val,
                                   uint32_t n_val, bool squaring = false) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << Wn));
    assert((n_val & 1u) == 1u && "trace test: n must be odd");
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
        sturm_backend_create(STURM_MODE_APPEND, 64u);
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
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    const uint32_t expect_r = (a_val * b_val) % n_val;
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
    // W=2 simulator: 4*W (inputs) + 2W+8 (oneshot peak) ≈ 16 qubits at W=2,
    //   plus headroom: 25 qubits is comfortable.  Bypass kMaxQubits=17 cap.
    constexpr uint32_t n_orkan_w2 = 25u;

    std::printf("sturm-7cix Beat C oneshot: n==0 short-circuit:\n");
    run_n_zero_oneshot();
    std::puts("  PASS: n==0 leaves a/b/n/r unchanged, no allocation");

    // Single W=2 simulator witness — the only orkan-driven correctness
    // case in the file.  Mirrors test_mul_mod_dsl.cpp's strategy of
    // keeping one end-to-end unitary witness while shifting bulk
    // coverage to APPEND-mode trace replays.
    std::printf("sturm-7cix Beat C oneshot: single W=2 simulator witness "
                "(2, 2, 3):\n");
    run_oneshot_classical_case<W2>(/*a=*/2u, /*b=*/2u, /*n=*/3u,
                                   n_orkan_w2, /*bypass=*/true);
    std::puts("  PASS: orkan-simulator forward for (2 * 2) mod 3 == 1");

    std::printf("sturm-7cix Beat C oneshot: W=2 exhaustive trace sweep "
                "(odd n in {1, 3}):\n");
    std::size_t cases_w2 = 0u;
    for (uint32_t n_val : {1u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_oneshot_trace_case<W2>(a_val, b_val, n_val);
                ++cases_w2;
            }
        }
    }
    // n=1 → 1 case; n=3 → 9 cases; total = 10.
    assert(cases_w2 == 10u && "W=2 sweep covered all (a, b) for odd n");
    std::printf("  PASS: %zu W=2 oneshot trace cases\n", cases_w2);

    std::printf("sturm-7cix Beat C oneshot: squaring-aliasing trace cases "
                "(a == b at W=2, odd n):\n");
    for (uint32_t n_val : {1u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            run_oneshot_trace_case<W2>(a_val, a_val, n_val, /*squaring=*/true);
        }
    }
    std::puts("  PASS: oneshot squaring-aliasing handles a == b correctly");

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

    std::printf("All sturm-7cix Beat C oneshot forward tests passed.\n");
    return 0;
}
