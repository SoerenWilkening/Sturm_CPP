// test_mul_div_upperw_garbage.cpp — sturm-pqs0
//
// Verifies that the pre-existing unconditional releases in qint_t::operator*=
// (upper W bits of the 2W Cuccaro product) and qint_t::operator/= (remainder
// register) register their leaks with sturm::detail::garbage_registry before
// calling QubitPool::release. This makes the leak discoverable for a future
// transpiler-driven uncomputation pass (bd sturm-njul) rather than silently
// discarding quantum information when operands are in superposition.
//
// Matrix:
//   1. Uncontrolled *=, W>=2 — registry grows by exactly one record with tag
//      MUL_UPPER_W, W=2, ctrl_qubit=-1, and qubit_indices holding the W upper
//      bits of the 2W result register.
//   2. Uncontrolled /=, W>=2 — registry grows by exactly one record with tag
//      DIV_REMAINDER, W=2, ctrl_qubit=-1, and qubit_indices holding the
//      remainder register.
//   3. Controlled *= under WHEN — registry grows by TWO records: MUL_UPPER_W
//      (new this issue) AND MUL_ASSIGN (the pre-existing lower-W result leak
//      registered by sturm-h5it.3).
//   4. Controlled /= under WHEN — registry grows by TWO records: DIV_REMAINDER
//      (new this issue) AND DIV_ASSIGN (the pre-existing quotient-register
//      leak registered by sturm-h5it.4).
//
// Tight qubit budgets: STURM_MAX_QUBITS=17. We stay on the classical
// fast-path-defeating branch by pre-promoting operands to quantum for the
// controlled cases, matching the pattern used by test_when_mul_lossy's
// test_garbage_accounting_two_ops.

#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/control/garbage_registry.hpp"
#include "sturm/control/when_scope_garbage.hpp"  // sturm-njul ScopedGarbageConsumeGuard
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── ScopedCtx (COUNT_ONLY) ───────────────────────────────────────────────────
struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 17u) {
        ctx  = sturm_backend_create(mode, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── 1. Uncontrolled *=: MUL_UPPER_W registered once ─────────────────────────

static void test_mul_uncontrolled_upper_w_registered() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    static constexpr std::size_t W = 2;

    // Force the non-fast-path: promote A to quantum so *= takes the
    // allocator branch where MUL_UPPER_W can register.
    sturm::qint_t<W> a(1), b(2);
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = sturm::QubitPool::instance().allocate();
    }
    a.super_mask = (1ULL << W) - 1ULL;

    const std::size_t before = sturm::detail::garbage_registry::snapshot().size();
    a *= b;
    const auto& snap = sturm::detail::garbage_registry::snapshot();
    const std::size_t after = snap.size();

    // Uncontrolled path: exactly ONE record (MUL_UPPER_W). No MUL_ASSIGN
    // because that is only registered under WHEN.
    assert(after == before + 1u &&
           "uncontrolled *= must register exactly one MUL_UPPER_W record");

    const auto& rec = snap.back();
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::MUL_UPPER_W);
    assert(rec.W == static_cast<int>(W));
    assert(rec.ctrl_qubit == -1 && "uncontrolled path ctrl_qubit sentinel is -1");
    assert(rec.qubit_indices.size() == W);
    // Upper W indices must be distinct and non-negative (real qubits).
    for (std::size_t i = 0; i < W; ++i) {
        assert(rec.qubit_indices[i] >= 0);
        for (std::size_t j = i + 1; j < W; ++j) {
            assert(rec.qubit_indices[i] != rec.qubit_indices[j]);
        }
    }

    std::puts("  PASS: test_mul_uncontrolled_upper_w_registered");
}

// ── 2. Uncontrolled /=: DIV_REMAINDER registered once ───────────────────────

static void test_div_uncontrolled_remainder_registered() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    static constexpr std::size_t W = 2;

    sturm::qint_t<W> a(3), b(2);
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = sturm::QubitPool::instance().allocate();
    }
    a.super_mask = (1ULL << W) - 1ULL;

    const std::size_t before = sturm::detail::garbage_registry::snapshot().size();
    a /= b;
    const auto& snap = sturm::detail::garbage_registry::snapshot();
    const std::size_t after = snap.size();

    assert(after == before + 1u &&
           "uncontrolled /= must register exactly one DIV_REMAINDER record");

    const auto& rec = snap.back();
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::DIV_REMAINDER);
    assert(rec.W == static_cast<int>(W));
    assert(rec.ctrl_qubit == -1 && "uncontrolled path ctrl_qubit sentinel is -1");
    assert(rec.qubit_indices.size() == W);
    for (std::size_t i = 0; i < W; ++i) {
        assert(rec.qubit_indices[i] >= 0);
        for (std::size_t j = i + 1; j < W; ++j) {
            assert(rec.qubit_indices[i] != rec.qubit_indices[j]);
        }
    }

    std::puts("  PASS: test_div_uncontrolled_remainder_registered");
}

