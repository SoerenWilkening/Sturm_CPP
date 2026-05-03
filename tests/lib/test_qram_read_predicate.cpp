// test_qram_read_predicate.cpp -- sturm-2w6h.3 (Beat B2a).
//
// Pins lib_qram_eq_k_{compute,uncompute} from
// include/sturm/detail/lib/qram_read_predicate.hpp for the QRAM backend
// gate-emission epic (plan `docs/plan_qram_backend.md` §5 Beat B2a).
//
// Coverage:
//   T1 classical value-side, K = 1..4 exhaustive (iv, kv).
//   T2 gate-set discipline: emitted gates are {X, CX, CCX} only and no
//      higher-level Layer-A op leaks through the recording sink. K = 2
//      (direct CCX), K = 3 (NC sandwich w/ ancilla CCX). Note: in
//      STURM_BACKEND_ENABLED builds the X/CX/CCX primitives land in
//      `ctx->ir`, not on current_sink() — the empty-sink check is the
//      complementary half of the plan §3.2 "recording-sink pins
//      gate-set" wording.
//   T3 K = 2 statevector: i in (|00>+|01>+|10>+|11>)/2, k = 1 →
//      P(eq_k = 1) ≈ 0.25 after compute, P(eq_k = 0) ≈ 1 after uncompute.
//
// LoC budget: <= 250 (plan §1, §5 / B2a).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_predicate.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <string>

