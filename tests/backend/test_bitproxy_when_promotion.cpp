// test_bitproxy_when_promotion.cpp -- M1/M3: BitProxy struct tests.
//
// Tests:
//   1. BitProxy construction from qint and qbool -- verify accessor values.
//   2. BitProxy XOR: quantum source promotes target.
//   3. BitProxy XOR: classical 0 source is identity (no qubit allocated, no gate).
//   4. BitProxy XOR: classical 1 source inside WHEN promotes target.
//   5. BitProxy AND-XOR: classical folding (b classical 1 -> CX not CCX).
//   6. BitProxy AND-XOR: classical 0 folds to skip (no gate).
//   7. ensure_quantum: initializes with X for value=1.
//   8. sizeof(BitProxy) <= 40 bytes.
//
// Harness: APPEND mode BackendContext + GateIR inspection.

#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>
#include <cstddef>

// ── ScopedAppendCtx ─────────────────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 32u);
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

// ── Gate counting helpers ───────────────────────────────────────────────────

static size_t count_kind(const sturm::GateIR& ir, sturm_gate_kind_t kind,
                         size_t from = 0) {
    size_t n = 0;
    for (size_t i = from; i < ir.size(); ++i) {
        if (ir.at(i).kind == kind) ++n;
    }
    return n;
}

// ── Test 1: BitProxy construction from qint and qbool ───────────────────────

static void test_construction_from_qint_and_qbool() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // qint_t<4> with classical value 5 (binary 0101).
    sturm::qint_t<4> reg(5);
    // Allocate qubit for bit 0 to make it quantum.
    reg.qubits[0] = sturm::QubitPool::instance().allocate();
    reg.super_mask = 1ULL;

    sturm::BitProxy bp0(reg, 0);
    sturm::BitProxy bp1(reg, 1);
    sturm::BitProxy bp2(reg, 2);

    // bp0: qubit allocated, quantum, bit value = 1.
    assert(bp0.qubit_index() == reg.qubits[0]);
    assert(bp0.is_quantum() == true);
    assert(bp0.bit_value() == true);   // bit 0 of 5 = 1

    // bp1: qubit not allocated, classical, bit value = 0.
    assert(bp1.qubit_index() == -1);
    assert(bp1.is_quantum() == false);
    assert(bp1.bit_value() == false);  // bit 1 of 5 = 0

    // bp2: qubit not allocated, classical, bit value = 1.
    assert(bp2.qubit_index() == -1);
    assert(bp2.is_quantum() == false);
    assert(bp2.bit_value() == true);   // bit 2 of 5 = 1

    // Construction from qbool.
    sturm::qbool qb(true);
    qb.qubits[0] = sturm::QubitPool::instance().allocate();
    qb.super_mask = 1ULL;

    sturm::BitProxy bpq(qb);
    assert(bpq.qubit_index() == qb.qubits[0]);
    assert(bpq.is_quantum() == true);
    assert(bpq.bit_value() == true);
    assert(bpq.bit_pos == 0);

    // set_bit_value test.
    sturm::qint_t<4> reg2(0);
    sturm::BitProxy bp_sv(reg2, 2);
    assert(bp_sv.bit_value() == false);
    bp_sv.set_bit_value(true);
    assert(bp_sv.bit_value() == true);
    assert((reg2.value & (1 << 2)) != 0);  // bit 2 set in parent
    bp_sv.set_bit_value(false);
    assert(bp_sv.bit_value() == false);
    assert((reg2.value & (1 << 2)) == 0);

    std::puts("PASS: test_construction_from_qint_and_qbool");
}

// ── Test 2: BitProxy XOR: quantum source promotes target ────────────────────

static void test_xor_quantum_source_promotes_target() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // Source: quantum bit (qubit allocated).
    sturm::qbool src_q(false);
    src_q.qubits[0] = sturm::QubitPool::instance().allocate();
    src_q.super_mask = 1ULL;
    sturm::BitProxy source(src_q);

    // Target: classical bit (no qubit).
    sturm::qbool tgt_q(false);
    sturm::BitProxy target(tgt_q);

    assert(!target.is_quantum());

    size_t before = sc.ir().size();
    target ^= source;
    size_t after = sc.ir().size();

    // Target must now be quantum (qubit allocated via ensure_quantum).
    assert(target.is_quantum() && "target must be promoted to quantum");
    // CX gate must be emitted.
    assert(after > before && "must emit gates");
    assert(count_kind(sc.ir(), STURM_GATE_CX, before) >= 1 &&
           "must emit at least one CX gate");

    std::puts("PASS: test_xor_quantum_source_promotes_target");
}

