// test_measure.cpp — M5: Measurement op per mode (TDD, written before implementation).
//
// Tests:
//   1. COUNT_ONLY: sturm_measure returns the stored classical value (0 or 1).
//   2. APPEND:     same deterministic placeholder as COUNT_ONLY.
//   3. SIMULATE:   H|0> state on qubit 0 — repeated samples produce roughly 50/50
//                  outcome distribution within a 5-sigma statistical bound.
//   4. SIMULATE:   |0> state always measures 0; |1> state always measures 1.
//   5. SIMULATE:   after measurement, second measurement agrees (collapse idempotent).
//
// C ABI under test (declared in core.h):
//   int sturm_measure(uint32_t qubit)
//     — uses thread-local context
//
// C++ helper under test (declared in measure.hpp):
//   int sturm::measure_qubit(uint32_t qubit, BackendContext& ctx)
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/measure.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ── Helpers ───────────────────────────────────────────────────────────────────

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL: %s\n", (msg));              \
            std::abort();                                            \
        }                                                            \
    } while (0)

// ── Fixture: context with an OrkanBridge installed ───────────────────────────
//
// RAII wrapper that:
//   - creates a BackendContext with the requested mode
//   - optionally attaches an OrkanBridge (SIMULATE mode)
//   - installs the context as the thread-local context
//   - tears down cleanly on destruction

struct ScopedCtx {
    sturm_backend_context_t* ctx{nullptr};
    sturm::OrkanBridge*      bridge{nullptr};

    ScopedCtx(sturm_mode_t mode, uint32_t n_qubits = 1u) {
        ctx = sturm_backend_create(mode);
        assert(ctx);
        if (mode == STURM_MODE_SIMULATE) {
            bridge = new sturm::OrkanBridge();
            bridge->allocate(n_qubits);
            ctx->orkan_state_ptr = bridge;
        }
        sturm_set_thread_context(ctx);
    }

    ~ScopedCtx() {
        sturm_set_thread_context(nullptr);
        ctx->orkan_state_ptr = nullptr; // bridge not owned by ctx
        sturm_backend_destroy(ctx);
        delete bridge;
    }
};

// ── Test 1: COUNT_ONLY returns stored classical value 0 ──────────────────────

static void test_count_only_returns_classical_0() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);
    // get_classical_value returns 0 by default for any unwritten qubit
    // (sturm-t2sk).
    sc.ctx->set_classical_value(0u, 0);
    int result = sturm_measure(0u);
    CHECK(result == 0, "COUNT_ONLY: qubit with classical val 0 must return 0");
}

// ── Test 2: COUNT_ONLY returns stored classical value 1 ──────────────────────

static void test_count_only_returns_classical_1() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);
    sc.ctx->set_classical_value(1u, 1);
    int result = sturm_measure(1u);
    CHECK(result == 1, "COUNT_ONLY: qubit with classical val 1 must return 1");
}

// ── Test 3: APPEND behaves like COUNT_ONLY (deterministic placeholder) ────────

static void test_append_deterministic_0() {
    ScopedCtx sc(STURM_MODE_APPEND);
    sc.ctx->set_classical_value(0u, 0);
    int r = sturm_measure(0u);
    CHECK(r == 0, "APPEND: returns classical value 0");
}

static void test_append_deterministic_1() {
    ScopedCtx sc(STURM_MODE_APPEND);
    sc.ctx->set_classical_value(2u, 1);
    int r = sturm_measure(2u);
    CHECK(r == 1, "APPEND: returns classical value 1");
}

// ── Test 4: SIMULATE |0> always measures 0 ───────────────────────────────────

static void test_simulate_deterministic_zero() {
    for (int trial = 0; trial < 20; ++trial) {
        ScopedCtx sc(STURM_MODE_SIMULATE, 1u);
        // State is |0> by default after allocate.
        int outcome = sturm_measure(0u);
        CHECK(outcome == 0, "SIMULATE |0> must always measure 0");
    }
}

// ── Test 5: SIMULATE |1> always measures 1 ───────────────────────────────────

static void test_simulate_deterministic_one() {
    for (int trial = 0; trial < 20; ++trial) {
        ScopedCtx sc(STURM_MODE_SIMULATE, 1u);
        // Prepare |1> by applying X to qubit 0.
        orkan::apply_x(sc.bridge->state(), 0u);
        int outcome = sturm_measure(0u);
        CHECK(outcome == 1, "SIMULATE |1> must always measure 1");
    }
}

