// test_sink_dispatch.cpp — Step 8: End-to-end sink dispatch tests
// (spec §8, Implementation Plan §8)
//
// Covers:
//   1. quantum_add: qint += qint with one super bit → record, control==-1,
//      mask widened from lowest super bit upward.
//   2. quantum_add inside WHEN(qbool(0.5)) → record control equals flag qubit.
//   3. quantum_mul with one super bit → record, output mask is all-ones (~0ULL).
//   4. quantum_xor with disjoint super bits → record, mask == exact OR of operand masks.
//   5. quantum_eq with super inputs → record, result qbool.is_super==true, qubit allocated.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/control/when.hpp"

#include <cassert>
#include <cstdio>

// sturm-1os7 (D0 re-spell miss): bare `qint` here used to alias
// sturm::qint_t<64> (pre-B1). Post-B1 (sturm-65rs.6) sturm::qint resolves
// to sturm::frontend::qint, which lacks the backend surface this file
// exercises (super_mask, qubits[], compound-assigns +=, ^=). PRD §6 R1
// mechanical migration recipe: introduce a local type-alias `using qint =
// sturm::qint_t<64>;` so every site below keeps its backend semantics
// with a one-line diff. Drift back to bare sturm::qint is pinned by
// tests/regressions/test_qint_callsite_respelling_*.cpp (per-surface split).
using qint = sturm::qint_t<64>;
using sturm::qbool;
using sturm::RecordingSink;
using sturm::ScopedSink;
using sturm::QubitPool;

// ── Helper: build a qint with one super bit pre-allocated ────────────────────

static qint make_super(int64_t val, uint64_t mask, int bit_pos) {
    qint q(val);
    q.super_mask = mask;
    q.qubits[bit_pos] = QubitPool::instance().allocate();
    return q;
}

// ── Test 1: quantum_add, no WHEN, control == -1 ──────────────────────────────
// qint a with super bit 2 (mask=0x4), b classical.
// After a += b:
//   • exactly one record with op=="quantum_add"
//   • record.control == -1
//   • a.super_mask == mask_addsub(0x4, 0) = (0x4 | (~0ULL << 2))

static void test_add_no_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(3, 0x4, 2);
    qint b(1);

    a += b;

    // Exactly one quantum_add record
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_add");

    // Control is -1 (no WHEN guard active)
    assert(rs.records()[0].control == -1);

    // Mask widened from bit 2 upward
    const uint64_t expected = (0x4ULL | (~0ULL << 2));
    assert(a.super_mask == expected);

    std::puts("PASS: test_add_no_control");
}

// ── Test 2: quantum_add inside WHEN — control == flag qubit ──────────────────
// Same operation wrapped in WHEN(qbool(0.5)).
// The record's control field must equal flag.qubits[0].

static void test_add_with_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool flag(0.5);   // superposed; qubit allocated; prepare() emitted
    assert(flag.super_mask & 1);
    int flag_qubit = flag.qubits[0];
    assert(flag_qubit >= 0);

    // Clear the prepare() record so we only see the add record below.
    rs.clear();

    qint a = make_super(3, 0x4, 2);
    qint b(1);

    WHEN(flag) {
        a += b;
    }

    // Exactly one quantum_add record
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_add");

    // Control must be the flag's qubit index
    assert(rs.records()[0].control == flag_qubit);

    std::puts("PASS: test_add_with_control");
}

// ── Test 3: quantum_mul — output mask is all-ones ─────────────────────────────
// Any superposed bit in either operand → mask_muldiv → ~0ULL for Width==64.

static void test_mul_mask_allones() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(3, 0x4, 2);
    qint b(2);

    qint c = a * b;

    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_mul");
    assert(rs.records()[0].control == -1);

    // mul mask saturation: any bit set → all-ones
    assert(c.super_mask == ~0ULL);

    std::puts("PASS: test_mul_mask_allones");
}

// ── Test 4: quantum_xor — disjoint super bits, mask == OR ────────────────────
// a has bit 1 super (mask=0x2), b has bit 3 super (mask=0x8).
// Result mask must be 0x2 | 0x8 == 0xA.

static void test_xor_disjoint_masks() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(0b0010, 0x2, 1);
    qint b = make_super(0b1000, 0x8, 3);

    qint c = a ^ b;

    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_xor");
    assert(rs.records()[0].control == -1);

    // Mask is exact OR of operand masks
    assert(c.super_mask == (0x2ULL | 0x8ULL));

    // Output qubits allocated at both super positions
    assert(c.qubits[1] >= 0);
    assert(c.qubits[3] >= 0);

    std::puts("PASS: test_xor_disjoint_masks");
}

// ── Test 5: quantum_eq — result is superposed qbool with allocated qubit ─────
// a has a super bit; b is classical.
// (a == b) → qbool with is_super==true, qubits[0]>=0, one quantum_eq record.

static void test_eq_super_result() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint a = make_super(5, 0x4, 2);
    qint b(5);

    qbool r = (a == b);

    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_eq");
    assert(rs.records()[0].control == -1);

    // Result qbool carries superposition from the inputs
    assert(r.super_mask & 1);

    // A result qubit was allocated
    assert(r.qubits[0] >= 0);

    std::puts("PASS: test_eq_super_result");
}

// ── Test 6: quantum_xor inside WHEN — control == flag qubit ──────────────────
// Extra scenario: verify control routing for a bitwise op, not just arithmetic.

static void test_xor_with_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool flag(0.5);
    assert(flag.super_mask & 1);
    int flag_qubit = flag.qubits[0];
    rs.clear();   // discard prepare() record

    qint a = make_super(0b0010, 0x2, 1);
    qint b(0b0010);

    WHEN(flag) {
        a ^= b;
    }

    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_xor");
    assert(rs.records()[0].control == flag_qubit);

    std::puts("PASS: test_xor_with_control");
}

// ── Test 7: quantum_mul inside WHEN — control propagated ─────────────────────

static void test_mul_with_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool flag(0.5);
    int flag_qubit = flag.qubits[0];
    rs.clear();

    qint a = make_super(3, 0x4, 2);
    qint b(2);

    qint c;
    WHEN(flag) {
        c = a * b;
    }

    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_mul");
    assert(rs.records()[0].control == flag_qubit);

    std::puts("PASS: test_mul_with_control");
}

// ── Test 8: quantum_eq inside WHEN — control propagated ──────────────────────

static void test_eq_with_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool flag(0.5);
    int flag_qubit = flag.qubits[0];
    rs.clear();

    qint a = make_super(5, 0x4, 2);
    qint b(5);

    qbool result;
    WHEN(flag) {
        result = (a == b);
    }

    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "quantum_eq");
    assert(rs.records()[0].control == flag_qubit);

    std::puts("PASS: test_eq_with_control");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_add_no_control();
    test_add_with_control();
    test_mul_mask_allones();
    test_xor_disjoint_masks();
    test_eq_super_result();
    test_xor_with_control();
    test_mul_with_control();
    test_eq_with_control();
    std::puts("All test_sink_dispatch tests passed.");
    return 0;
}