// ── Test 3: BitProxy XOR: classical 0 source is identity ────────────────────

static void test_xor_classical_0_is_identity() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // Source: classical 0.
    sturm::qbool src_q(false);
    sturm::BitProxy source(src_q);
    assert(!source.is_quantum());
    assert(!source.bit_value());

    // Target: classical bit.
    sturm::qbool tgt_q(true);
    sturm::BitProxy target(tgt_q);

    size_t before = sc.ir().size();
    target ^= source;
    size_t after = sc.ir().size();

    // No gates emitted, no qubit allocated.
    assert(after == before && "XOR with classical 0 must emit no gates");
    assert(!target.is_quantum() && "target must stay classical");

    std::puts("PASS: test_xor_classical_0_is_identity");
}

// ── Test 4: BitProxy XOR: classical 1 source inside WHEN promotes target ────

static void test_xor_classical_1_inside_when_promotes() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // Source: classical 1.
    sturm::qbool src_q(true);
    sturm::BitProxy source(src_q);
    assert(!source.is_quantum());
    assert(source.bit_value());

    // Target: classical bit.
    sturm::qbool tgt_q(false);
    sturm::BitProxy target(tgt_q);

    // Enter a WHEN scope (superposed control).
    sturm::qbool flag(0.5);

    size_t before = sc.ir().size();
    WHEN(flag) {
        target ^= source;
    }
    size_t after = sc.ir().size();

    // Target must be promoted because we are inside WHEN.
    assert(target.is_quantum() &&
           "target must be promoted inside WHEN with classical-1 source");
    // Must emit at least an X gate (lifted as CX under WHEN control).
    assert(after > before && "must emit gates inside WHEN");

    std::puts("PASS: test_xor_classical_1_inside_when_promotes");
}

// ── Test 5: BitProxy AND-XOR: classical folding (b classical 1 -> CX) ──────

static void test_and_xor_classical_folding_cx() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // a: quantum.
    sturm::qbool a_q(false);
    a_q.qubits[0] = sturm::QubitPool::instance().allocate();
    a_q.super_mask = 1ULL;
    sturm::BitProxy a_bp(a_q);

    // b: classical 1.
    sturm::qbool b_q(true);
    sturm::BitProxy b_bp(b_q);
    assert(!b_bp.is_quantum());
    assert(b_bp.bit_value());

    // Target: classical.
    sturm::qbool tgt_q(false);
    sturm::BitProxy target(tgt_q);

    size_t before = sc.ir().size();
    {
        auto expr = a_bp & b_bp;
        target ^= expr;
    }
    size_t after = sc.ir().size();

    // Should emit CX (not CCX) because b is classical 1.
    assert(target.is_quantum() && "target must be promoted");
    size_t cx_count  = count_kind(sc.ir(), STURM_GATE_CX, before);
    size_t ccx_count = count_kind(sc.ir(), STURM_GATE_CCX, before);
    assert(cx_count >= 1 && "must emit CX (a quantum, b classical 1 -> CX fold)");
    assert(ccx_count == 0 && "must NOT emit CCX (b classical -> fold to CX)");

    std::puts("PASS: test_and_xor_classical_folding_cx");
}

// ── Test 6: BitProxy AND-XOR: classical 0 folds to skip ────────────────────

static void test_and_xor_classical_0_skip() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // a: quantum.
    sturm::qbool a_q(false);
    a_q.qubits[0] = sturm::QubitPool::instance().allocate();
    a_q.super_mask = 1ULL;
    sturm::BitProxy a_bp(a_q);

    // b: classical 0.
    sturm::qbool b_q(false);
    sturm::BitProxy b_bp(b_q);
    assert(!b_bp.is_quantum());
    assert(!b_bp.bit_value());

    // Target: classical.
    sturm::qbool tgt_q(false);
    sturm::BitProxy target(tgt_q);

    size_t before = sc.ir().size();
    {
        auto expr = a_bp & b_bp;
        target ^= expr;
    }
    size_t after = sc.ir().size();

    // AND with classical 0 always yields 0 -> skip.
    assert(after == before && "AND-XOR with classical 0 must emit no gates");
    assert(!target.is_quantum() && "target must stay classical");

    std::puts("PASS: test_and_xor_classical_0_skip");
}

