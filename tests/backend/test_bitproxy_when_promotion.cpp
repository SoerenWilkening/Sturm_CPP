// test_bitproxy_when_promotion.cpp -- M1/M3/M5: BitProxy struct tests.
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
//   9. Non-const operator[] returns BitProxy (M2).
//  10. WHEN + classical qint XOR with per-bit promotion (M5).
//  11. WHEN + classical qint AND (M5).
//  12. No WHEN: classical fast-path preserved (M5).
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

// ── Test 10: WHEN + classical qint XOR with per-bit promotion ───────────────
// M5: Only the bits where b has a 1 should be promoted to quantum.
// a=3 (0b0011), b=4 (0b0100).  Inside WHEN(flag), a ^= b should promote
// only bit 2 of a (b's only set bit).  Bits 0,1,3 stay classical.

static void test_when_qint_xor_per_bit_promotion() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> a(3);   // 0b0011
    sturm::qint_t<4> b(4);   // 0b0100

    // Both fully classical before WHEN.
    assert(a.super_mask == 0);
    assert(b.super_mask == 0);
    for (int i = 0; i < 4; ++i) {
        assert(a.qubits[i] == -1);
        assert(b.qubits[i] == -1);
    }

    // Create a superposed qbool flag.
    sturm::qbool flag(0.5);

    size_t before = sc.ir().size();
    WHEN(flag) {
        a ^= b;
    }
    size_t after = sc.ir().size();

    // Gates must have been emitted (at least the X for ensure_quantum init).
    assert(after > before && "WHEN + XOR must emit gates");

    // Only bit 2 of a should be promoted (has qubit), because b's only set
    // bit is bit 2.  Bits 0, 1, 3 of b are 0 -> XOR with 0 is identity,
    // no promotion.
    assert(a.qubits[2] >= 0 && "bit 2 must be promoted (b bit 2 == 1)");
    assert(a.qubits[0] == -1 && "bit 0 must stay classical (b bit 0 == 0)");
    assert(a.qubits[1] == -1 && "bit 1 must stay classical (b bit 1 == 0)");
    assert(a.qubits[3] == -1 && "bit 3 must stay classical (b bit 3 == 0)");

    // super_mask should reflect that only bit 2 is quantum.
    assert((a.super_mask & (1u << 2)) != 0 && "super_mask bit 2 must be set");
    assert((a.super_mask & (1u << 0)) == 0 && "super_mask bit 0 must be clear");
    assert((a.super_mask & (1u << 1)) == 0 && "super_mask bit 1 must be clear");
    assert((a.super_mask & (1u << 3)) == 0 && "super_mask bit 3 must be clear");

    // Classical value: 3 ^ 4 = 7 (0b0111).
    assert(a.value == 7 && "classical value must be 3 ^ 4 = 7");

    std::puts("PASS: test_when_qint_xor_per_bit_promotion");
}

// ── Test 11: WHEN + classical qint AND ──────────────────────────────────────
// M5: Inside WHEN(flag), a &= b with classical operands should produce
// quantum result bits where the AND of the classical bit values is 1.
// a=7 (0b0111), b=5 (0b0101).  AND result: 0b0101 (bits 0,2 are 1).
// Those result bits should be quantum (super_mask != 0).

static void test_when_qint_and_promotion() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> a(7);   // 0b0111
    sturm::qint_t<4> b(5);   // 0b0101

    assert(a.super_mask == 0);
    assert(b.super_mask == 0);

    sturm::qbool flag(0.5);

    size_t before = sc.ir().size();
    WHEN(flag) {
        a &= b;
    }
    size_t after = sc.ir().size();

    // Gates must have been emitted.
    assert(after > before && "WHEN + AND must emit gates");

    // Result super_mask must be nonzero: bits where (a_i AND b_i) == 1
    // get promoted because they're inside a WHEN with classical-1 AND result.
    // a=0b0111, b=0b0101 -> AND = 0b0101.  Bits 0 and 2 have AND result 1,
    // so those result bits should be quantum.
    assert(a.super_mask != 0 && "AND result must have quantum bits inside WHEN");

    // Classical value: 7 & 5 = 5.
    assert(a.value == 5 && "classical value must be 7 & 5 = 5");

    std::puts("PASS: test_when_qint_and_promotion");
}

// ── Test 12: No WHEN: classical fast-path preserved ─────────────────────────
// M5: Outside WHEN, a ^= b on fully classical qint_t must stay classical:
// no qubits allocated, super_mask == 0, correct classical value.

