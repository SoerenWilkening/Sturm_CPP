// test_qram_read_dsl_recording.cpp -- sturm-2w6h.4 (Beat B2).
//
// Pins lib_qram_read_qrom_dsl from
// include/sturm/detail/lib/qram_read_dsl.hpp for the QRAM backend
// gate-emission epic (plan `docs/plan_qram_backend.md` §5 Beat B2).
//
// Coverage (G2 per plan §3 / PRD §4):
//   - For (N, W) ∈ {(2, 2), (4, 4), (8, 4)} with a fixed seeded random
//     classical container `a`:
//     (a) Every IR record's gate kind is in {X, CX, CCX} — the
//         underlying `quantum_xor` / `quantum_and` primitives. No
//         rotations, no prepares, no compare/arith ops leak through.
//     (b) The current_sink() does not see Layer-A op records (the DSL
//         emits IR gates directly via emit_X_lifted / emit_CX_lifted /
//         emit_CCX_lifted; counter records like qram_read are not
//         emitted from the DSL helper itself — the umbrella umbrella
//         counter bumps live on the public dispatch helper, not in
//         the DSL body).
//     (c) The total IR primitive count matches the §4 cheat-sheet
//         formula recomputed by the test from (N, W, popcount(a[k])):
//
//             total = sum_{k=0..n-1}( 2 * predicate_cost(K)
//                                    + popcount(a[k] low-W) )
//
//         predicate_cost(K) = 2*K + lib_c_n_AND_dsl(K, eq_k) cost.
//             K==0: 1   (single X on eq_k from c_n_AND with 0 ctrls)
//             K==1: 1   (single CX from c_n_AND with 1 ctrl)
//             K==2: 1   (single CCX from c_n_AND with 2 ctrls)
//             K>=3: 2*K - 3 (NC sandwich)
//
//     The test recomputes the expected count — no hardcoded numbers.
//
// LoC budget: <= 300 (plan §1, §5 / B2).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>

namespace {

// APPEND-mode context helper for the gate-record inspection.
struct AppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit AppendCtx(uint32_t max_q = 128u) {
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

// ── ceil_log2 (matches the production header's helper) ──────────────
static std::size_t ceil_log2_runtime(std::size_t n) noexcept {
    if (n <= 1u) return 0u;
    std::size_t r = 0u;
    std::size_t v = n - 1u;
    while (v > 0u) { ++r; v >>= 1; }
    return r;
}

// ── cn_AND cost (gates emitted by lib_c_n_AND_dsl for n_controls) ──
// Per c_and_dsl.hpp:1-23 / sturm-nmf1:
//   n=0: bare X on tgt                                    — 1 gate
//   n=1: CX (operator^=)                                  — 1 gate
//   n=2: single CCX (lib_c_AND_dsl)                       — 1 gate
//   n>=3: Nielsen-Chuang sandwich:
//         forward sweep n-1 CCX (anc[0]+loop i=1..n-3 + tgt)
//         reverse sweep n-2 CCX (loop+anc[0])
//         total 2n-3 CCX gates  (matches header comment line 21).
static std::size_t c_n_AND_cost(std::size_t n_controls) noexcept {
    if (n_controls == 0u) return 1u;          // single X target
    if (n_controls == 1u) return 1u;          // single CX
    if (n_controls == 2u) return 1u;          // single CCX
    return 2u * n_controls - 3u;              // n>=3: NC sandwich
}

// ── predicate cost (per k, compute or uncompute is symmetric) ───────
// For each k we call lib_qram_eq_k_compute then ..._uncompute. Each
// body issues 2*K X-flips on i (for the bits where (k>>j) is 0 OR 1)
// — actually only the bits where bit-j of k is 0 get flipped. But
// since the X-flips are paired (compute does flip+flip, so 2 per bit
// where bit-j of k is 0). We measure the total over compute+uncompute:
//   For each k:
//     (compute) X-flips: 2 * popcount_zero_low_K(k)
//                          (each "if zero" bit gets flip + un-flip)
//     (compute) c_n_AND_cost(K)
//     (uncompute) same as compute
//   So per k: 4 * popcount_zero_low_K(k) + 2 * c_n_AND_cost(K)
//   Plus the WHEN(eq_k) body: popcount(a[k] & low-W mask) CX gates
//   (each b.bit(j).flip() under the eq_k WHEN scope lifts to a CX).
static std::size_t expected_total(std::size_t N, std::size_t W,
                                  const std::uint64_t* a) noexcept {
    const std::size_t K = ceil_log2_runtime(N);
    const std::uint64_t low_w_mask = (W >= 64u) ? ~std::uint64_t{0}
                                  : ((std::uint64_t{1} << W) - 1u);
    std::size_t total = 0u;
    for (std::size_t k = 0; k < N; ++k) {
        // X-flips on i bits where (k>>j) bit is 0, both in compute and uncompute.
        std::size_t n_zero = 0u;
        for (std::size_t j = 0; j < K; ++j) {
            if (((k >> j) & 1u) == 0u) ++n_zero;
        }
        // compute = n_zero (flip back) + c_n_AND_cost(K) + n_zero (the "undo" flips)
        const std::size_t compute_cost  = 2u * n_zero + c_n_AND_cost(K);
        const std::size_t uncompute_cost = compute_cost;  // same body
        // Payload XOR-fanout: popcount(a[k] & low_w_mask) CX under WHEN(eq_k).
        std::size_t pop = 0u;
        std::uint64_t v = a[k] & low_w_mask;
        while (v) { pop += static_cast<std::size_t>(v & 1u); v >>= 1; }
        total += compute_cost + uncompute_cost + pop;
    }
    return total;
}

// ── Build a qint_t<W> with allocated qubits and value-tracking. ────
// Used for `i` (W active bits, super_mask covering them).
template <std::size_t W>
static void allocate_quantum_register(sturm::qint_t<W>& reg,
                                      std::size_t active_bits,
                                      std::int64_t value = 0) {
    for (std::size_t j = 0; j < active_bits; ++j) {
        reg.qubits[j] = sturm::QubitPool::instance().allocate();
        reg.super_mask |= (1ULL << j);
    }
    reg.value = value;
}

// ── Build a fully classical container `a` (fixed seeded RNG). ──────
template <std::size_t W>
static void seeded_classical_container(sturm::qint_t<W>* a, std::size_t N,
                                       std::uint64_t seed,
                                       std::uint64_t* a_vals_out) {
    std::mt19937_64 rng(seed);
    const std::uint64_t low_w_mask =
        (W >= 64u) ? ~std::uint64_t{0} : ((std::uint64_t{1} << W) - 1u);
    for (std::size_t k = 0; k < N; ++k) {
        const std::uint64_t v = rng() & low_w_mask;
        a[k] = sturm::qint_t<W>(static_cast<std::int64_t>(v));
        a_vals_out[k] = v;
    }
}

// ── Run one (N, W) configuration. ───────────────────────────────────
template <std::size_t W>
static void run_config(std::size_t N, std::uint64_t seed) {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);
    AppendCtx ac{128u};

