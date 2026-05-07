// test_qbool_ops.cpp — M13: qbool forward-operator tests.
//
// Phase K PK-8 (sturm-ignd) retired the runtime lazy-fusion and
// destructor-uncompute gate-count pins that previously lived here. Under the
// new contract (principles B1b, B9, B10 — 2026-04-17), the only gate streams
// the runtime must produce are the direct forward emissions; fusion of the
// `c ^= (a & b)` / `c ^= (a | b)` patterns and the destructor-emitted inverses
// for `qbool r = (a & b)` / `qbool r = ~a` are compile-time concerns owned by
// the transpiler (PJ-1 ccnot_inplace + explicit uncompute_* calls). The
// gate-equivalence regression pairs in tests/transpiler pin that contract.
//
// Surviving tests:
//   test_qbool_xor_assign      — c ^= a emits 1 CX
//   test_qbool_xor_assign_ir   — c ^= a appends 1 CX record
//   test_qbool_flip            — a.flip() emits 1 X
//   test_qbool_flip_ir         — a.flip() appends 1 X record
//   test_qbool_under_when      — c ^= a under 1 control emits 1 CCX (lifted CX)
//   test_qbool_under_when_ir   — same, verify the CCX record
//   test_qbool_non_owning      — non-owning qbool destruction does NOT release
//   Mixed quantum/classical fast-path pass-through tests for operator& / |.
//
// Retired in PK-8 (gate-count pins / destructor-emit pins):
//   test_qbool_and_expr_xor, test_qbool_and_expr_xor_ir
//   test_qbool_or_expr_xor,  test_qbool_or_expr_xor_ir
//   test_qbool_and_materialize
//   test_qbool_not
//   test_qbool_under_when_and
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

#include "sturm/control/when.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── Scoped context helper ─────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 32u) {
        ctx  = sturm_backend_create(mode);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::BackendContext& bc() { return *ctx; }
};

// ── Helper: make a non-owning qbool with a specific qubit index ───────────────
//
// Uses qbool::make_non_owning() factory defined in qbool_ops.hpp / qbool.hpp.

static sturm::qbool make_qubit(int idx) {
    return sturm::qbool::make_non_owning(idx);
}

// ── test_qbool_xor_assign: c ^= a emits 1 CX ─────────────────────────────────

static void test_qbool_xor_assign() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);
    sturm::qbool c = make_qubit(1);

    c ^= a;

    assert(sc.ctx->gate_count == 1u);
    std::printf("  test_qbool_xor_assign: PASS\n");
}

// ── test_qbool_xor_assign_ir: c ^= a in APPEND mode emits CX record ──────────

static void test_qbool_xor_assign_ir() {
    ScopedCtx sc{STURM_MODE_APPEND};
    sturm::qbool a = make_qubit(0);
    sturm::qbool c = make_qubit(1);

    c ^= a;

    assert(sc.ctx->ir.size() == 1u);
    const auto& rec = sc.ctx->ir.at(0);
    assert(rec.kind == STURM_GATE_CX);
    assert(rec.qubits[0] == 0u);
    assert(rec.qubits[1] == 1u);
    std::printf("  test_qbool_xor_assign_ir: PASS\n");
}

// ── Phase K PK-8 (sturm-ignd): lazy-fusion / destructor-uncompute pins ───────
// The former tests that pinned `c ^= (a & b)` to 1 CCX,
// `c ^= (a | b)` to 3 gates, and `qbool r = (a & b)` /
// `qbool r = ~a` to a 2-gate materialize+destructor count were retired here.
// Those contracts now live exclusively in the transpile path
// (tests/transpiler/test_gate_equivalence.cpp, fixtures fuse_xor_and /
// or_single / ...) because principle B10 disallows destructor-emitted gates
// and principle B1b disallows runtime auto-inversion (docs/01_principles.md).

// ── test_qbool_flip: a.flip() emits 1 X ──────────────────────────────────────

static void test_qbool_flip() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);

    a.flip();

    assert(sc.ctx->gate_count == 1u);
    std::printf("  test_qbool_flip: PASS\n");
}

