// test_qram_bb_bus_recording.cpp — sturm-44bt.2 (Beat BB2).
//
// G2 gate-stream pin for bb_bus_traverse. Plan §5 Beat BB2 / §4.2.
//
// Coverage: (Nprime, W) ∈ {(2,2), (4,2), (4,4), (8,4)} with a fixed
// seeded-random classical `a` whose qubits are PROMOTED to quantum
// before the recording window opens, so the leaf-side CSWAPs emit
// the standard `1 CCX + 2 CNOT` shape per scalar.
//
// For each (Nprime, W) + addr sweep:
//   1. Pre-allocate qubits for addr, b, routers, transits, bus, a[k].
//   2. Run bb_setup_routers, then open the recording window.
//   3. Run bb_bus_traverse.
//   4. Assert every IR record is X / CX / CCX (no rotations / prepares).
//   5. Assert gate count matches the Phase 2 formula
//      4·W·(Nprime−1) CCX + (8·W·(Nprime−1) + W) CNOT + 0 X, with the
//      constants recomputed from (Nprime, W).
//   6. Run bb_teardown_routers + release pool. LoC ≤ 250.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_bb_bus.hpp"
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
#include <random>

namespace {

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

struct Phase2Counts { std::size_t ccx; std::size_t cnot; std::size_t x; };
constexpr Phase2Counts phase2_counts(std::size_t Nprime, std::size_t W) noexcept {
    const std::size_t I = (Nprime >= 1u) ? (Nprime - 1u) : 0u;
    return Phase2Counts{ /*ccx*/ 4u*W*I, /*cnot*/ 8u*W*I + W, /*x*/ 0u };
}

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
static void release_qint(sturm::qint_t<W>& q) {
    for (std::size_t j = 0; j < W; ++j) {
        if (q.qubits[j] >= 0) {
            sturm::QubitPool::instance().release(q.qubits[j]);
            q.qubits[j] = -1;
        }
    }
    q.super_mask = 0;
}

template <std::size_t W>
static void promote_qint(sturm::qint_t<W>& q) {
    for (std::size_t j = 0; j < W; ++j) {
        if (q.qubits[j] < 0) {
            q.qubits[j] = sturm::QubitPool::instance().allocate();
            q.super_mask |= (1ULL << j);
        }
    }
}

template <std::size_t W>
static void promote_qbool_array(std::array<sturm::qbool, W>& bus) {
    for (std::size_t j = 0; j < W; ++j) {
        if (bus[j].qubits[0] < 0) {
            bus[j].qubits[0]  = sturm::QubitPool::instance().allocate();
            bus[j].super_mask = 1ULL;
        }
    }
}

template <std::size_t Nprime, std::size_t W>
static void run_recording_config(std::uint64_t addr_value, std::uint32_t seed) {
    using namespace sturm;
    constexpr std::size_t K =
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

    qint_t<W> b;
    for (std::size_t j = 0; j < W; ++j) {
        b.qubits[j] = QubitPool::instance().allocate();
        b.super_mask |= (1ULL << j);
    }
    b.value = 0;

    std::array<detail_qram_bb::BBRouter, Nprime - 1u> routers{};
    std::array<std::array<qbool, W>, (Nprime >= 2u) ? (Nprime - 2u) : 0u> transits{};
    for (auto& blk : transits) promote_qbool_array<W>(blk);
    std::array<qbool, W> bus{};
    promote_qbool_array<W>(bus);

    // Seeded random classical `a` — promote each slot to quantum so the
    // leaf-side CSWAPs emit `1 CCX + 2 CNOT` per scalar. No X-prep is
    // emitted (lazy allocation only); qubits start in |0⟩.
    std::mt19937 rng(seed);
    qint_t<W> a[Nprime];
    for (std::size_t k = 0; k < Nprime; ++k) {
        const std::uint64_t mask = (W >= 64u) ? ~std::uint64_t{0}
                                              : ((std::uint64_t{1} << W) - 1u);
        a[k].value = static_cast<int64_t>(static_cast<std::uint64_t>(rng()) & mask);
        promote_qint<W>(a[k]);
    }

    detail_qram_bb::bb_setup_routers<Nprime>(addr, routers);

    rec.clear();
    const std::size_t ir_before = ac.ctx->ir.size();

    detail_qram_bb::bb_bus_traverse<Nprime, W>(routers, transits, bus, a, b);

    std::size_t actual_ccx = 0u, actual_cnot = 0u, actual_x = 0u;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        const auto& g = ac.ctx->ir.at(r);
        switch (g.kind) {
            case STURM_GATE_X:   ++actual_x;    break;
            case STURM_GATE_CX:  ++actual_cnot; break;
            case STURM_GATE_CCX: ++actual_ccx;  break;
            default:
                std::fprintf(stderr,
                    "BB2 recording: unexpected gate kind %d (Nprime=%zu, W=%zu, addr=%llu)\n",
                    static_cast<int>(g.kind), Nprime, W,
                    static_cast<unsigned long long>(addr_value));
                assert(false && "BB2: gate-set restricted to {X, CX, CCX}");
        }
    }