// ── Test 6: SIMULATE H|0> produces ~50/50 distribution ───────────────────────
//
// N_TRIALS measurements, each on a freshly prepared H|0> state.
// H|0> = (|0>+|1>)/sqrt(2) → P(0)=P(1)=0.5.
// Statistical bound: 5-sigma (sigma = sqrt(N*0.25)/N for Bernoulli(0.5)).

static void test_simulate_h0_approx_50_50() {
    static constexpr int N_TRIALS = 1000;

    int count_ones = 0;
    for (int i = 0; i < N_TRIALS; ++i) {
        ScopedCtx sc(STURM_MODE_SIMULATE, 1u);
        orkan::apply_h(sc.bridge->state(), 0u);  // H|0>
        int outcome = sturm_measure(0u);
        CHECK(outcome == 0 || outcome == 1,
              "SIMULATE: measure must return 0 or 1");
        count_ones += outcome;
    }

    double fraction = static_cast<double>(count_ones) / N_TRIALS;
    // sigma of fraction for Bernoulli(0.5): sqrt(p*(1-p)/N)
    double sigma = std::sqrt(0.25 / N_TRIALS);
    double bound = 5.0 * sigma;

    std::printf("  H|0> outcomes: %d/%d ones (frac=%.4f, 5sigma=%.4f)\n",
                count_ones, N_TRIALS, fraction, bound);

    CHECK(std::abs(fraction - 0.5) <= bound,
          "SIMULATE H|0> fraction outside 5-sigma bound");
}

// ── Test 7: SIMULATE collapse is idempotent ───────────────────────────────────
//
// After measuring H|0>, the state collapses.  A second measurement must agree
// with the first (regardless of outcome).

static void test_simulate_collapse_idempotent() {
    static constexpr int N_TRIALS = 50;

    for (int trial = 0; trial < N_TRIALS; ++trial) {
        ScopedCtx sc(STURM_MODE_SIMULATE, 1u);
        orkan::apply_h(sc.bridge->state(), 0u);
        int first  = sturm_measure(0u);
        int second = sturm_measure(0u);
        CHECK(first == second,
              "SIMULATE: second measure after collapse must agree with first");
    }
}

// ── Test 8: SIMULATE measure clears super_mask bit ───────────────────────────
//
// Set super_mask bit 0 before measuring; after measure_qubit returns the bit
// must be clear regardless of outcome.

static void test_simulate_clears_super_mask() {
    for (int trial = 0; trial < 20; ++trial) {
        ScopedCtx sc(STURM_MODE_SIMULATE, 1u);
        sc.ctx->super_mask = 0xFFFFFFFFu;  // all bits set, including bit 0
        sturm_measure(0u);
        CHECK((sc.ctx->super_mask & 1u) == 0u,
              "SIMULATE measure: super_mask bit 0 must be cleared after measure");
    }
}

// ── Test 9: SIMULATE measure clears promotion_mask bit ───────────────────────

static void test_simulate_clears_promotion_mask() {
    for (int trial = 0; trial < 20; ++trial) {
        ScopedCtx sc(STURM_MODE_SIMULATE, 1u);
        sc.ctx->promotion_mask = 0xFFFFFFFFu;
        sturm_measure(0u);
        CHECK((sc.ctx->promotion_mask & 1u) == 0u,
              "SIMULATE measure: promotion_mask bit 0 must be cleared after measure");
    }
}

// ── Test 10: COUNT_ONLY and APPEND leave masks untouched ──────────────────────
//
// In non-SIMULATE modes the masks are not modified; measure returns the stored
// classical value and the masks remain exactly as set.

