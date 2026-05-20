// test_qram_read_bb_padding.cpp — sturm-44bt.5 (Beat BB5).
// Power-of-2 padding pin (PRD §6, plan §5 BB5). N not a power of 2:
// instantiate at N' = 2^⌈log2 N⌉; phantom leaves [N, N') become
// W-qubit |0⟩^W ancilla blocks at call entry. Phase 2 leaf swap is
// uniform across real and phantom — phantom emits the same gate
// stream as a real quantum leaf. Assertions:
//   1. (N=3, W=2), classical a = {0x2, 0x1, 0x3}:
//      i=0 → b=0x2; i=2 → b=0x3; i=3 → b=0x0 (phantom |0⟩^W).
//   2. UB note: i ≥ 4 is UB per PRD §5; NOT tested.
//   3. RecordingSink gate count == (N'=4, W=2) §4.4 closed form
//      (CCX=32, CNOT=58, X=2 — recomputed from formula).
// LoC ≤ 200.

#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
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

constexpr double kTol = 1e-6;

struct Budget { std::size_t ccx, cnot, x; };
constexpr Budget closed_form_counts(std::size_t Np, std::size_t W) noexcept {
    return Budget{ 4u * (Np - 2u) + 4u * W * (Np - 1u),
                   2u * Np        + W * (8u * Np - 7u),
                   2u };
}

struct AppendCtx {
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    AppendCtx() {
        ctx = sturm_backend_create(STURM_MODE_APPEND); assert(ctx);
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~AppendCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
};

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q) {
        bridge.allocate_simulate(n_q);  // BB5: 18q > kMaxQubits=17
        ctx = sturm_backend_create(STURM_MODE_SIMULATE); assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

static double prob_reg_eq(orkan::state_t& sv, uint32_t n_total,
                          const int* qidx, std::size_t nb, std::uint64_t v) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        std::uint64_t g = 0u;
        for (std::size_t k = 0; k < nb; ++k)
            if (qidx[k] >= 0) g |= ((s >> qidx[k]) & 1u) << k;
        if (g == v) p += std::norm(orkan::amplitude(sv, s));
    }
    return p;
}

template <std::size_t W>
static void bind_idx(sturm::qint_t<W>& q, std::size_t nb, const int* idx,
                     std::int64_t v) {
    for (std::size_t j = 0; j < nb; ++j) {
        q.qubits[j] = idx[j]; q.super_mask |= (1ULL << j);
    }
    q.value = v;
}

template <std::size_t W>
static void alloc_qint(sturm::qint_t<W>& q, std::size_t nb, std::int64_t v) {
    for (std::size_t j = 0; j < nb; ++j) {
        q.qubits[j] = sturm::QubitPool::instance().allocate();
        q.super_mask |= (1ULL << j);
    }
    q.value = v;
}
template <std::size_t W>
static void release(sturm::qint_t<W>& q) {
    for (std::size_t j = 0; j < W; ++j)
        if (q.qubits[j] >= 0) {
            sturm::QubitPool::instance().release(q.qubits[j]); q.qubits[j] = -1;
        }
    q.super_mask = 0;
}

// (1) Statevector forward read at (N=3, W=2), classical a. Budget =
// K + W + 2(N'-1) + (N'-2)W + W + (N'-N)W = 2+2+6+4+2+2 = 18 qubits;
// > kMaxQubits=17 so SimCtx uses allocate_simulate (sturm-ovok).
static void test_sv_padding(std::uint64_t i_val, std::uint64_t expected_b) {
    using namespace sturm;
    constexpr std::size_t W = 2u, N = 3u, Nprime = 4u, K = 2u;
    constexpr std::uint64_t a_vals[N] = { 0x2u, 0x1u, 0x3u };
    constexpr uint32_t n_orkan = static_cast<uint32_t>(
        K + W + 2u * (Nprime - 1u) + (Nprime - 2u) * W + W
        + (Nprime - N) * W);

    QubitPool::instance().reset_for_testing();
    int addr_idx[K], b_idx[W];
    for (std::size_t j = 0; j < K; ++j) addr_idx[j] = QubitPool::instance().allocate();
    for (std::size_t j = 0; j < W; ++j) b_idx[j]    = QubitPool::instance().allocate();
    SimCtx sc{n_orkan};
    for (std::size_t j = 0; j < K; ++j)
        if ((i_val >> j) & 1u)
            orkan::apply_x(sc.sv(), static_cast<uint32_t>(addr_idx[j]));
    qint_t<W> i_reg, b_reg;
    bind_idx<W>(i_reg, K, addr_idx, static_cast<int64_t>(i_val));
    bind_idx<W>(b_reg, W, b_idx, 0);
    std::array<qint_t<W>, N> a;
    for (std::size_t k = 0; k < N; ++k) a[k].value = static_cast<int64_t>(a_vals[k]);
    QRAM_read(a, i_reg, b_reg);
    const double p_b = prob_reg_eq(sc.sv(), n_orkan, b_idx, W, expected_b);
    if (std::abs(p_b - 1.0) > kTol)
        std::fprintf(stderr, "BB5 fwd N=3 W=2 i=%llu P(b==0x%llX)=%g\n",
            static_cast<unsigned long long>(i_val),
            static_cast<unsigned long long>(expected_b), p_b);
    assert(std::abs(p_b - 1.0) < kTol && "BB5: P(b == expected) ≈ 1");
    i_reg.qubits.fill(-1); i_reg.super_mask = 0;
    b_reg.qubits.fill(-1); b_reg.super_mask = 0;
    for (std::size_t j = 0; j < K; ++j) QubitPool::instance().release(addr_idx[j]);
    for (std::size_t j = 0; j < W; ++j) QubitPool::instance().release(b_idx[j]);
}