// ── Test 7: ensure_quantum initializes with X for value=1 ───────────────────

static void test_ensure_quantum_x_for_value_1() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // Classical bit with value 1.
    sturm::qbool q(true);
    sturm::BitProxy bp(q);
    assert(!bp.is_quantum());
    assert(bp.bit_value() == true);

    size_t before = sc.ir().size();
    bp.ensure_quantum();
    size_t after = sc.ir().size();

    // Must now be quantum.
    assert(bp.is_quantum() && "must be quantum after ensure_quantum");
    // Must have emitted X gate (to initialize |0> -> |1>).
    assert(after == before + 1 && "must emit exactly 1 gate (X for value=1)");
    assert(sc.ir().at(before).kind == STURM_GATE_X &&
           "gate must be X (init classical 1 into quantum)");

    // Verify that ensure_quantum with value=0 emits no X.
    sturm::qbool q0(false);
    sturm::BitProxy bp0(q0);
    size_t before0 = sc.ir().size();
    bp0.ensure_quantum();
    size_t after0 = sc.ir().size();
    assert(bp0.is_quantum() && "must be quantum after ensure_quantum");
    assert(after0 == before0 && "must emit no gate for value=0 init");

    std::puts("PASS: test_ensure_quantum_x_for_value_1");
}

// ── Test 8: sizeof(BitProxy) <= 40 bytes ────────────────────────────────────

static void test_sizeof_bitproxy() {
    static_assert(sizeof(sturm::BitProxy) <= 40,
                  "BitProxy must be <= 40 bytes");
    std::printf("PASS: test_sizeof_bitproxy (sizeof=%zu)\n",
                sizeof(sturm::BitProxy));
}

// ── Test 9: Non-const operator[] returns BitProxy (M2) ─────────────────────

static void test_nonconst_operator_subscript_returns_bitproxy() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // qint_t<4> with classical value 5 (binary 0101).
    sturm::qint_t<4> reg(5);
    // Allocate qubit for bit 0 to make it quantum.
    reg.qubits[0] = sturm::QubitPool::instance().allocate();
    reg.super_mask = 1ULL;

    // Non-const operator[] should return BitProxy.
    sturm::BitProxy bp0 = reg[0];
    sturm::BitProxy bp1 = reg[1];
    sturm::BitProxy bp2 = reg[2];

    // bp0: qubit allocated, quantum, bit value = 1 (bit 0 of 5).
    assert(bp0.qubit_index() == reg.qubits[0]);
    assert(bp0.is_quantum() == true);
    assert(bp0.bit_value() == true);

    // bp1: classical, bit value = 0 (bit 1 of 5).
    assert(bp1.qubit_index() == -1);
    assert(bp1.is_quantum() == false);
    assert(bp1.bit_value() == false);

    // bp2: classical, bit value = 1 (bit 2 of 5).
    assert(bp2.qubit_index() == -1);
    assert(bp2.is_quantum() == false);
    assert(bp2.bit_value() == true);

    // Verify BitProxy writes back to parent via set_bit_value.
    bp1.set_bit_value(true);
    assert((reg.value & (1 << 1)) != 0 && "write-back to parent must work");
    bp1.set_bit_value(false);
    assert((reg.value & (1 << 1)) == 0);

    // Verify BitProxy from operator[] can be used for quantum operations.
    // Use bp2 (classical bit 2, value=1) as XOR source on a target.
    sturm::qbool tgt(false);
    sturm::BitProxy target(tgt);

    // XOR target with bp0 (quantum source) -- should promote target and emit CX.
    size_t before = sc.ir().size();
    target ^= bp0;
    assert(target.is_quantum() && "target must be promoted via operator[] BitProxy");
    assert(count_kind(sc.ir(), STURM_GATE_CX, before) >= 1);

    std::puts("PASS: test_nonconst_operator_subscript_returns_bitproxy");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_construction_from_qint_and_qbool();
    test_xor_quantum_source_promotes_target();
    test_xor_classical_0_is_identity();
    test_xor_classical_1_inside_when_promotes();
    test_and_xor_classical_folding_cx();
    test_and_xor_classical_0_skip();
    test_ensure_quantum_x_for_value_1();
    test_sizeof_bitproxy();
    test_nonconst_operator_subscript_returns_bitproxy();

    std::puts("\nAll M1/M2 BitProxy tests passed.");
    return 0;
}
