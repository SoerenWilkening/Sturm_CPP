// test_qram_read_bb_depth_scaling.cpp — sturm-44bt.6 (Beat BB6).
//
// Pins PRD §2 goal G4 — asymptotic CCX-depth shape of the bucket-brigade
// QRAM body matches the closed-form O(W · log²(N)) claim from the plan
// §4.5 phase walk.
//
// Fixture: fixed W, N ∈ {4, 8, 16, 32}, RecordingSink + APPEND-mode
// context (no orkan budget — the (N=32) instantiation's qubit footprint
// far exceeds the kMaxQubits=17 simulator cap, but we only need the
// gate-record stream here). Per N:
//   1. Run `lib_qram_read_bb_dsl<W, N>(a, i, b)` once.
//   2. Parse the IR window into a per-qubit timeline; compute CCX-depth
//      as the longest CCX chain across all qubits (a CCX gate touching
//      3 qubits advances each of those qubits' timeline to
//      `1 + max(d_q1, d_q2, d_q3)`).
//   3. Pin the §4.5 ceiling: depth(N) ≤ C₀ · W · log²(N) where C₀ is
//      derived from the N=4 measurement plus a 1.25× headroom, captured
//      as a constexpr below. The test fails (assertion) if any N
//      breaches the bound — that's a regression signal for the BB phase
//      walk's parallelism shape.
//
// Spec deviation (W = 5 instead of issue's "Fixed W = 4"). The BB DSL's
// `static_assert(W >= ct_log2(Nprime))` in `qram_read_bb_dsl.hpp:127`
// requires the address-width W to carry ⌈log₂(N')⌉ bits. For N=32,
// log₂(32)=5 — W=4 fails the static_assert at instantiation time and
// the test would not compile. We use the SMALLEST W that admits all
// four N values (W=5 ⇒ K=5 ≤ W). The depth-scaling shape `O(W · log²(N))`
// is invariant in W (only the constant prefactor scales), so the test
// still pins the polylog shape — just at a different prefactor than the
// hypothetical W=4 baseline. This deviation is flagged in the BB6
// closing report.
//
// LoC budget: ≤ 250 (plan §1, §5 / Beat BB6).
//
// Threading: -j6 / --parallel 6 only.

#define STURM_BACKEND_ENABLED 1

#include "sturm/detail/lib/qram_read_bb_dsl.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace {

// ── Width fixture (see header for the W = 5 vs W = 4 deviation note) ─
constexpr std::size_t W = 5u;

// ── log₂ helper for the bound recompute ─────────────────────────────
constexpr std::size_t ilog2_ceil(std::size_t n) noexcept {
    std::size_t r = 0u;
    while ((std::size_t{1} << r) < n) ++r;
    return r;
}

// APPEND-mode context wrapper — opens the IR-record window. Mirrors the
// BB3 / BB4 / BB5 test pattern. SIMULATE is unusable at N=32 (qubit
// budget » kMaxQubits=17), so every fixture runs APPEND-only.
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

