// test_qbool_acceptance_backend.cpp — M14: Final acceptance tests (backend).
//
// Maps to PRD §8 acceptance criteria requiring backend context:
//   AC1 — qbool(0.5).phi() += 1.0 compiles and emits an RZ gate via the sink.
//   AC2 — qbool(0.5).theta() += 1.0 compiles and emits an RY gate via the sink.
//   AC6 — WHEN(qbool_var) works: classical true (body runs), classical false
//          (body skipped), and superposed (quantum control set).
//   AC8 — operator~ uncomputes: X gate emitted on destruction of the result.

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

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

// ── Scoped backend context (APPEND mode) ─────────────────────────────────────
struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedAppendCtx(uint32_t max_q = 64u) {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, max_q);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    GateIR& ir() { return ctx->ir; }
};

// ── AC1: phi() on qbool emits phi_add (RZ gate) via the sink ─────────────────
//
// qbool(0.5) allocates a qubit and enters superposition.
// q.phi() += 1.0 must call current_sink()->phi_add(qubit, 1.0, ctrl).
// This maps to an RZ gate in the backend IR.
// We verify via RecordingSink that phi_add was recorded.
static void test_ac1_phi_on_qbool_emits_rz() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    RecordingSink rs;
    ScopedSink ss(&rs);

    // qbool(0.5): allocate qubit, prepare() called on sink.
    qbool q(0.5);
    CHECK((q.super_mask & 1) == 1u);
    CHECK(q.qubits[0] >= 0);

    // Clear the prepare() record — we only care about the phi_add below.
    rs.clear();

    // phi() += 1.0 must call current_sink()->phi_add(qubit, 1.0, -1).
    q.phi() += 1.0;

    CHECK(rs.records().size() == 1u);
    if (rs.records().size() >= 1u) {
        const auto& r = rs.records()[0];
        // "phi_add" corresponds to RZ in the backend gate taxonomy.
        CHECK(r.op == "phi_add");
        CHECK(!r.qubit_groups.empty() && !r.qubit_groups[0].empty());
        if (!r.qubit_groups.empty() && !r.qubit_groups[0].empty()) {
            CHECK(r.qubit_groups[0][0] == q.qubits[0]);
        }
        CHECK(!r.scalars.empty());
        if (!r.scalars.empty()) {
            CHECK(r.scalars[0] == 1.0);
        }
        // No quantum control (not inside a WHEN).
        CHECK(r.control == -1);
    }

    std::printf("PASS: AC1 phi() on qbool emits phi_add (RZ)\n");
}

// ── AC2: theta() on qbool emits theta_add (RY gate) via the sink ──────────────
//
// Same pattern as AC1 but for theta (maps to RY in the backend).
static void test_ac2_theta_on_qbool_emits_ry() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    RecordingSink rs;
    ScopedSink ss(&rs);

    qbool q(0.5);
    CHECK((q.super_mask & 1) == 1u);
    CHECK(q.qubits[0] >= 0);

    rs.clear();  // discard prepare() record

    // theta() += 1.0 must call current_sink()->theta_add(qubit, 1.0, -1).
    q.theta() += 1.0;

    CHECK(rs.records().size() == 1u);
    if (rs.records().size() >= 1u) {
        const auto& r = rs.records()[0];
        // "theta_add" corresponds to RY in the backend gate taxonomy.
        CHECK(r.op == "theta_add");
        CHECK(!r.qubit_groups.empty() && !r.qubit_groups[0].empty());
        if (!r.qubit_groups.empty() && !r.qubit_groups[0].empty()) {
            CHECK(r.qubit_groups[0][0] == q.qubits[0]);
        }
        CHECK(!r.scalars.empty());
        if (!r.scalars.empty()) {
            CHECK(r.scalars[0] == 1.0);
        }
        CHECK(r.control == -1);
    }

    std::printf("PASS: AC2 theta() on qbool emits theta_add (RY)\n");
}

// ── AC6: WHEN works — classical true, classical false, superposed ──────────────
//
// Three sub-cases from the PRD acceptance criterion:
//   (a) Classical true  → body executes.
//   (b) Classical false → body is skipped.
//   (c) Superposed      → body executes, WhenGuard::active_control() is non-null.

static void test_ac6_when_classical_true() {
    int counter = 0;
    qbool flag(true);  // classical true
    WHEN(flag) {
        ++counter;
    }
    CHECK(counter == 1);
    // After scope, active_control must be nullptr.
    CHECK(WhenGuard::active_control() == nullptr);
    std::printf("PASS: AC6 WHEN classical true\n");
}

static void test_ac6_when_classical_false() {
    int counter = 0;
    qbool flag(false);  // classical false
    WHEN(flag) {
        ++counter;
    }
    CHECK(counter == 0);
    CHECK(WhenGuard::active_control() == nullptr);
    std::printf("PASS: AC6 WHEN classical false\n");
}

static void test_ac6_when_superposed() {
    QubitPool::instance().reset_for_testing();

    RecordingSink rs;
    ScopedSink ss(&rs);

    int counter = 0;
    qbool* ctrl_inside = nullptr;

    qbool flag(0.5);  // superposed
    CHECK((flag.super_mask & 1) == 1u);
    CHECK(flag.qubits[0] >= 0);

    WHEN(flag) {
        ++counter;
        ctrl_inside = WhenGuard::active_control();
    }

    // Body must run.
    CHECK(counter == 1);
    // Inside WHEN, active_control() pointed to the flag.
    CHECK(ctrl_inside == &flag);
    // After scope, active_control must be nullptr.
    CHECK(WhenGuard::active_control() == nullptr);

    std::printf("PASS: AC6 WHEN superposed (quantum control)\n");
}

// ── AC8: operator~ uncomputes — X gate emitted on destruction ─────────────────
//
// ~q: allocates ancilla, emits X on ancilla (forward), stamps ADD_CONST(1) as
// the uncompute tag.  On destruction of the result, the destructor applies
// ADD_CONST(1) inverse which emits another X.
// Verify: IR shows 2 gates, both STURM_GATE_X.
static void test_ac8_operator_not_uncomputes() {
    ScopedAppendCtx sc;

    // Use a non-owning qbool at fixed qubit 0 to avoid pool interactions.
    qbool q = qbool::make_non_owning(0);
    q.super_mask = 1ULL;  // mark as quantum so ~q emits a gate

    {
        // ~q: forward X emitted, ADD_CONST(1) stamped for uncompute.
        qbool r = ~q;
        CHECK(r.qubits[0] >= 0);
        CHECK(sc.ir().size() == 1u);
        if (sc.ir().size() == 1u) {
            CHECK(sc.ir().at(0).kind == STURM_GATE_X);
        }
        // Destructor of r fires here: emits uncompute X (ADD_CONST(1) apply).
    }

    // After destruction: 2 total gates (forward X + uncompute X).
    CHECK(sc.ir().size() == 2u);
    if (sc.ir().size() >= 2u) {
        CHECK(sc.ir().at(1).kind == STURM_GATE_X);
    }

    std::printf("PASS: AC8 operator~ uncomputes (X gate emitted on destruction)\n");
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    test_ac1_phi_on_qbool_emits_rz();
    test_ac2_theta_on_qbool_emits_ry();
    test_ac6_when_classical_true();
    test_ac6_when_classical_false();
    test_ac6_when_superposed();
    test_ac8_operator_not_uncomputes();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
