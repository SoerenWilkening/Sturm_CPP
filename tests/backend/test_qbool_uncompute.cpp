// test_qbool_uncompute.cpp — M22/M19: qbool from comparison stamps COMPARE tag.
//
// Tests:
//   1. Constructing a qbool via operator== (a comparison) stamps the COMPARE
//      uncompute tag on the returned qbool.
//   2. When the comparison is performed, the DSL comparison circuit (lib_eq_dsl)
//      emits gates into the IR.
//   3. The source qint_t inputs are byte-identical before and after the block
//      (Bennett discipline: inputs are pristine).
//
// Strategy: use APPEND mode context so gate emissions are recorded in GateIR.
// M19 wiring: comparison operators call compare_dsl functions (lib_eq_dsl etc.)
// so gates are emitted during construction, and additional uncompute gates may
// be emitted by the destructor via the COMPARE uncompute tag.

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

// ── Test: qbool from comparison — DSL circuit emission + COMPARE tag check ────
//
// M19 wiring: operator== calls lib_eq_dsl which emits the DSL comparison circuit
// during construction of the qbool (forward gates emitted eagerly).
//
// The test verifies:
//   - COMPARE tag is stamped on the qbool (for uncompute destructor hook).
//   - IR grows (gates are emitted by the DSL comparison circuit).
//   - Source inputs are byte-identical before and after (Bennett discipline).

template <std::size_t W>
static void test_qbool_compare_uncompute() {
    ScopedAppendCtx sc;

    auto a = make_quantum<W>(5LL, 0);
    auto b = make_quantum<W>(7LL, static_cast<int>(W));

    const auto a_before = snap<W>(a);
    const auto b_before = snap<W>(b);

    const std::size_t ir_before = sc.ir().size();

    {
        // operator== calls lib_eq_dsl and returns a qbool with COMPARE tag.
        // M19: forward comparison circuit gates are emitted immediately here.
        sturm::qbool t = (a == b);

        // Verify the COMPARE tag was stamped.
        assert(t.uncompute_.tag == sturm::uncompute_op::kind::COMPARE
               && "qbool from operator== must carry COMPARE uncompute tag");

        // Scope exit: t destructor fires, calling apply(COMPARE) which emits
        // additional uncompute gates on the qbool's ancilla view.
    }

    // After destruction the IR must have grown (DSL circuit gates emitted).
    const std::size_t ir_after = sc.ir().size();
    assert(ir_after > ir_before
           && "Expected IR growth: comparison DSL circuit gates must be emitted");

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
