// test_mul_mod_dsl_adjoint.cpp -- sturm-kubb.5 P2.5 mul-mod-dsl 2.5 adjoint
//                                  round-trip.
//
// Plan §4.3 beat 2.5: forward `lib_mul_mod_dsl(a, b, n, W, r)` followed by
// `__lib_mul_mod_dsl_adj(a, b, n, W, r)` must return `r` to |0> for every
// (a, b, n) input that beats 2.3 / 2.4 cover.  Inputs `a, b, n` must remain
// unchanged across the full forward+adjoint pair, and `QubitPool::in_use()`
// must return to its pre-call value (no leaked ancillas).
//
// Mirrors `tests/lib/test_add_mod_dsl_adjoint.cpp` (sturm-yh3d.5) adapted
// for chain-style mul_mod (sturm-kubb.{2,3,4}).  Per the issue brief the
// W=2 exhaustive sweep uses the APPEND-mode classical-trace harness from
// beat 2.4 because per-case state-vector simulation costs ~250 s on
// chain-style mul_mod and 14 forward+adjoint cases would exceed ctest's
// 3600 s timeout; one single classical (2, 2, 3) case is also exercised
// through the orkan simulator to keep an end-to-end unitary-semantics
// witness for `invert<&lib_mul_mod_dsl<BitProxy>>()`.  W=3 uses the same
// classical-trace harness (the simulator is infeasible at W=3: ~35 live
// qubits ⇒ 2^35 ≈ 34 GB amplitudes per case).

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
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
static constexpr std::size_t W = 2;
static constexpr std::size_t W3 = 3u;
static constexpr uint32_t n_orkan_w2_full = 25u;  // bypass kMaxQubits cap.

// ── orkan-simulator witness for one classical (a, b, n) case at W=2 ──────
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

// W=2 single classical (a, b, n) case driven through orkan in simulator
// mode.  Asserts forward sets r=(a*b) mod n, adjoint zeros r, a/b/n
// preserved, pool live-count returns to pre-call value.  Kept because the
// forward primitive's exact unitary semantics (incl. control-stack pushes
// / pops) are richer than the classical-trace replay — we want one
// witness that confirms `invert<>()` resolves to a working adjoint
// against the real backend, not just against a bit-flip program.
static void run_roundtrip_case_w2_sim(uint32_t a_val, uint32_t b_val,
                                      uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    sturm::OrkanBridge bridge;
    orkan::allocate(bridge.state(), n_orkan_w2_full);
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_SIMULATE, 64u);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);
    orkan::state_t& sv = bridge.state();
    Reg a = make_reg(0,         a_val, sv);
    Reg b = make_reg(W,         b_val, sv);
    Reg n = make_reg(2 * W,     n_val, sv);
    Reg r = make_reg(3 * W,     0u,    sv);

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), W,
                                            r.bits.data());

    const uint32_t expect_r = (a_val * b_val) % n_val;
    assert(read_reg(sv, a.qi.data(), W, n_orkan_w2_full) == a_val);
    assert(read_reg(sv, b.qi.data(), W, n_orkan_w2_full) == b_val);
    assert(read_reg(sv, n.qi.data(), W, n_orkan_w2_full) == n_val);
    assert(read_reg(sv, r.qi.data(), W, n_orkan_w2_full) == expect_r &&
           "forward: r == (a*b) mod n");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl<BitProxy>>() must resolve");
    adj_ptr(a.bits.data(), b.bits.data(), n.bits.data(), W,
            r.bits.data());

    assert(read_reg(sv, a.qi.data(), W, n_orkan_w2_full) == a_val);
    assert(read_reg(sv, b.qi.data(), W, n_orkan_w2_full) == b_val);
    assert(read_reg(sv, n.qi.data(), W, n_orkan_w2_full) == n_val);
    assert(read_reg(sv, r.qi.data(), W, n_orkan_w2_full) == 0u &&
           "adjoint: r returned to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use &&
           "round-trip: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace harness shared by W=2 sweep and W=3 sweep ─────────────
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

// Generic forward+adjoint round-trip via APPEND-mode capture + classical
// bit-vector replay.  Asserts forward portion sets r == (a*b) mod n,
// adjoint portion returns r to 0 with a/b/n preserved, every ancilla bit
// is back to 0, and pool live-count returns to pre-call value.
template <std::size_t Wn>
static void run_roundtrip_case_trace(uint32_t a_val, uint32_t b_val,
                                     uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(Wn);
    int qi_a[Wn], qi_b[Wn], qi_n[Wn], qi_r[Wn];
    for (std::size_t i = 0; i < Wn; ++i) qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[Wn], b_own[Wn], n_own[Wn], r_own[Wn];
    sturm::BitProxy a_bits[Wn], b_bits[Wn], n_bits[Wn], r_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
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

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                            Wn, r_bits);
    const std::size_t fwd_gate_count = ctx->ir.size();

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl<BitProxy>>() must resolve");
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
    assert(read_reg_classical(bits, qi_r, Wn) == (a_val * b_val) % n_val &&
           "trace: forward portion sets r == (a*b) mod n");

    for (std::size_t i = fwd_gate_count; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));
    assert(read_reg_classical(bits, qi_a, Wn) == a_val);
    assert(read_reg_classical(bits, qi_b, Wn) == b_val);
    assert(read_reg_classical(bits, qi_n, Wn) == n_val);
    assert(read_reg_classical(bits, qi_r, Wn) == 0u &&
           "trace: r returned to |0> after adjoint");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "trace: ancilla bit not cleaned up");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use &&
           "trace: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    std::printf("sturm-kubb.5 P2.5 mul-mod-dsl: adjoint round-trip "
                "(W=2 single simulator witness):\n");
    run_roundtrip_case_w2_sim(/*a=*/2u, /*b=*/2u, /*n=*/3u);
    std::puts("  PASS: orkan-simulator round-trip for (2, 2, 3)");

    std::printf("sturm-kubb.5 P2.5 mul-mod-dsl: adjoint round-trip "
                "(W=2 exhaustive trace, all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_roundtrip_case_trace<W>(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1; n=2 → 4; n=3 → 9; total = 14.
    assert(cases_run == 14u);
    std::printf("  PASS: %zu W=2 cases (forward + adjoint zeros r, "
                "preserves a/b/n, no leaked ancillas)\n", cases_run);

    // W=3 random sweep — classical-trace mode (simulator infeasible at W=3).
    constexpr uint32_t    kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-kubb.5 P2.5 mul-mod-dsl: adjoint round-trip "
                "(W=3 random sweep, %zu cases, seed=%u, classical trace):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_roundtrip_case_trace<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (forward + adjoint round-trip "
                "in trace mode)\n", kW3Cases);

    std::printf("All sturm-kubb.5 tests passed.\n");
    return 0;
}
