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

// sturm-ztmf (D0 re-spell miss): bare `qint` here used to alias
// sturm::qint_t<64> (pre-B1). Post-B1 (sturm-65rs.6) sturm::qint resolves
// to sturm::frontend::qint, which lacks the backend surface this file
// exercises (super_mask, qubits[], phi(), theta()). PRD §6 R1 mechanical
// migration recipe: introduce a local type-alias `using qint =
// sturm::qint_t<64>;` so every site below keeps its backend semantics with
// a one-line diff. Drift back to bare sturm::qint is pinned by
// tests/regressions/test_qint_callsite_respelling_phi_theta.cpp
// (sturm-1os7 split the original drift-gate per backend-surface family).
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

// ── Test 7: rotation on qbool with no allocated qubit auto-promotes ──────────
// M17: Use qbool (qint_t<1> subclass) to verify auto-promotion + sink records.
// A classical qbool(true) has value=1, super_mask=0, qubits[0]==-1.
// phi() += should allocate a qubit, set super_mask, emit prepare() (because
// value bit 0 is 1), and then emit phi_add.  theta() += on the same already-
// promoted qbool must emit exactly 1 theta_add record (no re-promotion).

static void test_rotation_no_qubits_no_emission() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Classical qbool(true): value=1, super_mask=0, qubits[0]==-1.
    qbool c(true);
    assert(c.super_mask == 0 && "pre-condition: classical qbool has super_mask==0");
    assert(c.qubits[0] < 0    && "pre-condition: classical qbool has no qubit");

    c.phi() += 0.1;

    // After auto-promotion: super_mask must be 1 (bit 0 promoted).
    assert((c.super_mask & 1) != 0 && "auto-promotion must set super_mask bit 0");
    assert(c.qubits[0] >= 0         && "auto-promotion must allocate qubit");

    // Records: prepare(qubit, 1.0) because value bit 0 == 1,
    //          then phi_add(qubit, 0.1, -1).
    assert(rs.records().size() == 2 && "phi() += on qbool(true) must emit prepare + phi_add");
    assert(rs.records()[0].op == "prepare" && "first record must be prepare");
    assert(rs.records()[0].scalars[0] == 1.0 && "prepare probability must be 1.0");
    assert(rs.records()[1].op == "phi_add"   && "second record must be phi_add");
    assert(rs.records()[1].scalars[0] == 0.1 && "phi_add delta must equal 0.1");

    rs.clear();

    // theta() += on already-promoted qbool: qubit already allocated,
    // no re-promotion → exactly 1 theta_add record.
    c.theta() += 0.2;
    assert(rs.records().size() == 1 && "theta() += on promoted qbool must emit 1 theta_add");
    assert(rs.records()[0].op == "theta_add" && "record must be theta_add");
    assert(rs.records()[0].scalars[0] == 0.2 && "theta_add delta must equal 0.2");

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
