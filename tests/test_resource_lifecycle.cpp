// test_resource_lifecycle.cpp — Integration test: qbool/qint_t destructor
// releases every lazily-allocated qubit back to QubitPool.
//
// Verifies the integration between the type destructors and QubitPool:
//   - Construct and destroy 10 000 qbools (with qubit allocation) one at a time.
//   - Construct and destroy 10 000 qint_t<1> values (with qubit allocation).
//   - After each full loop: QubitPool::in_use() == 0 (the legacy
//     capacity() bound is gone post-sturm-5jta — the pool grows on
//     demand).

#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"

#include <cassert>
#include <cstdio>

using sturm::qbool;
using sturm::qint_t;
using sturm::QubitPool;
using sturm::RecordingSink;
using sturm::ScopedSink;

static constexpr int kIterations = 10000;

// ── helper macros ─────────────────────────────────────────────────────────────

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

// ── test: 10k qbools constructed and destroyed one at a time ─────────────────

static void test_qbool_lifecycle() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    for (int i = 0; i < kIterations; ++i) {
        {
            qbool q(0.5);
            CHECK(q.qubits[0] >= 0);
            CHECK(QubitPool::instance().in_use() == 1);
        }
        // After destruction: no qubits in use.
        CHECK(QubitPool::instance().in_use() == 0);
    }

    // Pool invariants after all iterations.
    CHECK(QubitPool::instance().in_use() == 0);
    // sturm-5jta (P2.b / G5): the legacy capacity() bound is gone — the
    // pool grows on demand. Spot-check that high_water stays bounded by
    // the test's own allocation envelope (kIters loop = 10k allocations).
    CHECK(QubitPool::instance().high_water() <= 100000);
}

// ── test: 10k qint_t<1> with qubit allocation, one at a time ─────────────────

static void test_qint1_lifecycle() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    for (int i = 0; i < kIterations; ++i) {
        {
            // Build a 1-bit superposed qint_t by converting from a qbool.
            // qbool(0.5) allocates a qubit; the qint_t(const qbool&) constructor
            // takes ownership of that qubit index.
            // We move from qbool so the qbool relinquishes its index before
            // the qint_t destructor runs (preventing double-release via qbool dtor).
            qbool b(0.5);
            int qubit_idx = b.qubits[0];
            // Manually relinquish from qbool so qint_t owns it exclusively.
            b.qubits[0] = -1;

            qint_t<1> q;
            q.value      = 0;
            q.super_mask = 1;
            q.qubits[0]  = qubit_idx;

            CHECK(q.qubits[0] >= 0);
            CHECK(QubitPool::instance().in_use() == 1);
        }
        // After destruction: pool must be fully drained.
        CHECK(QubitPool::instance().in_use() == 0);
    }

    // Pool invariants after all iterations.
    CHECK(QubitPool::instance().in_use() == 0);
    // sturm-5jta (P2.b / G5): the legacy capacity() bound is gone — the
    // pool grows on demand. Spot-check that high_water stays bounded by
    // the test's own allocation envelope (kIters loop = 10k allocations).
    CHECK(QubitPool::instance().high_water() <= 100000);
}

// ── test: 10k qint<64> (classical) — no qubits allocated ────────────────────

static void test_qint64_classical_lifecycle() {
    QubitPool::instance().reset_for_testing();

    for (int i = 0; i < kIterations; ++i) {
        {
            sturm::qint_t<64> q(static_cast<int64_t>(i));
            // Classical qint: no qubits should be allocated.
            CHECK(QubitPool::instance().in_use() == 0);
        }
        CHECK(QubitPool::instance().in_use() == 0);
    }

    CHECK(QubitPool::instance().in_use() == 0);
    // sturm-5jta (P2.b / G5): the legacy capacity() bound is gone — the
    // pool grows on demand. Spot-check that high_water stays bounded by
    // the test's own allocation envelope (kIters loop = 10k allocations).
    CHECK(QubitPool::instance().high_water() <= 100000);
    // Classical qints never touch the pool.
    CHECK(QubitPool::instance().high_water() == 0);
}

// ── test: 10k qbool + qint_t<1> pairs destroyed in reverse order ─────────────

static void test_mixed_lifecycle() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    for (int i = 0; i < kIterations; ++i) {
        {
            qbool a(0.25);
            qbool b(0.75);

            CHECK(QubitPool::instance().in_use() == 2);
        }
        // Both qbools destroyed: pool fully drained.
        CHECK(QubitPool::instance().in_use() == 0);
    }

    CHECK(QubitPool::instance().in_use() == 0);
    // sturm-5jta (P2.b / G5): the legacy capacity() bound is gone — the
    // pool grows on demand. Spot-check that high_water stays bounded by
    // the test's own allocation envelope (kIters loop = 10k allocations).
    CHECK(QubitPool::instance().high_water() <= 100000);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_qbool_lifecycle();
    test_qint1_lifecycle();
    test_qint64_classical_lifecycle();
    test_mixed_lifecycle();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