// ── 3. Controlled *=: MUL_UPPER_W + MUL_ASSIGN both registered ──────────────

static void test_mul_controlled_registers_both_tags() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    static constexpr std::size_t W = 2;

    sturm::qint_t<W> a(3), b(2);
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = sturm::QubitPool::instance().allocate();
        b.qubits[i] = sturm::QubitPool::instance().allocate();
    }
    a.super_mask = b.super_mask = (1ULL << W) - 1ULL;

    sturm::qbool ctrl(0.5);  // superposed control → WhenGuard takes quantum path

    const std::size_t before = sturm::detail::garbage_registry::snapshot().size();
    WHEN(ctrl) {
        a *= b;
    }
    const auto& snap = sturm::detail::garbage_registry::snapshot();
    const std::size_t after = snap.size();

    assert(after == before + 2u &&
           "controlled *= registers BOTH MUL_UPPER_W (new) and MUL_ASSIGN (from h5it.3)");

    // The ordering is: MUL_UPPER_W is registered first (before the release
    // of the upper bits), then MUL_ASSIGN after the CSWAP tail.
    const auto& r_upper = snap[before + 0];
    const auto& r_lower = snap[before + 1];
    assert(r_upper.tag == sturm::detail::garbage_registry::source_op_tag::MUL_UPPER_W);
    assert(r_lower.tag == sturm::detail::garbage_registry::source_op_tag::MUL_ASSIGN);
    assert(r_upper.W == static_cast<int>(W));
    assert(r_lower.W == static_cast<int>(W));
    // Both must carry the active WHEN's ctrl qubit.
    assert(r_upper.ctrl_qubit == ctrl.qubits[0]);
    assert(r_lower.ctrl_qubit == ctrl.qubits[0]);
    // The upper-W and lower-W indices must be disjoint (they are different
    // halves of the same 2W result register).
    for (int qu : r_upper.qubit_indices) {
        for (int ql : r_lower.qubit_indices) {
            assert(qu != ql &&
                   "upper-W and lower-W leak registers must be disjoint");
        }
    }

    std::puts("  PASS: test_mul_controlled_registers_both_tags");
}

// ── 4. Controlled /=: DIV_REMAINDER + DIV_ASSIGN both registered ────────────

static void test_div_controlled_registers_both_tags() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    static constexpr std::size_t W = 2;

    sturm::qint_t<W> a(3), b(2);
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = sturm::QubitPool::instance().allocate();
        b.qubits[i] = sturm::QubitPool::instance().allocate();
    }
    a.super_mask = b.super_mask = (1ULL << W) - 1ULL;

    sturm::qbool ctrl(0.5);

    const std::size_t before = sturm::detail::garbage_registry::snapshot().size();
    WHEN(ctrl) {
        a /= b;
    }
    const auto& snap = sturm::detail::garbage_registry::snapshot();
    const std::size_t after = snap.size();

    assert(after == before + 2u &&
           "controlled /= registers BOTH DIV_REMAINDER (new) and DIV_ASSIGN (from h5it.4)");

    const auto& r_rem = snap[before + 0];
    const auto& r_q   = snap[before + 1];
    assert(r_rem.tag == sturm::detail::garbage_registry::source_op_tag::DIV_REMAINDER);
    assert(r_q.tag   == sturm::detail::garbage_registry::source_op_tag::DIV_ASSIGN);
    assert(r_rem.W == static_cast<int>(W));
    assert(r_q.W   == static_cast<int>(W));
    assert(r_rem.ctrl_qubit == ctrl.qubits[0]);
    assert(r_q.ctrl_qubit   == ctrl.qubits[0]);

    // Remainder and quotient registers must be disjoint.
    for (int qr : r_rem.qubit_indices) {
        for (int qq : r_q.qubit_indices) {
            assert(qr != qq &&
                   "remainder and quotient leak registers must be disjoint");
        }
    }

    std::puts("  PASS: test_div_controlled_registers_both_tags");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    // sturm-njul: disable the scope-exit consumer so the controlled-path
    // tests in this file can still observe MUL_UPPER_W / DIV_REMAINDER +
    // MUL_ASSIGN / DIV_ASSIGN records post-WHEN.
    sturm::detail::ScopedGarbageConsumeGuard _njul_off(false);

    std::printf("sturm-pqs0 *= upper-W / /= remainder garbage-registry tests:\n");
    test_mul_uncontrolled_upper_w_registered();
    test_div_uncontrolled_remainder_registered();
    test_mul_controlled_registers_both_tags();
    test_div_controlled_registers_both_tags();
    std::printf("All sturm-pqs0 tests passed.\n");
    return 0;
}