    // Allocate `i` and `b` registers as full quantum (W active bits each).
    sturm::qint_t<W> i_reg;
    sturm::qint_t<W> b_reg;
    allocate_quantum_register<W>(i_reg, W, /*value=*/0);
    allocate_quantum_register<W>(b_reg, W, /*value=*/0);

    // Build classical container — fully classical (mask=0, no qubits).
    sturm::qint_t<W>* a = new sturm::qint_t<W>[N];
    std::uint64_t* a_vals = new std::uint64_t[N];
    seeded_classical_container<W>(a, N, seed, a_vals);

    rec.clear();
    const std::size_t ir_before = ac.ctx->ir.size();

    sturm::lib_qram_read_qrom_dsl<W>(a, N, i_reg, b_reg);

    // (a) Every emitted IR gate must be X / CX / CCX — no rotations,
    // no preparations, no higher-level ops.
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        const auto& g = ac.ctx->ir.at(r);
        const bool ok = (g.kind == STURM_GATE_X)
                     || (g.kind == STURM_GATE_CX)
                     || (g.kind == STURM_GATE_CCX);
        if (!ok) {
            std::fprintf(stderr,
                "B2 recording: unexpected gate kind %d (N=%zu, W=%zu)\n",
                static_cast<int>(g.kind), N, W);
        }
        assert(ok && "B2: gate-set restricted to {X, CX, CCX}");
    }

    // (b) sink must not see any Layer-A records — DSL emits IR
    // primitives directly through the lifted emitters; the umbrella
    // qram_read counter is bumped at the public dispatch site (not
    // by this DSL helper).
    if (!rec.records().empty()) {
        std::fprintf(stderr,
            "B2 recording: stray sink record op='%s' (N=%zu, W=%zu)\n",
            rec.records().front().op.c_str(), N, W);
    }
    assert(rec.records().empty()
           && "B2: lib_qram_read_qrom_dsl must not call sink ops");

    // (c) Total IR primitive count matches the §4 formula.
    const std::size_t actual = ac.ctx->ir.size() - ir_before;
    const std::size_t expected = expected_total(N, W, a_vals);
    if (actual != expected) {
        std::fprintf(stderr,
            "B2 recording: count mismatch N=%zu W=%zu expected=%zu actual=%zu\n",
            N, W, expected, actual);
    }
    assert(actual == expected
           && "B2: total primitive count must match §4 cheat-sheet formula");

    // ── Cleanup ────────────────────────────────────────────────────
    for (std::size_t j = 0; j < W; ++j) {
        if (i_reg.qubits[j] >= 0) {
            sturm::QubitPool::instance().release(i_reg.qubits[j]);
            i_reg.qubits[j] = -1;
        }
        if (b_reg.qubits[j] >= 0) {
            sturm::QubitPool::instance().release(b_reg.qubits[j]);
            b_reg.qubits[j] = -1;
        }
    }
    i_reg.super_mask = 0;
    b_reg.super_mask = 0;
    delete[] a;
    delete[] a_vals;
}

}  // namespace

int main() {
    std::puts("sturm-2w6h.4 Beat B2: lib_qram_read_qrom_dsl recording test:");

    // (N=2, W=2): K=1, single CX predicate; W payload bits.
    run_config<2u>(/*N=*/2u, /*seed=*/0xA1B2C3D4u);
    std::puts("  PASS: (N=2, W=2) gate-set + count match");

    // (N=4, W=4): K=2, single CCX predicate; W payload bits.
    run_config<4u>(/*N=*/4u, /*seed=*/0xDEADBEEFu);
    std::puts("  PASS: (N=4, W=4) gate-set + count match");

    // (N=8, W=4): K=3, NC sandwich predicate; W payload bits.
    run_config<4u>(/*N=*/8u, /*seed=*/0xCAFEBABEu);
    std::puts("  PASS: (N=8, W=4) gate-set + count match");

    std::puts("test_qram_read_dsl_recording: OK");
    return 0;
}
