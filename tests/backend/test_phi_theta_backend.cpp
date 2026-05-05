// test_phi_theta_backend.cpp — sturm-b1g: PhiProxy/ThetaProxy emit gates via backend.
//
// Tests (backend path only; frontend/sink fallback is covered by test_phase_amp.cpp):
//
//   1. phi += delta with COUNT_ONLY backend context → gate_count increments by 1
//      per allocated qubit (emit_RZ_lifted path).
//   2. theta += delta with COUNT_ONLY backend context → gate_count increments by 1
//      per allocated qubit (emit_RY_lifted path).
//   3. phi += delta with APPEND context, depth=0 → RZ gate emitted in IR.
//   4. theta += delta with APPEND context, depth=0 → RY gate emitted in IR.
//   5. phi += delta with APPEND context, depth=1 → CRZ gate emitted in IR.
//   6. theta += delta with APPEND context, depth=1 → CRY gate emitted in IR.
//   7. phi -= delta emits a gate (negated delta still routes via backend).
//   8. theta -= delta emits a gate (negated delta still routes via backend).
//   9. qbool(true) with no allocated qubit: phi += auto-promotes, emits X + RZ (2 gates).
//  10. qint with multiple allocated qubits: phi += delta emits one RZ per qubit.
//  11. RZ gate param equals the delta passed to phi += (round-trip check).
//  12. RY gate param equals the delta passed to theta += (round-trip check).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdio>
#include <cmath>

// sturm-1os7 (D0 re-spell miss): bare `qint` here used to alias
// sturm::qint_t<64> (pre-B1). Post-B1 (sturm-65rs.6) sturm::qint resolves
// to sturm::frontend::qint, which lacks the backend surface this file
// exercises (super_mask, qubits[], phi(), theta()). PRD §6 R1 mechanical
// migration recipe: introduce a local type-alias `using qint =
// sturm::qint_t<64>;` so every site below keeps its backend semantics
// with a one-line diff. Drift back to bare sturm::qint is pinned by
// tests/regressions/test_qint_callsite_respelling_*.cpp (per-surface split).
using qint = sturm::qint_t<64>;
using sturm::QubitPool;
using sturm::BackendContext;
using sturm::GateIR;
using sturm::GateRecord;

static constexpr double kDelta = M_PI / 4.0;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a 1-bit qint with one superposed qubit allocated at position 0.
// The QubitPool must be reset before calling this.
static qint make_super_bit() {
    qint q(0);
    q.super_mask = 0x1u;
    q.qubits[0]  = QubitPool::instance().allocate();
    return q;
}

// Scoped COUNT_ONLY backend context.
struct ScopedCountCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedCountCtx() {
        ctx  = sturm_backend_create(STURM_MODE_COUNT_ONLY, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCountCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    uint64_t gate_count() const { return ctx->gate_count; }
};

// Scoped APPEND backend context.
struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    const GateIR& ir() const { return ctx->ir; }
    BackendContext& bc()     { return *ctx; }
};

// ── Test 1: phi += with COUNT_ONLY backend → gate_count += 1 (RZ path) ───────

static void test_phi_add_backend_count() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint64_t before = sc.gate_count();
    q.phi() += kDelta;
    uint64_t after = sc.gate_count();

    // One qubit allocated: expect exactly 1 gate (RZ via emit_RZ_lifted).
    assert(after - before == 1u &&
           "phi += with backend context must emit 1 RZ gate (COUNT_ONLY)");
    std::printf("PASS test_phi_add_backend_count\n");
}

// ── Test 2: theta += with COUNT_ONLY backend → gate_count += 1 (RY path) ─────

static void test_theta_add_backend_count() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint64_t before = sc.gate_count();
    q.theta() += kDelta;
    uint64_t after = sc.gate_count();

    // One qubit allocated: expect exactly 1 gate (RY via emit_RY_lifted).
    assert(after - before == 1u &&
           "theta += with backend context must emit 1 RY gate (COUNT_ONLY)");
    std::printf("PASS test_theta_add_backend_count\n");
}

// ── Test 3: phi += with APPEND context, depth=0 → RZ gate in IR ──────────────

static void test_phi_add_backend_emits_RZ() {
    ScopedAppendCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint32_t expected_qubit = static_cast<uint32_t>(q.qubits[0]);

    size_t before = sc.ir().size();
    q.phi() += kDelta;
    size_t after = sc.ir().size();

    assert(after - before == 1u && "phi += backend depth=0 must emit 1 gate");
    const auto& rec = sc.ir().at(before);
    assert(rec.kind == STURM_GATE_RZ && "phi += backend must emit RZ");
    assert(rec.n == 1u && "RZ is 1-qubit");
    assert(rec.qubits[0] == expected_qubit && "RZ targets the proxy's qubit");
    std::printf("PASS test_phi_add_backend_emits_RZ\n");
}

