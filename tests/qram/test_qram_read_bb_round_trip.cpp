// test_qram_read_bb_round_trip.cpp — sturm-44bt.3 (Beat BB3).
// G3+G6 round-trip pin (PRD §2 / plan §5 Beat BB3 / §3.1). Issue rows:
//   5. (statevector) (N=2, W=4) and (N=4, W=2): forward → P(b==a[i])≈1;
//      adjoint then → P(b==0)≈1, every a[k] restored, classical i sweep.
//   6. (RecordingSink) (N=8, W=4) — too large for orkan; every
//      (op, qubits, controls) triple appears even-times across fwd+adj.
//   7. (sup-i) (N=2, W=4): H on i.bit(0); fwd+adj leaves b in |0⟩^W and
//      i.super_mask unchanged. LoC budget: ≤ 250.

#define STURM_BACKEND_ENABLED 1

#include "sturm/detail/lib/qram_read_bb_dsl.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

namespace {

constexpr double kTol = 1e-6;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q) {
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE); assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

struct AppendCtx {
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    explicit AppendCtx() {
        ctx = sturm_backend_create(STURM_MODE_APPEND); assert(ctx);
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~AppendCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
};

static double prob_register_eq(orkan::state_t& sv, uint32_t n_total,
                               const int* qidx, std::size_t n_bits,
                               std::uint64_t target_val) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        std::uint64_t got = 0u;
        for (std::size_t k = 0; k < n_bits; ++k)
            if (qidx[k] >= 0) got |= ((s >> qidx[k]) & 1u) << k;
        if (got == target_val) p += std::norm(orkan::amplitude(sv, s));
    }
    return p;
}

static std::tuple<int, std::vector<int>, int>
gate_key(const sturm::GateRecord& g) {
    std::vector<int> qs; qs.reserve(g.n);
    for (uint8_t k = 0; k < g.n; ++k) qs.push_back(static_cast<int>(g.qubits[k]));
    return {static_cast<int>(g.kind), qs, static_cast<int>(g.n)};
}

template <std::size_t W>
static void bind_qint(sturm::qint_t<W>& q, std::size_t n_bits,
                      const int* idx, std::int64_t value) {
    for (std::size_t j = 0; j < n_bits; ++j) {
        q.qubits[j] = idx[j]; q.super_mask |= (1ULL << j);
    }
    q.value = value;
}
template <std::size_t W>
static void release_addr_b(int* addr_idx, std::size_t K, int* b_idx) {
    for (std::size_t j = 0; j < K; ++j) sturm::QubitPool::instance().release(addr_idx[j]);
    for (std::size_t j = 0; j < W; ++j) sturm::QubitPool::instance().release(b_idx[j]);
}

// ── (5) Statevector round trip. a kept classical so the orkan budget
// stays within kMaxQubits=17.  Budget = K addr + W b + 2(N-1) routers
// + (N-2)*W transits + W bus (plan §3.1 table).
template <std::size_t W, std::size_t N, std::size_t K>
static void test_statevector_round_trip(const std::uint64_t (&a_vals)[N]) {
    using namespace sturm;
    constexpr uint32_t n_orkan = static_cast<uint32_t>(
        K + W + 2u * (N - 1u)
      + (N >= 2u ? (N - 2u) * W : 0u) + W);
    static_assert(n_orkan <= kMaxQubits, "BB3 round trip: exceeds orkan cap");

    for (std::size_t i_val = 0; i_val < N; ++i_val) {
        QubitPool::instance().reset_for_testing();
        int addr_idx[K], b_idx[W];
        for (std::size_t j = 0; j < K; ++j) addr_idx[j] = QubitPool::instance().allocate();
        for (std::size_t j = 0; j < W; ++j) b_idx[j]    = QubitPool::instance().allocate();

        SimCtx sc{n_orkan};
        for (std::size_t j = 0; j < K; ++j)
            if ((i_val >> j) & 1u)
                orkan::apply_x(sc.sv(), static_cast<uint32_t>(addr_idx[j]));

        qint_t<W> i_reg, b_reg;
        bind_qint<W>(i_reg, K, addr_idx, static_cast<int64_t>(i_val));
        bind_qint<W>(b_reg, W, b_idx, 0);

        qint_t<W> a[N];
        for (std::size_t k = 0; k < N; ++k) a[k].value = static_cast<int64_t>(a_vals[k]);

        lib_qram_read_bb_dsl<W, N>(a, i_reg, b_reg);
        const double p_b = prob_register_eq(sc.sv(), n_orkan, b_idx, W, a_vals[i_val]);
        if (std::abs(p_b - 1.0) > kTol)
            std::fprintf(stderr, "BB3 fwd N=%zu W=%zu i=%zu P(b==0x%llX)=%g\n",
                         N, W, i_val,
                         static_cast<unsigned long long>(a_vals[i_val]), p_b);
        assert(std::abs(p_b - 1.0) < kTol
               && "BB3: forward leaves P(b == a[i]) ≈ 1");

        __lib_qram_read_bb_dsl_adj<W, N>(a, i_reg, b_reg);
        const double p_zero = prob_register_eq(sc.sv(), n_orkan, b_idx, W, 0u);
        if (std::abs(p_zero - 1.0) > kTol)
            std::fprintf(stderr, "BB3 adj N=%zu W=%zu i=%zu P(b==0)=%g\n",
                         N, W, i_val, p_zero);
        assert(std::abs(p_zero - 1.0) < kTol
               && "BB3: forward + adjoint leaves P(b == 0) ≈ 1");

        // a[k] vacuously restored — a is classical.
        for (std::size_t k = 0; k < N; ++k)
            for (std::size_t j = 0; j < W; ++j) assert(a[k].qubits[j] < 0);

        i_reg.qubits.fill(-1); i_reg.super_mask = 0;
        b_reg.qubits.fill(-1); b_reg.super_mask = 0;
        release_addr_b<W>(addr_idx, K, b_idx);
    }
}

