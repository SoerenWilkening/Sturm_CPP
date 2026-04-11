// test_phase_amp.cpp — Step 10: Phase / amplitude proxy tests
// (Implementation Plan §10, PRD §7)
//
// Covers:
//   1. q.phi() += delta  → RecordingSink sees one phi_add(qubit, delta, -1).
//   2. q.theta() += delta → RecordingSink sees one theta_add(qubit, delta, -1).
//   3. q.phi() -= delta   → RecordingSink sees phi_add(qubit, -delta, -1).
//   4. q.theta() -= delta → RecordingSink sees theta_add(qubit, -delta, -1).
//   5. WHEN(qbool(0.5)) { q.phi() += delta }
//      → phi_add control arg equals flag.qubits[0].
//   6. WHEN(qbool(0.5)) { q.theta() += delta }
//      → theta_add control arg equals flag.qubits[0].
//   7. Rotations do NOT widen super_mask.
//   8. Rotation on a qint with no allocated qubits emits nothing (no crash).
//   9. Multiple allocated qubits → one phi_add record per qubit, all same control.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/control/when.hpp"

#include <cassert>
#include <cstdio>

using sturm::qint;
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

// ── Test 1: phi() += delta, no WHEN, control == -1 ──────────────────────────

static void test_phi_add_no_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q = make_super(1, 0x1, 0);
    const int expected_qubit = q.qubits[0];
    const uint64_t mask_before = q.super_mask;

    q.phi() += 0.25;

    // Exactly one phi_add record
    assert(rs.records().size() == 1);
    const auto& r = rs.records()[0];
    assert(r.op == "phi_add");

    // Correct qubit index
    assert(r.qubit_groups.size() == 1);
    assert(r.qubit_groups[0].size() == 1);
    assert(r.qubit_groups[0][0] == expected_qubit);

    // Correct scalar (delta)
    assert(r.scalars.size() == 1);
    assert(r.scalars[0] == 0.25);

    // No control
    assert(r.control == -1);

    // super_mask NOT widened by rotation
    assert(q.super_mask == mask_before);

    std::puts("PASS: test_phi_add_no_control");
}

// ── Test 2: theta() += delta, no WHEN, control == -1 ────────────────────────

static void test_theta_add_no_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q = make_super(1, 0x1, 0);
    const int expected_qubit = q.qubits[0];
    const uint64_t mask_before = q.super_mask;

    q.theta() += 0.5;

    assert(rs.records().size() == 1);
    const auto& r = rs.records()[0];
    assert(r.op == "theta_add");
    assert(r.qubit_groups[0][0] == expected_qubit);
    assert(r.scalars[0] == 0.5);
    assert(r.control == -1);

    // super_mask NOT widened
    assert(q.super_mask == mask_before);

    std::puts("PASS: test_theta_add_no_control");
}

// ── Test 3: phi() -= delta → phi_add(qubit, -delta, -1) ─────────────────────

static void test_phi_sub_no_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q = make_super(1, 0x1, 0);

    q.phi() -= 0.125;

    assert(rs.records().size() == 1);
    const auto& r = rs.records()[0];
    assert(r.op == "phi_add");
    assert(r.scalars[0] == -0.125);
    assert(r.control == -1);

    std::puts("PASS: test_phi_sub_no_control");
}

// ── Test 4: theta() -= delta → theta_add(qubit, -delta, -1) ─────────────────

static void test_theta_sub_no_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q = make_super(1, 0x1, 0);

    q.theta() -= 0.75;

    assert(rs.records().size() == 1);
    const auto& r = rs.records()[0];
    assert(r.op == "theta_add");
    assert(r.scalars[0] == -0.75);
    assert(r.control == -1);

    std::puts("PASS: test_theta_sub_no_control");
}

// ── Test 5: phi() += delta inside WHEN — control == flag qubit ───────────────

static void test_phi_add_with_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool flag(0.5);   // superposed; qubit allocated; prepare() emitted
    assert(flag.super_mask & 1);
    const int flag_qubit = flag.qubits[0];
    assert(flag_qubit >= 0);

    rs.clear();   // discard the prepare() record

    qint q = make_super(1, 0x1, 0);
    const int q_qubit = q.qubits[0];

    WHEN(flag) {
        q.phi() += 0.25;
    }

    assert(rs.records().size() == 1);
    const auto& r = rs.records()[0];
    assert(r.op == "phi_add");
    assert(r.qubit_groups[0][0] == q_qubit);
    assert(r.scalars[0] == 0.25);

    // Control must be routed to the flag's qubit
    assert(r.control == flag_qubit);

    std::puts("PASS: test_phi_add_with_control");
}

// ── Test 6: theta() += delta inside WHEN — control == flag qubit ─────────────