// ── Test 4: theta += with APPEND context, depth=0 → RY gate in IR ────────────

static void test_theta_add_backend_emits_RY() {
    ScopedAppendCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint32_t expected_qubit = static_cast<uint32_t>(q.qubits[0]);

    size_t before = sc.ir().size();
    q.theta() += kDelta;
    size_t after = sc.ir().size();

    assert(after - before == 1u && "theta += backend depth=0 must emit 1 gate");
    const auto& rec = sc.ir().at(before);
    assert(rec.kind == STURM_GATE_RY && "theta += backend must emit RY");
    assert(rec.n == 1u && "RY is 1-qubit");
    assert(rec.qubits[0] == expected_qubit && "RY targets the proxy's qubit");
    std::printf("PASS test_theta_add_backend_emits_RY\n");
}

// ── Test 5: phi += with APPEND context, depth=1 → CRZ gate in IR ─────────────

static void test_phi_add_backend_depth1_emits_CRZ() {
    ScopedAppendCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint32_t target_qubit = static_cast<uint32_t>(q.qubits[0]);
    uint32_t ctrl_qubit   = 7u;  // arbitrary ctrl qubit (not allocated in pool)

    sc.bc().control_stack.push_control(ctrl_qubit);

    size_t before = sc.ir().size();
    q.phi() += kDelta;
    size_t after = sc.ir().size();

    sc.bc().control_stack.pop_control();

    assert(after - before == 1u && "phi += backend depth=1 must emit 1 gate");
    const auto& rec = sc.ir().at(before);
    assert(rec.kind == STURM_GATE_CRZ && "phi += backend depth=1 must emit CRZ");
    assert(rec.n == 2u && "CRZ is 2-qubit");
    assert(rec.qubits[0] == ctrl_qubit   && "CRZ ctrl must be control_stack top");
    assert(rec.qubits[1] == target_qubit && "CRZ target must be proxy qubit");
    std::printf("PASS test_phi_add_backend_depth1_emits_CRZ\n");
}

// ── Test 6: theta += with APPEND context, depth=1 → CRY gate in IR ───────────

static void test_theta_add_backend_depth1_emits_CRY() {
    ScopedAppendCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint32_t target_qubit = static_cast<uint32_t>(q.qubits[0]);
    uint32_t ctrl_qubit   = 8u;  // arbitrary

    sc.bc().control_stack.push_control(ctrl_qubit);

    size_t before = sc.ir().size();
    q.theta() += kDelta;
    size_t after = sc.ir().size();

    sc.bc().control_stack.pop_control();

    assert(after - before == 1u && "theta += backend depth=1 must emit 1 gate");
    const auto& rec = sc.ir().at(before);
    assert(rec.kind == STURM_GATE_CRY && "theta += backend depth=1 must emit CRY");
    assert(rec.n == 2u && "CRY is 2-qubit");
    assert(rec.qubits[0] == ctrl_qubit   && "CRY ctrl must be control_stack top");
    assert(rec.qubits[1] == target_qubit && "CRY target must be proxy qubit");
    std::printf("PASS test_theta_add_backend_depth1_emits_CRY\n");
}

// ── Test 7: phi -= still emits a gate (negated delta routes via backend) ───────

static void test_phi_sub_backend_emits_gate() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint64_t before = sc.gate_count();
    q.phi() -= kDelta;
    uint64_t after = sc.gate_count();

    assert(after - before == 1u && "phi -= backend must still emit 1 gate");
    std::printf("PASS test_phi_sub_backend_emits_gate\n");
}

// ── Test 8: theta -= still emits a gate (negated delta routes via backend) ─────

static void test_theta_sub_backend_emits_gate() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    uint64_t before = sc.gate_count();
    q.theta() -= kDelta;
    uint64_t after = sc.gate_count();

    assert(after - before == 1u && "theta -= backend must still emit 1 gate");
    std::printf("PASS test_theta_sub_backend_emits_gate\n");
}

// ── Test 9: classical qbool auto-promotes on phi += (backend path) ───────────
// M17: Use qbool (qint_t<1> subclass) to verify auto-promotion + gate emission
// on the backend path.  A classical qbool(true) has value=1, super_mask=0,
// qubits[0]==-1.  phi() += must allocate the qubit, set super_mask, emit 1 X
// gate (because bit 0 is 1), and emit 1 RZ gate — total 2 gates.

