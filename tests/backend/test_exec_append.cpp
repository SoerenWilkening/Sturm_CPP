// test_exec_append.cpp — M8: APPEND executor tests.
// TDD: written before implementation, drives exec_append.hpp.
//
// Tests:
//   1. exec_append pushes a GateRecord onto ctx.ir (single record).
//   2. Gate order preserved: multiple sequential appends appear in insertion order.
//   3. param preserved bit-exact for rotation gates (Rx, Ry, Rz, P, CRx, CRy, CRz).
//   4. Arity respected: extra qubit slots are zeroed for gates with arity < 3.
//   5. exec_append does NOT increment the gate counter (dispatcher's job).

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cmath>
#include <cstdint>

// ── Helpers ───────────────────────────────────────────────────────────────────

static sturm_backend_context_t* make_append_ctx() {
    return sturm_backend_create(STURM_MODE_APPEND);
}

// ── Test 1: single push lands in ctx.ir ──────────────────────────────────────

static void test_single_push() {
    sturm_backend_context_t* ctx = make_append_ctx();
    assert(ctx != nullptr);

    uint32_t q[1] = {5u};
    sturm::exec_append(*ctx, STURM_GATE_X, q, 1u, 0.0);

    assert(ctx->ir.size() == 1u);
    const sturm::GateRecord& r = ctx->ir.at(0);
    assert(r.kind      == STURM_GATE_X);
    assert(r.qubits[0] == 5u);
    assert(r.n         == 1u);
    assert(r.param     == 0.0);

    sturm_backend_destroy(ctx);
}

// ── Test 2: gate order preserved ─────────────────────────────────────────────

static void test_gate_order_preserved() {
    sturm_backend_context_t* ctx = make_append_ctx();
    assert(ctx != nullptr);

    // Push four distinct gates in a known order.
    struct Step { sturm_gate_kind_t kind; uint32_t q0; uint8_t n; double param; };
    const Step steps[] = {
        { STURM_GATE_H,   0u, 1u, 0.0 },
        { STURM_GATE_CX,  0u, 2u, 0.0 },  // q[0]=ctrl, q[1]=tgt — we'll encode differently
        { STURM_GATE_RZ,  3u, 1u, 0.7853981633974483 },
        { STURM_GATE_CCX, 1u, 3u, 1u },   // n=3 but we set via n field below
    };

    uint32_t q2[2] = {0u, 1u};
    uint32_t q3[3] = {0u, 1u, 2u};

    // H q0
    {
        uint32_t q[1] = {0u};
        sturm::exec_append(*ctx, STURM_GATE_H, q, 1u, 0.0);
    }
    // CX q0, q1
    sturm::exec_append(*ctx, STURM_GATE_CX, q2, 2u, 0.0);
    // Rz(pi/4) q3
    {
        uint32_t q[1] = {3u};
        sturm::exec_append(*ctx, STURM_GATE_RZ, q, 1u, 0.7853981633974483);
    }
    // CCX q0,q1,q2
    sturm::exec_append(*ctx, STURM_GATE_CCX, q3, 3u, 0.0);

    assert(ctx->ir.size() == 4u);

    assert(ctx->ir.at(0).kind == STURM_GATE_H);
    assert(ctx->ir.at(1).kind == STURM_GATE_CX);
    assert(ctx->ir.at(2).kind == STURM_GATE_RZ);
    assert(ctx->ir.at(3).kind == STURM_GATE_CCX);

    // Spot-check qubit indices for ordering sanity.
    assert(ctx->ir.at(0).qubits[0] == 0u);
    assert(ctx->ir.at(1).qubits[0] == 0u);
    assert(ctx->ir.at(1).qubits[1] == 1u);
    assert(ctx->ir.at(2).qubits[0] == 3u);

    sturm_backend_destroy(ctx);
}

// ── Test 3: param preserved bit-exact for all rotation gates ─────────────────
//
// For each parametric gate kind, push a record with a specific double value
// and verify it comes back with the same bit pattern (no arithmetic, no loss).

