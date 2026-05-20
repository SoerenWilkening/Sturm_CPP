// test_qram_bb_routers_recording.cpp — sturm-44bt.1 (Beat BB1).
//
// G2 gate-stream pin for bb_setup_routers / bb_teardown_routers
// (include/sturm/detail/lib/qram_read_bb_routers.hpp). Plan:
// docs/plan_qram_backend_bb.md §5 Beat BB1 / §4.1. Coverage:
//   Nprime ∈ {2, 4, 8} (tree depths d = 1, 2, 3), classical addr ∈ [0, Nprime).
//   T1 (setup) — (a) every IR record is X / CX / CCX (no rotations);
//   (b) setup gate count = 2·(Nprime−2) CCX + Nprime CNOT + 1 X
//   (recomputed from Nprime); (c) total count matches closed form —
//   proves compile-time recursive walk did not collapse into a runtime
//   for-loop (runtime-for fallback would over- or under-count).
//   T2 (teardown) — (a) setup+teardown is BALANCED: every (kind, qubits,
//   arity) triple appears an even count; (b) post-teardown router state
//   = (0, 0) for every router (qbool::value bit 0 zeroed).
// LoC budget: ≤ 250.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_bb_routers.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

namespace {

// APPEND-mode context helper.
struct AppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit AppendCtx() {
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

// (kind, qubits, arity) key for balance counting across a round trip.
static std::tuple<int, std::vector<int>, int>
gate_key(const sturm::GateRecord& g) {
    std::vector<int> qs;
    qs.reserve(g.n);
    for (uint8_t k = 0; k < g.n; ++k) qs.push_back(static_cast<int>(g.qubits[k]));
    return {static_cast<int>(g.kind), qs, static_cast<int>(g.n)};
}

// Closed-form Phase 1 totals from plan §4.1.
struct Phase1Counts { std::size_t ccx; std::size_t cnot; std::size_t x; };
constexpr Phase1Counts phase1_counts(std::size_t Nprime) noexcept {
    // 2(Nprime − 2) CCX + Nprime CNOT + 1 X.
    const std::size_t non_root = (Nprime >= 2u) ? (Nprime - 2u) : 0u;
    return Phase1Counts{
        /*ccx*/  2u * non_root,
        /*cnot*/ Nprime,
        /*x*/    1u
    };
}

// ── Setup an addr register holding a classical value with K live qubits. ───
template <std::size_t W>
static void make_classical_addr(sturm::qint_t<W>& addr, std::size_t K,
                                std::uint64_t value) {
    for (std::size_t j = 0; j < K; ++j) {
        addr.qubits[j] = sturm::QubitPool::instance().allocate();
        addr.super_mask |= (1ULL << j);
    }
    addr.value = static_cast<int64_t>(value);
}

template <std::size_t W>
static void release_addr(sturm::qint_t<W>& addr) {
    for (std::size_t j = 0; j < W; ++j) {
        if (addr.qubits[j] >= 0) {
            sturm::QubitPool::instance().release(addr.qubits[j]);
            addr.qubits[j] = -1;
        }
    }
    addr.super_mask = 0;
}

// ── Run a single (Nprime, addr) configuration. ─────────────────────────────
template <std::size_t Nprime, std::size_t W>
static void run_recording_config(std::uint64_t addr_value) {
    using namespace sturm;

    constexpr std::size_t K =
        (Nprime <= 1u) ? 0u :
        (Nprime <= 2u) ? 1u :
        (Nprime <= 4u) ? 2u :
        (Nprime <= 8u) ? 3u :
        (Nprime <= 16u) ? 4u : 5u;
    static_assert(W >= K, "test: W must cover Nprime address bits");

    QubitPool::instance().reset_for_testing();
    RecordingSink rec;
    ScopedSink scope(&rec);
    AppendCtx ac;

    qint_t<W> addr;
    make_classical_addr<W>(addr, K, addr_value);

    std::array<detail_qram_bb::BBRouter, (Nprime >= 1u) ? (Nprime - 1u) : 0u> routers{};

    // ── T1: bb_setup_routers gate-set + count. ──────────────────────
    rec.clear();
    const std::size_t ir_before = ac.ctx->ir.size();

    detail_qram_bb::bb_setup_routers<Nprime>(addr, routers);

    // (a) Every IR record is X / CX / CCX.
    std::size_t actual_ccx = 0u, actual_cnot = 0u, actual_x = 0u;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        const auto& g = ac.ctx->ir.at(r);
        switch (g.kind) {
            case STURM_GATE_X:   ++actual_x;    break;
            case STURM_GATE_CX:  ++actual_cnot; break;
            case STURM_GATE_CCX: ++actual_ccx;  break;
            default:
                std::fprintf(stderr,
                    "BB1 recording: unexpected gate kind %d (Nprime=%zu, addr=%llu)\n",
                    static_cast<int>(g.kind), Nprime,
                    static_cast<unsigned long long>(addr_value));
                assert(false && "BB1: gate-set restricted to {X, CX, CCX}");
        }
    }

