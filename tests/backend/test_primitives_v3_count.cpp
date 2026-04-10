// test_primitives_v3_count.cpp — M11: primitives_v3 COUNT_ONLY mode test.
//
// Verifies that gate_count increments correctly for each v3 primitive
// when called in COUNT_ONLY mode.
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cstdio>

// ── Fixture: scoped context installer ────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 17u) {
        ctx  = sturm_backend_create(mode, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── Test: primitive_X increments gate_count by 1 ─────────────────────────────

static void test_primitive_X_count() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sc.ctx->gate_count == 0u);
    sturm::primitive_X(*sc.ctx, 0u);
    assert(sc.ctx->gate_count == 1u);
    sturm::primitive_X(*sc.ctx, 0u);
    assert(sc.ctx->gate_count == 2u);
    std::printf("  primitive_X COUNT_ONLY: PASS\n");
}

// ── Test: primitive_XOR increments gate_count by 1 ───────────────────────────

static void test_primitive_XOR_count() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sc.ctx->gate_count == 0u);
    sturm::primitive_XOR(*sc.ctx, 0u, 1u);
    assert(sc.ctx->gate_count == 1u);
    sturm::primitive_XOR(*sc.ctx, 0u, 1u);
    assert(sc.ctx->gate_count == 2u);
    std::printf("  primitive_XOR COUNT_ONLY: PASS\n");
}

// ── Test: primitive_AND increments gate_count by 1 ───────────────────────────

static void test_primitive_AND_count() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sc.ctx->gate_count == 0u);
    sturm::primitive_AND(*sc.ctx, 0u, 1u, 2u);
    assert(sc.ctx->gate_count == 1u);
    sturm::primitive_AND(*sc.ctx, 0u, 1u, 2u);
    assert(sc.ctx->gate_count == 2u);
    std::printf("  primitive_AND COUNT_ONLY: PASS\n");
}

// ── Test: primitive_phase increments gate_count by 1 ─────────────────────────

static void test_primitive_phase_count() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sc.ctx->gate_count == 0u);
    sturm::primitive_phase(*sc.ctx, 0u, 1.5707963267948966);
    assert(sc.ctx->gate_count == 1u);
    sturm::primitive_phase(*sc.ctx, 0u, 0.785398163397448);
    assert(sc.ctx->gate_count == 2u);
    std::printf("  primitive_phase COUNT_ONLY: PASS\n");
}

// ── Test: primitive_phi_add increments gate_count by 1 ───────────────────────

static void test_primitive_phi_add_count() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sc.ctx->gate_count == 0u);
    sturm::primitive_phi_add(*sc.ctx, 0u, 1.5707963267948966);
    assert(sc.ctx->gate_count == 1u);
    sturm::primitive_phi_add(*sc.ctx, 0u, 0.785398163397448);
    assert(sc.ctx->gate_count == 2u);
    std::printf("  primitive_phi_add COUNT_ONLY: PASS\n");
}

// ── Test: mixed calls accumulate correctly ────────────────────────────────────

static void test_mixed_primitives_count() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sc.ctx->gate_count == 0u);
    sturm::primitive_X(*sc.ctx, 0u);           // 1
    sturm::primitive_XOR(*sc.ctx, 0u, 1u);    // 2
    sturm::primitive_AND(*sc.ctx, 0u, 1u, 2u); // 3
    sturm::primitive_phase(*sc.ctx, 0u, 1.0);  // 4
    sturm::primitive_phi_add(*sc.ctx, 0u, 1.0);// 5
    assert(sc.ctx->gate_count == 5u);
    std::printf("  mixed primitives COUNT_ONLY: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M11 primitives_v3 COUNT_ONLY tests:\n");
    test_primitive_X_count();
    test_primitive_XOR_count();
    test_primitive_AND_count();
    test_primitive_phase_count();
    test_primitive_phi_add_count();
    test_mixed_primitives_count();
    std::printf("All M11 primitives_v3 COUNT_ONLY tests passed.\n");
    return 0;
}