static void test_no_when_classical_xor_fast_path() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> a(3);   // 0b0011
    sturm::qint_t<4> b(4);   // 0b0100

    assert(a.super_mask == 0);
    assert(b.super_mask == 0);

    size_t before = sc.ir().size();
    a ^= b;
    size_t after = sc.ir().size();

    // No gates: pure classical arithmetic.
    assert(after == before && "classical XOR outside WHEN must emit no gates");

    // super_mask stays 0: no bits promoted.
    assert(a.super_mask == 0 && "super_mask must be 0 outside WHEN");

    // No qubits allocated.
    for (int i = 0; i < 4; ++i) {
        assert(a.qubits[i] == -1 && "no qubits must be allocated outside WHEN");
    }

    // Correct classical value: 3 ^ 4 = 7.
    assert(a.value == 7 && "classical value must be 3 ^ 4 = 7");

    std::puts("PASS: test_no_when_classical_xor_fast_path");
}

// ── Test 13: WHEN + classical += ────────────────────────────────────────────
// M10: Classical a(3) += b(2) inside WHEN(flag) must produce super_mask != 0
// and correct classical value 5.

static void test_when_classical_add_assign() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> a(3);
    sturm::qint_t<4> b(2);

    assert(a.super_mask == 0);
    assert(b.super_mask == 0);

    sturm::qbool flag(0.5);

    size_t before = sc.ir().size();
    WHEN(flag) {
        a += b;
    }
    size_t after = sc.ir().size();

    assert(after > before && "WHEN + += must emit gates");
    assert(a.super_mask != 0 && "a must be promoted to quantum inside WHEN");
    assert(a.value == 5 && "classical value must be 3 + 2 = 5");

    std::puts("PASS: test_when_classical_add_assign");
}

// ── Test 14: WHEN + classical -= ────────────────────────────────────────────
// M10: Classical a(5) -= b(2) inside WHEN(flag) must produce super_mask != 0
// and correct classical value 3.

static void test_when_classical_sub_assign() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> a(5);
    sturm::qint_t<4> b(2);

    assert(a.super_mask == 0);
    assert(b.super_mask == 0);

    sturm::qbool flag(0.5);

    size_t before = sc.ir().size();
    WHEN(flag) {
        a -= b;
    }
    size_t after = sc.ir().size();

    assert(after > before && "WHEN + -= must emit gates");
    assert(a.super_mask != 0 && "a must be promoted to quantum inside WHEN");
    assert(a.value == 3 && "classical value must be 5 - 2 = 3");

    std::puts("PASS: test_when_classical_sub_assign");
}

// ── Test 15: WHEN + classical *= ────────────────────────────────────────────
// M10: Classical a(3) *= b(2) inside WHEN(flag) must produce super_mask != 0
// and correct classical value 6.

static void test_when_classical_mul_assign() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> a(3);
    sturm::qint_t<4> b(2);

    assert(a.super_mask == 0);
    assert(b.super_mask == 0);

    sturm::qbool flag(0.5);

    size_t before = sc.ir().size();
    WHEN(flag) {
        a *= b;
    }
    size_t after = sc.ir().size();

    assert(after > before && "WHEN + *= must emit gates");
    assert(a.super_mask != 0 && "a must be promoted to quantum inside WHEN");
    assert(a.value == 6 && "classical value must be 3 * 2 = 6");

    std::puts("PASS: test_when_classical_mul_assign");
}

// ── Test 16: No WHEN: fast-path preserved for all arithmetic ops ────────────
// M10: Outside WHEN, classical a op= b must have super_mask == 0 and no
// qubits allocated for +=, -=, *=, /=, %=.

static void test_no_when_arith_fast_path_all_ops() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // += fast-path
    {
        sturm::qint_t<4> a(3), b(2);
        size_t before = sc.ir().size();
        a += b;
        assert(sc.ir().size() == before && "+= outside WHEN must emit no gates");
        assert(a.super_mask == 0 && "+= super_mask must be 0");
        assert(a.value == 5);
        for (int i = 0; i < 4; ++i) assert(a.qubits[i] == -1);
    }
    // -= fast-path
    {
        sturm::qint_t<4> a(5), b(2);
        size_t before = sc.ir().size();
        a -= b;
        assert(sc.ir().size() == before && "-= outside WHEN must emit no gates");
        assert(a.super_mask == 0 && "-= super_mask must be 0");
        assert(a.value == 3);
        for (int i = 0; i < 4; ++i) assert(a.qubits[i] == -1);
    }
    // *= fast-path
    {
        sturm::qint_t<4> a(3), b(2);
        size_t before = sc.ir().size();
        a *= b;
        assert(sc.ir().size() == before && "*= outside WHEN must emit no gates");
        assert(a.super_mask == 0 && "*= super_mask must be 0");
        assert(a.value == 6);
        for (int i = 0; i < 4; ++i) assert(a.qubits[i] == -1);
    }
    // /= fast-path
    {
        sturm::qint_t<4> a(6), b(2);
        size_t before = sc.ir().size();
        a /= b;
        assert(sc.ir().size() == before && "/= outside WHEN must emit no gates");
        assert(a.super_mask == 0 && "/= super_mask must be 0");
        assert(a.value == 3);
        for (int i = 0; i < 4; ++i) assert(a.qubits[i] == -1);
    }
    // %= fast-path
    {
        sturm::qint_t<4> a(7), b(3);
        size_t before = sc.ir().size();
        a %= b;
        assert(sc.ir().size() == before && "%= outside WHEN must emit no gates");
        assert(a.super_mask == 0 && "%= super_mask must be 0");
        assert(a.value == 1);
        for (int i = 0; i < 4; ++i) assert(a.qubits[i] == -1);
    }

    std::puts("PASS: test_no_when_arith_fast_path_all_ops");
}