template <std::size_t Wv>
static void alloc_qint(sturm::qint_t<Wv>& q, std::size_t nb,
                       std::int64_t v) {
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

// ── CCX-depth analysis ──────────────────────────────────────────────
// Walk the IR window [ir_before, ir_after) and compute the longest CCX
// chain across all qubits. For every CCX (3-qubit) record, advance each
// touched qubit's depth to `1 + max(d_q[0], d_q[1], d_q[2])`. The total
// CCX-depth is the max over the final per-qubit map. We track only CCX
// gates per the §4.5 derivation — CNOT / X layers run at constant
// overhead within each CCX layer's depth-1 control budget (B5a) and
// do not contribute additional asymptotic depth. Returns the longest
// CCX chain seen across all qubits.
static std::size_t compute_ccx_depth(sturm_backend_context_t* ctx,
                                     std::size_t ir_before,
                                     std::size_t ir_after) {
    std::unordered_map<std::uint32_t, std::size_t> depth_of;
    std::size_t max_depth = 0u;
    for (std::size_t r = ir_before; r < ir_after; ++r) {
        const auto& rec = ctx->ir.at(r);
        if (rec.kind != STURM_GATE_CCX) continue;
        // Find the max current depth among the qubits this CCX touches.
        std::size_t base = 0u;
        for (std::uint8_t k = 0; k < rec.n; ++k) {
            const auto it = depth_of.find(rec.qubits[k]);
            if (it != depth_of.end() && it->second > base) base = it->second;
        }
        // Advance every touched qubit to base + 1.
        const std::size_t nd = base + 1u;
        for (std::uint8_t k = 0; k < rec.n; ++k) {
            depth_of[rec.qubits[k]] = nd;
        }
        if (nd > max_depth) max_depth = nd;
    }
    return max_depth;
}

// ── Single (N) measurement ──────────────────────────────────────────
// Allocate qint registers, open APPEND, call lib_qram_read_bb_dsl,
// compute CCX-depth, release. Promote each a[k] to quantum so the
// §4.4 closed form applies uniformly across all fixtures.
template <std::size_t N>
static std::size_t measure_depth_for_N() {
    using namespace sturm;
    constexpr std::size_t K = ilog2_ceil(N);
    static_assert(K <= W, "BB6: K must fit in W (address-width invariant)");

    QubitPool::instance().reset_for_testing();
    AppendCtx ac;

    qint_t<W> i_reg, b_reg;
    alloc_qint<W>(i_reg, K, /*value=*/0);
    alloc_qint<W>(b_reg, W, /*value=*/0);

    std::array<qint_t<W>, N> a;
    for (std::size_t k = 0; k < N; ++k) {
        alloc_qint<W>(a[k], W, static_cast<std::int64_t>(k));
    }

    const std::size_t ir_before = ac.ctx->ir.size();
    lib_qram_read_bb_dsl<W, N>(a.data(), i_reg, b_reg);
    const std::size_t ir_after = ac.ctx->ir.size();

    const std::size_t depth = compute_ccx_depth(ac.ctx, ir_before, ir_after);

    release_qint<W>(i_reg);
    release_qint<W>(b_reg);
    for (std::size_t k = 0; k < N; ++k) release_qint<W>(a[k]);

    return depth;
}

// ── Main pin: ceiling per §4.5 ──────────────────────────────────────
// Derivation of C₀ (plan `docs/plan_qram_backend_bb.md` §4.5):
//   depth(N) ≤ C₀ · W · log²(N)
//   ⇒ C₀ ≥ depth(4) / (W · log²(4)) = depth(4) / (W · 4)
//
// The 1.25× headroom absorbs the constant-shift between the asymptotic
// shape and the small-N measurement (the (N=4) point includes one
// layer of phase-1 setup + one layer of phase-3 teardown that does NOT
// scale with log²(N) — those constants vanish for large N but inflate
// the small-N intercept). The ceiling holds for every N ∈ {4, 8, 16, 32}
// against this single derived C₀.
//
// `kHeadroom` lives at the call site so the constant + comment travel
// together; the BB6 reviewer must inspect this comment if the test
// ever flips and the constant needs re-tuning.
constexpr double kHeadroom = 1.25;

static double bound_for(std::size_t N, double C0) noexcept {
    const std::size_t lN = ilog2_ceil(N);
    return C0 * static_cast<double>(W) * static_cast<double>(lN * lN);
}

static void test_depth_scaling_pin() {
    // Measure each N.
    const std::size_t d4  = measure_depth_for_N<4u>();
    const std::size_t d8  = measure_depth_for_N<8u>();
    const std::size_t d16 = measure_depth_for_N<16u>();
    const std::size_t d32 = measure_depth_for_N<32u>();

    // Derive C₀ from the N=4 measurement plus 1.25× headroom. log²(4) = 4.
    const double C0_raw = static_cast<double>(d4)
                          / (static_cast<double>(W) * 4.0);
    const double C0     = C0_raw * kHeadroom;

    // Forensic print (issue requirement: "Print actual depth + bound on
    // each fixture for forensic re-tuning"). Output is on the test's
    // stdout — captured by ctest --output-on-failure if the pin trips.
    std::printf("BB6 depth_scaling (W=%zu, kHeadroom=%.3f):\n",
                W, kHeadroom);
    std::printf("  C0_raw = depth(4) / (W * log2(4)^2) = %zu / (%zu * 4) = %.4f\n",
                d4, W, C0_raw);
    std::printf("  C0     = C0_raw * kHeadroom         = %.4f\n", C0);

    struct Row { std::size_t N; std::size_t depth; };
    const Row rows[] = { {4u, d4}, {8u, d8}, {16u, d16}, {32u, d32} };
    for (const auto& r : rows) {
        const double bnd = bound_for(r.N, C0);
        std::printf("  N=%2zu: depth=%4zu  bound=C0*W*log2(N)^2=%8.3f  "
                    "ratio=%.4f  log2(N)=%zu\n",
                    r.N, r.depth, bnd,
                    static_cast<double>(r.depth) / bnd, ilog2_ceil(r.N));
        // Strict ceiling pin.
        assert(static_cast<double>(r.depth) <= bnd + 1e-9
               && "BB6 §4.5: depth(N) breached C0 * W * log2(N)^2");
    }
}

}  // namespace

int main() {
    std::puts("sturm-44bt.6 Beat BB6: lib_qram_read_bb_dsl depth-scaling pin:");
    test_depth_scaling_pin();
    std::puts("  PASS: depth(N) ≤ C0 * W * log2(N)^2 for N ∈ {4, 8, 16, 32}");
    std::puts("test_qram_read_bb_depth_scaling: OK");
    return 0;
}
