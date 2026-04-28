// test_mul_mod_dsl_oneshot_adjoint.cpp -- sturm-7cix Beat C: lib_mul_mod_dsl
//                                          oneshot adjoint round-trip tests.
//
// Forward (lib_mul_mod_dsl_oneshot) followed by adjoint
// (__lib_mul_mod_dsl_oneshot_adj) must zero r, preserve a/b/n, and return
// the QubitPool live-count to its pre-call value, for every (a, b, n)
// input the forward sweep covers (W=2 odd-n exhaustive sweep + W=3 random
// sweep + the W=2 single (2, 2, 3) simulator witness + the squaring case).
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

// W=2 simulator round-trip: forward + adjoint must zero r, preserve a/b/n.
template <std::size_t W>
static void run_roundtrip_sim(uint32_t a_val, uint32_t b_val, uint32_t n_val,
                              uint32_t n_orkan, bool bypass_cap) {
    assert(a_val < n_val && b_val < n_val && (n_val & 1u) == 1u);
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int reserved[64];
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
    assert(read_reg(sc.sv(), r.qi.data(), W, n_orkan) == expect_r
           && "forward: r == (a*b) mod n");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl_oneshot<BitProxy>>() must resolve");
    adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), W, r.bits.data());

    assert(read_reg(sc.sv(), a.qi.data(), W, n_orkan) == a_val
           && "round-trip: a unchanged");
    assert(read_reg(sc.sv(), b.qi.data(), W, n_orkan) == b_val
           && "round-trip: b unchanged");
    assert(read_reg(sc.sv(), n.qi.data(), W, n_orkan) == n_val
           && "round-trip: n unchanged");
    assert(read_reg(sc.sv(), r.qi.data(), W, n_orkan) == 0u
           && "round-trip: r returned to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count restored");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace harness for round-trip (mirrors the forward test) ───────
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
    default: std::abort();
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
static void run_roundtrip_trace(uint32_t a_val, uint32_t b_val, uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << Wn));
    assert((n_val & 1u) == 1u);
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(Wn);
    int qi_a[Wn], qi_b[Wn], qi_n[Wn], qi_r[Wn];
    for (std::size_t i = 0; i < Wn; ++i) qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    sturm::qbool a_own[Wn], b_own[Wn], n_own[Wn], r_own[Wn];
    sturm::BitProxy a_bits[Wn], b_bits[Wn], n_bits[Wn], r_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
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

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                    Wn, r_bits);
    const std::size_t fwd_gate_count = ctx->ir.size();

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr);
    adj_ptr(a_bits, b_bits, n_bits, Wn, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < fwd_gate_count; ++i)
        apply_gate_classical(bits, ctx->ir.at(i));
    assert(read_reg_classical(bits, qi_r, Wn) == (a_val * b_val) % n_val);

    for (std::size_t i = fwd_gate_count; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    assert(read_reg_classical(bits, qi_a, Wn) == a_val);
    assert(read_reg_classical(bits, qi_b, Wn) == b_val);
    assert(read_reg_classical(bits, qi_n, Wn) == n_val);
    assert(read_reg_classical(bits, qi_r, Wn) == 0u
           && "trace round-trip: r returned to |0>");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use);

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    constexpr std::size_t W2 = 2u;
    constexpr std::size_t W3 = 3u;
    constexpr uint32_t n_orkan_w2 = 25u;

    std::printf("sturm-7cix Beat C oneshot: W=2 single simulator witness "
                "(2, 2, 3):\n");
    run_roundtrip_sim<W2>(/*a=*/2u, /*b=*/2u, /*n=*/3u,
                          n_orkan_w2, /*bypass=*/true);
    std::puts("  PASS: orkan-simulator round-trip for (2, 2, 3)");

    std::printf("sturm-7cix Beat C oneshot: W=2 exhaustive trace round-trip "
                "(odd n in {1, 3}):\n");
    std::size_t cases_w2 = 0u;
    for (uint32_t n_val : {1u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_roundtrip_trace<W2>(a_val, b_val, n_val);
                ++cases_w2;
            }
        }
    }
    assert(cases_w2 == 10u);
    std::printf("  PASS: %zu W=2 oneshot round-trips\n", cases_w2);

    std::printf("sturm-7cix Beat C oneshot: W=3 random trace round-trip "
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
        run_roundtrip_trace<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 oneshot round-trips\n", kW3Cases);

    std::printf("All sturm-7cix Beat C oneshot adjoint tests passed.\n");
    return 0;
}