// ── test_qbool_flip_ir: X record in APPEND mode ───────────────────────────────

static void test_qbool_flip_ir() {
    ScopedCtx sc{STURM_MODE_APPEND};
    sturm::qbool a = make_qubit(0);

    a.flip();

    assert(sc.ctx->ir.size() == 1u);
    assert(sc.ctx->ir.at(0).kind == STURM_GATE_X);
    assert(sc.ctx->ir.at(0).qubits[0] == 0u);
    std::printf("  test_qbool_flip_ir: PASS\n");
}

// ── test_qbool_under_when: c ^= a under 1 control emits 1 CCX ────────────────

static void test_qbool_under_when() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    // qubit 0 = ctrl, qubit 1 = a, qubit 2 = c
    sturm::qbool ctrl = make_qubit(0);
    sturm::qbool a    = make_qubit(1);
    sturm::qbool c    = make_qubit(2);

    // Push a control qubit onto the control stack to simulate WHEN(ctrl)
    sc.bc().control_stack.push_control(0u);

    c ^= a;  // under 1 control: CX is lifted to CCX

    sc.bc().control_stack.pop_control();

    assert(sc.ctx->gate_count == 1u);

    // Check in APPEND mode that CCX was emitted
    std::printf("  test_qbool_under_when (count): PASS\n");
}

// ── test_qbool_under_when_ir: verify CCX emitted in APPEND mode ──────────────

static void test_qbool_under_when_ir() {
    ScopedCtx sc{STURM_MODE_APPEND};

    sturm::qbool a = make_qubit(1);
    sturm::qbool c = make_qubit(2);

    sc.bc().control_stack.push_control(0u);
    c ^= a;
    sc.bc().control_stack.pop_control();

    assert(sc.ctx->ir.size() == 1u);
    assert(sc.ctx->ir.at(0).kind == STURM_GATE_CCX);
    // Control is [0], a is [1], c is [2]
    assert(sc.ctx->ir.at(0).qubits[0] == 0u);
    assert(sc.ctx->ir.at(0).qubits[1] == 1u);
    assert(sc.ctx->ir.at(0).qubits[2] == 2u);

    std::printf("  test_qbool_under_when_ir: PASS\n");
}

// ── Phase K PK-3 (sturm-pzye): M8 uncompute_op tag inspection tests retired ──
// The uncompute_op tagged union was retired in Phase K PK-3. Forward operators
// no longer stamp a per-object inverse descriptor — inverses are emitted by
// transpiler-synthesised uncompute_* free functions (uncompute_api.hpp).
// The former operator_not_stamps_ADD_CONST_uncompute,
// and_expr_stamps_BITWISE_SELF_uncompute, or_expr_stamps_BITWISE_SELF_uncompute,
// no_QboolUncompute_enum_exists checks no longer have referents.

// ── test_qbool_non_owning: non-owning qbool does NOT release qubit ────────────

static void test_qbool_non_owning() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    // Allocate a qubit from the global pool
    int idx = sturm::QubitPool::instance().allocate();
    int initial_in_use = sturm::QubitPool::instance().in_use();

    {
        // Create a non-owning qbool — destructor must NOT call release()
        sturm::qbool q = sturm::qbool::make_non_owning(idx);
        assert(q.qubits[0] == idx);
        assert(!q.owning_);
    }
    // After q is destroyed, the qubit should still be "in use"
    assert(sturm::QubitPool::instance().in_use() == initial_in_use);

    // Cleanup
    sturm::QubitPool::instance().release(idx);

    std::printf("  test_qbool_non_owning: PASS\n");
}

// ── Mixed quantum/classical qbool materialization tests ──────────────────────

// Helper: make a quantum qbool (has qubit, super_mask=1, given value).
static sturm::qbool make_quantum_qbool(int idx, bool val) {
    auto q = sturm::qbool::make_non_owning(idx, val ? 1 : 0, 1ULL);
    return q;
}

