// test_qram_read_bb_qreg_gates.cpp — sturm-44bt.4 (Beat BB4).
// First gate-emission test for the qreg path (PRD §2 G1+G5; plan §5
// BB4). Exercises: public `QRAM_read` → `qram_read_dispatch<W, N>` →
// `lib_qram_read_bb_dsl<W, N>`. Routing is data-classicality-agnostic
// (PRD §4); qreg arm fires when `any_super_mask != 0`. Tests:
// (1) Gate counts via §4.4 closed form at (N=4, W=2) — all `a[k]`
//     promoted to quantum. Counts RECOMPUTED from (Nprime, W); the
//     formula yields CCX=32, CNOT=58, X=2. Asserts qreg_read=1,
//     qrom_read=0, umbrella=1.
// (2) qreg statevector at (N=4, W=2, 17 qubits) — a[3].bit(0) in
//     |+⟩, classical i=3 → b.bit(0) entangled with a[3].bit(0).
// LoC budget: ≤ 250 (plan §1).

#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
#include "sturm/detail/lib/qram_read_bb_dsl.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
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

// Plan §4.4 closed-form gate budget per (N', W).
struct Budget { std::size_t ccx, cnot, x; };
constexpr Budget closed_form_counts(std::size_t Nprime, std::size_t W) noexcept {
    return Budget{
        /*ccx =*/ 4u * (Nprime - 2u) + 4u * W * (Nprime - 1u),
        /*cnot=*/ 2u * Nprime + W * (8u * Nprime - 7u),
        /*x   =*/ 2u
    };
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
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE); assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Test 1: (N=4, W=2) gate counts + counter parity ─────────────────
// Promote every `a[k]` to quantum so the §4.4 closed form applies
// (standard 2-CNOT + 1-CCX scalar shape per leaf-side CSWAP). The
// promotion sets super_mask on all slots → dispatch routes to qreg
// via `any_super_mask != 0`. Pins: §4.4 closed-form CCX/CNOT/X +
// qreg_read=1 + qrom_read=0 + umbrella=1.
static void test_qreg_gate_counts_and_counters() {
    using namespace sturm;
    constexpr std::size_t W = 2u, N = 4u, K = 2u;
    constexpr std::uint64_t kAvals[N] = { 0x1u, 0x2u, 0x3u, 0x0u };

    QubitPool::instance().reset_for_testing();
    CounterSink sink; ScopedSink scope(&sink);
    qram::reset_qram_read_count();
    AppendCtx ac;

    qint_t<W> i_reg;
    for (std::size_t j = 0; j < K; ++j) {
        i_reg.qubits[j] = QubitPool::instance().allocate();
        i_reg.super_mask |= (1ULL << j);
    }
    i_reg.value = 2;

    qint_t<W> b_reg;
    for (std::size_t j = 0; j < W; ++j) {
        b_reg.qubits[j] = QubitPool::instance().allocate();
        b_reg.super_mask |= (1ULL << j);
    }
    b_reg.value = 0;

    std::array<qint_t<W>, N> a;
    for (std::size_t k = 0; k < N; ++k) {
        a[k].value = static_cast<int64_t>(kAvals[k]);
        for (std::size_t j = 0; j < W; ++j) {
            a[k].qubits[j] = QubitPool::instance().allocate();
            a[k].super_mask |= (1ULL << j);
        }
    }

    const std::size_t ir_before = ac.ctx->ir.size();
    QRAM_read(a, i_reg, b_reg);

    constexpr Budget expected = closed_form_counts(N, W);
    std::size_t got_ccx = 0u, got_cnot = 0u, got_x = 0u;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        switch (static_cast<int>(ac.ctx->ir.at(r).kind)) {
            case STURM_GATE_X:   ++got_x;    break;
            case STURM_GATE_CX:  ++got_cnot; break;
            case STURM_GATE_CCX: ++got_ccx;  break;
            default:
                std::fprintf(stderr, "BB4 qreg_gates: unexpected IR kind %d\n",
                    static_cast<int>(ac.ctx->ir.at(r).kind));
                assert(false && "BB4: gate-set restricted to {X, CX, CCX}");
        }
    }
    if (got_x != expected.x || got_cnot != expected.cnot
        || got_ccx != expected.ccx) {
        std::fprintf(stderr,
            "BB4 qreg_gates §4.4 (N=%zu,W=%zu): X=%zu/%zu CX=%zu/%zu CCX=%zu/%zu\n",
            N, W, got_x, expected.x, got_cnot, expected.cnot,
            got_ccx, expected.ccx);
    }
    assert(got_x   == expected.x   && "BB4 §4.4: X count");
    assert(got_cnot == expected.cnot && "BB4 §4.4: CNOT count");
    assert(got_ccx == expected.ccx  && "BB4 §4.4: CCX count");

    assert(sink.count("qreg_read") == 1u && "BB4: qreg_read fires once");
    assert(sink.count("qrom_read") == 0u && "BB4: qrom_read stays at 0");
    assert(sink.count("qram_read") == 1u && "BB4: umbrella sink hook");
    assert(sturm::qram::qram_read_count() == 1u
           && "BB4: umbrella thread-local == 1");

    // Release manually-allocated qubits before destructors.
    auto release = [](qint_t<W>& q) {
        for (std::size_t j = 0; j < W; ++j)
            if (q.qubits[j] >= 0) {
                QubitPool::instance().release(q.qubits[j]); q.qubits[j] = -1;
            }
        q.super_mask = 0;
    };
    release(i_reg); release(b_reg);
    for (std::size_t k = 0; k < N; ++k) release(a[k]);
    qram::reset_qram_read_count();
}

