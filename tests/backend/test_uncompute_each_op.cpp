// test_uncompute_each_op.cpp — M22: Wire remaining operators to uncompute.
//
// Tests per operator (parameterized over W in {4, 8, 16}):
//   - operator-  (binary qint - qint)
//   - operator+  (qint + qint)
//   - operator*  (qint * qint)
//   - operator/  (qint / qint)
//   - operator%  (qint % qint)
//   - operator&  (bitwise AND)
//   - operator|  (bitwise OR)
//   - operator^  (bitwise XOR)
//   - operator~  (bitwise NOT)
//   - operator== (comparison → qbool)
//   - operator<  (comparison → qbool)
//   - operator<= (comparison → qbool)
//   - operator>  (comparison → qbool)
//   - operator>= (comparison → qbool)
//   - operator!= (comparison → qbool)
//
// Each test follows the Bennett discipline check:
//   1. Snapshot source inputs before the op.
//   2. Perform the op in a block so the result temporary is destroyed at block exit.
//   3. Assert memcmp equality of input snapshots before vs. after.
//   4. Assert the returned temporary carries the correct uncompute_op tag.
//
// Harness: plain assert + main (no gtest).

#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/uncompute_run.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// ── Fixture: scoped APPEND context ───────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::GateIR& ir() { return ctx->ir; }
    sturm::BackendContext& bctx() { return *ctx; }
};

// ── Snapshot helpers ──────────────────────────────────────────────────────────

template <std::size_t W>
struct QintSnap {
    int64_t  value;
    uint64_t super_mask;
    std::array<int, W> qubits;
};

template <std::size_t W>
static QintSnap<W> snap(const sturm::qint_t<W>& q) {
    QintSnap<W> s;
    s.value      = q.value;
    s.super_mask = q.super_mask;
    s.qubits     = q.qubits;
    return s;
}

template <std::size_t W>
static bool snap_eq(const QintSnap<W>& x, const QintSnap<W>& y) {
    return x.value == y.value
        && x.super_mask == y.super_mask
        && x.qubits == y.qubits;
}

// ── Helper: build a quantum qint_t<W> with deterministic qubit indices ────────
// base_qubit: first physical qubit index to assign.

template <std::size_t W>
static sturm::qint_t<W> make_quantum(int64_t val, int base_qubit) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (W < 64) ? ((1ULL << W) - 1u) : ~0ULL;
    for (std::size_t i = 0; i < W; ++i)
        q.qubits[i] = base_qubit + static_cast<int>(i);
    return q;
}

// ── Prevent double-release of manually assigned qubit indices ─────────────────
// Call on qint_t objects whose qubits were never allocated through the pool.

template <std::size_t W>
static void clear_qubits(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0;
}

// ── Comparison: build a quantum qbool with a deterministic qubit index ─────────
static void clear_qbool_qubits(sturm::qbool& b) {
    b.qubits[0]  = -1;
    b.super_mask = 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-operator test templates (parameterized over W)
// ═══════════════════════════════════════════════════════════════════════════════

// ── qint-qint arithmetic tests retired ─────────────────────────────────────
// The five qint-qint operators (+, -, *, /, %) no longer stamp an uncompute
// tag — Phase C moved that uncomputation responsibility to the transpiler
// (sturm-transpile emits `uncompute_{add,sub,mul,div,mod}_qint(a, b);` at
// scope exit). The per-operator Bennett-discipline checks that lived here
// were redundant with the transpiler snapshot + end-to-end suites, so the
// five tests were removed when the tags were retired (2026-04-15).

// ── operator& (bitwise AND) ───────────────────────────────────────────────────

template <std::size_t W>
static void test_bitwise_and_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(0b1010LL, 0);
    auto b = make_quantum<W>(0b1100LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = a & b;
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF
               && "operator&(qint,qint) must stamp BITWISE_SELF tag");
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator&: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator&: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_bitwise_and_tag<W=%zu>: PASS\n", W);
}

// ── operator| (bitwise OR) ────────────────────────────────────────────────────

template <std::size_t W>
static void test_bitwise_or_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(0b1010LL, 0);
    auto b = make_quantum<W>(0b0101LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = a | b;
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF
               && "operator|(qint,qint) must stamp BITWISE_SELF tag");
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator|: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator|: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_bitwise_or_tag<W=%zu>: PASS\n", W);
}

// ── operator^ (bitwise XOR) ───────────────────────────────────────────────────