// (3) Gate count == (N'=4, W=2) closed form. Promote each real a[k]
// to quantum so leaf CSWAPs emit 2 CNOT + 1 CCX per scalar. Phantom
// leaf is BB-allocated (W qubits to |0⟩) — same CSWAP stream.
static void test_gate_count_padding() {
    using namespace sturm;
    constexpr std::size_t W = 2u, N = 3u, Nprime = 4u, K = 2u;
    constexpr std::uint64_t a_vals[N] = { 0x2u, 0x1u, 0x3u };

    QubitPool::instance().reset_for_testing();
    AppendCtx ac;

    qint_t<W> i_reg, b_reg;
    alloc_qint<W>(i_reg, K, 2);
    alloc_qint<W>(b_reg, W, 0);
    std::array<qint_t<W>, N> a;
    for (std::size_t k = 0; k < N; ++k)
        alloc_qint<W>(a[k], W, static_cast<int64_t>(a_vals[k]));

    const std::size_t ir_before = ac.ctx->ir.size();
    QRAM_read(a, i_reg, b_reg);

    constexpr Budget expected = closed_form_counts(Nprime, W);
    std::size_t got_ccx = 0u, got_cnot = 0u, got_x = 0u;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        switch (static_cast<int>(ac.ctx->ir.at(r).kind)) {
            case STURM_GATE_X:   ++got_x;    break;
            case STURM_GATE_CX:  ++got_cnot; break;
            case STURM_GATE_CCX: ++got_ccx;  break;
            default:
                std::fprintf(stderr, "BB5: unexpected IR kind %d at r=%zu\n",
                    static_cast<int>(ac.ctx->ir.at(r).kind), r);
                assert(false && "BB5: gate-set ⊆ {X, CX, CCX}");
        }
    }
    if (got_x != expected.x || got_cnot != expected.cnot
        || got_ccx != expected.ccx) {
        std::fprintf(stderr, "BB5 §4.4 (N=%zu, Np=%zu, W=%zu): "
            "X=%zu/%zu CX=%zu/%zu CCX=%zu/%zu\n",
            N, Nprime, W, got_x, expected.x, got_cnot, expected.cnot,
            got_ccx, expected.ccx);
    }
    assert(got_x    == expected.x    && "BB5 §4.4: X count");
    assert(got_cnot == expected.cnot && "BB5 §4.4: CNOT count");
    assert(got_ccx  == expected.ccx  && "BB5 §4.4: CCX count");

    release<W>(i_reg); release<W>(b_reg);
    for (std::size_t k = 0; k < N; ++k) release<W>(a[k]);
}

}  // namespace
int main() {
    std::puts("sturm-44bt.5 Beat BB5: QRAM_read power-of-2 padding test:");
    test_sv_padding(/*i=*/0u, /*expected=*/0x2u);
    test_sv_padding(/*i=*/2u, /*expected=*/0x3u);
    test_sv_padding(/*i=*/3u, /*expected=*/0x0u);
    std::puts("  PASS: (N=3, W=2) statevector forward reads (i ∈ {0, 2, 3})");
    test_gate_count_padding();
    std::puts("  PASS: (N=3, W=2) gate count == (N'=4, W=2) §4.4 closed form");
    std::puts("test_qram_read_bb_padding: OK");
    return 0;
}