namespace {

constexpr double kTol = 1e-6;

// SIMULATE-mode context helper (mirrors the SimCtx pattern across
// tests/lib/ and tests/backend/).
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 64u) {
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
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

// APPEND-mode context helper for the gate-set inspection test.
struct AppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit AppendCtx(uint32_t max_q = 64u) {
        ctx = sturm_backend_create(STURM_MODE_APPEND, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~AppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── T1: classical value semantics, exhaustive sweep ─────────────────
static void test_classical_value_exhaustive() {
    for (std::size_t K = 1; K <= 4; ++K) {
        const std::size_t lim = std::size_t{1} << K;
        for (std::size_t iv = 0; iv < lim; ++iv) {
            for (std::size_t kv = 0; kv < lim; ++kv) {
                sturm::qint_t<4> i_reg = static_cast<int64_t>(iv);
                sturm::qbool eq_k;  // classical false
                assert(eq_k.value == 0);
                sturm::lib_qram_eq_k_compute<4>(i_reg, kv, K, eq_k);
                const int expected = (iv == kv) ? 1 : 0;
                assert(eq_k.value == expected
                       && "T1: eq_k.value matches (i == k)");
                assert(i_reg.value == static_cast<int64_t>(iv)
                       && "T1: i.value unchanged after compute");
                sturm::lib_qram_eq_k_uncompute<4>(i_reg, kv, K, eq_k);
                assert(eq_k.value == 0
                       && "T1: eq_k.value back to zero after uncompute");
                assert(i_reg.value == static_cast<int64_t>(iv)
                       && "T1: i.value unchanged after uncompute");
            }
        }
    }
}

// ── T2: gate-set discipline ────────────────────────────────────────
static void test_recording_gate_set() {
    auto run_one = [](std::size_t K, std::size_t kv) {
        sturm::QubitPool::instance().reset_for_testing();
        sturm::RecordingSink rec;
        sturm::ScopedSink scope(&rec);
        AppendCtx ac{64u};

        sturm::qint_t<4> i_reg;
        for (std::size_t j = 0; j < K; ++j) {
            i_reg.qubits[j] = sturm::QubitPool::instance().allocate();
            i_reg.super_mask |= (1ULL << j);
        }
        sturm::qbool eq_k;
        eq_k.qubits[0] = sturm::QubitPool::instance().allocate();
        eq_k.super_mask = 1ULL;

        rec.clear();
        const std::size_t ir_before = ac.ctx->ir.size();
        sturm::lib_qram_eq_k_compute<4>(i_reg, kv, K, eq_k);

        // (a) Every IR gate in the new tail must be X / CX / CCX.
        bool saw_ccx = false;
        for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
            const auto& g = ac.ctx->ir.at(r);
            const bool ok = (g.kind == STURM_GATE_X)
                         || (g.kind == STURM_GATE_CX)
                         || (g.kind == STURM_GATE_CCX);
            if (!ok) {
                std::fprintf(stderr,
                    "T2: unexpected gate kind %d (K=%zu, kv=%zu)\n",
                    static_cast<int>(g.kind), K, kv);
            }
            assert(ok && "T2: gate-set restricted to {X, CX, CCX}");
            if (g.kind == STURM_GATE_CCX) saw_ccx = true;
        }
        // (b) No Layer-A op should have leaked through the sink.
        if (!rec.records().empty()) {
            std::fprintf(stderr,
                "T2: stray sink record op='%s' (K=%zu, kv=%zu)\n",
                rec.records().front().op.c_str(), K, kv);
        }
        assert(rec.records().empty()
               && "T2: helper must not call sink quantum_*/prepare/rotation");
        // (c) At least one CCX from the c_n_AND chain.
        assert(saw_ccx
               && "T2: lib_c_n_AND_dsl must have emitted at least one CCX");

        // Cleanup: release qubits owned by hand-allocated views.
        sturm::QubitPool::instance().release(eq_k.qubits[0]);
        eq_k.qubits[0] = -1;
        eq_k.super_mask = 0;
        for (std::size_t j = 0; j < K; ++j) {
            sturm::QubitPool::instance().release(i_reg.qubits[j]);
            i_reg.qubits[j] = -1;
        }
        i_reg.super_mask = 0;
    };

    for (std::size_t kv = 0; kv < 4u; ++kv) run_one(/*K=*/2, kv);
    run_one(/*K=*/3, /*kv=*/0);
    run_one(/*K=*/3, /*kv=*/5);
    run_one(/*K=*/3, /*kv=*/7);
}

// ── T3: K = 2 statevector witness, uniform-superposed i ────────────
// Layout: q0=i_0, q1=i_1, q2=eq_k. Total live qubits 3 (peak <= 4 with
// the c_n_AND fast-path; well under the 17-qubit orkan budget).
//
// After compute(k = 1) on i ∈ (|00>+|01>+|10>+|11>)/2:
//   only the |i = 1> branch matches, so P(eq_k = 1) = 1/4.
// After uncompute (same body), eq_k returns to |0>: P(eq_k = 0) = 1.
static void test_statevector_uniform_superposition() {
    sturm::QubitPool::instance().reset_for_testing();
    int qi0 = sturm::QubitPool::instance().allocate();
    int qi1 = sturm::QubitPool::instance().allocate();
    int qe  = sturm::QubitPool::instance().allocate();
    assert(qi0 == 0 && qi1 == 1 && qe == 2 && "deterministic qubit layout");

    SimCtx sc{/*n_q=*/4u, /*max_q=*/64u};

    orkan::apply_h(sc.sv(), static_cast<uint32_t>(qi0));
    orkan::apply_h(sc.sv(), static_cast<uint32_t>(qi1));

    sturm::qint_t<4> i_reg;
    i_reg.qubits[0] = qi0;
    i_reg.qubits[1] = qi1;
    i_reg.super_mask = 0b11;
    sturm::qbool eq_k;
    eq_k.qubits[0] = qe;
    eq_k.super_mask = 1ULL;

    sturm::lib_qram_eq_k_compute<4>(i_reg, /*k=*/1u, /*K=*/2u, eq_k);

    auto& sv = sc.sv();
    const uint64_t dim = uint64_t{1} << sc.bridge.num_qubits();
    double p_eq_one = 0.0;
    for (uint64_t s = 0; s < dim; ++s) {
        const auto amp = orkan::amplitude(sv, s);
        if ((s >> qe) & 1u) p_eq_one += std::norm(amp);
    }
    assert(std::abs(p_eq_one - 0.25) < kTol
           && "T3: P(eq_k = 1) ≈ 0.25 after compute");

    sturm::lib_qram_eq_k_uncompute<4>(i_reg, /*k=*/1u, /*K=*/2u, eq_k);

    double p_eq_zero = 0.0;
    for (uint64_t s = 0; s < dim; ++s) {
        const auto amp = orkan::amplitude(sv, s);
        if (((s >> qe) & 1u) == 0u) p_eq_zero += std::norm(amp);
    }
    assert(std::abs(p_eq_zero - 1.0) < kTol
           && "T3: P(eq_k = 0) ≈ 1.0 after uncompute");

    i_reg.qubits[0] = -1;
    i_reg.qubits[1] = -1;
    i_reg.super_mask = 0;
    eq_k.qubits[0] = -1;
    eq_k.super_mask = 0;
    sturm::QubitPool::instance().release(qi0);
    sturm::QubitPool::instance().release(qi1);
    sturm::QubitPool::instance().release(qe);
}

}  // namespace

int main() {
    std::puts("sturm-2w6h.3 Beat B2a: lib_qram_eq_k tests:");
    test_classical_value_exhaustive();
    std::puts("  PASS: T1 classical value exhaustive");
    test_recording_gate_set();
    std::puts("  PASS: T2 recording-sink gate-set");
    test_statevector_uniform_superposition();
    std::puts("  PASS: T3 statevector uniform superposition");
    std::puts("test_qram_read_predicate: OK");
    return 0;
}
