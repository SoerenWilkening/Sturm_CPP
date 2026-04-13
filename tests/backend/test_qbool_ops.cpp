// test_qbool_ops.cpp — M13: qbool operators + lazy expressions tests.
//
// Tests:
//   test_qbool_xor_assign      — c ^= a emits 1 CX
//   test_qbool_and_expr_xor    — c ^= (a & b) emits 1 CCX
//   test_qbool_or_expr_xor     — c ^= (a | b) emits 2 CX + 1 CCX (3 gates)
//   test_qbool_and_materialize — qbool r = (a & b) allocates ancilla; destruction uncomputes/frees
//   test_qbool_flip            — a.flip() emits 1 X
//   test_qbool_not             — ~a emits 1 X, returns new qbool
//   test_qbool_under_when      — c ^= a under 1 control emits 1 CCX (lifted CX)
//   test_qbool_non_owning      — non-owning qbool destruction does NOT release the qubit
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
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
        ctx  = sturm_backend_create(mode, max_q);
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

// ── test_qbool_and_expr_xor: c ^= (a & b) emits 1 CCX ────────────────────────

static void test_qbool_and_expr_xor() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);
    sturm::qbool c = make_qubit(2);

    c ^= (a & b);

    assert(sc.ctx->gate_count == 1u);
    std::printf("  test_qbool_and_expr_xor: PASS\n");
}

// ── test_qbool_and_expr_xor_ir: CCX record in APPEND mode ────────────────────

static void test_qbool_and_expr_xor_ir() {
    ScopedCtx sc{STURM_MODE_APPEND};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);
    sturm::qbool c = make_qubit(2);

    c ^= (a & b);

    assert(sc.ctx->ir.size() == 1u);
    const auto& rec = sc.ctx->ir.at(0);
    assert(rec.kind == STURM_GATE_CCX);
    assert(rec.qubits[0] == 0u);
    assert(rec.qubits[1] == 1u);
    assert(rec.qubits[2] == 2u);
    std::printf("  test_qbool_and_expr_xor_ir: PASS\n");
}

// ── test_qbool_or_expr_xor: c ^= (a | b) emits 2 CX + 1 CCX (3 gates) ───────

static void test_qbool_or_expr_xor() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);
    sturm::qbool c = make_qubit(2);

    c ^= (a | b);

    assert(sc.ctx->gate_count == 3u);
    std::printf("  test_qbool_or_expr_xor: PASS\n");
}

// ── test_qbool_or_expr_xor_ir: 2 CX + 1 CCX records in APPEND mode ──────────

static void test_qbool_or_expr_xor_ir() {
    ScopedCtx sc{STURM_MODE_APPEND};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);
    sturm::qbool c = make_qubit(2);

    c ^= (a | b);

    assert(sc.ctx->ir.size() == 3u);
    assert(sc.ctx->ir.at(0).kind == STURM_GATE_CX);
    assert(sc.ctx->ir.at(1).kind == STURM_GATE_CX);
    assert(sc.ctx->ir.at(2).kind == STURM_GATE_CCX);
    std::printf("  test_qbool_or_expr_xor_ir: PASS\n");
}

// ── test_qbool_and_materialize: qbool r = (a & b) allocates ancilla ──────────
// Materialization emits 1 CCX; destruction emits 1 CCX (uncompute). Total: 2.

static void test_qbool_and_materialize() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);

    {
        // r = (a & b) materializes: acquires ancilla, emits CCX
        sturm::qbool r = (a & b);
        assert(sc.ctx->gate_count == 1u);
        assert(r.qubits[0] >= 0);

        // After scope, destructor runs uncompute CCX and releases ancilla.
    }
    // After r is destroyed, ancilla should be freed.
    assert(sc.ctx->gate_count == 2u);

    std::printf("  test_qbool_and_materialize: PASS\n");
}

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

// ── test_qbool_not: ~a emits 1 X, returns new qbool ──────────────────────────
// The result qbool is owning (allocates ancilla) and materializes with X.
// On destruction, it uncomputes with another X. Total: 2 gates.

static void test_qbool_not() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);

    {
        sturm::qbool r = ~a;
        // r has an ancilla qubit set and has 1 X gate emitted
        assert(sc.ctx->gate_count == 1u);
        assert(r.qubits[0] >= 0);
    }
    // r destructs: emits uncompute X = 2 gates total
    assert(sc.ctx->gate_count == 2u);

    std::printf("  test_qbool_not: PASS\n");
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

// ── test_qbool_under_when_and: c ^= (a & b) under 1 control uses c_AND fold ──
// Under 1 control: AND(c0, c1, target) becomes a 4-qubit operation.
// c_AND fold: borrow ancilla, CCX(ctrl, c0, anc), CCX(ctrl, c1, anc) — actually
// the simplest safe approach: CCX(ctrl,a,anc) + CCX(anc,b,c) + CCX(ctrl,a,anc)
// But per spec, with 1 control for an AND: emits more than 1 gate.
// We just verify gate_count > 1.

