// test_qram_read_qrom_gates.cpp -- sturm-2w6h.7 (Beat B5).
//
// End-to-end gate-stream + statevector test for the public
// `QRAM_read(a, i, b)` entry-point — closes v1 of the QRAM backend
// gate-emission epic (PRD `docs/prd_qram_backend.md`, plan
// `docs/plan_qram_backend.md` §5 Beat B5). Exercises every layer:
// public dispatch -> mask-OR routing -> QROM helper -> DSL body
// -> adjoint sibling -> split telemetry counter.
//
// Fixture: (N, W) = (4, 4), `a = {0xA, 0x5, 0xF, 0x0}`. K = 2.
// Pins T1/G5 forward record (CX/CCX budget, sink set, qrom split,
// umbrella, mask immutability), T2/G3 round trip (statevector zero +
// balanced gate stream), T3/G1 superposed index ((|0>+|2>)/sqrt(2)).
//
// Out-of-range UB (PRD §5): the QROM body sweeps k = 0..N-1 only;
// passing i >= N is UB. This test deliberately does NOT call
// QRAM_read with i = 5 on N = 4 — see PRD §5.
//
// Threading: -j6 / --parallel 6 only. LoC budget: <= 300 (plan §1).

#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
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
constexpr std::size_t W = 4u;
constexpr std::size_t N = 4u;
constexpr std::uint64_t kAvals[N] = { 0xAu, 0x5u, 0xFu, 0x0u };

// APPEND-mode context for IR-record inspection.
struct AppendCtx {
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    explicit AppendCtx(uint32_t max_q = 128u) {
        ctx = sturm_backend_create(STURM_MODE_APPEND, max_q); assert(ctx);
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~AppendCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
};

// SIMULATE-mode context for the statevector pins.
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q); assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

// Per-k cost from plan §4 cheat-sheet for K = 2: predicate compute =
// (n_zero_low_K X-flips) + (1 CCX from c_n_AND); uncompute = same body.
// Payload = popcount(a[k]) CX under WHEN(eq_k).
struct PerKCost { std::size_t x_count, cx_count, ccx_count; };
static PerKCost per_k_cost(std::size_t k, std::size_t K, std::uint64_t a_k) noexcept {
    std::size_t n_zero = 0u, pop = 0u;
    for (std::size_t j = 0; j < K; ++j) if (((k >> j) & 1u) == 0u) ++n_zero;
    std::uint64_t v = a_k & ((std::uint64_t{1} << W) - 1u);
    while (v) { pop += static_cast<std::size_t>(v & 1u); v >>= 1; }
    return PerKCost{ /*x=*/2u * (2u * n_zero), /*cx=*/pop, /*ccx=*/2u };
}

static void make_container(std::array<sturm::qint_t<W>, N>& a) {
    for (std::size_t k = 0; k < N; ++k)
        a[k] = sturm::qint_t<W>(static_cast<std::int64_t>(kAvals[k]));
}

// Probability that the bits at qidx[0..n) encode `target_val`.
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

// SIMULATE layout: q[0..3] = i, q[4..7] = b, q[8] = eq_k, q[9..10]
// = c_n_AND ancillas. Caller pre-reserves q[0..7]; bind/unbind below.
// (Predicate / WHEN-payload only emit gates on allocated bits — see
// qram_read_predicate.hpp:122.)
static void reserve_sim_pool(int reserved[8]) {
    sturm::QubitPool::instance().reset_for_testing();
    for (uint32_t r = 0; r < 8u; ++r)
        reserved[r] = sturm::QubitPool::instance().allocate();
    assert(reserved[0] == 0 && "pool deterministic layout");
}
static void bind_layout(sturm::qint_t<W>& i_reg, sturm::qint_t<W>& b_reg,
                        const int* reserved, std::int64_t i_value) {
    for (std::size_t j = 0; j < W; ++j) {
        i_reg.qubits[j] = reserved[j];      i_reg.super_mask |= (1ULL << j);
        b_reg.qubits[j] = reserved[W + j];  b_reg.super_mask |= (1ULL << j);
    }
    i_reg.value = i_value; b_reg.value = 0;
}
static void unbind_layout(sturm::qint_t<W>& i_reg, sturm::qint_t<W>& b_reg,
                          const int* reserved) {
    i_reg.qubits.fill(-1); b_reg.qubits.fill(-1);
    i_reg.super_mask = 0; b_reg.super_mask = 0;
    for (uint32_t r = 0; r < 8u; ++r) sturm::QubitPool::instance().release(reserved[r]);
}

