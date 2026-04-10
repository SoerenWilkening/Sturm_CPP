// test_qbool_unified.cpp — M6: Tests for qbool-as-subclass basics.
//
// Verifies that qbool inherits from qint_t<1> and that the inherited
// fields and constructors work correctly.
//
// Tests:
//   1.  qbool_is_subclass_of_qint1       — static_assert is_base_of
//   2.  qbool_sizeof_not_bloated          — sizeof(qbool) <= 56
//   3.  qbool_default_ctor               — value==0, super_mask==0, qubits[0]==-1, owning_==true
//   4.  qbool_bool_ctor_true             — qbool(true) → value==1, super_mask==0
//   5.  qbool_bool_ctor_false            — qbool(false) → value==0, super_mask==0
//   6.  qbool_has_phi                    — q.phi() compiles (inherited PhiProxy)
//   7.  qbool_has_theta                  — q.theta() compiles (inherited ThetaProxy)
//   8.  qbool_explicit_bool_cast         — static_cast<bool>(qbool(true)) == true
//   9.  qbool_make_non_owning            — owning_==false, correct qubit index
//   10. qbool_copy_does_not_share_qubits — copy has qubits[0]==-1
//   11. qbool_move_transfers_ownership   — destination owns, source cleared
//   12. qbool_implicit_from_bool         — qbool q = true; compiles

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/recording_sink.hpp"

#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace sturm;

// ── Compile-time check: qbool must be a subclass of qint_t<1> ─────────────────
static_assert(std::is_base_of_v<qint_t<1>, qbool>,
              "qbool must inherit from qint_t<1>");

// ── Compile-time check: sizeof(qbool) must not be bloated ─────────────────────
static_assert(sizeof(qbool) <= 56,
              "sizeof(qbool) <= 56 required (no new data members beyond base)");

// ── Test helpers ──────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                    \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while(0)

// ── Test 1: is_base_of (compile-time, already verified above) ─────────────────
static void test_qbool_is_subclass_of_qint1() {
    // The static_assert above covers this at compile time.
    // We add a runtime CHECK as a documentation anchor.
    CHECK((std::is_base_of_v<qint_t<1>, qbool>));
}

// ── Test 2: sizeof not bloated ────────────────────────────────────────────────
static void test_qbool_sizeof_not_bloated() {
    // static_assert above already enforces this; runtime check for reporting.
    CHECK(sizeof(qbool) <= 56);
}

// ── Test 3: default constructor ───────────────────────────────────────────────
static void test_qbool_default_ctor() {
    QubitPool::instance().reset_for_testing();
    qbool q;
    CHECK(q.value      == 0);
    CHECK(q.super_mask == 0);
    CHECK(q.qubits[0]  == -1);
    CHECK(q.owning_    == true);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 4: bool constructor (true) ──────────────────────────────────────────
static void test_qbool_bool_ctor_true() {
    QubitPool::instance().reset_for_testing();
    qbool q(true);
    CHECK(q.value      == 1);
    CHECK(q.super_mask == 0);
    CHECK(q.qubits[0]  == -1);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 5: bool constructor (false) ─────────────────────────────────────────
static void test_qbool_bool_ctor_false() {
    QubitPool::instance().reset_for_testing();
    qbool q(false);
    CHECK(q.value      == 0);
    CHECK(q.super_mask == 0);
    CHECK(q.qubits[0]  == -1);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 6: has phi (inherited PhiProxy) ─────────────────────────────────────
static void test_qbool_has_phi() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qbool q(true);
    // Compiling this line verifies the inherited phi() accessor exists.
    auto p = q.phi();
    // Suppress unused-variable warning.
    (void)p;
    // Verify that calling phi() += on an unallocated qubit is a no-op
    // (no sink records emitted since qubits[0] == -1).
    CHECK(rs.records().empty());
}

// ── Test 7: has theta (inherited ThetaProxy) ─────────────────────────────────
static void test_qbool_has_theta() {
    RecordingSink rs;
    ScopedSink scope(&rs);
    qbool q(true);
    auto t = q.theta();
    (void)t;
    CHECK(rs.records().empty());
}

// ── Test 8: explicit bool cast ────────────────────────────────────────────────
static void test_qbool_explicit_bool_cast() {
    qbool qt(true);
    CHECK(static_cast<bool>(qt) == true);

    qbool qf(false);
    CHECK(static_cast<bool>(qf) == false);
}

// ── Test 9: make_non_owning ───────────────────────────────────────────────────
static void test_qbool_make_non_owning() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Allocate one real qubit.
    int idx = QubitPool::instance().allocate();
    CHECK(idx >= 0);
    CHECK(QubitPool::instance().in_use() == 1);

    {
        qbool view = qbool::make_non_owning(idx);
        CHECK(view.owning_   == false);
        CHECK(view.qubits[0] == idx);
        // view goes out of scope here — must NOT release the qubit.
    }

    // Qubit should still be in use.
    CHECK(QubitPool::instance().in_use() == 1);

    // Release manually.
    QubitPool::instance().release(idx);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 10: copy does not share qubits ──────────────────────────────────────
static void test_qbool_copy_does_not_share_qubits() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool orig(true);
    // Simulate orig having an allocated qubit by using make_non_owning trick;
    // Actually allocate a qubit and assign it directly.
    int idx = QubitPool::instance().allocate();
    orig.qubits[0] = idx;
    orig.owning_   = true;

    // Copy-construct.
    qbool copy(orig);

    // Copy must NOT share the qubit index.
    CHECK(copy.qubits[0] == -1);
    // Copy should have the same value/super_mask.
    CHECK(copy.value      == orig.value);
    CHECK(copy.super_mask == orig.super_mask);
    // Pool still has only 1 qubit in use (orig's).
    CHECK(QubitPool::instance().in_use() == 1);
}

// ── Test 11: move transfers ownership ────────────────────────────────────────
static void test_qbool_move_transfers_ownership() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool src(true);
    int idx = QubitPool::instance().allocate();
    src.qubits[0] = idx;
    src.owning_   = true;

    // Move-construct.
    qbool dst(std::move(src));

    // Destination owns the qubit.
    CHECK(dst.owning_   == true);
    CHECK(dst.qubits[0] == idx);

    // Source no longer owns and has cleared qubit.
    CHECK(src.owning_   == false);
    CHECK(src.qubits[0] == -1);

    // Pool still has 1 qubit in use (dst holds it).
    CHECK(QubitPool::instance().in_use() == 1);
}

// ── Test 12: implicit construction from bool ──────────────────────────────────
static void test_qbool_implicit_from_bool() {
    // This must compile without an explicit cast — verifies the constructor
    // is NOT marked explicit.
    qbool q = true;
    CHECK(q.value == 1);

    qbool q2 = false;
    CHECK(q2.value == 0);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    test_qbool_is_subclass_of_qint1();
    test_qbool_sizeof_not_bloated();
    test_qbool_default_ctor();
    test_qbool_bool_ctor_true();
    test_qbool_bool_ctor_false();
    test_qbool_has_phi();
    test_qbool_has_theta();
    test_qbool_explicit_bool_cast();
    test_qbool_make_non_owning();
    test_qbool_copy_does_not_share_qubits();
    test_qbool_move_transfers_ownership();
    test_qbool_implicit_from_bool();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
