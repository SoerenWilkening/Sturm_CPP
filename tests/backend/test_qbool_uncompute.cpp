// test_qbool_uncompute.cpp — M22/M19: qbool from comparison emits DSL circuit.
//
// Phase D retirement (2026-04-15): the COMPARE uncompute tag was removed once
// the transpiler emits `uncompute_{eq,ne,lt,le,gt,ge}_qint(r, a, b)` at scope
// exit. The qbool destructor no longer stamps or replays a compare circuit —
// so the tag-check in test_qbool_compare_uncompute was dropped and the test
// now only verifies DSL gate emission and Bennett-pristine source registers.
//
// Tests:
//   1. Constructing a qbool via operator== (a comparison) runs the DSL
//      comparison circuit (lib_eq_dsl) and emits gates into the IR.
//   2. The source qint_t inputs are byte-identical before and after the block
//      (Bennett discipline: inputs are pristine).
//
// Strategy: use APPEND mode context so gate emissions are recorded in GateIR.

#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

// ── Fixture ───────────────────────────────────────────────────────────────────

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
    sturm::GateIR& ir() { return ctx->ir; }
};

// ── Snapshot helpers ──────────────────────────────────────────────────────────

template <std::size_t W>
struct QintSnap {
    int64_t  value;
    uint64_t super_mask;
    std::array<int, W> qubits;
};

template <std::size_t W>
static QintSnap<W> snap(const sturm::qint_t<W>& q) {
    QintSnap<W> s;
    s.value      = q.value;
    s.super_mask = q.super_mask;
    s.qubits     = q.qubits;
    return s;
}

template <std::size_t W>
static bool snap_eq(const QintSnap<W>& x, const QintSnap<W>& y) {
    return x.value == y.value
        && x.super_mask == y.super_mask
        && x.qubits == y.qubits;
}

// Build a quantum qint_t<W> with deterministic (non-pooled) qubit indices.
template <std::size_t W>
static sturm::qint_t<W> make_quantum(int64_t val, int base_qubit) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (W < 64) ? ((1ULL << W) - 1u) : ~0ULL;
    for (std::size_t i = 0; i < W; ++i)
        q.qubits[i] = base_qubit + static_cast<int>(i);
    return q;
}

// Prevent double-release: qubit indices were never allocated through the pool.
template <std::size_t W>
static void clear_qubits(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0;
}

// ── Test: qbool from comparison — DSL circuit emission, Bennett pristine ────
//
// M19 wiring: operator== calls lib_eq_dsl which emits the DSL comparison circuit
// during construction of the qbool (forward gates emitted eagerly).
//
// Phase D retirement (2026-04-15): the qbool destructor no longer replays a
// COMPARE stub — the transpiler now emits `uncompute_eq_qint(r, a, b)` at the
// matching scope exit. This test therefore only verifies:
//   - IR grows (gates are emitted by the DSL comparison circuit).
//   - Source inputs are byte-identical before and after (Bennett discipline).