    // (b) Setup gate count equals the §4.1 closed form.
    const auto expected = phase1_counts(Nprime);
    if (actual_x != expected.x || actual_cnot != expected.cnot
        || actual_ccx != expected.ccx) {
        std::fprintf(stderr,
            "BB1 recording: setup count mismatch Nprime=%zu addr=%llu\n"
            "  X:   expected=%zu actual=%zu\n"
            "  CX:  expected=%zu actual=%zu\n"
            "  CCX: expected=%zu actual=%zu\n",
            Nprime, static_cast<unsigned long long>(addr_value),
            expected.x, actual_x,
            expected.cnot, actual_cnot,
            expected.ccx, actual_ccx);
    }
    assert(actual_x   == expected.x   && "BB1: setup X count must match §4.1");
    assert(actual_cnot == expected.cnot && "BB1: setup CNOT count must match §4.1");
    assert(actual_ccx == expected.ccx && "BB1: setup CCX count must match §4.1");

    // (c) Recursive-unrolling sanity: total count matches closed form.
    const std::size_t setup_total = actual_x + actual_cnot + actual_ccx;
    const std::size_t expected_total = expected.x + expected.cnot + expected.ccx;
    assert(setup_total == expected_total
           && "BB1: total setup gate count = compile-time unrolled walk");

    // Sink saw no Layer-A op records — DSL emits IR primitives directly.
    assert(rec.records().empty()
           && "BB1: bb_setup_routers must not call sink ops");

    // ── T2: bb_teardown_routers, then assert balance + (0,0) state. ──
    detail_qram_bb::bb_teardown_routers<Nprime>(addr, routers);

    // (a) Round-trip is balanced: every (kind, qubits, arity) even.
    std::map<std::tuple<int, std::vector<int>, int>, std::size_t> counter;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        ++counter[gate_key(ac.ctx->ir.at(r))];
    }
    for (const auto& [key, count] : counter) {
        if ((count % 2u) != 0u) {
            const auto& [kind, qs, n] = key;
            std::fprintf(stderr,
                "BB1: unbalanced kind=%d arity=%d count=%zu Nprime=%zu addr=%llu\n",
                kind, n, count, Nprime,
                static_cast<unsigned long long>(addr_value));
        }
        assert((count % 2u) == 0u
               && "BB1: every (kind, qubits, arity) must appear an even number of times");
    }

    // (b) Post-teardown router state is (0, 0) — value tracker zeroed.
    for (std::size_t k = 0; k < (Nprime - 1u); ++k) {
        const auto& r = routers[k];
        if ((r.is_left.value & 1) != 0 || (r.is_right.value & 1) != 0) {
            std::fprintf(stderr,
                "BB1: router[%zu] not in (0,0) after teardown — "
                "is_left.value=%lld is_right.value=%lld (Nprime=%zu, addr=%llu)\n",
                k, static_cast<long long>(r.is_left.value),
                static_cast<long long>(r.is_right.value), Nprime,
                static_cast<unsigned long long>(addr_value));
        }
        assert((r.is_left.value & 1)  == 0
               && "BB1: router.is_left.value must be 0 post-teardown");
        assert((r.is_right.value & 1) == 0
               && "BB1: router.is_right.value must be 0 post-teardown");
    }

    release_addr<W>(addr);
}

}  // namespace

int main() {
    std::puts("sturm-44bt.1 Beat BB1: bb_setup/teardown_routers recording test:");

    // Nprime = 2 (d=1). K=1. Sweep addr ∈ {0, 1}.
    run_recording_config<2u, 2u>(0u);
    run_recording_config<2u, 2u>(1u);
    std::puts("  PASS: Nprime=2 setup count + balance + (0,0) state");

    // Nprime = 4 (d=2). K=2. Sweep addr ∈ {0, 1, 2, 3}.
    run_recording_config<4u, 2u>(0u);
    run_recording_config<4u, 2u>(1u);
    run_recording_config<4u, 2u>(2u);
    run_recording_config<4u, 2u>(3u);
    std::puts("  PASS: Nprime=4 setup count + balance + (0,0) state");

    // Nprime = 8 (d=3). K=3. Sweep a few addresses across the tree.
    run_recording_config<8u, 3u>(0u);
    run_recording_config<8u, 3u>(3u);
    run_recording_config<8u, 3u>(5u);
    run_recording_config<8u, 3u>(7u);
    std::puts("  PASS: Nprime=8 setup count + balance + (0,0) state");

    std::puts("test_qram_bb_routers_recording: OK");
    return 0;
}
