// test_qint_classical.cpp — Step 6 TDD: classical (no-superposition) tests for qint_t
// All operations with mask==0 inputs must produce correct int64 results,
// super_mask==0, all qubits==-1, and no sink calls.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/recording_sink.hpp"

#include <cassert>
#include <cstdint>

// sturm-pl7o (D0 re-spell miss): bare `qint` here used to alias
// sturm::qint_t<64> (pre-B1). Post-B1 (sturm-65rs.6) sturm::qint resolves
// to sturm::frontend::qint, which lacks the backend surface this file
// exercises (super_mask, qubits[], value, compound-assigns +=, -=, *=,
// /=, %=). PRD §6 R1 mechanical migration recipe: introduce a local
// type-alias `using qint = sturm::qint_t<64>;` so every site below keeps
// its backend semantics with a one-line diff. Drift back to bare
// sturm::qint is pinned by tests/regressions/test_qint_callsite_respelling.cpp.
using qint = sturm::qint_t<64>;
using sturm::qint_t;
using sturm::RecordingSink;
using sturm::ScopedSink;

// ── helpers ───────────────────────────────────────────────────────────────────

static void check_classical(const qint& q) {
    assert(q.super_mask == 0);
    for (int i = 0; i < 64; ++i) {
        assert(q.qubits[i] == -1);
    }
}

// ── default constructor ───────────────────────────────────────────────────────

static void test_default_ctor() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint q;
    assert(q.value == 0);
    check_classical(q);
    assert(rs.records().empty());
}

// ── int64_t constructor ───────────────────────────────────────────────────────

static void test_int_ctor() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint q(42);
    assert(q.value == 42);
    check_classical(q);
    assert(rs.records().empty());
}

// ── operator int64_t ──────────────────────────────────────────────────────────

static void test_int64_conversion() {
    qint q(99);
    int64_t v = static_cast<int64_t>(q);
    assert(v == 99);
}

// ── addition ──────────────────────────────────────────────────────────────────

static void test_add() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(3), b(4);
    qint c = a + b;
    assert(c.value == 7);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_add_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(10), b(5);
    a += b;
    assert(a.value == 15);
    check_classical(a);
    assert(rs.records().empty());
}

// ── subtraction ───────────────────────────────────────────────────────────────

static void test_sub() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(10), b(3);
    qint c = a - b;
    assert(c.value == 7);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_sub_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(10), b(3);
    a -= b;
    assert(a.value == 7);
    check_classical(a);
    assert(rs.records().empty());
}

// ── unary negation ────────────────────────────────────────────────────────────

static void test_unary_neg() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5);
    qint b = -a;
    assert(b.value == -5);
    check_classical(b);
    assert(rs.records().empty());
}

// ── multiplication ────────────────────────────────────────────────────────────

static void test_mul() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(6), b(7);
    qint c = a * b;
    assert(c.value == 42);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_mul_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(6), b(7);
    a *= b;
    assert(a.value == 42);
    check_classical(a);
    assert(rs.records().empty());
}

// ── division ──────────────────────────────────────────────────────────────────

static void test_div() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(42), b(6);
    qint c = a / b;
    assert(c.value == 7);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_div_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(42), b(6);
    a /= b;
    assert(a.value == 7);
    check_classical(a);
    assert(rs.records().empty());
}

// ── modulo ────────────────────────────────────────────────────────────────────

static void test_mod() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(17), b(5);
    qint c = a % b;
    assert(c.value == 2);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_mod_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(17), b(5);
    a %= b;
    assert(a.value == 2);
    check_classical(a);
    assert(rs.records().empty());
}

// ── pow (free function) ───────────────────────────────────────────────────────

static void test_pow() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint base(2), exp(10);
    qint result = sturm::pow(base, exp);
    assert(result.value == 1024);
    check_classical(result);
    assert(rs.records().empty());
}

static void test_pow_int() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint base(3);
    qint result = sturm::pow(base, int64_t(4));
    assert(result.value == 81);
    check_classical(result);
    assert(rs.records().empty());
}

// ── bitwise AND ───────────────────────────────────────────────────────────────

static void test_and() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0b1100), b(0b1010);
    qint c = a & b;
    assert(c.value == 0b1000);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_and_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0b1100), b(0b1010);
    a &= b;
    assert(a.value == 0b1000);
    check_classical(a);
    assert(rs.records().empty());
}

// ── bitwise OR ────────────────────────────────────────────────────────────────

static void test_or() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0b1100), b(0b1010);
    qint c = a | b;
    assert(c.value == 0b1110);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_or_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0b1100), b(0b1010);
    a |= b;
    assert(a.value == 0b1110);
    check_classical(a);
    assert(rs.records().empty());
}

// ── bitwise XOR ───────────────────────────────────────────────────────────────

static void test_xor() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0b1100), b(0b1010);
    qint c = a ^ b;
    assert(c.value == 0b0110);
    check_classical(c);
    assert(rs.records().empty());
}

static void test_xor_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0b1100), b(0b1010);
    a ^= b;
    assert(a.value == 0b0110);
    check_classical(a);
    assert(rs.records().empty());
}