// ── Test 2: (N=4, W=2) statevector — 17 qubits (orkan cap). ─────────
// Layout: a[3].bit(0) at q[0], i (K=2), b (W=2), routers (6),
// transits (4), bus (W=2). Prepare a[3].bit(0) in |+⟩ via H. Set
// i = 3 (X on i.qubits[0,1]). Call QRAM_read. Expected: b.bit(0)
// entangled with a[3].bit(0) (P(b0 == a3_bit0) ≈ 1.0).
static void test_qreg_statevector() {
    using namespace sturm;
    constexpr std::size_t W = 2u, N = 4u, K = 2u;
    constexpr uint32_t n_orkan =
        1u + K + W + 2u * (N - 1u) + (N - 2u) * W + W;
    static_assert(n_orkan <= kMaxQubits,
                  "BB4 qreg statevector: budget exceeds orkan cap");

    QubitPool::instance().reset_for_testing();
    const int a3_bit0_q = QubitPool::instance().allocate();
    assert(a3_bit0_q == 0);
    int i_qidx[K], b_qidx[W];
    for (std::size_t j = 0; j < K; ++j) i_qidx[j] = QubitPool::instance().allocate();
    for (std::size_t j = 0; j < W; ++j) b_qidx[j] = QubitPool::instance().allocate();

    SimCtx sc{n_orkan};
    orkan::apply_h(sc.sv(), static_cast<uint32_t>(a3_bit0_q));
    for (std::size_t j = 0; j < K; ++j)
        orkan::apply_x(sc.sv(), static_cast<uint32_t>(i_qidx[j]));

    qint_t<W> i_reg;
    for (std::size_t j = 0; j < K; ++j) {
        i_reg.qubits[j] = i_qidx[j];
        i_reg.super_mask |= (1ULL << j);
    }
    i_reg.value = 3;
    qint_t<W> b_reg;
    for (std::size_t j = 0; j < W; ++j) {
        b_reg.qubits[j] = b_qidx[j];
        b_reg.super_mask |= (1ULL << j);
    }
    b_reg.value = 0;

    std::array<qint_t<W>, N> a;
    a[0].value = 0x1; a[1].value = 0x2; a[2].value = 0x3;
    a[3].value = 0;
    a[3].qubits[0] = a3_bit0_q;
    a[3].super_mask |= 1ULL;

    QRAM_read(a, i_reg, b_reg);

    auto prob_eq_bit = [&](int q_a, int q_b, std::uint64_t target_xor) {
        double p = 0.0;
        const std::uint64_t dim = std::uint64_t{1} << n_orkan;
        for (std::uint64_t s = 0; s < dim; ++s) {
            const std::uint64_t xa = (s >> q_a) & 1ULL;
            const std::uint64_t xb = (s >> q_b) & 1ULL;
            if ((xa ^ xb) == target_xor) p += std::norm(orkan::amplitude(sc.sv(), s));
        }
        return p;
    };

    const double p_corr = prob_eq_bit(a3_bit0_q, b_qidx[0], /*xor=*/0u);

    double p_b1z = 0.0;
    const std::uint64_t dim = std::uint64_t{1} << n_orkan;
    for (std::uint64_t s = 0; s < dim; ++s) {
        if (((s >> b_qidx[1]) & 1ULL) == 0u)
            p_b1z += std::norm(orkan::amplitude(sc.sv(), s));
    }

    if (std::abs(p_corr - 1.0) > kTol)
        std::fprintf(stderr, "BB4 qreg sv: P(b0==a3_bit0) = %g\n", p_corr);
    if (std::abs(p_b1z - 1.0) > kTol)
        std::fprintf(stderr, "BB4 qreg sv: P(b1==0) = %g\n", p_b1z);
    assert(std::abs(p_corr - 1.0) < kTol
           && "BB4 qreg sv: b.bit(0) perfectly correlated with a[3].bit(0)");
    assert(std::abs(p_b1z - 1.0) < kTol
           && "BB4 qreg sv: b.bit(1) deterministically 0 (a[3].bit(1) was 0)");

    i_reg.qubits.fill(-1); i_reg.super_mask = 0;
    b_reg.qubits.fill(-1); b_reg.super_mask = 0;
    a[3].qubits.fill(-1); a[3].super_mask = 0;
    QubitPool::instance().release(a3_bit0_q);
    for (std::size_t j = 0; j < K; ++j) QubitPool::instance().release(i_qidx[j]);
    for (std::size_t j = 0; j < W; ++j) QubitPool::instance().release(b_qidx[j]);
}

}  // namespace

int main() {
    std::puts("sturm-44bt.4 Beat BB4: QRAM_read qreg-path gate-stream test:");
    test_qreg_gate_counts_and_counters();
    std::puts("  PASS: (N=4, W=2) §4.4 counts + qreg_read=1 + umbrella=1");
    test_qreg_statevector();
    std::puts("  PASS: (N=4, W=2, 17q) statevector b.bit(0) entangled with a[3].bit(0)");
    std::puts("test_qram_read_bb_qreg_gates: OK");
    return 0;
}
