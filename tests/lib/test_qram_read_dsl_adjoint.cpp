// test_qram_read_dsl_adjoint.cpp -- sturm-2w6h.5 (Beat B3).
//
// Pins __lib_qram_read_qrom_dsl_adj from
// include/sturm/detail/lib/qram_read_dsl_adj.hpp (plan
// docs/plan_qram_backend.md §5 Beat B3, goal G3). The forward body
// is self-adjoint, so the adjoint just re-runs the forward sweep —
// the B3 contract is the STURM_REGISTER_ADJOINT enrolment.
//
// Coverage: T1 recording-sink balanced round trip for (N, W) ∈
// {(2, 2), (4, 4)}; T2 statevector round trip zeroing b for
// classical i ∈ {0..3}; T3 same with H on i.bit(0); T4 compile-time
// invert<>() resolution.
//
// LoC budget: <= 250 (plan §1, §5 / B3).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

namespace {

constexpr double kTol = 1e-6;

// APPEND-mode context for IR-record inspection.
struct AppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit AppendCtx(uint32_t max_q = 128u) {
        ctx = sturm_backend_create(STURM_MODE_APPEND);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~AppendCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
};

// SIMULATE-mode context for the statevector round trips.
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
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

template <std::size_t W>
static void make_container(sturm::qint_t<W>* a, std::size_t N) {
    static const std::uint64_t kVals[8] = { 0xAu, 0x5u, 0xFu, 0x0u,
                                            0x3u, 0xCu, 0x6u, 0x9u };
    const std::uint64_t low_w = (W >= 64u) ? ~std::uint64_t{0}
                                           : (std::uint64_t{1} << W) - 1u;
    for (std::size_t k = 0; k < N; ++k) {
        a[k] = sturm::qint_t<W>(static_cast<std::int64_t>(kVals[k & 7u] & low_w));
    }
}

// (kind, qubits, arity) key — every gate has at most 3 qubit slots
// (CCX). The triple is a sufficient witness of "the same gate twice".
static std::tuple<int, std::vector<int>, int>
gate_key(const sturm::GateRecord& g) {
    std::vector<int> qs;
    qs.reserve(g.n);
    for (uint8_t k = 0; k < g.n; ++k) qs.push_back(static_cast<int>(g.qubits[k]));
    return {static_cast<int>(g.kind), qs, static_cast<int>(g.n)};
}

// ── T1: Recording-sink round trip — balanced gate stream. ───────────
template <std::size_t W>
static void test_balanced_round_trip(std::size_t N) {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);
    AppendCtx ac{128u};

    sturm::qint_t<W> i_reg, b_reg;
    for (std::size_t j = 0; j < W; ++j) {
        i_reg.qubits[j] = sturm::QubitPool::instance().allocate();
        i_reg.super_mask |= (1ULL << j);
        b_reg.qubits[j] = sturm::QubitPool::instance().allocate();
        b_reg.super_mask |= (1ULL << j);
    }
    std::vector<sturm::qint_t<W>> a(N);
    make_container<W>(a.data(), N);

    rec.clear();
    const std::size_t ir_before = ac.ctx->ir.size();

    sturm::lib_qram_read_qrom_dsl<W>(a.data(), N, i_reg, b_reg);
    sturm::__lib_qram_read_qrom_dsl_adj<W>(a.data(), N, i_reg, b_reg);

    std::map<std::tuple<int, std::vector<int>, int>, std::size_t> counter;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        ++counter[gate_key(ac.ctx->ir.at(r))];
    }
    for (const auto& [key, count] : counter) {
        if ((count % 2u) != 0u) {
            const auto& [kind, qs, n] = key;
            std::fprintf(stderr,
                "T1: unbalanced gate kind=%d arity=%d count=%zu (N=%zu, W=%zu)\n",
                kind, n, count, N, W);
        }
        assert((count % 2u) == 0u
               && "T1: every (kind, qubits, arity) triple must appear "
                  "an even number of times across the round trip");
    }
    assert(rec.records().empty()
           && "T1: DSL helpers must not call sink ops directly");

    for (std::size_t j = 0; j < W; ++j) {
        if (i_reg.qubits[j] >= 0) sturm::QubitPool::instance().release(i_reg.qubits[j]);
        if (b_reg.qubits[j] >= 0) sturm::QubitPool::instance().release(b_reg.qubits[j]);
        i_reg.qubits[j] = -1; b_reg.qubits[j] = -1;
    }
    i_reg.super_mask = 0; b_reg.super_mask = 0;
}

