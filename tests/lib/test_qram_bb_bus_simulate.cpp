// test_qram_bb_bus_simulate.cpp — sturm-44bt.2 (Beat BB2).
//
// G1 statevector pin for bb_bus_traverse. Plan §5 Beat BB2 / §3.1.
//
// Fixtures (Nprime, W) ∈ {(2, 4), (4, 2)} — both ≤ 16 qubits when `a`
// is kept fully classical (no qubits allocated). bb_bus_traverse handles
// the classical-leaf case by emitting a controlled-XOR-classical pattern.
// Qubit budgets: (2,4) → 11; (4,2) → 16.
//
// Coverage: classical-addr sweep (both fixtures) — P(b==a[addr])≈1, every
// router/transit/bus qubit back to |0⟩, addr qubits restored, "a[k] qubits
// restored" vacuous (classical). Superposed-index sanity at (2,4): H on
// addr.bit(0) then run; P(b=a[0])≈0.5 and P(b=a[1])≈0.5. LoC ≤ 250.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_bb_bus.hpp"
#include "sturm/detail/lib/qram_read_bb_routers.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr double kTol = 1e-6;

struct SimCtx {
    sturm::OrkanBridge       bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q) {
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

static double prob_register_eq(orkan::state_t& sv, uint32_t n_total,
                               const int* qidx, std::size_t n_bits,
                               std::uint64_t target_val) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        std::uint64_t got = 0u;
        for (std::size_t k = 0; k < n_bits; ++k) {
            if (qidx[k] >= 0) got |= ((s >> qidx[k]) & 1u) << k;
        }
        if (got == target_val) p += std::norm(orkan::amplitude(sv, s));
    }
    return p;
}

static double prob_single(orkan::state_t& sv, uint32_t n_total, int q, int b) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        const int sv_bit = (q >= 0) ? static_cast<int>((s >> q) & 1) : 0;
        if (sv_bit == b) p += std::norm(orkan::amplitude(sv, s));
    }
    return p;
}

