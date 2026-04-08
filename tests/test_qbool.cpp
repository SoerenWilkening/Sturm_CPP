// test_qbool.cpp — TDD tests for qbool (Step 3, spec §2)
// Tests:
//   - Default ctor: value==false, is_super==false, qubits[0]==-1
//   - qbool(true): implicit bool ctor, value==true
//   - qbool(0.5): is_super==true, qubits[0]>=0, RecordingSink saw prepare(qubit, 0.5)
//   - static_cast<bool> round trip

#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>

using namespace sturm;

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

// ── Individual test cases ─────────────────────────────────────────────────────

static void test_default_ctor() {
    QubitPool::instance().reset_for_testing();
    qbool q;
    CHECK(q.value     == false);
    CHECK(q.is_super  == false);
    CHECK(q.qubits[0] == -1);
    CHECK(QubitPool::instance().in_use() == 0);
}

static void test_bool_ctor_true() {
    QubitPool::instance().reset_for_testing();
    qbool q(true);
    CHECK(q.value     == true);
    CHECK(q.is_super  == false);
    CHECK(q.qubits[0] == -1);
}

static void test_bool_ctor_false() {
    QubitPool::instance().reset_for_testing();
    qbool q(false);
    CHECK(q.value     == false);
    CHECK(q.is_super  == false);
    CHECK(q.qubits[0] == -1);
}

static void test_implicit_bool_conversion() {
    // qbool(bool) must be an implicit conversion (not explicit)
    qbool q = true;   // implicit construction
    CHECK(q.value == true);
    qbool q2 = false;
    CHECK(q2.value == false);
}

static void test_prob_ctor_allocates_qubit() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool q(0.5);
    CHECK(q.is_super  == true);
    CHECK(q.qubits[0] >= 0);
    CHECK(QubitPool::instance().in_use() == 1);
}

static void test_prob_ctor_calls_prepare() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool q(0.5);

    const auto& recs = rs.records();
    CHECK(recs.size() == 1);
    if (!recs.empty()) {
        CHECK(recs[0].op == "prepare");
        CHECK(!recs[0].qubit_groups.empty());
        if (!recs[0].qubit_groups.empty()) {
            CHECK(!recs[0].qubit_groups[0].empty());
            if (!recs[0].qubit_groups[0].empty()) {
                CHECK(recs[0].qubit_groups[0][0] == q.qubits[0]);
            }
        }
        CHECK(!recs[0].scalars.empty());
        if (!recs[0].scalars.empty()) {
            CHECK(recs[0].scalars[0] == 0.5);
        }
    }
}

static void test_prob_ctor_different_probs() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool q1(0.25);
    qbool q2(0.75);

    CHECK(q1.is_super == true);
    CHECK(q2.is_super == true);
    CHECK(q1.qubits[0] >= 0);
    CHECK(q2.qubits[0] >= 0);
    CHECK(q1.qubits[0] != q2.qubits[0]);

    const auto& recs = rs.records();
    CHECK(recs.size() == 2);
    if (recs.size() >= 2) {
        CHECK(recs[0].scalars[0] == 0.25);
        CHECK(recs[1].scalars[0] == 0.75);
    }
}

static void test_explicit_bool_cast() {
    // static_cast<bool> round trip
    qbool qt(true);
    CHECK(static_cast<bool>(qt) == true);

    qbool qf(false);
    CHECK(static_cast<bool>(qf) == false);
}

static void test_explicit_bool_cast_super() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool q(0.5);
    // TODO(backend): real measurement; for now returns value (false by default)
    bool b = static_cast<bool>(q);
    CHECK(b == false);  // value is false (default for superposed)
}

static void test_ensure_qubit_classical() {
    // ensure_qubit on a classical false qbool should allocate a qubit
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool q(false);  // classical, no qubit
    CHECK(q.qubits[0] == -1);

    q.ensure_qubit();
    CHECK(q.qubits[0] >= 0);
    CHECK(QubitPool::instance().in_use() == 1);
}

static void test_ensure_qubit_already_allocated() {
    // ensure_qubit on already-allocated qbool must not double-allocate
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qbool q(0.5);
    int first_qubit = q.qubits[0];

    q.ensure_qubit();  // second call — must be a no-op
    CHECK(q.qubits[0] == first_qubit);
    CHECK(QubitPool::instance().in_use() == 1);
}

static void test_destructor_releases_qubit() {
    QubitPool::instance().reset_for_testing();
    {
        RecordingSink rs;
        ScopedSink scope(&rs);
        qbool q(0.5);
        CHECK(QubitPool::instance().in_use() == 1);
    }  // q destroyed here
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    test_default_ctor();
    test_bool_ctor_true();
    test_bool_ctor_false();
    test_implicit_bool_conversion();
    test_prob_ctor_allocates_qubit();
    test_prob_ctor_calls_prepare();
    test_prob_ctor_different_probs();
    test_explicit_bool_cast();
    test_explicit_bool_cast_super();
    test_ensure_qubit_classical();
    test_ensure_qubit_already_allocated();
    test_destructor_releases_qubit();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