static void test_theta_add_with_control() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool flag(0.5);
    assert(flag.super_mask & 1);
    const int flag_qubit = flag.qubits[0];
    rs.clear();

    qint q = make_super(1, 0x1, 0);
    const int q_qubit = q.qubits[0];

    WHEN(flag) {
        q.theta() += 0.5;
    }

    assert(rs.records().size() == 1);
    const auto& r = rs.records()[0];
    assert(r.op == "theta_add");
    assert(r.qubit_groups[0][0] == q_qubit);
    assert(r.scalars[0] == 0.5);
    assert(r.control == flag_qubit);

    std::puts("PASS: test_theta_add_with_control");
}

// ── Test 7: rotation on qint with no allocated qubits auto-promotes ───────────
// M15: PhiProxy/ThetaProxy auto-promote classical bits before emitting gates.
// A fully classical qint(42) should have its bits allocated on phi() +=,
// with X gates emitted for bits that are 1 in the classical value, and then
// rotation records for all 64 bits.

static void test_rotation_no_qubits_no_emission() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Fully classical qint — no qubits allocated (all == -1)
    qint q(0);   // value=0: no bits set, so no X gates, just rotation gates
    assert(q.super_mask == 0);

    q.phi() += 0.1;

    // After auto-promotion: super_mask has all 64 bits set, records are non-empty
    assert(q.super_mask != 0 && "auto-promotion must set super_mask");

    // 64 phi_add records (one per bit in qint_t<64>)
    // No X gates because value == 0 (no classical-1 bits to initialize)
    assert(!rs.records().empty() && "phi() += on classical qint must emit records after auto-promotion");
    for (const auto& r : rs.records()) {
        assert(r.op == "phi_add" && "all records should be phi_add (value==0, no X gates)");
    }
    assert(rs.records().size() == 64 && "one phi_add per bit position");

    rs.clear();

    // theta() += on already-promoted qint emits 64 theta_add records (all qubits allocated)
    q.theta() += 0.2;
    assert(rs.records().size() == 64 && "one theta_add per bit position");

    std::puts("PASS: test_rotation_no_qubits_no_emission");
}

// ── Test 8: theta() += on fresh classical qint auto-promotes (sink path) ──────
// M16: ThetaProxy::operator+= must allocate qubits and set super_mask for
// unallocated (classical) bits before emitting theta_add records (sink path).
// Start from a *fresh* classical qint (phi never called), call theta() +=,
// and verify super_mask is set and theta_add records are emitted.

static void test_theta_rotation_fresh_classical_qint() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Fully classical qint — no qubits allocated (all == -1), phi never called
    qint q(0);
    assert(q.super_mask == 0 && "pre-condition: fresh classical qint has super_mask==0");

    q.theta() += 0.3;

    // After auto-promotion: super_mask is non-zero
    assert(q.super_mask != 0 && "theta() += on fresh classical qint must set super_mask");

    // 64 theta_add records (one per bit in qint_t<64>)
    // value==0 → no prepare() calls, only theta_add records
    assert(!rs.records().empty() && "theta() += on fresh classical qint must emit records");
    for (const auto& r : rs.records()) {
        assert(r.op == "theta_add" && "all records must be theta_add (value==0, no prepare calls)");
    }
    assert(rs.records().size() == 64 && "one theta_add per bit position in qint_t<64>");

    std::puts("PASS: test_theta_rotation_fresh_classical_qint");
}

// ── Test 9 (old 8): multiple allocated qubits → one record per qubit ─────────
// qint with two super bits → phi() += delta emits two phi_add records,
// one per allocated qubit, both with control == -1.

static void test_phi_add_multi_qubits() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint q(0b101);
    q.super_mask   = 0x5;   // bits 0 and 2
    q.qubits[0] = QubitPool::instance().allocate();
    q.qubits[2] = QubitPool::instance().allocate();

    const int q0 = q.qubits[0];
    const int q2 = q.qubits[2];

    q.phi() += 0.3;

    // Two phi_add records (one per allocated qubit)
    assert(rs.records().size() == 2);
    assert(rs.records()[0].op == "phi_add");
    assert(rs.records()[1].op == "phi_add");

    // Qubit indices appear in order (bit 0 first, bit 2 second)
    assert(rs.records()[0].qubit_groups[0][0] == q0);
    assert(rs.records()[1].qubit_groups[0][0] == q2);

    // Same delta, same (absent) control
    assert(rs.records()[0].scalars[0] == 0.3);
    assert(rs.records()[1].scalars[0] == 0.3);
    assert(rs.records()[0].control == -1);
    assert(rs.records()[1].control == -1);

    // super_mask unchanged
    assert(q.super_mask == 0x5);

    std::puts("PASS: test_phi_add_multi_qubits");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_phi_add_no_control();
    test_theta_add_no_control();
    test_phi_sub_no_control();
    test_theta_sub_no_control();
    test_phi_add_with_control();
    test_theta_add_with_control();
    test_rotation_no_qubits_no_emission();
    test_theta_rotation_fresh_classical_qint();
    test_phi_add_multi_qubits();
    std::puts("All test_phase_amp tests passed.");
    return 0;
}