// ── bitwise NOT ───────────────────────────────────────────────────────────────

static void test_not() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(0);
    qint b = ~a;
    assert(b.value == ~int64_t(0));
    check_classical(b);
    assert(rs.records().empty());
}

// ── left shift ────────────────────────────────────────────────────────────────

static void test_shl() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(1);
    qint b = a << 3;
    assert(b.value == 8);
    check_classical(b);
    assert(rs.records().empty());
}

static void test_shl_zero() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5);
    qint b = a << 0;
    assert(b.value == 5);
    check_classical(b);
    assert(rs.records().empty());
}

static void test_shl_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(1);
    a <<= 3;
    assert(a.value == 8);
    check_classical(a);
    assert(rs.records().empty());
}

// ── right shift ───────────────────────────────────────────────────────────────

static void test_shr() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(64);
    qint b = a >> 3;
    assert(b.value == 8);
    check_classical(b);
    assert(rs.records().empty());
}

static void test_shr_zero() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5);
    qint b = a >> 0;
    assert(b.value == 5);
    check_classical(b);
    assert(rs.records().empty());
}

static void test_shr_assign() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(64);
    a >>= 3;
    assert(a.value == 8);
    check_classical(a);
    assert(rs.records().empty());
}

// ── comparisons (classical) ───────────────────────────────────────────────────

static void test_eq_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5), b(5);
    sturm::qbool r = (a == b);
    assert(r.value == 1);
    assert(!(r.super_mask & 1));
    assert(r.qubits[0] == -1);
    assert(rs.records().empty());
}

static void test_neq_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5), b(6);
    sturm::qbool r = (a != b);
    assert(r.value == 1);
    assert(!(r.super_mask & 1));
    assert(rs.records().empty());
}

static void test_lt_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(3), b(5);
    sturm::qbool r = (a < b);
    assert(r.value == 1);
    assert(!(r.super_mask & 1));
    assert(rs.records().empty());
}

static void test_le_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5), b(5);
    sturm::qbool r = (a <= b);
    assert(r.value == 1);
    assert(!(r.super_mask & 1));
    assert(rs.records().empty());
}

static void test_gt_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(7), b(5);
    sturm::qbool r = (a > b);
    assert(r.value == 1);
    assert(!(r.super_mask & 1));
    assert(rs.records().empty());
}

static void test_ge_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint a(5), b(5);
    sturm::qbool r = (a >= b);
    assert(r.value == 1);
    assert(!(r.super_mask & 1));
    assert(rs.records().empty());
}

// ── bit subscript operator ────────────────────────────────────────────────────

static void test_subscript_classical() {
    qint a(0b101);  // bits 0 and 2 are set
    sturm::qbool b0 = a[0];
    sturm::qbool b1 = a[1];
    sturm::qbool b2 = a[2];
    assert(b0.value == 1);
    assert(b1.value == 0);
    assert(b2.value == 1);
    assert(!(b0.super_mask & 1));
}

// ── qbool conversion ──────────────────────────────────────────────────────────

static void test_from_qbool_classical() {
    sturm::qbool flag(true);
    qint q(flag);
    assert(q.value == 1);
    assert(q.super_mask == 0);
}

static void test_to_qbool_classical() {
    qint a(0xFF);
    sturm::qbool b = static_cast<sturm::qbool>(a);
    assert(b.value == 1);
    assert(!(b.super_mask & 1));
}

// ── assignment ────────────────────────────────────────────────────────────────

static void test_assign_int() {
    qint a;
    a = 77;
    assert(a.value == 77);
    check_classical(a);
}

static void test_assign_qint() {
    qint a(10), b;
    b = a;
    assert(b.value == 10);
    check_classical(b);
}

// ── width parameterization ────────────────────────────────────────────────────

static void test_width8() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qint_t<8> a(5), b(3);
    auto c = a + b;
    assert(c.value == 8);
    assert(c.super_mask == 0);
    for (int i = 0; i < 8; ++i) assert(c.qubits[i] == -1);
    assert(rs.records().empty());
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_default_ctor();
    test_int_ctor();
    test_int64_conversion();
    test_add();
    test_add_assign();
    test_sub();
    test_sub_assign();
    test_unary_neg();
    test_mul();
    test_mul_assign();
    test_div();
    test_div_assign();
    test_mod();
    test_mod_assign();
    test_pow();
    test_pow_int();
    test_and();
    test_and_assign();
    test_or();
    test_or_assign();
    test_xor();
    test_xor_assign();
    test_not();
    test_shl();
    test_shl_zero();
    test_shl_assign();
    test_shr();
    test_shr_zero();
    test_shr_assign();
    test_eq_classical();
    test_neq_classical();
    test_lt_classical();
    test_le_classical();
    test_gt_classical();
    test_ge_classical();
    test_subscript_classical();
    test_from_qbool_classical();
    test_to_qbool_classical();
    test_assign_int();
    test_assign_qint();
    test_width8();
    return 0;
}
