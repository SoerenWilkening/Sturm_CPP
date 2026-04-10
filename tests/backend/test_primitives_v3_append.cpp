// test_primitives_v3_append.cpp — M11: primitives_v3 APPEND mode test.
//
// Verifies that each v3 primitive pushes the correct GateRecord to the IR
// when called in APPEND mode.
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/backend/ir.hpp"
#include "sturm/backend/primitives_v3.hpp"

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

// ── Test: primitive_X pushes STURM_GATE_X record ─────────────────────────────

static void test_primitive_X_append() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sc.ctx->ir.size() == 0u);

    sturm::primitive_X(*sc.ctx, 3u);

    assert(sc.ctx->ir.size() == 1u);
    const sturm::GateRecord& r = sc.ctx->ir.at(0);
    assert(r.kind      == STURM_GATE_X);
    assert(r.n         == 1u);
    assert(r.qubits[0] == 3u);
    assert(r.param     == 0.0);
    std::printf("  primitive_X APPEND: PASS\n");
}

// ── Test: primitive_XOR pushes STURM_GATE_CX record ──────────────────────────

static void test_primitive_XOR_append() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sc.ctx->ir.size() == 0u);

    sturm::primitive_XOR(*sc.ctx, 2u, 5u);

    assert(sc.ctx->ir.size() == 1u);
    const sturm::GateRecord& r = sc.ctx->ir.at(0);
    assert(r.kind      == STURM_GATE_CX);
    assert(r.n         == 2u);
    assert(r.qubits[0] == 2u);  // ctrl
    assert(r.qubits[1] == 5u);  // tgt
    assert(r.param     == 0.0);
    std::printf("  primitive_XOR APPEND: PASS\n");
}

// ── Test: primitive_AND pushes STURM_GATE_CCX record ─────────────────────────

static void test_primitive_AND_append() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sc.ctx->ir.size() == 0u);

    sturm::primitive_AND(*sc.ctx, 1u, 4u, 7u);

    assert(sc.ctx->ir.size() == 1u);
    const sturm::GateRecord& r = sc.ctx->ir.at(0);
    assert(r.kind      == STURM_GATE_CCX);
    assert(r.n         == 3u);
    assert(r.qubits[0] == 1u);  // ctrl0
    assert(r.qubits[1] == 4u);  // ctrl1
    assert(r.qubits[2] == 7u);  // tgt
    assert(r.param     == 0.0);
    std::printf("  primitive_AND APPEND: PASS\n");
}

// ── Test: primitive_phase pushes STURM_GATE_RY record with correct theta ──────

static void test_primitive_phase_append() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sc.ctx->ir.size() == 0u);

    const double theta = 1.5707963267948966;  // pi/2
    sturm::primitive_phase(*sc.ctx, 0u, theta);

    assert(sc.ctx->ir.size() == 1u);
    const sturm::GateRecord& r = sc.ctx->ir.at(0);
    assert(r.kind      == STURM_GATE_RY);
    assert(r.n         == 1u);
    assert(r.qubits[0] == 0u);
    assert(r.param     == theta);
    std::printf("  primitive_phase APPEND: PASS\n");
}

// ── Test: primitive_phi_add pushes STURM_GATE_RZ record with correct theta ────

static void test_primitive_phi_add_append() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sc.ctx->ir.size() == 0u);

    const double theta = 0.785398163397448;  // pi/4
    sturm::primitive_phi_add(*sc.ctx, 6u, theta);

    assert(sc.ctx->ir.size() == 1u);
    const sturm::GateRecord& r = sc.ctx->ir.at(0);
    assert(r.kind      == STURM_GATE_RZ);
    assert(r.n         == 1u);
    assert(r.qubits[0] == 6u);
    assert(r.param     == theta);
    std::printf("  primitive_phi_add APPEND: PASS\n");
}

// ── Test: sequential calls build up correct IR ───────────────────────────────

static void test_sequential_append() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sc.ctx->ir.size() == 0u);

    sturm::primitive_X(*sc.ctx, 0u);
    sturm::primitive_XOR(*sc.ctx, 0u, 1u);
    sturm::primitive_AND(*sc.ctx, 0u, 1u, 2u);

    assert(sc.ctx->ir.size() == 3u);
    assert(sc.ctx->ir.at(0).kind == STURM_GATE_X);
    assert(sc.ctx->ir.at(1).kind == STURM_GATE_CX);
    assert(sc.ctx->ir.at(2).kind == STURM_GATE_CCX);
    assert(sc.ctx->gate_count == 3u);
    std::printf("  sequential primitives APPEND: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M11 primitives_v3 APPEND tests:\n");
    test_primitive_X_append();
    test_primitive_XOR_append();
    test_primitive_AND_append();
    test_primitive_phase_append();
    test_primitive_phi_add_append();
    test_sequential_append();
    std::printf("All M11 primitives_v3 APPEND tests passed.\n");
    return 0;
}
