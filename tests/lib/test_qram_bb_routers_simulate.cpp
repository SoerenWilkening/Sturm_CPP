// test_qram_bb_routers_simulate.cpp — sturm-44bt.1 (Beat BB1).
//
// Statevector pin for bb_setup_routers / bb_teardown_routers
// (include/sturm/detail/lib/qram_read_bb_routers.hpp).
// Plan: docs/plan_qram_backend_bb.md §5 Beat BB1. Fixture (Nprime, W)
// = (4, 2): 6 router qubits + 2 addr qubits = 8 sim qubits (orkan
// kMaxQubits=17). For each classical addr ∈ {0..3}: setup → on-path
// |L⟩/|R⟩ with P≈1, off-path |wait⟩; teardown → all (0,0), addr unchanged.
// LoC budget: ≤ 200.

#define STURM_BACKEND_ENABLED 1
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

namespace {

constexpr double      kTol   = 1e-6;
constexpr std::size_t Nprime = 4u;
constexpr std::size_t K      = 2u;          // log2(Nprime)
constexpr std::size_t W      = 2u;          // addr width

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

// Joint probability that (q0, q1) encode the classical pair (b0, b1).
static double prob_pair(orkan::state_t& sv, uint32_t n_total,
                        int q0, int q1, int b0, int b1) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        const int s0 = (q0 >= 0) ? static_cast<int>((s >> q0) & 1) : 0;
        const int s1 = (q1 >= 0) ? static_cast<int>((s >> q1) & 1) : 0;
        if (s0 == b0 && s1 == b1) p += std::norm(orkan::amplitude(sv, s));
    }
    return p;
}

// Marginal P(qubit q = b).
static double prob_single(orkan::state_t& sv, uint32_t n_total, int q, int b) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        const int sv_bit = (q >= 0) ? static_cast<int>((s >> q) & 1) : 0;
        if (sv_bit == b) p += std::norm(orkan::amplitude(sv, s));
    }
    return p;
}

// addr bit ℓ drives the level-ℓ router pick (left = 0, right = 1).
//   Nprime=4 layout: routers[0]=root, routers[1]=L child, routers[2]=R child.
//   Off-path child stays in (0, 0).
static void run_classical_addr(std::int64_t addr_val) {
    using namespace sturm;
    QubitPool::instance().reset_for_testing();

    int reserved_addr[K];
    for (uint32_t j = 0; j < K; ++j) {
        reserved_addr[j] = QubitPool::instance().allocate();
    }
    constexpr uint32_t n_orkan = 2u + 2u * (Nprime - 1u);  // 8
    SimCtx sc{n_orkan};

    // Encode classical addr via X gates.
    for (std::size_t j = 0; j < K; ++j) {
        if ((static_cast<std::uint64_t>(addr_val) >> j) & 1ULL) {
            orkan::apply_x(sc.sv(), static_cast<uint32_t>(reserved_addr[j]));
        }
    }
    qint_t<W> addr;
    for (std::size_t j = 0; j < K; ++j) {
        addr.qubits[j] = reserved_addr[j];
        addr.super_mask |= (1ULL << j);
    }
    addr.value = addr_val;

    std::array<detail_qram_bb::BBRouter, Nprime - 1u> routers{};

    // ── T1: setup. ───────────────────────────────────────────────────────────
    detail_qram_bb::bb_setup_routers<Nprime>(addr, routers);

    const int addr_bit0 = static_cast<int>(addr_val & 1);            // root pick
    const int addr_bit1 = static_cast<int>((addr_val >> 1) & 1);     // level-1 pick
    const int root_L    = 1 - addr_bit0;
    const int root_R    = addr_bit0;
    const int on_L      = 1 - addr_bit1;
    const int on_R      = addr_bit1;
    const std::size_t on_idx  = (addr_bit0 == 0) ? 1u : 2u;
    const std::size_t off_idx = (addr_bit0 == 0) ? 2u : 1u;

    auto check_pair = [&](std::size_t idx, int eL, int eR, const char* tag) {
        const double p = prob_pair(sc.sv(), n_orkan,
                                   routers[idx].is_left.qubits[0],
                                   routers[idx].is_right.qubits[0], eL, eR);
        if (std::abs(p - 1.0) > kTol) {
            std::fprintf(stderr,
                "BB1 sim %s: addr=%lld router[%zu] P((L=%d,R=%d))=%g != 1\n",
                tag, static_cast<long long>(addr_val), idx, eL, eR, p);
        }
        assert(std::abs(p - 1.0) < kTol);
    };

    // (a) Root + on-path child carry L/R per addr.
    check_pair(0,      root_L, root_R, "setup-root");
    check_pair(on_idx, on_L,   on_R,   "setup-onpath");
    // (b) Off-path child stays in |wait⟩.
    check_pair(off_idx, 0, 0, "setup-offpath");

    // ── T2: teardown. ────────────────────────────────────────────────────────
    detail_qram_bb::bb_teardown_routers<Nprime>(addr, routers);

    // (a) Every router back to (0, 0) with P ≈ 1.
    for (std::size_t k = 0; k < (Nprime - 1u); ++k) {
        check_pair(k, 0, 0, "teardown");
    }
    // (b) addr's qubits unchanged.
    for (std::size_t j = 0; j < K; ++j) {
        const int bit_j = static_cast<int>(
            (static_cast<std::uint64_t>(addr_val) >> j) & 1ULL);
        const double p = prob_single(sc.sv(), n_orkan, reserved_addr[j], bit_j);
        if (std::abs(p - 1.0) > kTol) {
            std::fprintf(stderr,
                "BB1 sim: addr=%lld addr.bit(%zu) post-teardown P(%d)=%g != 1\n",
                static_cast<long long>(addr_val), j, bit_j, p);
        }
        assert(std::abs(p - 1.0) < kTol);
    }

    // Cleanup — clear so destructors do not double-release.
    for (std::size_t k = 0; k < (Nprime - 1u); ++k) {
        routers[k].is_left.qubits[0]  = -1;
        routers[k].is_right.qubits[0] = -1;
    }
    addr.qubits.fill(-1);
    addr.super_mask = 0;
    for (uint32_t r = 0; r < K; ++r) {
        QubitPool::instance().release(reserved_addr[r]);
    }
}

}  // namespace

int main() {
    std::puts("sturm-44bt.1 Beat BB1: bb_setup/teardown_routers simulate test:");
    for (std::int64_t a = 0; a < static_cast<std::int64_t>(Nprime); ++a) {
        run_classical_addr(a);
    }
    std::puts("  PASS: classical addr ∈ {0..3} on-path / off-path routers");
    std::puts("  PASS: teardown restores every router to |wait⟩");
    std::puts("  PASS: teardown leaves addr unchanged");
    std::puts("test_qram_bb_routers_simulate: OK");
    return 0;
}
