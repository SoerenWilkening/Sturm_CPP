// test_qram_read_bb_sequential.cpp — sturm-44bt.6 (Beat BB6).
// Pins PRD §2 G3 (round-trip via ancilla-pool snapshot) + G5 (sink
// counter parity) across 5 sequential `QRAM_read` calls at (N=4, W=2).
// Catches: (a) RAII / QubitPool leak (B6) — pool restored per call;
// (b) cross-call gate leak — each window matches §4.4 single-call pin;
// (c) counter accumulation — qrom_read=5 / qreg_read=5 / umbrella=5.
//
// Fixture (N=4, W=2): K=2 ≤ W=2, total user qubits 16 (under kMax=17).
// §4.4 closed form (verified in BB4 review against the 26-CNOT table
// typo — formula is the single source of truth, plan §4.4):
//   CCX = 4·(N'-2) + 4W·(N'-1)   = 4·2 + 4·2·3 = 32
//   CNOT= 2·N'     + W·(8N'-7)   = 8   + 2·25  = 58
//   X   = 2
// LoC budget: ≤ 200 (plan §1, §5 / Beat BB6).

#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

struct Budget { std::size_t ccx, cnot, x; };
constexpr Budget closed_form_counts(std::size_t Np, std::size_t Wv) noexcept {
    return Budget{
        /*ccx =*/ 4u * (Np - 2u) + 4u * Wv * (Np - 1u),
        /*cnot=*/ 2u * Np        + Wv * (8u * Np - 7u),
        /*x   =*/ 2u
    };
}