template <std::size_t W>
static void test_bitwise_xor_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(0b1010LL, 0);
    auto b = make_quantum<W>(0b1100LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = a ^ b;
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF
               && "operator^(qint,qint) must stamp BITWISE_SELF tag");
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator^: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator^: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_bitwise_xor_tag<W=%zu>: PASS\n", W);
}

// ── operator~ (bitwise NOT) ───────────────────────────────────────────────────

template <std::size_t W>
static void test_bitwise_not_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(0b1010LL, 0);

    const auto a_before = snap<W>(a);

    {
        auto t = ~a;
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::BITWISE_SELF
               && "operator~(qint) must stamp BITWISE_SELF tag");
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator~: 'a' must be pristine after block");

    clear_qubits(a);
    std::printf("  test_bitwise_not_tag<W=%zu>: PASS\n", W);
}

// ── operator== (comparison → qbool) ──────────────────────────────────────────

template <std::size_t W>
static void test_compare_eq_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(5LL, 0);
    auto b = make_quantum<W>(5LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = (a == b);
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "operator==(qint,qint) must stamp COMPARE tag on qbool result");
        clear_qbool_qubits(t);
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator==: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator==: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_compare_eq_tag<W=%zu>: PASS\n", W);
}

// ── operator!= (comparison → qbool) ──────────────────────────────────────────

template <std::size_t W>
static void test_compare_neq_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(5LL, 0);
    auto b = make_quantum<W>(7LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = (a != b);
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "operator!=(qint,qint) must stamp COMPARE tag on qbool result");
        clear_qbool_qubits(t);
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator!=: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator!=: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_compare_neq_tag<W=%zu>: PASS\n", W);
}

// ── operator< (comparison → qbool) ───────────────────────────────────────────

template <std::size_t W>
static void test_compare_lt_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(3LL, 0);
    auto b = make_quantum<W>(5LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = (a < b);
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "operator<(qint,qint) must stamp COMPARE tag on qbool result");
        clear_qbool_qubits(t);
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator<: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator<: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_compare_lt_tag<W=%zu>: PASS\n", W);
}

// ── operator<= ────────────────────────────────────────────────────────────────

template <std::size_t W>
static void test_compare_le_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(3LL, 0);
    auto b = make_quantum<W>(5LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = (a <= b);
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "operator<=(qint,qint) must stamp COMPARE tag on qbool result");
        clear_qbool_qubits(t);
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator<=: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator<=: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_compare_le_tag<W=%zu>: PASS\n", W);
}

// ── operator> ─────────────────────────────────────────────────────────────────

template <std::size_t W>
static void test_compare_gt_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(7LL, 0);
    auto b = make_quantum<W>(2LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = (a > b);
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "operator>(qint,qint) must stamp COMPARE tag on qbool result");
        clear_qbool_qubits(t);
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator>: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator>: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_compare_gt_tag<W=%zu>: PASS\n", W);
}

// ── operator>= ────────────────────────────────────────────────────────────────

template <std::size_t W>
static void test_compare_ge_tag() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(7LL, 0);
    auto b = make_quantum<W>(7LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    {
        auto t = (a >= b);
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "operator>=(qint,qint) must stamp COMPARE tag on qbool result");
        clear_qbool_qubits(t);
    }

    assert(snap_eq<W>(snap<W>(a), a_before)
           && "operator>=: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "operator>=: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_compare_ge_tag<W=%zu>: PASS\n", W);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Width-parameterized runner macros
// ═══════════════════════════════════════════════════════════════════════════════

template <std::size_t W>
static void run_all_for_width() {
    std::printf("── W=%zu ──\n", W);
    // qint-qint arithmetic tests retired in Phase C — uncomputation is now
    // the transpiler's responsibility; see comment block above.
    test_bitwise_and_tag<W>();
    test_bitwise_or_tag<W>();
    test_bitwise_xor_tag<W>();
    test_bitwise_not_tag<W>();
    test_compare_eq_tag<W>();
    test_compare_neq_tag<W>();
    test_compare_lt_tag<W>();
    test_compare_le_tag<W>();
    test_compare_gt_tag<W>();
    test_compare_ge_tag<W>();
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M22 uncompute_each_op tests:\n");
    run_all_for_width<4>();
    run_all_for_width<8>();
    run_all_for_width<16>();
    std::printf("All M22 uncompute_each_op tests passed.\n");
    return 0;
}