    const auto expected = phase2_counts(Nprime, W);
    if (actual_x != expected.x || actual_cnot != expected.cnot
        || actual_ccx != expected.ccx) {
        std::fprintf(stderr,
            "BB2 recording: Phase 2 count mismatch Nprime=%zu W=%zu addr=%llu\n"
            "  X:   expected=%zu actual=%zu\n"
            "  CX:  expected=%zu actual=%zu\n"
            "  CCX: expected=%zu actual=%zu\n",
            Nprime, W, static_cast<unsigned long long>(addr_value),
            expected.x, actual_x,
            expected.cnot, actual_cnot,
            expected.ccx, actual_ccx);
    }
    assert(actual_x   == expected.x   && "BB2: Phase 2 must emit 0 X gates");
    assert(actual_cnot == expected.cnot && "BB2: Phase 2 CNOT count must match §4.2");
    assert(actual_ccx == expected.ccx && "BB2: Phase 2 CCX count must match §4.2");

    assert(rec.records().empty()
           && "BB2: bb_bus_traverse must not call sink ops");

    detail_qram_bb::bb_teardown_routers<Nprime>(addr, routers);

    for (auto& slot : a) release_qint<W>(slot);
    for (auto& blk : transits) for (auto& q : blk) {
        if (q.qubits[0] >= 0) {
            QubitPool::instance().release(q.qubits[0]);
            q.qubits[0]  = -1; q.super_mask = 0;
        }
    }
    for (auto& q : bus) {
        if (q.qubits[0] >= 0) {
            QubitPool::instance().release(q.qubits[0]);
            q.qubits[0] = -1; q.super_mask = 0;
        }
    }
    for (auto& r : routers) {
        if (r.is_left.qubits[0]  >= 0) QubitPool::instance().release(r.is_left.qubits[0]);
        if (r.is_right.qubits[0] >= 0) QubitPool::instance().release(r.is_right.qubits[0]);
        r.is_left.qubits[0]  = -1;
        r.is_right.qubits[0] = -1;
    }
    release_qint<W>(b);
    release_qint<W>(addr);
}

}  // namespace

int main() {
    std::puts("sturm-44bt.2 Beat BB2: bb_bus_traverse recording test:");

    run_recording_config<2u, 2u>(0u, 0x1u);
    run_recording_config<2u, 2u>(1u, 0x1u);
    std::puts("  PASS: (Nprime=2, W=2) Phase 2 gate count + gate-set");

    run_recording_config<4u, 2u>(0u, 0x2u);
    run_recording_config<4u, 2u>(1u, 0x2u);
    run_recording_config<4u, 2u>(2u, 0x2u);
    run_recording_config<4u, 2u>(3u, 0x2u);
    std::puts("  PASS: (Nprime=4, W=2) Phase 2 gate count + gate-set");

    run_recording_config<4u, 4u>(0u, 0x3u);
    run_recording_config<4u, 4u>(3u, 0x3u);
    std::puts("  PASS: (Nprime=4, W=4) Phase 2 gate count + gate-set");

    run_recording_config<8u, 4u>(0u, 0x4u);
    run_recording_config<8u, 4u>(5u, 0x4u);
    run_recording_config<8u, 4u>(7u, 0x4u);
    std::puts("  PASS: (Nprime=8, W=4) Phase 2 gate count + gate-set");

    std::puts("test_qram_bb_bus_recording: OK");
    return 0;
}
