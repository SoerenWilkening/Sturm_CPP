// test_qint_superposed.cpp — Step 6 TDD: superposition tests for qint_t
// Verifies mask widening rules, qubit allocation, and sink dispatch.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdint>

// sturm-ypit (D0 re-spell miss): bare `qint` here used to alias
// sturm::qint_t<64> (pre-B1). Post-B1 (sturm-65rs.6) sturm::qint resolves
// to sturm::frontend::qint, which lacks the backend surface this file
// exercises (super_mask, qubits[]). PRD §6 R1 mechanical migration recipe:
// introduce a local type-alias `using qint = sturm::qint_t<64>;` so every
// site below keeps its backend semantics with a one-line diff. Drift back
// to bare sturm::qint is pinned by tests/regressions/test_qint_callsite_respelling_super_mask.cpp
// (sturm-1os7 split the original drift-gate per backend-surface family).
using qint = sturm::qint_t<64>;
using sturm::RecordingSink;
using sturm::ScopedSink;
using sturm::QubitPool;

// ── helper: build a qint with super_mask and one pre-allocated qubit ──────────

static qint make_super(int64_t val, uint64_t mask, int bit_pos) {
    qint q(val);
    q.super_mask = mask;
    q.qubits[bit_pos] = QubitPool::instance().allocate();
    return q;
}

// ── addsub mask widening ──────────────────────────────────────────────────────

static void test_add_mask_widening() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // bit 2 is superposed (mask = 0x4), ctz(0x4)=2, so output mask = all ones from bit 2 up
    qint a = make_super(3, 0x4, 2);
    qint b(1);  // classical

    qint c = a + b;
    // mask_addsub(0x4, 0) = (0x4 | (~0ULL << 2)) & width_mask(64)
    const uint64_t expected_mask = (0x4ULL | (~0ULL << 2));
    assert(c.super_mask == expected_mask);
    assert(c.qubits[2] >= 0);  // output allocated at bit 2+
    // sink got called once
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_add");
    assert(rs.records()[0].control == -1);
}

static void test_sub_mask_widening() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(10, 0x4, 2);
    qint b(1);

    qint c = a - b;
    const uint64_t expected_mask = (0x4ULL | (~0ULL << 2));
    assert(c.super_mask == expected_mask);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_sub");
}

static void test_add_assign_mask_widening() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(3, 0x4, 2);
    qint b(1);
    a += b;

    const uint64_t expected_mask = (0x4ULL | (~0ULL << 2));
    assert(a.super_mask == expected_mask);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_add");
}

// ── bitwise mask rules (OR of operand masks) ──────────────────────────────────

static void test_xor_mask_or() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // a has bit 1 super, b has bit 3 super
    qint a = make_super(0b0010, 0x2, 1);
    qint b = make_super(0b1000, 0x8, 3);

    qint c = a ^ b;
    assert(c.super_mask == (0x2 | 0x8));
    assert(c.qubits[1] >= 0);
    assert(c.qubits[3] >= 0);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_xor");
}

static void test_and_mask_or() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(0xFF, 0x3, 0);
    qint b = make_super(0xF0, 0xC, 2);

    qint c = a & b;
    assert(c.super_mask == (0x3 | 0xC));
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_and");
}

static void test_or_mask_or() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(0x0F, 0x1, 0);
    qint b = make_super(0xF0, 0x2, 1);

    qint c = a | b;
    assert(c.super_mask == (0x1 | 0x2));
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_or");
}

// ── not mask ──────────────────────────────────────────────────────────────────

static void test_not_mask_passthrough() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(0x5, 0x4, 2);
    qint b = ~a;
    assert(b.super_mask == 0x4);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_not");
}

// ── unary negation mask ───────────────────────────────────────────────────────

static void test_neg_mask_widening() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(5, 0x4, 2);
    qint b = -a;
    // unary neg uses mask_addsub(mask, 0, 64) style via dispatch_unary
    // super_mask != 0 means a sink call happened
    assert(b.super_mask != 0);
    assert(rs.records().size() == 1);
    // Unary negation is dispatched as quantum_sub([], a_qubits, ctrl)
    assert(rs.records()[0].op == "quantum_sub");
}

// ── mul/div mask all-ones ─────────────────────────────────────────────────────

static void test_mul_mask_allones() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(3, 0x4, 2);
    qint b(2);

    qint c = a * b;
    // any bit set → all-ones for Width=64
    assert(c.super_mask == ~0ULL);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_mul");
}

static void test_div_mask_allones() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(12, 0x4, 2);
    qint b(3);

    qint c = a / b;
    assert(c.super_mask == ~0ULL);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_div");
}

static void test_mod_mask_allones() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(17, 0x4, 2);
    qint b(5);

    qint c = a % b;
    assert(c.super_mask == ~0ULL);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_mod");
}

// ── shift mask rules ──────────────────────────────────────────────────────────

static void test_shl_mask() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(1, 0x1, 0);
    qint b = a << 2;
    // mask_shl(0x1, 2, 64) = 0x4
    assert(b.super_mask == 0x4);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_shl");
}

static void test_shr_mask() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(8, 0x8, 3);
    qint b = a >> 2;
    // mask_shr(0x8, 2, 64) = 0x2
    assert(b.super_mask == 0x2);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_shr");
}

// ── compare with super inputs → qbool result ─────────────────────────────────

static void test_eq_super() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(5, 0x4, 2);
    qint b(5);

    sturm::qbool r = (a == b);
    assert((r.super_mask & 1) == 1);
    assert(r.qubits[0] >= 0);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_eq");
    assert(rs.records()[0].control == -1);
}

static void test_lt_super() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(3, 0x4, 2);
    qint b(5);

    sturm::qbool r = (a < b);
    assert((r.super_mask & 1) == 1);
    assert(r.qubits[0] >= 0);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_lt");
}

// ── bit subscript with super qubit ───────────────────────────────────────────

static void test_subscript_super() {
    QubitPool::instance().reset_for_testing();

    qint a = make_super(0b101, 0x4, 2);
    sturm::qbool b = a[2];
    assert((b.super_mask & 1) == 1);
    assert(b.qubits[0] == a.qubits[2]);  // shared qubit
}

// ── qubit allocation at set bit positions ────────────────────────────────────

static void test_qubit_allocation_positions() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // bits 1 and 3 are superposed
    qint a;
    a.value = 0xA;
    a.super_mask = 0xA;  // bits 1 and 3
    a.qubits[1] = QubitPool::instance().allocate();
    a.qubits[3] = QubitPool::instance().allocate();

    qint b(1);
    qint c = a ^ b;

    // output should have qubits allocated at positions where mask is set
    assert(c.super_mask == 0xA);
    assert(c.qubits[1] >= 0);
    assert(c.qubits[3] >= 0);
}

// ── pow superposed ────────────────────────────────────────────────────────────

static void test_pow_super() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint base = make_super(2, 0x4, 2);
    qint exp(3);

    qint result = sturm::pow(base, exp);
    assert(result.super_mask == ~0ULL);
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_pow");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_add_mask_widening();
    test_sub_mask_widening();
    test_add_assign_mask_widening();
    test_xor_mask_or();
    test_and_mask_or();
    test_or_mask_or();
    test_not_mask_passthrough();
    test_neg_mask_widening();
    test_mul_mask_allones();
    test_div_mask_allones();
    test_mod_mask_allones();
    test_shl_mask();
    test_shr_mask();
    test_eq_super();
    test_lt_super();
    test_subscript_super();
    test_qubit_allocation_positions();
    test_pow_super();
    return 0;
}