// ── T1 (G5): forward record pin via the public entry-point. ────────
static void test_forward_record_pin() {
    using sturm::qram::reset_qram_read_count;
    using sturm::qram::qram_read_count;
    int reserved[8]; reserve_sim_pool(reserved);

    sturm::RecordingSink rec; sturm::ScopedSink scope(&rec);
    AppendCtx ac{128u};
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a; make_container(a);
    sturm::qint_t<W> i, b;
    bind_layout(i, b, reserved, /*i_value=*/2);
    const auto i_mask_before = i.super_mask;

    rec.clear();
    const std::size_t ir_before = ac.ctx->ir.size();

    sturm::QRAM_read(a, i, b);

    // Pin 1 (count): exact CX / CCX from the per-k budget. K = 2.
    constexpr std::size_t K = 2u;
    std::size_t exp_x = 0u, exp_cx = 0u, exp_ccx = 0u;
    for (std::size_t k = 0; k < N; ++k) {
        const auto c = per_k_cost(k, K, kAvals[k]);
        exp_x += c.x_count; exp_cx += c.cx_count; exp_ccx += c.ccx_count;
    }
    std::size_t got_x = 0u, got_cx = 0u, got_ccx = 0u;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        switch (static_cast<int>(ac.ctx->ir.at(r).kind)) {
            case STURM_GATE_X:   ++got_x;   break;
            case STURM_GATE_CX:  ++got_cx;  break;
            case STURM_GATE_CCX: ++got_ccx; break;
            default:
                std::fprintf(stderr, "B5 forward: unexpected IR kind %d\n",
                    static_cast<int>(ac.ctx->ir.at(r).kind));
                assert(false && "B5: only {X, CX, CCX} expected");
        }
    }
    if (got_x != exp_x || got_cx != exp_cx || got_ccx != exp_ccx)
        std::fprintf(stderr,
            "B5 forward: got X=%zu CX=%zu CCX=%zu, expected %zu/%zu/%zu\n",
            got_x, got_cx, got_ccx, exp_x, exp_cx, exp_ccx);
    assert(got_x == exp_x && got_cx == exp_cx && got_ccx == exp_ccx
           && "B5: X / CX / CCX counts match per-k budget");

    // Pin 2 (sink set): the DSL emits IR primitives directly via the
    // lifted emitters, not sink ops — the public entry-point bumps
    // `qrom_read` on the sink. quantum_xor/_and remain admissible per
    // the issue's gate-set membership clause.
    std::size_t qrom_records = 0u, qreg_records = 0u;
    for (const auto& r : rec.records()) {
        const bool ok = (r.op == "quantum_xor") || (r.op == "quantum_and")
                     || (r.op == "qrom_read")   || (r.op == "qreg_read");
        if (!ok) std::fprintf(stderr, "B5 forward: stray op='%s'\n", r.op.c_str());
        assert(ok && "B5: sink records ⊆ "
                     "{quantum_xor, quantum_and, qrom_read, qreg_read}");
        if (r.op == "qrom_read") ++qrom_records;
        if (r.op == "qreg_read") ++qreg_records;
    }
    assert(qrom_records == 1u && qreg_records == 0u
           && "B5: split telemetry — qrom_read fires once, qreg_read never");
    assert(qram_read_count() == 1u && "B5: umbrella qram_read_count == 1");
    assert(i.super_mask == i_mask_before
           && "B5: i.super_mask unchanged across QRAM_read");

    unbind_layout(i, b, reserved);
    reset_qram_read_count();
}

