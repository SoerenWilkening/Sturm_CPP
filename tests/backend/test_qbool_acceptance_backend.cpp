// test_qbool_acceptance_backend.cpp — M14: Final acceptance tests (backend).
//
// Maps to PRD §8 acceptance criteria requiring backend context:
//   AC1 — qbool(0.5).phi() += 1.0 compiles and emits an RZ gate via the sink.
//   AC2 — qbool(0.5).theta() += 1.0 compiles and emits an RY gate via the sink.
//   AC6 — WHEN(qbool_var) works: classical true (body runs), classical false
//          (body skipped), and superposed (quantum control set).
//
// Phase K PK-4 (sturm-l6jq): AC8 (operator~ destructor-emitted X gate) has been
// retired. Principle B10 codifies destructors as release-only; inverse gate
// emission for `~q` lives in the transpile path, which is exercised by the
// gate-equivalence regression suite.

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"

#include <cassert>
#include <cmath>
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

// ── AC1: phi() on qbool emits RZ gate (via backend IR) ────────────────────��──
//
// qbool(0.5) allocates a qubit and enters superposition.
// q.phi() += 1.0 must emit an RZ gate via emit_RZ_lifted (backend path).
// With a backend context active (STURM_BACKEND_ENABLED), the proxy routes to
// the backend IR rather than to the frontend sink.
static void test_ac1_phi_on_qbool_emits_rz() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // qbool(0.5): allocate qubit, prepare() called (emitted to IR, not sink).
    qbool q(0.5);
    CHECK((q.super_mask & 1) == 1u);
    CHECK(q.qubits[0] >= 0);

    // phi() += 1.0 must emit one RZ gate to the backend IR.
    size_t before = sc.ir().size();
    q.phi() += 1.0;
    size_t after = sc.ir().size();

    CHECK(after - before == 1u);
    if (after > before) {
        const auto& rec = sc.ir().at(before);
        CHECK(rec.kind == STURM_GATE_RZ);
        CHECK(rec.n == 1u);
        CHECK(rec.qubits[0] == static_cast<uint32_t>(q.qubits[0]));
        CHECK(std::abs(rec.param - 1.0) < 1e-12);
    }

    std::printf("PASS: AC1 phi() on qbool emits phi_add (RZ)\n");
}

// ── AC2: theta() on qbool emits RY gate (via backend IR) ─────────────────────
//
// Same pattern as AC1 but for theta (maps to RY in the backend).
static void test_ac2_theta_on_qbool_emits_ry() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    qbool q(0.5);
    CHECK((q.super_mask & 1) == 1u);
    CHECK(q.qubits[0] >= 0);

    // theta() += 1.0 must emit one RY gate to the backend IR.
    size_t before = sc.ir().size();
    q.theta() += 1.0;
    size_t after = sc.ir().size();

    CHECK(after - before == 1u);
    if (after > before) {
        const auto& rec = sc.ir().at(before);
        CHECK(rec.kind == STURM_GATE_RY);
        CHECK(rec.n == 1u);
        CHECK(rec.qubits[0] == static_cast<uint32_t>(q.qubits[0]));
        CHECK(std::abs(rec.param - 1.0) < 1e-12);
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

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    test_ac1_phi_on_qbool_emits_rz();
    test_ac2_theta_on_qbool_emits_ry();
    test_ac6_when_classical_true();
    test_ac6_when_classical_false();
    test_ac6_when_superposed();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