// Shared body for T2/T3 statevector round trips. `init_i` is run
// before the forward sweep and is the place where i is encoded —
// classical via X gates or superposed via H.
template <typename InitI>
static void run_simulate_round_trip(std::int64_t i_value, InitI init_i) {
    constexpr std::size_t W = 4u, N = 4u;
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_orkan = 11u;  // W + W + 1 + (W-2)
    int reserved[8];
    for (uint32_t r = 0; r < 8u; ++r) {
        reserved[r] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool deterministic layout");

    SimCtx sc{n_orkan, /*max_q=*/128u};
    init_i(sc.sv(), reserved);

    sturm::qint_t<W> i_reg, b_reg;
    for (std::size_t j = 0; j < W; ++j) {
        i_reg.qubits[j] = reserved[j];
        i_reg.super_mask |= (1ULL << j);
        b_reg.qubits[j] = reserved[W + j];
        b_reg.super_mask |= (1ULL << j);
    }
    i_reg.value = i_value; b_reg.value = 0;
    const auto i_mask_before = i_reg.super_mask;

    sturm::qint_t<W> a[N];
    make_container<W>(a, N);

    sturm::lib_qram_read_qrom_dsl<W>(a, N, i_reg, b_reg);
    sturm::__lib_qram_read_qrom_dsl_adj<W>(a, N, i_reg, b_reg);

    const double p0 = prob_register_eq(sc.sv(), n_orkan,
                                       b_reg.qubits.data(), W, /*target=*/0u);
    if (std::abs(p0 - 1.0) > kTol) {
        std::fprintf(stderr, "round trip: P(b==0)=%g (expected 1.0)\n", p0);
    }
    assert(std::abs(p0 - 1.0) < kTol
           && "forward + adjoint must zero b across the round trip");
    assert(i_reg.super_mask == i_mask_before
           && "i.super_mask unchanged across the round trip");

    i_reg.qubits.fill(-1); b_reg.qubits.fill(-1);
    i_reg.super_mask = 0; b_reg.super_mask = 0;
    for (uint32_t r = 0; r < 8u; ++r) sturm::QubitPool::instance().release(reserved[r]);
}

// T4: compile-time invert<>() lookup pinned via static_assert.
static void test_invert_resolves() {
    constexpr auto adj4 = sturm::invert<&sturm::lib_qram_read_qrom_dsl<4u>>();
    static_assert(adj4 == &sturm::__lib_qram_read_qrom_dsl_adj<4u>,
                  "invert<&lib_qram_read_qrom_dsl<4>> must resolve to adj<4>");
    constexpr auto adj2 = sturm::invert<&sturm::lib_qram_read_qrom_dsl<2u>>();
    static_assert(adj2 == &sturm::__lib_qram_read_qrom_dsl_adj<2u>,
                  "invert<&lib_qram_read_qrom_dsl<2>> must resolve to adj<2>");
    (void)adj4; (void)adj2;
}

}  // namespace

int main() {
    std::puts("sturm-2w6h.5 Beat B3: lib_qram_read_qrom_dsl adjoint test:");
    test_invert_resolves();
    std::puts("  PASS: T4 invert<>() resolves to registered adjoint");

    test_balanced_round_trip<2u>(/*N=*/2u);
    std::puts("  PASS: T1 balanced gate stream (N=2, W=2)");
    test_balanced_round_trip<4u>(/*N=*/4u);
    std::puts("  PASS: T1 balanced gate stream (N=4, W=4)");

    constexpr std::size_t W = 4u;
    for (std::int64_t iv = 0; iv < 4; ++iv) {
        run_simulate_round_trip(iv, [iv](orkan::state_t& sv, const int*) {
            for (std::size_t j = 0; j < W; ++j) {
                if ((iv >> j) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(j));
            }
        });
    }
    std::puts("  PASS: T2 statevector round trip zeros b for classical i ∈ {0..3}");

    run_simulate_round_trip(/*i_value=*/0,
        [](orkan::state_t& sv, const int* res) {
            orkan::apply_h(sv, static_cast<uint32_t>(res[0]));
        });
    std::puts("  PASS: T3 statevector round trip zeros b for superposed i");

    std::puts("test_qram_read_dsl_adjoint: OK");
    return 0;
}