static void test_phi_add_no_qubits_no_emission() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();

    // Classical qbool(true): value=1, super_mask=0, qubits[0]==-1.
    sturm::qbool c(true);
    assert(c.super_mask == 0 && "pre-condition: classical qbool has super_mask==0");
    assert(c.qubits[0] < 0   && "pre-condition: classical qbool has no qubit");

    uint64_t before = sc.gate_count();
    c.phi() += kDelta;
    uint64_t after = sc.gate_count();

    // After auto-promotion: 1 X gate (for classical-1 bit) + 1 RZ gate = 2 gates.
    assert(after > before && "phi += backend, classical qbool: must auto-promote and emit gates");
    assert((c.super_mask & 1) != 0 && "phi += must set super_mask bit 0 after auto-promotion");
    assert(c.qubits[0] >= 0        && "phi += must allocate qubit after auto-promotion");
    assert(after - before == 2u && "phi += on qbool(true): 1 X gate + 1 RZ gate = 2 gates");
    std::printf("PASS test_phi_add_no_qubits_no_emission\n");
}

// ── Test 10: classical qint auto-promotes on theta += (backend path) ─────────
// M16: ThetaProxy::operator+= must allocate qubits and set super_mask for
// unallocated (classical) bits before emitting rotation gates.
// A classical qint(0) with no qubits allocated should auto-promote all 64 bits
// and emit 64 RY gates (one per bit).

static void test_theta_add_no_qubits_no_emission() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();
    // Classical qint with value=0 — all qubits == -1, no bits set → no X gates
    qint q(0);
    assert(q.super_mask == 0 && "pre-condition: classical qint has super_mask==0");
    uint64_t before = sc.gate_count();
    q.theta() += kDelta;
    uint64_t after = sc.gate_count();

    // After auto-promotion: 64 bits promoted → 64 RY gates emitted
    assert(after > before && "theta += backend, classical qint: must auto-promote and emit gates");
    assert(q.super_mask != 0 && "theta += must set super_mask after auto-promotion");
    assert(after - before == 64u && "theta += promotes all 64 bits of qint_t<64>, emits 64 RY gates");
    std::printf("PASS test_theta_add_no_qubits_no_emission\n");
}

// ── Test 11 (old 10): multiple allocated qubits → one RZ per qubit ───────────

static void test_phi_add_multi_qubits_backend() {
    ScopedCountCtx sc;
    QubitPool::instance().reset_for_testing();

    // Manually allocate two qubits at positions 0 and 2.
    qint q(0b101);
    q.super_mask = 0x5u;
    q.qubits[0]  = QubitPool::instance().allocate();
    q.qubits[2]  = QubitPool::instance().allocate();

    uint64_t before = sc.gate_count();
    q.phi() += kDelta;
    uint64_t after = sc.gate_count();

    // Positions 0 and 2 are allocated; positions 1, 3, … are -1.
    assert(after - before == 2u &&
           "phi += backend, 2 allocated qubits: must emit 2 RZ gates");
    std::printf("PASS test_phi_add_multi_qubits_backend\n");
}

// ── Test 12 (old 11): RZ gate param equals delta (round-trip check) ──────────

static void test_phi_add_backend_param_correct() {
    ScopedAppendCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    const double delta = 1.23456789;

    size_t before = sc.ir().size();
    q.phi() += delta;
    const auto& rec = sc.ir().at(before);
    assert(std::abs(rec.param - delta) < 1e-12 &&
           "RZ gate param must equal delta passed to phi +=");
    std::printf("PASS test_phi_add_backend_param_correct\n");
}

// ── Test 13 (old 12): RY gate param equals delta (round-trip check) ──────────

static void test_theta_add_backend_param_correct() {
    ScopedAppendCtx sc;
    QubitPool::instance().reset_for_testing();

    qint q = make_super_bit();
    const double delta = 0.98765432;

    size_t before = sc.ir().size();
    q.theta() += delta;
    const auto& rec = sc.ir().at(before);
    assert(std::abs(rec.param - delta) < 1e-12 &&
           "RY gate param must equal delta passed to theta +=");
    std::printf("PASS test_theta_add_backend_param_correct\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("sturm-b1g: PhiProxy/ThetaProxy backend gate emission tests:\n");

    test_phi_add_backend_count();
    test_theta_add_backend_count();
    test_phi_add_backend_emits_RZ();
    test_theta_add_backend_emits_RY();
    test_phi_add_backend_depth1_emits_CRZ();
    test_theta_add_backend_depth1_emits_CRY();
    test_phi_sub_backend_emits_gate();
    test_theta_sub_backend_emits_gate();
    test_phi_add_no_qubits_no_emission();
    test_theta_add_no_qubits_no_emission();
    test_phi_add_multi_qubits_backend();
    test_phi_add_backend_param_correct();
    test_theta_add_backend_param_correct();

    std::printf("All sturm-b1g phi/theta backend gate emission tests passed.\n");
    return 0;
}