// ── Test 17: Gate count comparison — WHEN-promoted vs pre-quantum += ────────
// M10: Compare gate counts for WHEN-promoted (classical operands inside WHEN,
// lazy promotion via BitProxy) vs pre-promoted (all-quantum) a += b.
//
// BitProxy performs classical folding: bits that are classically 0 in the
// addend skip gate emission entirely.  Pre-promoted operands are fully quantum
// so the adder processes all bit-pairs.  Therefore WHEN-promoted should emit
// fewer-or-equal gates (classical folding optimizes), not more.
// Both must emit a non-zero number of gates.

static void test_gate_count_when_promoted_vs_prequantum_add() {
    // Run 1: WHEN-promoted (classical operands, lazy promotion via BitProxy).
    size_t gates_when;
    {
        sturm::QubitPool::instance().reset_for_testing();
        ScopedAppendCtx sc;

        sturm::qint_t<4> a(3);
        sturm::qint_t<4> b(2);
        sturm::qbool flag(0.5);

        size_t before = sc.ir().size();
        WHEN(flag) {
            a += b;
        }
        gates_when = sc.ir().size() - before;
    }

    // Run 2: Pre-promoted (qubits allocated eagerly, all-quantum).
    size_t gates_pre;
    {
        sturm::QubitPool::instance().reset_for_testing();
        ScopedAppendCtx sc;

        sturm::qint_t<4> a(3);
        sturm::qint_t<4> b(2);
        // Eagerly allocate qubits and set super_mask for both operands.
        for (int i = 0; i < 4; ++i) {
            a.qubits[i] = sturm::QubitPool::instance().allocate();
            if ((a.value >> i) & 1) {
                const auto q = static_cast<uint32_t>(a.qubits[i]);
                execute_gate(*sc.ctx, STURM_GATE_X, &q, 1u, 0.0);
            }
        }
        a.super_mask = 0xF;
        for (int i = 0; i < 4; ++i) {
            b.qubits[i] = sturm::QubitPool::instance().allocate();
            if ((b.value >> i) & 1) {
                const auto q = static_cast<uint32_t>(b.qubits[i]);
                execute_gate(*sc.ctx, STURM_GATE_X, &q, 1u, 0.0);
            }
        }
        b.super_mask = 0xF;

        sturm::qbool flag(0.5);

        size_t before = sc.ir().size();
        WHEN(flag) {
            a += b;
        }
        gates_pre = sc.ir().size() - before;
    }

    // Both must emit gates.
    assert(gates_when > 0 && "WHEN-promoted gate count must be nonzero");
    assert(gates_pre  > 0 && "pre-quantum gate count must be nonzero");

    // WHEN-promoted uses BitProxy classical folding, so it should emit
    // fewer-or-equal gates compared to the all-quantum path.
    assert(gates_when <= gates_pre &&
           "WHEN-promoted (classical folding) must emit <= gates vs pre-quantum");

    std::printf("PASS: test_gate_count_when_promoted_vs_prequantum_add "
                "(when=%zu pre=%zu)\n", gates_when, gates_pre);
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
    test_when_qint_xor_per_bit_promotion();
    test_when_qint_and_promotion();
    test_no_when_classical_xor_fast_path();
    test_when_classical_add_assign();
    test_when_classical_sub_assign();
    test_when_classical_mul_assign();
    test_no_when_arith_fast_path_all_ops();
    test_gate_count_when_promoted_vs_prequantum_add();

    std::puts("\nAll M1/M2/M5/M10 BitProxy tests passed.");
    return 0;
}