template <std::size_t W>
static void test_qbool_compare_uncompute() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(5LL, 0);
    auto b = make_quantum<W>(7LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    const std::size_t ir_before = sc.ir().size();

    {
        // operator== calls lib_eq_dsl and emits the forward comparison circuit.
        sturm::qbool t = (a == b);
        (void)t;

        // Scope exit: t destructor fires. Phase D: the qbool no longer
        // carries a COMPARE tag, so destruction does not emit extra gates;
        // uncomputation is the transpiler's responsibility.
    }

    // IR must have grown from the forward DSL circuit gates.
    const std::size_t ir_after = sc.ir().size();
    assert(ir_after > ir_before
           && "Expected IR growth: comparison DSL circuit gates must be emitted");

    // Bennett: source qints are pristine.
    assert(snap_eq<W>(snap<W>(a), a_before)
           && "qbool compare: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "qbool compare: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_qbool_compare_uncompute<W=%zu>: PASS\n", W);
}

// ── M10: BITWISE_SELF uncompute tests ────────────────────────────────────────
//
// These tests verify that the BITWISE_SELF uncompute path (implemented in M9)
// correctly emits the inverse gate sequence when a materialized qbool goes out
// of scope.  No reference to QboolUncompute appears here — all uncompute goes
// through uncompute_op::kind::BITWISE_SELF and ADD_CONST.
//
// Helper: make a non-owning qbool with a fixed qubit index (no pool allocation).
static sturm::qbool make_qbool_at(int idx) {
    return sturm::qbool::make_non_owning(idx);
}

// ── and_uncompute_emits_ccx ───────────────────────────────────────────────────
// Materialize qbool r = (a & b).  Forward: 1 CCX.
// On destruction, BITWISE_SELF(AND) emits 1 CCX(a_qubit, b_qubit, ancilla).
// Verify the final gate in the log is CCX with correct qubit ordering.

static void and_uncompute_emits_ccx() {
    ScopedAppendCtx sc;

    // Fixed non-pool qubits for a and b.
    sturm::qbool a = make_qbool_at(0);
    sturm::qbool b = make_qbool_at(1);

    // Both operands must have a qubit (non-owning already sets qubits[0]).
    // super_mask must be set so these look like quantum bools to the operators.
    a.super_mask = 1ULL;
    b.super_mask = 1ULL;

    {
        // Materialization: AndExpr<qbool>::operator qbool() emits CCX(0,1,anc).
        sturm::qbool r = (a & b);
        assert(r.qubits[0] >= 0 && "ancilla must have been allocated");

        // The ancilla qubit index is what will appear as the CCX target.
        const uint32_t expected_anc = static_cast<uint32_t>(r.qubits[0]);

        // Record current IR size (forward emission has already happened).
        const std::size_t ir_after_forward = sc.ir().size();
        assert(ir_after_forward >= 1u && "forward CCX must be in IR");

        // Destructor runs here: BITWISE_SELF(AND) apply() emits CCX(0,1,anc).
        // Verify after scope that exactly 1 more CCX was appended.
        const std::size_t ir_before_uncompute = ir_after_forward;

        // Store expected values before r destructs.
        const uint32_t anc_qubit = expected_anc;
        (void)ir_before_uncompute; (void)anc_qubit;

        // Scope exit — r destructor fires here.
    }

    // After destruction: one additional CCX (the uncompute) must be at the end.
    const std::size_t total_gates = sc.ir().size();
    assert(total_gates >= 2u && "forward + uncompute must produce at least 2 gates");

    // The last gate must be CCX.
    const sturm::GateRecord& last = sc.ir().at(total_gates - 1u);
    assert(last.kind == STURM_GATE_CCX
           && "and_uncompute: final gate must be CCX");

    // The uncompute CCX must target qubits 0 (a) and 1 (b) as controls.
    assert(last.qubits[0] == 0u && "and_uncompute: CCX ctrl0 must be a.qubits[0]");
    assert(last.qubits[1] == 1u && "and_uncompute: CCX ctrl1 must be b.qubits[0]");

    std::printf("  and_uncompute_emits_ccx: PASS\n");
}

// ── or_uncompute_emits_three_gates ───────────────────────────────────────────
// Materialize qbool r = (a | b).  Forward: CX(a,anc) + CX(b,anc) + CCX(a,b,anc).
// On destruction, BITWISE_SELF(OR) reverse: CCX(a,b,anc) + CX(b,anc) + CX(a,anc).
// Verify gate log ends with CCX, CX, CX in that order.

static void or_uncompute_emits_three_gates() {
    ScopedAppendCtx sc;

    sturm::qbool a = make_qbool_at(2);
    sturm::qbool b = make_qbool_at(3);
    a.super_mask = 1ULL;
    b.super_mask = 1ULL;

    std::size_t ir_before_uncompute;
    {
        // OrExpr<qbool>::operator qbool() emits CX+CX+CCX = 3 gates forward.
        sturm::qbool r = (a | b);
        assert(r.qubits[0] >= 0 && "ancilla must have been allocated");
        assert(sc.ir().size() == 3u && "forward emission must be 3 gates");

        ir_before_uncompute = sc.ir().size();
        // Scope exit — destructor fires BITWISE_SELF(OR) uncompute.
    }

    // Uncompute should have appended 3 more gates: CCX + CX + CX.
    const std::size_t total = sc.ir().size();
    assert(total == 6u && "forward 3 + uncompute 3 = 6 total gates");
    (void)ir_before_uncompute;

    // Verify uncompute sequence order: CCX then CX then CX.
    const sturm::GateRecord& uc0 = sc.ir().at(3u);
    const sturm::GateRecord& uc1 = sc.ir().at(4u);
    const sturm::GateRecord& uc2 = sc.ir().at(5u);

    assert(uc0.kind == STURM_GATE_CCX && "or_uncompute gate[3] must be CCX");
    assert(uc1.kind == STURM_GATE_CX  && "or_uncompute gate[4] must be CX");
    assert(uc2.kind == STURM_GATE_CX  && "or_uncompute gate[5] must be CX");

    // Verify qubit ordering: CCX(a=2, b=3, anc), CX(b=3,anc), CX(a=2,anc).
    assert(uc0.qubits[0] == 2u && "CCX ctrl0 must be a.qubits[0]=2");
    assert(uc0.qubits[1] == 3u && "CCX ctrl1 must be b.qubits[0]=3");
    assert(uc1.qubits[0] == 3u && "CX(b) ctrl must be b.qubits[0]=3");
    assert(uc2.qubits[0] == 2u && "CX(a) ctrl must be a.qubits[0]=2");

    std::printf("  or_uncompute_emits_three_gates: PASS\n");
}

// ── not_uncompute_emits_x ────────────────────────────────────────────────────
// qbool r = ~q stamps ADD_CONST(1) uncompute.
// Forward: X on allocated ancilla.
// Uncompute: sub_const(1) emits STURM_GATE_X on the ancilla (superposed bit).
// Verify the final gate in the log is STURM_GATE_X.

static void not_uncompute_emits_x() {
    ScopedAppendCtx sc;

    sturm::qbool q = make_qbool_at(4);
    q.super_mask = 1ULL;

    {
        // ~q: allocates ancilla, emits X on ancilla, stamps ADD_CONST(1).
        sturm::qbool r = ~q;
        assert(r.qubits[0] >= 0 && "ancilla must have been allocated by ~");
        assert(sc.ir().size() == 1u && "forward ~q emits 1 X gate");
        assert(r.uncompute_.tag == sturm::uncompute_op::kind::ADD_CONST
               && "not: must stamp ADD_CONST uncompute");
        // Destructor: ADD_CONST(1).apply() → sub_const(1) → STURM_GATE_X on ancilla.
    }

    // After destruction: 2 gates total (forward X + uncompute X).
    assert(sc.ir().size() == 2u && "not_uncompute: total must be 2 gates");

    // Final gate must be STURM_GATE_X (the uncompute).
    const sturm::GateRecord& last = sc.ir().at(1u);
    assert(last.kind == STURM_GATE_X
           && "not_uncompute: final gate must be X (sub_const stub)");

    std::printf("  not_uncompute_emits_x: PASS\n");
}

// ── nested_when_and_uncompute ────────────────────────────────────────────────
// Materialize qbool r = (a & b) inside a WHEN scope (control pushed onto stack).
// Verify that uncompute fires correctly: the destructor still emits CCX even
// when the control stack is non-empty (execute_gate in apply() goes to IR
// directly — uncompute is not further control-lifted at the apply() level).

static void nested_when_and_uncompute() {
    ScopedAppendCtx sc;

    // ctrl=qubit 5, a=qubit 6, b=qubit 7.
    sturm::qbool a = make_qbool_at(6);
    sturm::qbool b = make_qbool_at(7);
    a.super_mask = 1ULL;
    b.super_mask = 1ULL;

    // Simulate a WHEN scope by pushing a control qubit onto the context stack.
    // (The WhenGuard macro modifies TLS; here we use the control_stack directly.)
    sc.ctx->control_stack.push_control(5u);

    std::size_t ir_after_forward;
    {
        // AndExpr<qbool>::operator qbool() calls primitive_AND directly (no lifting).
        // Forward: CCX(6, 7, anc).
        sturm::qbool r = (a & b);
        assert(r.qubits[0] >= 0 && "ancilla must be allocated");
        ir_after_forward = sc.ir().size();
        assert(ir_after_forward == 1u && "forward AND emits 1 CCX");
        // Destructor runs here, inside the WHEN scope (control stack depth=1).
        // apply() for BITWISE_SELF(AND) calls execute_gate(CCX) directly.
    }

    sc.ctx->control_stack.pop_control();

    // Uncompute must have emitted CCX (BITWISE_SELF AND is self-inverse).
    const std::size_t total = sc.ir().size();
    assert(total == 2u && "forward 1 + uncompute 1 = 2 gates total");

    // The uncompute CCX must be at index 1.
    const sturm::GateRecord& uncompute_gate = sc.ir().at(1u);
    assert(uncompute_gate.kind == STURM_GATE_CCX
           && "nested_when_and_uncompute: uncompute gate must be CCX");
    assert(uncompute_gate.qubits[0] == 6u && "CCX ctrl0 must be a.qubits[0]=6");
    assert(uncompute_gate.qubits[1] == 7u && "CCX ctrl1 must be b.qubits[0]=7");

    std::printf("  nested_when_and_uncompute: PASS\n");
}

// ── uncompute_after_move ─────────────────────────────────────────────────────
// Verify that uncompute fires on the move destination, not the source.
// After move, the source has owning_=false and qubits[0]=-1, so its destructor
// is a no-op.  The destination inherits uncompute_ and the ancilla qubit, so
// its destructor emits the CCX uncompute gate.

static void uncompute_after_move() {
    ScopedAppendCtx sc;

    sturm::qbool a = make_qbool_at(8);
    sturm::qbool b = make_qbool_at(9);
    a.super_mask = 1ULL;
    b.super_mask = 1ULL;

    {
        // Materialize r = (a & b): forward CCX emitted.
        sturm::qbool r = (a & b);
        assert(r.qubits[0] >= 0 && "ancilla must be allocated");
        assert(r.owning_ && "r must be owning after materialization");
        assert(r.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF);

        // Move into r2.  r becomes non-owning with cleared qubits.
        // r2 inherits ownership, qubit index, and uncompute_ tag.
        sturm::qbool r2 = std::move(r);

        assert(!r.owning_ && "moved-from r must be non-owning");
        assert(r.qubits[0] == -1 && "moved-from r must have cleared qubit");
        assert(r2.owning_ && "r2 must own the qubit after move");
        assert(r2.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF
               && "r2 must inherit uncompute_ tag");

        // r destructs first (scope end order): no-op (non-owning, no uncompute).
        // r2 destructs second: emits CCX uncompute.
        // Both r and r2 go out of scope here.
    }

    // After both destructors: total must be 2 (1 forward + 1 uncompute).
    // If the source r had incorrectly fired, we'd see 2 extra attempts.
    const std::size_t total = sc.ir().size();
    assert(total == 2u
           && "uncompute_after_move: exactly 1 forward + 1 uncompute CCX expected");

    // Verify the uncompute gate is CCX.
    const sturm::GateRecord& uncompute_gate = sc.ir().at(1u);
    assert(uncompute_gate.kind == STURM_GATE_CCX
           && "uncompute_after_move: uncompute gate must be CCX");
    assert(uncompute_gate.qubits[0] == 8u && "CCX ctrl0 must be a.qubits[0]=8");
    assert(uncompute_gate.qubits[1] == 9u && "CCX ctrl1 must be b.qubits[0]=9");

    std::printf("  uncompute_after_move: PASS\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M22 qbool uncompute tests:\n");
    test_qbool_compare_uncompute<4>();
    test_qbool_compare_uncompute<8>();
    test_qbool_compare_uncompute<16>();

    std::printf("\nM10 BITWISE_SELF uncompute tests:\n");
    and_uncompute_emits_ccx();
    or_uncompute_emits_three_gates();
    not_uncompute_emits_x();
    nested_when_and_uncompute();
    uncompute_after_move();

    std::printf("All qbool uncompute tests passed.\n");
    return 0;
}