struct AppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    AppendCtx() {
        ctx = sturm_backend_create(STURM_MODE_APPEND);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~AppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

constexpr std::size_t W = 2u;
constexpr std::size_t N = 4u;
constexpr std::size_t K = 2u;  // log2(N)
constexpr std::size_t kCalls = 5u;

template <std::size_t Wv>
static void alloc_qint(sturm::qint_t<Wv>& q, std::size_t nb, std::int64_t v) {
    for (std::size_t j = 0; j < nb; ++j) {
        q.qubits[j] = sturm::QubitPool::instance().allocate();
        q.super_mask |= (1ULL << j);
    }
    q.value = v;
}
template <std::size_t Wv>
static void release_qint(sturm::qint_t<Wv>& q) {
    for (std::size_t j = 0; j < Wv; ++j)
        if (q.qubits[j] >= 0) {
            sturm::QubitPool::instance().release(q.qubits[j]);
            q.qubits[j] = -1;
        }
    q.super_mask = 0u;
}

static Budget count_in_window(sturm_backend_context_t* ctx,
                              std::size_t lo, std::size_t hi) {
    Budget got{0u, 0u, 0u};
    for (std::size_t r = lo; r < hi; ++r) {
        switch (static_cast<int>(ctx->ir.at(r).kind)) {
            case STURM_GATE_X:   ++got.x;    break;
            case STURM_GATE_CX:  ++got.cnot; break;
            case STURM_GATE_CCX: ++got.ccx;  break;
            default:
                std::fprintf(stderr,
                    "BB6 sequential: unexpected IR kind %d at r=%zu\n",
                    static_cast<int>(ctx->ir.at(r).kind), r);
                assert(false && "BB6: gate-set ⊆ {X, CX, CCX}");
        }
    }
    return got;
}

// (1) 5 sequential calls, all `a[k]` promoted to quantum so §4.4
// closed form applies. Snapshot QubitPool::in_use() before+after each
// call; assert per-call window matches single-call pin from BB3;
// cumulative counters sum to 5. Note: with every slot promoted, the
// dispatch any_super_mask is non-zero → qreg arm fires. This test
// pins both arms by parameterising on whether any slot is promoted —
// the qrom variant strips super_mask to route through qrom_read.
template <bool ForceQrom>
static void run_sequential_loop(const char* tag,
                                std::size_t expected_qrom,
                                std::size_t expected_qreg) {
    using namespace sturm;
    constexpr Budget expected = closed_form_counts(N, W);

    QubitPool::instance().reset_for_testing();
    CounterSink sink;
    ScopedSink scope(&sink);
    qram::reset_qram_read_count();
    AppendCtx ac;

    qint_t<W> i_reg, b_reg;
    alloc_qint<W>(i_reg, K, /*value=*/0);
    alloc_qint<W>(b_reg, W, /*value=*/0);
    std::array<qint_t<W>, N> a;
    for (std::size_t k = 0; k < N; ++k)
        alloc_qint<W>(a[k], W, static_cast<std::int64_t>(k));

    if constexpr (ForceQrom) {
        // Strip super_mask on every slot to route via QROM arm (any_super=0).
        // The qubit indices stay allocated so the BB CSWAP scalar path
        // still hits the 2-CNOT + 1-CCX shape per leaf — §4.4 holds.
        for (std::size_t k = 0; k < N; ++k) a[k].super_mask = 0u;
    }

    const int pool_entry = QubitPool::instance().in_use();

    for (std::size_t c = 0; c < kCalls; ++c) {
        const int pool_before = QubitPool::instance().in_use();
        assert(pool_before == pool_entry
               && "BB6: pool drift detected before call");
        const std::size_t ir_before = ac.ctx->ir.size();

        QRAM_read(a, i_reg, b_reg);

        const int pool_after = QubitPool::instance().in_use();
        const std::size_t ir_after = ac.ctx->ir.size();

        if (pool_after != pool_entry)
            std::fprintf(stderr, "BB6 %s[%zu]: pool drift entry=%d after=%d\n",
                tag, c, pool_entry, pool_after);
        assert(pool_after == pool_entry
               && "BB6: QubitPool round-trip (size restored)");

        const Budget got = count_in_window(ac.ctx, ir_before, ir_after);
        if (got.x != expected.x || got.cnot != expected.cnot
            || got.ccx != expected.ccx)
            std::fprintf(stderr,
                "BB6 %s[%zu]: §4.4 (N=%zu, W=%zu) "
                "X=%zu/%zu CX=%zu/%zu CCX=%zu/%zu\n",
                tag, c, N, W, got.x, expected.x, got.cnot, expected.cnot,
                got.ccx, expected.ccx);
        assert(got.x    == expected.x    && "BB6: per-call X count");
        assert(got.cnot == expected.cnot && "BB6: per-call CNOT count");
        assert(got.ccx  == expected.ccx  && "BB6: per-call CCX count");
    }

    assert(sink.count("qrom_read") == expected_qrom
           && "BB6: qrom_read cumulative");
    assert(sink.count("qreg_read") == expected_qreg
           && "BB6: qreg_read cumulative");
    assert(sink.count("qram_read") == kCalls
           && "BB6: umbrella sink qram_read == kCalls");
    assert(qram::qram_read_count() == kCalls
           && "BB6: thread-local umbrella == kCalls");

    release_qint<W>(i_reg);
    release_qint<W>(b_reg);
    for (std::size_t k = 0; k < N; ++k) release_qint<W>(a[k]);
    qram::reset_qram_read_count();
}

}  // namespace

int main() {
    std::puts("sturm-44bt.6 Beat BB6: sequential-call telemetry test:");
    // (1) 5 qrom calls: qrom_read=5, qreg_read=0, umbrella=5.
    run_sequential_loop</*ForceQrom=*/true>("qrom", /*qrom=*/kCalls, /*qreg=*/0u);
    std::puts("  PASS: 5x qrom — pool round-trip + per-call §4.4 + "
              "qrom_read=5 + umbrella=5");
    // (2) Reset, 5 qreg calls (slots superposed): qrom_read=0, qreg_read=5.
    run_sequential_loop</*ForceQrom=*/false>("qreg", /*qrom=*/0u, /*qreg=*/kCalls);
    std::puts("  PASS: 5x qreg — qreg_read=5, qrom_read=0, umbrella=5");
    std::puts("test_qram_read_bb_sequential: OK");
    return 0;
}
