// test_conversions.cpp — Step 9: Conversion test sweep
// (Implementation Plan §9, PRD §6 conversion table)
//
// Covers all six rows of the PRD §6 conversion table:
//   Row 1: int64_t → qint  (implicit)
//   Row 2: qint    → int64_t (explicit)
//   Row 3: bool    → qbool  (implicit)
//   Row 4: qbool   → bool   (explicit)
//   Row 5: qbool   → qint   (implicit, zero-extend bit 0)
//   Row 6: qint    → qbool  (explicit, keep bit 0 only)
//
// Extra cases from the plan:
//   - qint(qbool(0.5)):  mask & 1 == 1, shared qubit index
//   - qbool(qint(0xFF)) explicit narrow: value==true, is_super==false,
//     lowest qubit shared
//   - No unexpected sink calls during conversion-only paths
//     (RecordingSink::records() must be empty except for the prepare()
//     emitted by qbool(double) itself)

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

// sturm-1os7 (D0 re-spell miss): bare `qint` here used to alias
// sturm::qint_t<64> (pre-B1). Post-B1 (sturm-65rs.6) sturm::qint resolves
// to sturm::frontend::qint, which lacks the backend surface this file
// exercises (q.value, q.super_mask, q.qubits[]). PRD §6 R1 mechanical
// migration recipe: introduce a local type-alias `using qint =
// sturm::qint_t<64>;` so every site below keeps its backend semantics
// with a one-line diff. Drift back to bare sturm::qint is pinned by
// tests/regressions/test_qint_callsite_respelling_*.cpp (per-surface split).
using qint = sturm::qint_t<64>;
using sturm::qbool;
using sturm::RecordingSink;
using sturm::ScopedSink;
using sturm::QubitPool;

// ── Row 1: int64_t → qint  (implicit) ────────────────────────────────────────
// Semantics: value=src, mask=0, no qubits, no emissions.

static void test_int64_to_qint_implicit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Implicit conversion
    qint q = static_cast<int64_t>(42);

    assert(q.value      == 42);
    assert(q.super_mask == 0);
    // All qubit slots must be unallocated
    for (int idx : q.qubits) {
        assert(idx == -1);
    }
    // No sink calls
    assert(rs.records().empty());

    // Also test direct int64_t constructor path
    qint q2(static_cast<int64_t>(-7));
    assert(q2.value      == -7);
    assert(q2.super_mask == 0);
    assert(rs.records().empty());

    // Assigning int64_t to an existing qint
    qint q3;
    q3 = static_cast<int64_t>(100);
    assert(q3.value      == 100);
    assert(q3.super_mask == 0);
    assert(rs.records().empty());

    std::puts("PASS: test_int64_to_qint_implicit");
}

// ── Row 2: qint → int64_t  (explicit) ────────────────────────────────────────
// Semantics: returns value field as-is (stub measurement; superposed bits
//            ignored — user's responsibility).

static void test_qint_to_int64_explicit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q(static_cast<int64_t>(99));
    int64_t v = static_cast<int64_t>(q);
    assert(v == 99);

    // Works on negative values too
    qint q2(static_cast<int64_t>(-3));
    assert(static_cast<int64_t>(q2) == -3);

    // Works on zero
    qint q3(static_cast<int64_t>(0));
    assert(static_cast<int64_t>(q3) == 0);

    // No sink calls during conversion
    assert(rs.records().empty());

    std::puts("PASS: test_qint_to_int64_explicit");
}

// ── Row 3: bool → qbool  (implicit) ──────────────────────────────────────────
// Semantics: value=src, is_super=false.

static void test_bool_to_qbool_implicit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool t = true;
    assert(t.value    == 1);
    assert(!(t.super_mask & 1));
    assert(t.qubits[0] == -1);

    qbool f = false;
    assert(f.value    == 0);
    assert(!(f.super_mask & 1));
    assert(f.qubits[0] == -1);

    // No sink calls
    assert(rs.records().empty());

    std::puts("PASS: test_bool_to_qbool_implicit");
}

// ── Row 4: qbool → bool  (explicit) ──────────────────────────────────────────
// Semantics: returns value field.

static void test_qbool_to_bool_explicit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool t(true);
    assert(static_cast<bool>(t) == true);

    qbool f(false);
    assert(static_cast<bool>(f) == false);

    // No sink calls
    assert(rs.records().empty());

    std::puts("PASS: test_qbool_to_bool_explicit");
}

// ── Row 5: qbool → qint  (implicit, zero-extend bit 0) ───────────────────────
// Semantics:
//   value     = b.value ? 1 : 0
//   super_mask has bit 0 set iff b.is_super
//   qubits[0] shares b.qubits[0]
//   bits 1–63 are classical 0 (qubits[-1], mask bits 0)

static void test_qbool_to_qint_implicit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Classical true qbool → qint 1
    {
        qbool b(true);
        qint q = b;   // implicit
        assert(q.value      == 1);
        assert(q.super_mask == 0);      // classical, not super
        assert(q.qubits[0]  == b.qubits[0]);  // -1 for classical
        // bits 1–63 are 0 and unallocated
        for (std::size_t i = 1; i < 64; ++i) {
            assert(q.qubits[i] == -1);
        }
    }

    // Classical false qbool → qint 0
    {
        qbool b(false);
        qint q = b;
        assert(q.value      == 0);
        assert(q.super_mask == 0);
        assert(q.qubits[0]  == -1);
    }

    assert(rs.records().empty());

    std::puts("PASS: test_qbool_to_qint_implicit");
}

