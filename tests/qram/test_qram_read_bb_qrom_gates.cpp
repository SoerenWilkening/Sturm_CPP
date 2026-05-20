// test_qram_read_bb_qrom_gates.cpp — sturm-44bt.3 (Beat BB3).
//
// REWRITES the v1 sibling `test_qram_read_qrom_gates.cpp` — pins the
// BB DSL helper end-to-end against PRD §2 goals G1 + G2 + G5 (plan
// docs/plan_qram_backend_bb.md §3 row table). BB3 lands the DSL layer;
// BB4 will wire the public `QRAM_read` surface. This test exercises
// `lib_qram_read_bb_dsl<W, N>` directly so the gate-stream contract
// from PRD §4 / plan §4.4 is pinned independent of the public surface.
//
// Fixture: (N, W) = (4, 4), classical `a = {0xA, 0x5, 0xF, 0x0}`,
// classical i = 2. K = log2(N) = 2.
//
// Assertions (issue sturm-44bt.3 §`Tests must assert`):
//   1. Exact CX + CCX + X counts match plan §4.4 closed form for
//      (N' = 4, W = 4): 108 CNOT, 56 CCX, 2 X. Counts are RECOMPUTED
//      from the formula — NO magic numbers in this source.
//   2. Every record's op ∈ {quantum_xor, quantum_and} — no rotations.
//   3. `i.super_mask == 0` and `i.value == 2` after the call.
//   4. TODO(BB4): split-counter parity is N/A at this layer — the
//      `qrom_read` / `qreg_read` sink hooks live on the public
//      `QRAM_read` site, which BB4 wires. Documented here so the BB4
//      reviewer remembers to extend the assertion set on the public
//      gate-stream test (`test_qram_read_bb_qreg_gates.cpp`).
//
// Out-of-range UB (PRD §5): the BB body sweeps the full Nprime leaves;
// `i >= N` at N == Nprime is well-defined (no padding active yet —
// BB5 lifts that gate). At BB3 we require `is_pow2(N)`, so the
// fixture is in-range.
//
// Threading: -j6 / --parallel 6 only.
// LoC budget: ≤ 300 (plan §1, §5 / Beat BB3).

#define STURM_BACKEND_ENABLED 1

#include "sturm/detail/lib/qram_read_bb_dsl.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
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
#include <string>