static void test_param_bit_exact() {
    // Pairs of (gate_kind, arity) for every parametric gate in the set.
    struct ParamGate { sturm_gate_kind_t kind; uint8_t arity; };
    const ParamGate pg[] = {
        { STURM_GATE_P,   1u },
        { STURM_GATE_RX,  1u },
        { STURM_GATE_RY,  1u },
        { STURM_GATE_RZ,  1u },
        { STURM_GATE_CRX, 2u },
        { STURM_GATE_CRY, 2u },
        { STURM_GATE_CRZ, 2u },
    };

    // A set of "interesting" double values including irrational-ish constants.
    const double angles[] = {
        0.0,
        1.5707963267948966,   // pi/2
        3.141592653589793,    // pi
        0.39269908169872414,  // pi/8
        -2.356194490192345,   // -3pi/4
        1.0e-15,
        1.0e15,
    };

    uint32_t q2[2] = {0u, 1u};

    for (const auto& g : pg) {
        for (double angle : angles) {
            sturm_backend_context_t* ctx = make_append_ctx();
            assert(ctx != nullptr);

            const uint32_t* qp = (g.arity == 1) ? &q2[0] : q2;
            sturm::exec_append(*ctx, g.kind, qp, g.arity, angle);

            assert(ctx->ir.size() == 1u);
            // Use memcmp via union trick — we need bit-exact equality.
            double stored = ctx->ir.at(0).param;
            // The most reliable bit-exact check: reinterpret as uint64_t.
            uint64_t stored_bits, angle_bits;
            __builtin_memcpy(&stored_bits, &stored, 8);
            __builtin_memcpy(&angle_bits,  &angle,  8);
            assert(stored_bits == angle_bits &&
                   "param must be stored bit-exact, no transformation");

            sturm_backend_destroy(ctx);
        }
    }
}

// ── Test 4: arity respected — extra slots zeroed ─────────────────────────────
//
// For arity-1 gates, qubits[1] and qubits[2] must be 0.
// For arity-2 gates, qubits[2] must be 0.
// For arity-3 gates, all three slots are significant — no zeroing needed, but
// we verify n is stored correctly.

static void test_arity_extra_slots_zeroed() {
    sturm_backend_context_t* ctx = make_append_ctx();
    assert(ctx != nullptr);

    // Arity 1: X on qubit 7.
    {
        uint32_t q[1] = {7u};
        sturm::exec_append(*ctx, STURM_GATE_X, q, 1u, 0.0);
        const auto& r = ctx->ir.at(0);
        assert(r.n         == 1u);
        assert(r.qubits[0] == 7u);
        assert(r.qubits[1] == 0u && "arity-1: slot[1] must be zeroed");
        assert(r.qubits[2] == 0u && "arity-1: slot[2] must be zeroed");
    }

    // Arity 2: CX on qubits 3, 9.
    {
        uint32_t q[2] = {3u, 9u};
        sturm::exec_append(*ctx, STURM_GATE_CX, q, 2u, 0.0);
        const auto& r = ctx->ir.at(1);
        assert(r.n         == 2u);
        assert(r.qubits[0] == 3u);
        assert(r.qubits[1] == 9u);
        assert(r.qubits[2] == 0u && "arity-2: slot[2] must be zeroed");
    }

    // Arity 3: CCX on qubits 1, 4, 11.
    {
        uint32_t q[3] = {1u, 4u, 11u};
        sturm::exec_append(*ctx, STURM_GATE_CCX, q, 3u, 0.0);
        const auto& r = ctx->ir.at(2);
        assert(r.n         == 3u);
        assert(r.qubits[0] == 1u);
        assert(r.qubits[1] == 4u);
        assert(r.qubits[2] == 11u);
    }

    sturm_backend_destroy(ctx);
}

// ── Test 5: exec_append does NOT increment the gate counter ──────────────────
//
// Gate counting is the dispatcher's job (M13); the executor itself must leave
// ctx.gate_count unchanged — same contract as exec_count.

static void test_no_counter_increment() {
    sturm_backend_context_t* ctx = make_append_ctx();
    assert(ctx != nullptr);

    uint64_t before = sturm_gate_count(ctx);

    uint32_t q[2] = {0u, 1u};
    sturm::exec_append(*ctx, STURM_GATE_H,  q, 1u, 0.0);
    sturm::exec_append(*ctx, STURM_GATE_CX, q, 2u, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before && "exec_append must not touch the gate counter");

    // But the IR buffer should have grown.
    assert(ctx->ir.size() == 2u);

    sturm_backend_destroy(ctx);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_single_push();
    test_gate_order_preserved();
    test_param_bit_exact();
    test_arity_extra_slots_zeroed();
    test_no_counter_increment();
    return 0;
}