// ── T2 (G3): round-trip pin — zero b + balanced gate stream. ───────
static void test_round_trip_pin() {
    using sturm::qram::reset_qram_read_count;
    constexpr uint32_t n_orkan = 11u;  // W + W + 1 (eq_k) + (W-2) c_n_AND
    int reserved[8]; reserve_sim_pool(reserved);

    sturm::RecordingSink rec; sturm::ScopedSink scope(&rec);
    SimCtx sc{n_orkan, /*max_q=*/128u};
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a; make_container(a);
    sturm::qint_t<W> i_reg, b_reg;
    bind_layout(i_reg, b_reg, reserved, /*i_value=*/2);

    rec.clear();
    const std::size_t ir_before = sc.ctx->ir.size();

    sturm::QRAM_read(a, i_reg, b_reg);
    sturm::__QRAM_read_adj(a, i_reg, b_reg);

    const double p0 = prob_register_eq(sc.sv(), n_orkan,
                                       b_reg.qubits.data(), W, /*target=*/0u);
    if (std::abs(p0 - 1.0) > kTol)
        std::fprintf(stderr, "B5 round trip: P(b==0)=%g (expected 1.0)\n", p0);
    assert(std::abs(p0 - 1.0) < kTol
           && "B5: forward + adjoint must zero b on the statevector");

    std::map<std::tuple<int, std::vector<int>, int>, std::size_t> counter;
    for (std::size_t r = ir_before; r < sc.ctx->ir.size(); ++r)
        ++counter[gate_key(sc.ctx->ir.at(r))];
    for (const auto& [key, count] : counter) {
        if ((count % 2u) != 0u) std::fprintf(stderr,
            "B5 round trip: unbalanced kind=%d arity=%d count=%zu\n",
            std::get<0>(key), std::get<2>(key), count);
        assert((count % 2u) == 0u
               && "B5: every gate triple appears even-times across round trip");
    }

    unbind_layout(i_reg, b_reg, reserved);
    reset_qram_read_count();
}

// ── T3 (G1): superposed index — (|0>+|2>)/sqrt(2) splits b evenly. ─
static void test_superposed_index_pin() {
    using sturm::qram::reset_qram_read_count;
    constexpr uint32_t n_orkan = 11u;
    int reserved[8]; reserve_sim_pool(reserved);

    SimCtx sc{n_orkan, /*max_q=*/128u};
    reset_qram_read_count();

    // H on i.bit(1) — qubit at reserved[1]. With i.bit(0) = |0> this
    // produces (|0> + |2>)/sqrt(2) over the K = 2 active address bits.
    orkan::apply_h(sc.sv(), static_cast<uint32_t>(reserved[1]));

    sturm::qint_t<W> i_reg, b_reg;
    bind_layout(i_reg, b_reg, reserved, /*i_value=*/0);
    const auto i_mask_before = i_reg.super_mask;

    std::array<sturm::qint_t<W>, N> a; make_container(a);

    sturm::QRAM_read(a, i_reg, b_reg);

    const double p_a0 = prob_register_eq(sc.sv(), n_orkan,
                                         b_reg.qubits.data(), W, kAvals[0]);
    const double p_a2 = prob_register_eq(sc.sv(), n_orkan,
                                         b_reg.qubits.data(), W, kAvals[2]);
    if (std::abs((p_a0 + p_a2) - 1.0) > kTol
        || std::abs(p_a0 - 0.5) > kTol || std::abs(p_a2 - 0.5) > kTol) {
        std::fprintf(stderr,
            "B5 superposed: P(a[0])=%g P(a[2])=%g sum=%g\n",
            p_a0, p_a2, p_a0 + p_a2);
    }
    assert(std::abs((p_a0 + p_a2) - 1.0) < kTol
           && std::abs(p_a0 - 0.5) < kTol && std::abs(p_a2 - 0.5) < kTol
           && "B5: P(b=a[0]) ~ P(b=a[2]) ~ 0.5, sum ~ 1.0");
    assert(i_reg.super_mask == i_mask_before
           && "B5: i.super_mask unchanged across superposed-index call");

    unbind_layout(i_reg, b_reg, reserved);
    reset_qram_read_count();
}

}  // namespace

int main() {
    std::puts("sturm-2w6h.7 Beat B5: end-to-end QRAM_read gate-stream test:");
    test_forward_record_pin();    std::puts("  PASS: T1/G5 forward record");
    test_round_trip_pin();        std::puts("  PASS: T2/G3 round trip");
    test_superposed_index_pin();  std::puts("  PASS: T3/G1 superposed index");
    std::puts("test_qram_read_qrom_gates: OK");
    return 0;
}