// ── Row 6: qint → qbool  (explicit, keep bit 0 only) ─────────────────────────
// Semantics:
//   out.value    = (q.value & 1) != 0
//   out.is_super = (q.super_mask & 1) != 0
//   out.qubits[0]= q.qubits[0]
//   bits 1–63 discarded

static void test_qint_to_qbool_explicit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Odd value → true
    {
        qint q(static_cast<int64_t>(7));   // 0b111, bit 0 is 1
        qbool b = static_cast<qbool>(q);
        assert(b.value    == 1);
        assert(!(b.super_mask & 1));       // q.super_mask == 0
        assert(b.qubits[0] == -1);         // q.qubits[0] == -1
    }

    // Even value → false
    {
        qint q(static_cast<int64_t>(8));   // 0b1000, bit 0 is 0
        qbool b = static_cast<qbool>(q);
        assert(b.value    == 0);
        assert(!(b.super_mask & 1));
    }

    // Zero value → false
    {
        qint q(static_cast<int64_t>(0));
        qbool b = static_cast<qbool>(q);
        assert(b.value    == 0);
        assert(!(b.super_mask & 1));
    }

    assert(rs.records().empty());

    std::puts("PASS: test_qint_to_qbool_explicit");
}

// ── Extra: qint(qbool(0.5)) — superposed qbool to qint ───────────────────────
// Implementation Plan Step 9:
//   mask & 1 == 1 (bit 0 is super)
//   qubits[0] shares the original qbool's qubit

static void test_qint_from_super_qbool() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // qbool(0.5) allocates a qubit and emits prepare()
    qbool b(0.5);
    assert(b.super_mask & 1);
    assert(b.qubits[0] >= 0);
    int b_qubit = b.qubits[0];

    // Exactly one prepare() record so far
    assert(rs.records().size() == 1);
    assert(rs.records()[0].op == "prepare");

    // Implicit conversion qbool → qint
    qint q = b;

    assert(q.value      == 0);                 // b.value is false (default)
    assert((q.super_mask & 1ULL) == 1ULL);     // bit 0 is super
    assert(q.qubits[0]  == b_qubit);           // shared qubit index

    // bits 1–63 are unallocated and classical
    assert((q.super_mask >> 1) == 0ULL);
    for (std::size_t i = 1; i < 64; ++i) {
        assert(q.qubits[i] == -1);
    }

    // No extra sink calls beyond the single prepare()
    assert(rs.records().size() == 1);

    std::puts("PASS: test_qint_from_super_qbool");
}

// ── Extra: qbool(qint(0xFF)) — explicit narrow, classical ────────────────────
// Implementation Plan Step 9:
//   value == true (bit 0 of 0xFF is 1)
//   is_super == false (qint has no super mask)
//   lowest qubit shared (both -1 for classical, confirmed equal)

static void test_qbool_from_classical_qint() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q(static_cast<int64_t>(0xFF));
    qbool b = static_cast<qbool>(q);

    assert(b.value    == 1);       // bit 0 of 0xFF is 1
    assert(!(b.super_mask & 1));   // q.super_mask == 0
    // qubits[0] shares q.qubits[0]; both are -1 for classical
    assert(b.qubits[0] == q.qubits[0]);

    // No sink calls during conversion
    assert(rs.records().empty());

    std::puts("PASS: test_qbool_from_classical_qint");
}

// ── Extra: qint(0xFF) → qbool: superposed qint with bit 0 super ──────────────
// When the source qint has bit 0 in the super_mask, the result qbool must have
// is_super == true and the shared qubit index.

static void test_qbool_from_super_qint_bit0() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Build a qint with bit 0 superposed
    qint q(static_cast<int64_t>(1));
    q.super_mask = 1ULL;
    q.qubits[0]  = QubitPool::instance().allocate();
    int q_qubit  = q.qubits[0];

    qbool b = static_cast<qbool>(q);

    assert(b.value    == 1);      // (1 & 1) != 0
    assert(b.super_mask & 1);     // super_mask & 1 == 1
    assert(b.qubits[0] == q_qubit);

    // No unexpected sink calls
    assert(rs.records().empty());

    std::puts("PASS: test_qbool_from_super_qint_bit0");
}

// ── Extra: conversion does not call sink except for prepare() ─────────────────
// Converting between types must never emit quantum_add / quantum_xor / etc.
// Only qbool(double) is allowed to emit prepare().

static void test_no_spurious_sink_calls() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // All classical conversions
    qint  qi  = static_cast<int64_t>(5);
    int64_t v = static_cast<int64_t>(qi);
    (void)v;

    qbool  qb  = true;
    bool   bv  = static_cast<bool>(qb);
    (void)bv;

    qint  qi2 = qb;       // qbool → qint
    (void)qi2;

    qbool qb2 = static_cast<qbool>(qi);  // qint → qbool
    (void)qb2;

    // No records at all — not even prepare()
    assert(rs.records().empty());

    std::puts("PASS: test_no_spurious_sink_calls");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_int64_to_qint_implicit();
    test_qint_to_int64_explicit();
    test_bool_to_qbool_implicit();
    test_qbool_to_bool_explicit();
    test_qbool_to_qint_implicit();
    test_qint_to_qbool_explicit();
    test_qint_from_super_qbool();
    test_qbool_from_classical_qint();
    test_qbool_from_super_qint_bit0();
    test_no_spurious_sink_calls();
    std::puts("All test_conversions tests passed.");
    return 0;
}