static void test_non_simulate_modes_leave_masks_untouched() {
    // COUNT_ONLY
    {
        ScopedCtx sc(STURM_MODE_COUNT_ONLY);
        sc.ctx->super_mask     = 0x5u;
        sc.ctx->promotion_mask = 0xAu;
        sturm_measure(0u);
        CHECK(sc.ctx->super_mask     == 0x5u,
              "COUNT_ONLY: super_mask must be unchanged after measure");
        CHECK(sc.ctx->promotion_mask == 0xAu,
              "COUNT_ONLY: promotion_mask must be unchanged after measure");
    }
    // APPEND
    {
        ScopedCtx sc(STURM_MODE_APPEND);
        sc.ctx->super_mask     = 0x3u;
        sc.ctx->promotion_mask = 0xCu;
        sturm_measure(0u);
        CHECK(sc.ctx->super_mask     == 0x3u,
              "APPEND: super_mask must be unchanged after measure");
        CHECK(sc.ctx->promotion_mask == 0xCu,
              "APPEND: promotion_mask must be unchanged after measure");
    }
}

// ── Test 11: classical_values dynamic widening (sturm-t2sk) ───────────────────
//
// The pre-sturm-t2sk array was capped at 17 entries; a write at qubit ≥ 17
// silently no-op'd. Confirm that the new vector-backed storage grows on demand
// and that high-index qubits read back exactly what was written.

static void test_classical_values_dynamic_widening() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);
    // 17 was the old cap; 25 is comfortably past it. 63 exercises the
    // top of the new u64 mask range as a stress check.
    sc.ctx->set_classical_value(25u, 1);
    sc.ctx->set_classical_value(63u, 1);
    CHECK(sturm_measure(25u) == 1,
          "sturm-t2sk: COUNT_ONLY must return stored value at qubit 25");
    CHECK(sturm_measure(63u) == 1,
          "sturm-t2sk: COUNT_ONLY must return stored value at qubit 63");
    // Unwritten high index reads as 0 (placeholder contract).
    CHECK(sturm_measure(40u) == 0,
          "sturm-t2sk: COUNT_ONLY must return 0 for unwritten qubit index");
}

// ── Test 12: per-context mask widening to u64 (sturm-7at0) ────────────────────
//
// The pre-sturm-7at0 super_mask / promotion_mask were uint32_t; writes that set
// bit ≥ 32 truncated. Confirm that the widened u64 mask can carry bits 32-63
// across a non-SIMULATE measure call (which leaves the masks untouched per
// test 10).

static void test_per_context_mask_widening_u64() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);
    const uint64_t hi_super     = uint64_t{1} << 35;
    const uint64_t hi_promotion = uint64_t{1} << 40;
    sc.ctx->super_mask     = hi_super;
    sc.ctx->promotion_mask = hi_promotion;
    sturm_measure(0u);  // non-SIMULATE: masks untouched
    CHECK(sc.ctx->super_mask     == hi_super,
          "sturm-7at0: super_mask must carry bit 35 across a measure call");
    CHECK(sc.ctx->promotion_mask == hi_promotion,
          "sturm-7at0: promotion_mask must carry bit 40 across a measure call");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("test_measure: COUNT_ONLY classical-0 ...\n");
    test_count_only_returns_classical_0();

    std::printf("test_measure: COUNT_ONLY classical-1 ...\n");
    test_count_only_returns_classical_1();

    std::printf("test_measure: APPEND deterministic-0 ...\n");
    test_append_deterministic_0();

    std::printf("test_measure: APPEND deterministic-1 ...\n");
    test_append_deterministic_1();

    std::printf("test_measure: SIMULATE |0> deterministic ...\n");
    test_simulate_deterministic_zero();

    std::printf("test_measure: SIMULATE |1> deterministic ...\n");
    test_simulate_deterministic_one();

    std::printf("test_measure: SIMULATE H|0> ~50/50 ...\n");
    test_simulate_h0_approx_50_50();

    std::printf("test_measure: SIMULATE collapse idempotent ...\n");
    test_simulate_collapse_idempotent();

    std::printf("test_measure: SIMULATE clears super_mask ...\n");
    test_simulate_clears_super_mask();

    std::printf("test_measure: SIMULATE clears promotion_mask ...\n");
    test_simulate_clears_promotion_mask();

    std::printf("test_measure: non-SIMULATE modes leave masks untouched ...\n");
    test_non_simulate_modes_leave_masks_untouched();

    std::printf("test_measure: classical_values dynamic widening (sturm-t2sk) ...\n");
    test_classical_values_dynamic_widening();

    std::printf("test_measure: per-context mask widening to u64 (sturm-7at0) ...\n");
    test_per_context_mask_widening_u64();

    std::printf("PASS: all test_measure tests passed.\n");
    return 0;
}