// ── (6) RecordingSink balance at (N=8, W=4). ────────────────────────
static void test_recording_balance_N8_W4() {
    using namespace sturm;
    constexpr std::size_t W = 4u, N = 8u, K = 3u;

    QubitPool::instance().reset_for_testing();
    RecordingSink rec; ScopedSink scope(&rec);
    AppendCtx ac;

    int addr_idx[K], b_idx[W];
    for (std::size_t j = 0; j < K; ++j) addr_idx[j] = QubitPool::instance().allocate();
    for (std::size_t j = 0; j < W; ++j) b_idx[j]    = QubitPool::instance().allocate();

    qint_t<W> i_reg, b_reg;
    bind_qint<W>(i_reg, K, addr_idx, 3);
    bind_qint<W>(b_reg, W, b_idx, 0);

    qint_t<W> a[N];
    for (std::size_t k = 0; k < N; ++k)
        a[k].value = static_cast<int64_t>((0x9ABCDEF0ull >> (k * 4u)) & 0xFu);

    const std::size_t ir_before = ac.ctx->ir.size();
    lib_qram_read_bb_dsl<W, N>(a, i_reg, b_reg);
    __lib_qram_read_bb_dsl_adj<W, N>(a, i_reg, b_reg);

    std::map<std::tuple<int, std::vector<int>, int>, std::size_t> counter;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r)
        ++counter[gate_key(ac.ctx->ir.at(r))];
    for (const auto& [key, count] : counter) {
        if ((count % 2u) != 0u)
            std::fprintf(stderr,
                "BB3 N=8 W=4 round trip unbalanced kind=%d arity=%d count=%zu\n",
                std::get<0>(key), std::get<2>(key), count);
        assert((count % 2u) == 0u
               && "BB3: every (op, qubits, controls) appears even-times");
    }

    release_addr_b<W>(addr_idx, K, b_idx);
}

// ── (7) Superposed-i round trip at (N=2, W=4). ──────────────────────
static void test_superposed_i_round_trip() {
    using namespace sturm;
    constexpr std::size_t W = 4u, N = 2u, K = 1u;
    constexpr std::uint64_t a_vals[N] = { 0xAu, 0x5u };
    constexpr uint32_t n_orkan = static_cast<uint32_t>(
        K + W + 2u * (N - 1u) + (N >= 2u ? (N - 2u) * W : 0u) + W);
    static_assert(n_orkan <= kMaxQubits, "BB3 superposed: exceeds orkan cap");

    QubitPool::instance().reset_for_testing();
    int addr_idx[K], b_idx[W];
    for (std::size_t j = 0; j < K; ++j) addr_idx[j] = QubitPool::instance().allocate();
    for (std::size_t j = 0; j < W; ++j) b_idx[j]    = QubitPool::instance().allocate();

    SimCtx sc{n_orkan};
    orkan::apply_h(sc.sv(), static_cast<uint32_t>(addr_idx[0]));

    qint_t<W> i_reg, b_reg;
    bind_qint<W>(i_reg, K, addr_idx, 0);
    bind_qint<W>(b_reg, W, b_idx, 0);
    const auto i_mask_before = i_reg.super_mask;

    qint_t<W> a[N];
    for (std::size_t k = 0; k < N; ++k) a[k].value = static_cast<int64_t>(a_vals[k]);

    lib_qram_read_bb_dsl<W, N>(a, i_reg, b_reg);
    __lib_qram_read_bb_dsl_adj<W, N>(a, i_reg, b_reg);

    const double p_zero = prob_register_eq(sc.sv(), n_orkan, b_idx, W, 0u);
    if (std::abs(p_zero - 1.0) > kTol)
        std::fprintf(stderr, "BB3 sup-i round trip P(b==0)=%g\n", p_zero);
    assert(std::abs(p_zero - 1.0) < kTol
           && "BB3: superposed-i round trip leaves b in |0⟩^W");
    assert(i_reg.super_mask == i_mask_before
           && "BB3: superposed-i round trip leaves i.super_mask unchanged");

    i_reg.qubits.fill(-1); i_reg.super_mask = 0;
    b_reg.qubits.fill(-1); b_reg.super_mask = 0;
    release_addr_b<W>(addr_idx, K, b_idx);
}

}  // namespace

int main() {
    std::puts("sturm-44bt.3 Beat BB3: lib_qram_read_bb_dsl round-trip test:");
    {
        constexpr std::uint64_t a24[2] = { 0xAu, 0x5u };
        test_statevector_round_trip<4u, 2u, 1u>(a24);
        std::puts("  PASS: (N=2, W=4) statevector round trip");
    }
    {
        constexpr std::uint64_t a42[4] = { 0x2u, 0x1u, 0x3u, 0x0u };
        test_statevector_round_trip<2u, 4u, 2u>(a42);
        std::puts("  PASS: (N=4, W=2) statevector round trip");
    }
    test_recording_balance_N8_W4();
    std::puts("  PASS: (N=8, W=4) RecordingSink gate-balance round trip");
    test_superposed_i_round_trip();
    std::puts("  PASS: (N=2, W=4) superposed-i round trip");
    std::puts("test_qram_read_bb_round_trip: OK");
    return 0;
}