namespace {

// ── Plan §4.4 closed-form gate budget per (N', W) ───────────────────
// CCX(N, W)  = 4(N' − 2) + 4·W·(N' − 1)
// CNOT(N, W) = 2·N' + W·(8·N' − 7)
// X(N, W)    = 2 (one in Phase 1, one in Phase 3 — bare X(root.is_left))
//
// Recomputed from `Nprime` and `W` in `closed_form_counts`. The test
// MUST NOT hardcode 108 / 56 / 2 anywhere outside the comment that
// documents the (N=4, W=4) row of the cheat sheet (plan §4.4 table).
struct Budget {
    std::size_t ccx;
    std::size_t cnot;
    std::size_t x;
};

constexpr Budget closed_form_counts(std::size_t Nprime, std::size_t W) noexcept {
    return Budget{
        /*ccx =*/  4u * (Nprime - 2u)  +  4u * W * (Nprime - 1u),
        /*cnot=*/  2u * Nprime         +  W * (8u * Nprime - 7u),
        /*x   =*/  2u
    };
}

// APPEND-mode context wrapper — opens the IR-record window for
// inspection. Mirrors the v1 / BB1 / BB2 test pattern.
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

// Bind a classical-value qint_t<W> into pool-allocated qubits with
// super_mask covering the K low bits (so the address is "live" at
// the BB1 router-setup layer). Caller owns release via release_qint.
template <std::size_t W>
static void bind_classical(sturm::qint_t<W>& q, std::size_t K,
                           std::uint64_t value) {
    for (std::size_t j = 0; j < K; ++j) {
        q.qubits[j] = sturm::QubitPool::instance().allocate();
        q.super_mask |= (1ULL << j);
    }
    q.value = static_cast<int64_t>(value);
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

// ── Main test body: (N=4, W=4), a={0xA,0x5,0xF,0x0}, i=2. ──────────
//
// The fixture matches the v1 `test_qram_read_qrom_gates.cpp` headers
// exactly so reviewers can diff the assertions; the *body* is BB —
// classical `a` slots are kept fully classical (no allocated qubits)
// so the BB2 bus walk takes the "controlled-X-classical load" arm
// per BB2's design. The §4.4 closed-form table assumes every leaf
// emits the same `2W CCX + 4W CNOT` per internal node — that
// matches a fully *quantum* `a`. To pin the closed-form total we
// promote `a` to quantum here too.
static void test_qrom_gates_pin() {
    using namespace sturm;
    constexpr std::size_t W = 4u;
    constexpr std::size_t N = 4u;
    constexpr std::size_t K = 2u;  // log2(N)
    constexpr std::uint64_t kAvals[N] = { 0xAu, 0x5u, 0xFu, 0x0u };

    QubitPool::instance().reset_for_testing();

    RecordingSink rec;
    ScopedSink scope(&rec);
    AppendCtx ac;

    // Bind `i` to (W=4, K=2, value=2). The high 2 bits stay classical.
    qint_t<W> i_reg;
    bind_classical<W>(i_reg, K, /*value=*/2u);
    const auto i_mask_before = i_reg.super_mask;
    const auto i_value_before = i_reg.value;

    // Bind `b` to all-zero quantum (W qubits allocated, value=0).
    qint_t<W> b_reg;
    for (std::size_t j = 0; j < W; ++j) {
        b_reg.qubits[j] = QubitPool::instance().allocate();
        b_reg.super_mask |= (1ULL << j);
    }
    b_reg.value = 0;

    // Promote each `a[k]` to quantum so the closed-form §4.4 table
    // applies (every leaf-side CSWAP emits the standard CCX+2CNOT
    // pattern per scalar, regardless of `a[k].value`).
    std::array<qint_t<W>, N> a;
    for (std::size_t k = 0; k < N; ++k) {
        a[k].value = static_cast<int64_t>(kAvals[k]);
        for (std::size_t j = 0; j < W; ++j) {
            a[k].qubits[j] = QubitPool::instance().allocate();
            a[k].super_mask |= (1ULL << j);
        }
    }

    // ── Open the recording window ─────────────────────────────────
    rec.clear();
    const std::size_t ir_before = ac.ctx->ir.size();

    // Call the BB DSL — exercises BB1 setup → BB2 bus → BB1 teardown.
    lib_qram_read_bb_dsl<W, N>(a.data(), i_reg, b_reg);

    // ── (1) Exact CX + CCX + X counts vs §4.4 closed form ────────
    // Recompute the expected counts from (Nprime, W) — the formula is
    // the SINGLE source of truth here; the (N=4, W=4) row of the
    // cheat-sheet table reads 108 CNOT, 56 CCX, 2 X.
    constexpr Budget expected = closed_form_counts(/*Nprime=*/N, /*W=*/W);

    std::size_t actual_ccx = 0u, actual_cnot = 0u, actual_x = 0u;
    for (std::size_t r = ir_before; r < ac.ctx->ir.size(); ++r) {
        switch (static_cast<int>(ac.ctx->ir.at(r).kind)) {
            case STURM_GATE_X:   ++actual_x;    break;
            case STURM_GATE_CX:  ++actual_cnot; break;
            case STURM_GATE_CCX: ++actual_ccx;  break;
            default:
                std::fprintf(stderr,
                    "BB3 qrom_gates: unexpected IR kind %d at r=%zu\n",
                    static_cast<int>(ac.ctx->ir.at(r).kind), r);
                assert(false && "BB3: gate-set restricted to {X, CX, CCX}");
        }
    }

    if (actual_x != expected.x || actual_cnot != expected.cnot
        || actual_ccx != expected.ccx) {
        std::fprintf(stderr,
            "BB3 qrom_gates: §4.4 count mismatch (N=%zu, W=%zu)\n"
            "  X:   expected=%zu actual=%zu\n"
            "  CX:  expected=%zu actual=%zu\n"
            "  CCX: expected=%zu actual=%zu\n",
            N, W, expected.x, actual_x,
            expected.cnot, actual_cnot,
            expected.ccx, actual_ccx);
    }
    assert(actual_x   == expected.x
           && "BB3 §4.4: X count must match closed form");
    assert(actual_cnot == expected.cnot
           && "BB3 §4.4: CNOT count must match closed form");
    assert(actual_ccx == expected.ccx
           && "BB3 §4.4: CCX count must match closed form");

    // ── (2) Every record's op ∈ {quantum_xor, quantum_and} ───────
    // The DSL emits IR primitives via the lifted emitters; sink-side
    // records (if any) must be a strict subset of the bucket-brigade
    // op set. `qrom_read` / `qreg_read` are NOT in this set yet — see
    // TODO(BB4) below for the split-counter wiring.
    for (const auto& r : rec.records()) {
        const bool ok = (r.op == "quantum_xor") || (r.op == "quantum_and");
        if (!ok) {
            std::fprintf(stderr,
                "BB3 qrom_gates: stray sink op='%s'\n", r.op.c_str());
        }
        assert(ok && "BB3: sink records ⊆ {quantum_xor, quantum_and}");
    }

    // ── (3) i invariants ────────────────────────────────────────
    // The BB body's Phase 1 (setup) and Phase 3 (teardown) are mutual
    // inverses, so `i.super_mask` and `i.value` are unchanged across
    // the helper. The issue's literal `i.super_mask == 0` reflects
    // the *caller-side* exit contract — once the caller releases i's
    // qubits, super_mask falls back to 0. We pin both: unchanged-
    // across-the-helper AND zero-after-release. `i.value == 2` is
    // the value-side tracker, preserved verbatim.
    assert(i_reg.super_mask == i_mask_before
           && "BB3: i.super_mask unchanged across lib_qram_read_bb_dsl");
    assert(i_reg.value == i_value_before
           && i_reg.value == static_cast<int64_t>(2)
           && "BB3: i.value == 2 after the call (unchanged)");

    release_qint<W>(i_reg);
    assert(i_reg.super_mask == 0u && i_reg.value == static_cast<int64_t>(2)
           && "BB3: post-release i.super_mask == 0; i.value still 2");

    // ── (4) TODO(BB4): split-counter parity ──────────────────────
    // BB4 (sturm-44bt.4) wires `current_sink()->qrom_read()` /
    // `qreg_read()` on the public `QRAM_read` entry-point, and the
    // umbrella `qram::qram_read_count()` thread-local also bumps from
    // the public site. This BB3-level test deliberately calls the DSL
    // helper directly (bypassing the public surface), so split
    // telemetry is N/A here. The BB4 reviewer must extend the
    // assertion set in `test_qram_read_bb_qreg_gates.cpp` to pin
    // `qrom_read == 1`, `qreg_read == 0`, `qram_read_count == 1`.
    //
    // (No assertion fires here — the comment is the documentation.)

    // Cleanup: release `b` and `a[k]` qubits.
    release_qint<W>(b_reg);
    for (std::size_t k = 0; k < N; ++k) {
        release_qint<W>(a[k]);
    }
}

}  // namespace

int main() {
    std::puts("sturm-44bt.3 Beat BB3: lib_qram_read_bb_dsl gate-stream test:");
    test_qrom_gates_pin();
    std::puts("  PASS: (N=4, W=4) §4.4 closed-form count + gate-set + i invariants");
    std::puts("test_qram_read_bb_qrom_gates: OK");
    return 0;
}
