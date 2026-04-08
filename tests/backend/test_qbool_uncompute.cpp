// test_qbool_uncompute.cpp — M22: qbool destructor emits comparison inverse.
//
// Tests:
//   1. Constructing a qbool via operator== (a comparison) stamps the COMPARE
//      uncompute tag on the returned qbool.
//   2. When the qbool goes out of scope, its destructor calls
//      uncompute_op::apply(COMPARE, ctx, self) which emits both the forward
//      compare circuit AND its inverse into the IR in sequence (Bennett).
//   3. The IR therefore contains an even number of new gate records: first half
//      are forward (param > 0) and second half are inverse (param < 0).
//   4. The source qint_t inputs are byte-identical before and after the block
//      (Bennett discipline: inputs are pristine).
//
// Strategy: use APPEND mode context so gate emissions are recorded in GateIR.
// The stub compare_forward emits STURM_GATE_CX with param = +cmp_kind.
// The stub compare_inverse emits STURM_GATE_CX with param = -cmp_kind.

#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

// ── Fixture ───────────────────────────────────────────────────────────────────

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

// Build a quantum qint_t<W> with deterministic (non-pooled) qubit indices.
template <std::size_t W>
static sturm::qint_t<W> make_quantum(int64_t val, int base_qubit) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (W < 64) ? ((1ULL << W) - 1u) : ~0ULL;
    for (std::size_t i = 0; i < W; ++i)
        q.qubits[i] = base_qubit + static_cast<int>(i);
    return q;
}

// Prevent double-release: qubit indices were never allocated through the pool.
template <std::size_t W>
static void clear_qubits(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0;
}

// ── Test: qbool from comparison — Bennett + IR record check ───────────────────
//
// The COMPARE apply case emits both compare_forward and compare_inverse on the
// qbool's own qint_base view (width=1, super_mask=1, qubit=0).  That means each
// apply call emits exactly 2 gate records: one forward (param > 0) and one
// inverse (param < 0).
//
// The test verifies:
//   - COMPARE tag is stamped on the qbool.
//   - No IR growth during construction (forward is deferred to destructor).
//   - IR grows by exactly 2 after destruction (1 forward + 1 inverse).
//   - Gate params: forward > 0, inverse < 0.
//   - Source inputs are byte-identical before and after (Bennett).

template <std::size_t W>
static void test_qbool_compare_uncompute() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(5LL, 0);
    auto b = make_quantum<W>(7LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    const std::size_t ir_before = sc.ir().size();

    {
        // operator== returns a qbool with COMPARE tag.
        sturm::qbool t = (a == b);

        // Verify the COMPARE tag was stamped.
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "qbool from operator== must carry COMPARE uncompute tag");

        // No forward gates emitted during construction (deferred to apply).
        const std::size_t ir_mid = sc.ir().size();
        assert(ir_mid == ir_before
               && "No IR growth expected during qbool construction (gates deferred to destructor)");

        // Scope exit: t destructor fires, calling apply(COMPARE) which emits
        // compare_forward + compare_inverse on the qbool's ancilla view.
    }

    // After destruction the IR must have grown by exactly 2 records:
    //   [0] forward gate: param = +cmp_kind  (compare_forward stub)
    //   [1] inverse gate: param = -cmp_kind  (compare_inverse stub)
    const std::size_t ir_after = sc.ir().size();
    const std::size_t total_new = ir_after - ir_before;

    assert(total_new == 2u
           && "Expected exactly 2 gate records (1 forward + 1 inverse compare)");

    // Verify gate record semantics.
    const auto& fwd = sc.ir().at(ir_before + 0);
    const auto& inv = sc.ir().at(ir_before + 1);

    assert(fwd.param > 0.0
           && "Forward compare gate must have positive param (cmp_kind)");
    assert(inv.param < 0.0
           && "Inverse compare gate must have negative param (-cmp_kind)");
    assert(fwd.param == -inv.param
           && "Forward and inverse gate params must be negations of each other");

    // Bennett: source qints are pristine.
    assert(snap_eq<W>(snap<W>(a), a_before)
           && "qbool compare: 'a' must be pristine after block");
    assert(snap_eq<W>(snap<W>(b), b_before)
           && "qbool compare: 'b' must be pristine after block");

    clear_qubits(a);
    clear_qubits(b);
    std::printf("  test_qbool_compare_uncompute<W=%zu>: PASS\n", W);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M22 qbool uncompute tests:\n");
    test_qbool_compare_uncompute<4>();
    test_qbool_compare_uncompute<8>();
    test_qbool_compare_uncompute<16>();
    std::printf("All M22 qbool uncompute tests passed.\n");
    return 0;
}