// Run one (Nprime, W, K) probe at a given classical or superposed addr.
//   - `addr_value` is the classical address (ignored when superposed=true).
//   - When superposed==true, H is applied to addr.bit(0) (LSB) so the
//     address is in (|0⟩ + |1⟩)/√2 over its LSB.
template <std::size_t Nprime, std::size_t W, std::size_t K>
static void run_probe(std::int64_t addr_value, bool superposed,
                      const std::uint64_t* a_vals,
                      double (*check_b)(double, double, double),
                      std::uint64_t expected_b0, std::uint64_t expected_b1) {
    using namespace sturm;
    constexpr std::size_t T = (Nprime >= 2u) ? (Nprime - 2u) : 0u;
    constexpr uint32_t n_orkan = static_cast<uint32_t>(
        K + 2u*W + T*W + 2u*(Nprime - 1u));
    static_assert(n_orkan <= kMaxQubits, "BB2 sim: exceeds orkan cap");

    QubitPool::instance().reset_for_testing();
    std::vector<int> all_idx;
    int addr_idx[K], b_idx[W], bus_idx[W];
    std::vector<int> transit_idx(T*W);
    for (std::size_t j = 0; j < K; ++j) { addr_idx[j] = QubitPool::instance().allocate(); all_idx.push_back(addr_idx[j]); }
    for (std::size_t j = 0; j < W; ++j) { b_idx[j]    = QubitPool::instance().allocate(); all_idx.push_back(b_idx[j]); }
    for (std::size_t j = 0; j < W; ++j) { bus_idx[j]  = QubitPool::instance().allocate(); all_idx.push_back(bus_idx[j]); }
    for (std::size_t j = 0; j < T*W; ++j) { transit_idx[j] = QubitPool::instance().allocate(); all_idx.push_back(transit_idx[j]); }

    SimCtx sc{n_orkan};

    if (superposed) {
        orkan::apply_h(sc.sv(), static_cast<uint32_t>(addr_idx[0]));
    } else {
        for (std::size_t j = 0; j < K; ++j) {
            if ((static_cast<std::uint64_t>(addr_value) >> j) & 1ULL) {
                orkan::apply_x(sc.sv(), static_cast<uint32_t>(addr_idx[j]));
            }
        }
    }

    qint_t<W> addr;
    for (std::size_t j = 0; j < K; ++j) { addr.qubits[j] = addr_idx[j]; addr.super_mask |= (1ULL << j); }
    addr.value = superposed ? 0 : addr_value;
    const auto addr_mask_before = addr.super_mask;

    qint_t<W> b;
    for (std::size_t j = 0; j < W; ++j) { b.qubits[j] = b_idx[j]; b.super_mask |= (1ULL << j); }
    b.value = 0;

    std::array<qbool, W> bus{};
    for (std::size_t j = 0; j < W; ++j) {
        bus[j].qubits[0] = bus_idx[j]; bus[j].super_mask = 1ULL; bus[j].owning_ = false;
    }

    std::array<std::array<qbool, W>, T> transits{};
    for (std::size_t t = 0; t < T; ++t) for (std::size_t j = 0; j < W; ++j) {
        transits[t][j].qubits[0] = transit_idx[t*W + j];
        transits[t][j].super_mask = 1ULL;
        transits[t][j].owning_ = false;
    }

    qint_t<W> a[Nprime];
    for (std::size_t k = 0; k < Nprime; ++k) a[k].value = static_cast<int64_t>(a_vals[k]);

    std::array<detail_qram_bb::BBRouter, Nprime - 1u> routers{};
    detail_qram_bb::bb_setup_routers<Nprime>(addr, routers);
    detail_qram_bb::bb_bus_traverse<Nprime, W>(routers, transits, bus, a, b);
    detail_qram_bb::bb_teardown_routers<Nprime>(addr, routers);

    if (superposed) {
        const double p0 = prob_register_eq(sc.sv(), n_orkan, b_idx, W, expected_b0);
        const double p1 = prob_register_eq(sc.sv(), n_orkan, b_idx, W, expected_b1);
        if (check_b(p0, p1, 0.5) > kTol) {
            std::fprintf(stderr,
                "BB2 sim sup: Nprime=%zu W=%zu P(b=0x%llX)=%g P(b=0x%llX)=%g\n",
                Nprime, W,
                static_cast<unsigned long long>(expected_b0), p0,
                static_cast<unsigned long long>(expected_b1), p1);
        }
        assert(std::abs(p0 - 0.5) < kTol);
        assert(std::abs(p1 - 0.5) < kTol);
    } else {
        const std::uint64_t expected_b = a_vals[static_cast<std::size_t>(addr_value)];
        const double p_b = prob_register_eq(sc.sv(), n_orkan, b_idx, W, expected_b);
        if (std::abs(p_b - 1.0) > kTol) {
            std::fprintf(stderr,
                "BB2 sim: Nprime=%zu W=%zu addr=%lld P(b==0x%llX)=%g\n",
                Nprime, W, static_cast<long long>(addr_value),
                static_cast<unsigned long long>(expected_b), p_b);
        }
        assert(std::abs(p_b - 1.0) < kTol);
    }

    // Bus / transits / routers all return to |0⟩.
    for (std::size_t j = 0; j < W; ++j)
        assert(std::abs(prob_single(sc.sv(), n_orkan, bus_idx[j], 0) - 1.0) < kTol);
    for (std::size_t j = 0; j < T*W; ++j)
        assert(std::abs(prob_single(sc.sv(), n_orkan, transit_idx[j], 0) - 1.0) < kTol);
    for (std::size_t k = 0; k < (Nprime - 1u); ++k) {
        assert(std::abs(prob_single(sc.sv(), n_orkan, routers[k].is_left.qubits[0], 0) - 1.0) < kTol);
        assert(std::abs(prob_single(sc.sv(), n_orkan, routers[k].is_right.qubits[0], 0) - 1.0) < kTol);
    }
    // addr.super_mask unchanged; classical addr-bit values restored.
    assert(addr.super_mask == addr_mask_before);
    if (!superposed) {
        for (std::size_t j = 0; j < K; ++j) {
            const int bit_j = static_cast<int>(
                (static_cast<std::uint64_t>(addr_value) >> j) & 1ULL);
            assert(std::abs(prob_single(sc.sv(), n_orkan, addr_idx[j], bit_j) - 1.0) < kTol);
        }
    }
    // a is classical (no qubits) — assertion is vacuous but pin it.
    for (std::size_t k = 0; k < Nprime; ++k)
        for (std::size_t j = 0; j < W; ++j) assert(a[k].qubits[j] < 0);

    // Cleanup.
    addr.qubits.fill(-1); addr.super_mask = 0;
    b.qubits.fill(-1);    b.super_mask    = 0;
    for (auto& q : bus) { q.qubits[0] = -1; q.super_mask = 0; q.owning_ = false; }
    for (auto& blk : transits) for (auto& q : blk) {
        q.qubits[0] = -1; q.super_mask = 0; q.owning_ = false;
    }
    for (auto& r : routers) {
        if (r.is_left.qubits[0]  >= 0) QubitPool::instance().release(r.is_left.qubits[0]);
        if (r.is_right.qubits[0] >= 0) QubitPool::instance().release(r.is_right.qubits[0]);
        r.is_left.qubits[0]  = -1;
        r.is_right.qubits[0] = -1;
    }
    for (int q : all_idx) QubitPool::instance().release(q);
}

// Helper: difference signal for the superposed-split check.
static double sup_check(double p0, double p1, double target) {
    const double d0 = std::abs(p0 - target);
    const double d1 = std::abs(p1 - target);
    return (d0 > d1) ? d0 : d1;
}

}  // namespace

int main() {
    std::puts("sturm-44bt.2 Beat BB2: bb_bus_traverse simulate test:");

    {
        constexpr std::uint64_t a24[2] = { 0xAu, 0x5u };
        for (std::int64_t a = 0; a < 2; ++a)
            run_probe<2u, 4u, 1u>(a, false, a24, sup_check, 0u, 0u);
    }
    std::puts("  PASS: (Nprime=2, W=4) classical addr sweep");

    {
        constexpr std::uint64_t a42[4] = { 0x2u, 0x1u, 0x3u, 0x0u };
        for (std::int64_t a = 0; a < 4; ++a)
            run_probe<4u, 2u, 2u>(a, false, a42, sup_check, 0u, 0u);
    }
    std::puts("  PASS: (Nprime=4, W=2) classical addr sweep");

    {
        constexpr std::uint64_t a24[2] = { 0xAu, 0x5u };
        run_probe<2u, 4u, 1u>(0, true, a24, sup_check, a24[0], a24[1]);
    }
    std::puts("  PASS: (Nprime=2, W=4) superposed addr.bit(0) split");

    std::puts("test_qram_bb_bus_simulate: OK");
    return 0;
}