// (quantum & classical_true) preserves super_mask and qubit.
static void test_qbool_and_mixed_quantum_classical_true() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_quantum_qbool(0, true);  // quantum, value=1
    sturm::qbool b(true);                           // classical true, no qubit

    sturm::qbool r = (a & b);

    // true & quantum = quantum pass-through
    assert(r.qubits[0] == 0);
    assert(r.super_mask == 1ULL);
    assert((r.value & 1) == 1);
    assert(!r.owning_);
    assert(sc.ctx->gate_count == 0u);  // no gates needed for classical fold

    std::printf("  test_qbool_and_mixed_quantum_classical_true: PASS\n");
}

// (classical_false & quantum) returns classical false.
static void test_qbool_and_mixed_classical_false_quantum() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a(false);                          // classical false, no qubit
    sturm::qbool b = make_quantum_qbool(1, true);   // quantum, value=1

    sturm::qbool r = (a & b);

    // false & anything = false
    assert(r.qubits[0] == -1);
    assert(r.super_mask == 0ULL);
    assert((r.value & 1) == 0);
    assert(sc.ctx->gate_count == 0u);

    std::printf("  test_qbool_and_mixed_classical_false_quantum: PASS\n");
}

// (quantum | classical_false) preserves super_mask and qubit.
static void test_qbool_or_mixed_quantum_classical_false() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_quantum_qbool(0, true);  // quantum, value=1
    sturm::qbool b(false);                          // classical false, no qubit

    sturm::qbool r = (a | b);

    // quantum | false = quantum pass-through
    assert(r.qubits[0] == 0);
    assert(r.super_mask == 1ULL);
    assert((r.value & 1) == 1);
    assert(!r.owning_);
    assert(sc.ctx->gate_count == 0u);

    std::printf("  test_qbool_or_mixed_quantum_classical_false: PASS\n");
}

// (classical_true | quantum) returns classical true.
static void test_qbool_or_mixed_classical_true_quantum() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a(true);                           // classical true, no qubit
    sturm::qbool b = make_quantum_qbool(1, false);  // quantum, value=0

    sturm::qbool r = (a | b);

    // true | anything = true
    assert(r.qubits[0] == -1);
    assert(r.super_mask == 0ULL);
    assert((r.value & 1) == 1);
    assert(sc.ctx->gate_count == 0u);

    std::printf("  test_qbool_or_mixed_classical_true_quantum: PASS\n");
}

// WHEN(quantum & classical_true) enters body and emits controlled gates.
static void test_qbool_when_mixed_and() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool c = make_quantum_qbool(0, true);  // quantum, value=1, super_mask=1
    sturm::qbool d(true);                           // classical true, no qubit
    sturm::qbool target = make_qubit(1);

    bool body_ran = false;
    WHEN(c & d) {
        body_ran = true;
        target.flip();  // should emit controlled X (lifted to CX under 1 control)
    }

    assert(body_ran);
    assert(sc.ctx->gate_count == 1u);  // CX (X lifted under 1 control)

    std::printf("  test_qbool_when_mixed_and: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M13 qbool forward-operator tests:\n");
    test_qbool_xor_assign();
    test_qbool_xor_assign_ir();
    test_qbool_flip();
    test_qbool_flip_ir();
    test_qbool_under_when();
    test_qbool_under_when_ir();
    test_qbool_non_owning();
    // Mixed quantum/classical classical-fold fast paths (operator& / | / WHEN)
    test_qbool_and_mixed_quantum_classical_true();
    test_qbool_and_mixed_classical_false_quantum();
    test_qbool_or_mixed_quantum_classical_false();
    test_qbool_or_mixed_classical_true_quantum();
    test_qbool_when_mixed_and();
    // Phase K PK-3: M8 uncompute_op tag inspection tests retired.
    // Phase K PK-8: lazy-fusion and destructor-uncompute gate-count pins
    //               retired (see file header). The transpile-path pairs in
    //               tests/transpiler/test_gate_equivalence.cpp pin the new
    //               contract.
    std::printf("All M13 qbool_ops tests passed.\n");
    return 0;
}