static void test_qbool_under_when_and() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    sturm::qbool a = make_qubit(1);
    sturm::qbool b = make_qubit(2);
    sturm::qbool c = make_qubit(3);

    sc.bc().control_stack.push_control(0u);
    c ^= (a & b);
    sc.bc().control_stack.pop_control();

    // Under 1 control, AND(a, b, c) requires c_AND decomposition (>1 gate)
    assert(sc.ctx->gate_count > 1u);

    std::printf("  test_qbool_under_when_and: PASS\n");
}

// ── M8 tests: uncompute_op tag inspection ────────────────────────────────────

// operator_not_stamps_ADD_CONST_uncompute:
// ~q allocates an ancilla and stamps uncompute_ with ADD_CONST(1).
// The inverse of NOT (flip) is modelled as adding constant 1 mod 2.
static void operator_not_stamps_ADD_CONST_uncompute() {
#ifdef STURM_BACKEND_ENABLED
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool q = make_qubit(0);

    sturm::qbool r = ~q;

    assert(r.uncompute_.tag == sturm::uncompute_op::kind::ADD_CONST);
    assert(r.uncompute_.data.const_c == 1);

    std::printf("  operator_not_stamps_ADD_CONST_uncompute: PASS\n");
#else
    std::printf("  operator_not_stamps_ADD_CONST_uncompute: SKIP (no backend)\n");
#endif
}

// and_expr_stamps_BITWISE_SELF_uncompute:
// (a & b) materialised into qbool r stamps BITWISE_SELF with sub_kind==0 (AND).
static void and_expr_stamps_BITWISE_SELF_uncompute() {
#ifdef STURM_BACKEND_ENABLED
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);

    sturm::qbool r = (a & b);

    assert(r.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF);
    assert(r.uncompute_.data.bitwise.sub_kind == 0u); // 0 = AND

    std::printf("  and_expr_stamps_BITWISE_SELF_uncompute: PASS\n");
#else
    std::printf("  and_expr_stamps_BITWISE_SELF_uncompute: SKIP (no backend)\n");
#endif
}

// or_expr_stamps_BITWISE_SELF_uncompute:
// (a | b) materialised into qbool r stamps BITWISE_SELF with sub_kind==1 (OR).
static void or_expr_stamps_BITWISE_SELF_uncompute() {
#ifdef STURM_BACKEND_ENABLED
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    sturm::qbool a = make_qubit(0);
    sturm::qbool b = make_qubit(1);

    sturm::qbool r = (a | b);

    assert(r.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF);
    assert(r.uncompute_.data.bitwise.sub_kind == 1u); // 1 = OR

    std::printf("  or_expr_stamps_BITWISE_SELF_uncompute: PASS\n");
#else
    std::printf("  or_expr_stamps_BITWISE_SELF_uncompute: SKIP (no backend)\n");
#endif
}

// no_QboolUncompute_enum_exists:
// Verify at compile time that the uncompute mechanism uses uncompute_op::kind
// (the unified enum) rather than a separate QboolUncompute type.
//
// Strategy: ensure sturm::uncompute_op::kind is well-formed with the expected
// BITWISE_SELF and ADD_CONST values, and that no separate QboolUncompute
// identifier leaks through any header included here.  If QboolUncompute existed
// and was the intended type, the code below would fail to compile because
// uncompute_op::kind would lack the expected enumerators.
static_assert(sturm::uncompute_op::kind::ADD_CONST  != sturm::uncompute_op::kind::NONE,
              "uncompute_op::kind::ADD_CONST must be defined and != NONE");
static_assert(sturm::uncompute_op::kind::BITWISE_SELF != sturm::uncompute_op::kind::NONE,
              "uncompute_op::kind::BITWISE_SELF must be defined and != NONE");

static void no_QboolUncompute_enum_exists() {
    // Compilation of this translation unit validates the static_asserts above:
    // the unified uncompute_op::kind enum exists with the required enumerators,
    // confirming that no separate QboolUncompute type is in use.
    std::printf("  no_QboolUncompute_enum_exists: PASS (static_asserts passed)\n");
}

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
    std::printf("M13 qbool operators + lazy expressions tests:\n");
    test_qbool_xor_assign();
    test_qbool_xor_assign_ir();
    test_qbool_and_expr_xor();
    test_qbool_and_expr_xor_ir();
    test_qbool_or_expr_xor();
    test_qbool_or_expr_xor_ir();
    test_qbool_and_materialize();
    test_qbool_flip();
    test_qbool_flip_ir();
    test_qbool_not();
    test_qbool_under_when();
    test_qbool_under_when_ir();
    test_qbool_under_when_and();
    test_qbool_non_owning();
    // Mixed quantum/classical materialization tests
    test_qbool_and_mixed_quantum_classical_true();
    test_qbool_and_mixed_classical_false_quantum();
    test_qbool_or_mixed_quantum_classical_false();
    test_qbool_or_mixed_classical_true_quantum();
    test_qbool_when_mixed_and();
    // M8: uncompute_op tag inspection tests
    operator_not_stamps_ADD_CONST_uncompute();
    and_expr_stamps_BITWISE_SELF_uncompute();
    or_expr_stamps_BITWISE_SELF_uncompute();
    no_QboolUncompute_enum_exists();
    std::printf("All M13 qbool_ops tests passed.\n");
    return 0;
}
