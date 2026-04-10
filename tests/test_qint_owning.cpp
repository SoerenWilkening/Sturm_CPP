// test_qint_owning.cpp — TDD tests for owning_ flag on qint_t<W> (M1/M2)
//
// Tests:
//   1. qint_t_default_is_owning           — default ctor has owning_ == true
//   2. qint_t_make_non_owning_flag        — make_non_owning returns owning_ == false
//   3. qint_t_non_owning_does_not_release — non-owning view destruction leaves
//                                           original qubits valid in pool
//   4. qint_t_owning_does_release        — owning qint_t releases qubits on dtor
//   5. qint_t_move_transfers_ownership   — move-ctor: dst owning, src non-owning
//   6. qint_t_copy_does_not_share_ownership — copy: copy has owning_=true, qubits=-1
//   7. qint_t_move_assign_transfers_ownership — move-assign same as move-ctor

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/recording_sink.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using sturm::qint_t;
using sturm::QubitPool;
using sturm::RecordingSink;
using sturm::ScopedSink;

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

// ── Test 1: default-constructed qint_t<4> has owning_ == true ─────────────────

static void test_qint_t_default_is_owning() {
    qint_t<4> q;
    CHECK(q.owning_ == true);
}

// ── Test 2: make_non_owning returns owning_ == false ──────────────────────────

static void test_qint_t_make_non_owning_flag() {
    std::array<int, 4> qs = {0, 1, 2, 3};
    auto q = qint_t<4>::make_non_owning(qs, 7, 0xFULL);
    CHECK(q.owning_ == false);
    CHECK(q.value == 7);
    CHECK(q.super_mask == 0xFULL);
    CHECK(q.qubits[0] == 0);
    CHECK(q.qubits[1] == 1);
    CHECK(q.qubits[2] == 2);
    CHECK(q.qubits[3] == 3);
}

// ── Test 3: non-owning view destruction does not release qubits ───────────────

static void test_qint_t_non_owning_does_not_release() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    // Allocate 4 qubits directly.
    int q0 = QubitPool::instance().allocate();
    int q1 = QubitPool::instance().allocate();
    int q2 = QubitPool::instance().allocate();
    int q3 = QubitPool::instance().allocate();
    CHECK(QubitPool::instance().in_use() == 4);

    // Create a non-owning view of those qubits.
    {
        std::array<int, 4> indices = {q0, q1, q2, q3};
        auto view = qint_t<4>::make_non_owning(indices, 0, 0);
        CHECK(view.owning_ == false);
        // view goes out of scope here — should NOT release qubits.
    }

    // All 4 qubits should still be in the pool as "in use".
    CHECK(QubitPool::instance().in_use() == 4);

    // Clean up manually.
    QubitPool::instance().release(q0);
    QubitPool::instance().release(q1);
    QubitPool::instance().release(q2);
    QubitPool::instance().release(q3);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 4: owning qint_t releases qubits on destruction ─────────────────────

static void test_qint_t_owning_does_release() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    {
        qint_t<4> q;
        // Manually assign allocated qubits to simulate a "live" qint.
        q.qubits[0] = QubitPool::instance().allocate();
        q.qubits[1] = QubitPool::instance().allocate();
        q.qubits[2] = QubitPool::instance().allocate();
        q.qubits[3] = QubitPool::instance().allocate();
        q.owning_ = true;
        CHECK(QubitPool::instance().in_use() == 4);
        // q goes out of scope; destructor should release all 4 qubits.
    }

    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 5: move-construct transfers ownership ────────────────────────────────

static void test_qint_t_move_transfers_ownership() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint_t<4> src;
    src.qubits[0] = QubitPool::instance().allocate();
    src.qubits[1] = QubitPool::instance().allocate();
    src.qubits[2] = QubitPool::instance().allocate();
    src.qubits[3] = QubitPool::instance().allocate();
    src.owning_ = true;

    int saved_q0 = src.qubits[0];

    // Move-construct.
    qint_t<4> dst(std::move(src));

    // Destination is owning.
    CHECK(dst.owning_ == true);
    CHECK(dst.qubits[0] == saved_q0);

    // Source is no longer owning and has no qubits.
    CHECK(src.owning_ == false);
    for (int i = 0; i < 4; ++i) {
        CHECK(src.qubits[i] == -1);
    }

    // Pool still has 4 in use (dst holds them).
    CHECK(QubitPool::instance().in_use() == 4);
}

// ── Test 6: copy does not share ownership ─────────────────────────────────────

static void test_qint_t_copy_does_not_share_ownership() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint_t<4> orig;
    orig.qubits[0] = QubitPool::instance().allocate();
    orig.qubits[1] = QubitPool::instance().allocate();
    orig.owning_ = true;

    // Copy-construct.
    qint_t<4> copy(orig);

    // Copy has owning_ = true (owns its own fresh qubits, which are -1).
    CHECK(copy.owning_ == true);

    // Copy does NOT share qubit indices — all -1.
    for (int i = 0; i < 4; ++i) {
        CHECK(copy.qubits[i] == -1);
    }

    // Pool still has only 2 qubits in use (orig's).
    CHECK(QubitPool::instance().in_use() == 2);
}

// ── Test 7: move assignment transfers ownership ───────────────────────────────

static void test_qint_t_move_assign_transfers_ownership() {
    QubitPool::instance().reset_for_testing();
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint_t<4> src;
    src.qubits[0] = QubitPool::instance().allocate();
    src.qubits[1] = QubitPool::instance().allocate();
    src.qubits[2] = QubitPool::instance().allocate();
    src.qubits[3] = QubitPool::instance().allocate();
    src.owning_ = true;

    int saved_q0 = src.qubits[0];

    qint_t<4> dst;
    // dst has no qubits yet; move-assign.
    dst = std::move(src);

    // Destination is owning with src's qubits.
    CHECK(dst.owning_ == true);
    CHECK(dst.qubits[0] == saved_q0);

    // Source is non-owning with cleared qubits.
    CHECK(src.owning_ == false);
    for (int i = 0; i < 4; ++i) {
        CHECK(src.qubits[i] == -1);
    }

    // Pool has 4 in use (dst holds them).
    CHECK(QubitPool::instance().in_use() == 4);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_qint_t_default_is_owning();
    test_qint_t_make_non_owning_flag();
    test_qint_t_non_owning_does_not_release();
    test_qint_t_owning_does_release();
    test_qint_t_move_transfers_ownership();
    test_qint_t_copy_does_not_share_ownership();
    test_qint_t_move_assign_transfers_ownership();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
