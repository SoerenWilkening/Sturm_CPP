// test_when_and_or_lossy.cpp — sturm-h5it.2
//
// Verifies the CSWAP-and-leak tail for qint_t::operator&= and operator|=
// inside WHEN. Matrix per epic plan:
//   1. Uncontrolled regression — op outside WHEN keeps the release+relabel
//      fast path. Exercises the uncontrolled branch of the split tail.
//   2. Controlled ctrl=|1>  — a &= b / a |= b produces old_a op b in the
//      active-ctrl branch. Simulated classically via apply_x(ctrl).
//   3. Controlled ctrl=|0>  — a stays old_a (the bug the tail fixes).
//   4. Nested WHEN(x){WHEN(y){ a op= b }} — mutates iff x & y = 1. Verified
//      on the (x=1, y=1) and (x=1, y=0) assignments.
//   5. Garbage accounting — detail::garbage_registry grows by exactly one
//      record per controlled op, with the right W / ctrl_qubit / tag, and
//      the leaked register indices are disjoint across records.
//
// Superposition-control (ctrl=|+>) coverage is handled generically by
// test_when_control_stack_bridge; here we assert correctness on the basis
// assignments of ctrl (which, by linearity, pins the controlled-branch
// contract).
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/control/garbage_registry.hpp"
#include "sturm/control/when_scope_garbage.hpp"  // sturm-njul ScopedGarbageConsumeGuard
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── ScopedCtx (COUNT_ONLY or APPEND) ─────────────────────────────────────────
struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 128u) {
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

// ── SimCtx (SIMULATE) ────────────────────────────────────────────────────────
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

// Read the register value whose bit-i is stored at qubit index reg[i], from
// the first non-zero-amplitude basis state. Assumes a pure basis state.
template <class Arr>
static uint32_t read_reg(orkan::state_t& sv, const Arr& reg, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t v = 0u;
            for (std::size_t i = 0; i < reg.size(); ++i) {
                if (reg[i] >= 0) {
                    v |= (static_cast<uint32_t>((s >> reg[i]) & 1u) << i);
                }
            }
            return v;
        }
    }
    return 0u;
}

// ── 1. Uncontrolled regression: &= and |= outside WHEN keep fast path ────────

static void test_and_uncontrolled_fast_path() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    sturm::detail::garbage_registry::clear();

    sturm::qint_t<3> a(6), b(3);   // 0b110 & 0b011 = 0b010 = 2
    const std::uint64_t before = sturm::detail::garbage_registry::snapshot().size();
    a &= b;
    const std::uint64_t after = sturm::detail::garbage_registry::snapshot().size();

    assert(a.value == 2 && "classical fast path: 6 & 3 = 2");
    assert(a.super_mask == 0 && "fast path must not promote");
    for (int q : a.qubits) assert(q == -1 && "fast path allocates no qubits");
    assert(after == before && "uncontrolled &= must NOT register garbage");

    std::puts("  PASS: test_and_uncontrolled_fast_path");
}

static void test_or_uncontrolled_fast_path() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    sturm::detail::garbage_registry::clear();

    sturm::qint_t<3> a(5), b(2);   // 0b101 | 0b010 = 0b111 = 7
    const std::uint64_t before = sturm::detail::garbage_registry::snapshot().size();
    a |= b;
    const std::uint64_t after = sturm::detail::garbage_registry::snapshot().size();

    assert(a.value == 7 && "classical fast path: 5 | 2 = 7");
    assert(a.super_mask == 0 && "fast path must not promote");
    for (int q : a.qubits) assert(q == -1 && "fast path allocates no qubits");
    assert(after == before && "uncontrolled |= must NOT register garbage");

    std::puts("  PASS: test_or_uncontrolled_fast_path");
}

// ── 2. ctrl=|1> simulates to the computed value (old_a op b) ────────────────

static void test_and_ctrl_one() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = W;
    const uint32_t ctrl_qidx = 2u * W;   // qubit 4

    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int ctrl_reserved = sturm::QubitPool::instance().allocate();
    assert(ctrl_reserved == static_cast<int>(ctrl_qidx));

    SimCtx sc{17u, 128u};

    // Initialize: a=3 (0b11), b=2 (0b10), ctrl=1.  3 & 2 = 2.
    orkan::apply_x(sc.sv(), a_base + 0);
    orkan::apply_x(sc.sv(), a_base + 1);
    orkan::apply_x(sc.sv(), b_base + 1);
    orkan::apply_x(sc.sv(), ctrl_qidx);

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = static_cast<int>(a_base + i);
        b.qubits[i] = static_cast<int>(b_base + i);
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = 3; b.value = 2;

    sturm::qbool ctrl = sturm::qbool::make_non_owning(static_cast<int>(ctrl_qidx));
    ctrl.super_mask = 1ULL;
    ctrl.value      = 0;

    const std::size_t reg_before = sturm::detail::garbage_registry::snapshot().size();

    WHEN(ctrl) {
        a &= b;
    }

    const std::size_t reg_after = sturm::detail::garbage_registry::snapshot().size();
    assert(reg_after == reg_before + 1 && "one record per controlled &=");
    const auto& rec = sturm::detail::garbage_registry::snapshot().back();
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::AND_ASSIGN);
    assert(rec.W == static_cast<int>(W));
    assert(rec.qubit_indices.size() == W);

    // After the CSWAP, a.qubits[i] still points at the original input register
    // (a_base + i). Those physical qubits now hold 3 & 2 = 2 (0b10).
    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 2u && "ctrl=|1>: a must become 3 & 2 = 2");

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    ctrl.qubits[0] = -1; ctrl.owning_ = false;

    sturm::QubitPool::instance().release(ctrl_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_and_ctrl_one");
}

static void test_or_ctrl_one() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = W;
    const uint32_t ctrl_qidx = 2u * W;

    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int ctrl_reserved = sturm::QubitPool::instance().allocate();
    assert(ctrl_reserved == static_cast<int>(ctrl_qidx));

    SimCtx sc{17u, 128u};

    // Initialize: a=1 (0b01), b=2 (0b10), ctrl=1.  1 | 2 = 3.
    orkan::apply_x(sc.sv(), a_base + 0);
    orkan::apply_x(sc.sv(), b_base + 1);
    orkan::apply_x(sc.sv(), ctrl_qidx);

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = static_cast<int>(a_base + i);
        b.qubits[i] = static_cast<int>(b_base + i);
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = 1; b.value = 2;

    sturm::qbool ctrl = sturm::qbool::make_non_owning(static_cast<int>(ctrl_qidx));
    ctrl.super_mask = 1ULL;
    ctrl.value      = 0;

    const std::size_t reg_before = sturm::detail::garbage_registry::snapshot().size();

    WHEN(ctrl) {
        a |= b;
    }

    const std::size_t reg_after = sturm::detail::garbage_registry::snapshot().size();
    assert(reg_after == reg_before + 1 && "one record per controlled |=");
    const auto& rec = sturm::detail::garbage_registry::snapshot().back();
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::OR_ASSIGN);
    assert(rec.W == static_cast<int>(W));
    assert(rec.qubit_indices.size() == W);

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 3u && "ctrl=|1>: a must become 1 | 2 = 3");

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    ctrl.qubits[0] = -1; ctrl.owning_ = false;

    sturm::QubitPool::instance().release(ctrl_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_or_ctrl_one");
}

// ── 3. ctrl=|0>: a must retain old value (the bug being fixed) ──────────────

static void test_and_ctrl_zero_preserves_a() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = W;
    const uint32_t ctrl_qidx = 2u * W;

    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int ctrl_reserved = sturm::QubitPool::instance().allocate();
    assert(ctrl_reserved == static_cast<int>(ctrl_qidx));

    SimCtx sc{17u, 128u};

    // a=3 (0b11), b=2 (0b10), ctrl=0.  a must stay 3.
    orkan::apply_x(sc.sv(), a_base + 0);
    orkan::apply_x(sc.sv(), a_base + 1);
    orkan::apply_x(sc.sv(), b_base + 1);
    // NOTE: ctrl NOT flipped → |0>.

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = static_cast<int>(a_base + i);
        b.qubits[i] = static_cast<int>(b_base + i);
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = 3; b.value = 2;

    sturm::qbool ctrl = sturm::qbool::make_non_owning(static_cast<int>(ctrl_qidx));
    ctrl.super_mask = 1ULL;
    ctrl.value      = 0;

    WHEN(ctrl) {
        a &= b;
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 3u &&
           "ctrl=|0>: &= must leave a unchanged (the CSWAP+leak tail's raison d'etre)");

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    ctrl.qubits[0] = -1; ctrl.owning_ = false;

    sturm::QubitPool::instance().release(ctrl_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_and_ctrl_zero_preserves_a");
}

static void test_or_ctrl_zero_preserves_a() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = W;
    const uint32_t ctrl_qidx = 2u * W;

    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int ctrl_reserved = sturm::QubitPool::instance().allocate();
    assert(ctrl_reserved == static_cast<int>(ctrl_qidx));

    SimCtx sc{17u, 128u};

    // a=1 (0b01), b=2 (0b10), ctrl=0.  a must stay 1.
    orkan::apply_x(sc.sv(), a_base + 0);
    orkan::apply_x(sc.sv(), b_base + 1);
    // ctrl NOT flipped → |0>.

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = static_cast<int>(a_base + i);
        b.qubits[i] = static_cast<int>(b_base + i);
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = 1; b.value = 2;

    sturm::qbool ctrl = sturm::qbool::make_non_owning(static_cast<int>(ctrl_qidx));
    ctrl.super_mask = 1ULL;
    ctrl.value      = 0;

    WHEN(ctrl) {
        a |= b;
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 1u &&
           "ctrl=|0>: |= must leave a unchanged (old value 1)");

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    ctrl.qubits[0] = -1; ctrl.owning_ = false;

    sturm::QubitPool::instance().release(ctrl_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_or_ctrl_zero_preserves_a");
}

// ── 4. Nested WHEN(x){WHEN(y){ a op= b }} — gated by x & y ───────────────────
// Verifies the tail works at depth 1 (WhenGuard's swap-path lowering keeps the
// active depth at 1 per B5). Test (x=1,y=1) where op fires and (x=1,y=0)
// where it doesn't. Mind the 17-qubit cap: each controlled &=/|= allocates W
// result ancillas + a CCX sandwich ancilla on top of the 2W input + 2 ctrl +
// the nesting swap-path ancillas.

static void test_and_nested_when_both_one() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 1;
    const uint32_t a_base = 0u;
    const uint32_t b_base = W;
    const uint32_t x_qidx = 2u * W;
    const uint32_t y_qidx = 2u * W + 1u;

    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int x_reserved = sturm::QubitPool::instance().allocate();
    int y_reserved = sturm::QubitPool::instance().allocate();
    assert(x_reserved == static_cast<int>(x_qidx));
    assert(y_reserved == static_cast<int>(y_qidx));

    SimCtx sc{17u, 128u};

    // a=1, b=1, x=1, y=1 -> 1 & 1 = 1.
    orkan::apply_x(sc.sv(), a_base + 0);
    orkan::apply_x(sc.sv(), b_base + 0);
    orkan::apply_x(sc.sv(), x_qidx);
    orkan::apply_x(sc.sv(), y_qidx);

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = static_cast<int>(a_base + i);
        b.qubits[i] = static_cast<int>(b_base + i);
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = 1; b.value = 1;

    sturm::qbool xq = sturm::qbool::make_non_owning(static_cast<int>(x_qidx));
    xq.super_mask = 1ULL; xq.value = 0;
    sturm::qbool yq = sturm::qbool::make_non_owning(static_cast<int>(y_qidx));
    yq.super_mask = 1ULL; yq.value = 0;

    WHEN(xq) {
        WHEN(yq) {
            a &= b;
        }
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 1u && "nested (x=1,y=1): a must equal 1 & 1 = 1");

    assert(sturm::detail::garbage_registry::snapshot().size() == 1u);
    const auto& rec = sturm::detail::garbage_registry::snapshot().back();
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::AND_ASSIGN);

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    xq.qubits[0] = -1; xq.owning_ = false;
    yq.qubits[0] = -1; yq.owning_ = false;

    sturm::QubitPool::instance().release(x_reserved);
    sturm::QubitPool::instance().release(y_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_and_nested_when_both_one");
}

static void test_or_nested_when_x_one_y_zero_noop() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 1;
    const uint32_t a_base = 0u;
    const uint32_t b_base = W;
    const uint32_t x_qidx = 2u * W;
    const uint32_t y_qidx = 2u * W + 1u;

    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int x_reserved = sturm::QubitPool::instance().allocate();
    int y_reserved = sturm::QubitPool::instance().allocate();
    assert(x_reserved == static_cast<int>(x_qidx));
    assert(y_reserved == static_cast<int>(y_qidx));

    SimCtx sc{17u, 128u};

    // a=0, b=1, x=1, y=0 -> a must stay 0.  (If fired: 0 | 1 = 1.)
    orkan::apply_x(sc.sv(), b_base + 0);
    orkan::apply_x(sc.sv(), x_qidx);
    // y NOT flipped → |0>.

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = static_cast<int>(a_base + i);
        b.qubits[i] = static_cast<int>(b_base + i);
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = 0; b.value = 1;

    sturm::qbool xq = sturm::qbool::make_non_owning(static_cast<int>(x_qidx));
    xq.super_mask = 1ULL; xq.value = 0;
    sturm::qbool yq = sturm::qbool::make_non_owning(static_cast<int>(y_qidx));
    yq.super_mask = 1ULL; yq.value = 0;

    WHEN(xq) {
        WHEN(yq) {
            a |= b;
        }
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 0u && "nested (x=1,y=0): |= must leave a unchanged (old value 0)");

    // Garbage record still emitted at the gate-emission level (the tail
    // doesn't know that y=|0> — it trusts the control stack). One record
    // is registered whether or not the physical control is satisfied at
    // simulation time; this mirrors the div/mod/mul accounting contract.
    assert(sturm::detail::garbage_registry::snapshot().size() == 1u);
    const auto& rec = sturm::detail::garbage_registry::snapshot().back();
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::OR_ASSIGN);

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    xq.qubits[0] = -1; xq.owning_ = false;
    yq.qubits[0] = -1; yq.owning_ = false;

    sturm::QubitPool::instance().release(x_reserved);
    sturm::QubitPool::instance().release(y_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_or_nested_when_x_one_y_zero_noop");
}

// ── 5. Garbage accounting ────────────────────────────────────────────────────
// Two controlled ops in sequence (&= and |=) register two records, each with
// the right tag / W / ctrl_qubit. The leaked register indices are distinct
// across records.

static void test_garbage_accounting_and_or() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    sturm::qint_t<W> a(3), b(2), c(1);

    // Promote a, b, c to quantum so they survive the tail without forcing
    // emit_X_lifted to materialise classical bits under WHEN (which is a
    // separate pre-existing issue, outside this child's scope).
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = sturm::QubitPool::instance().allocate();
        b.qubits[i] = sturm::QubitPool::instance().allocate();
        c.qubits[i] = sturm::QubitPool::instance().allocate();
    }
    a.super_mask = b.super_mask = c.super_mask = (1ULL << W) - 1ULL;

    sturm::qbool ctrl(0.5);  // superposed control

    WHEN(ctrl) {
        a &= b;
    }
    WHEN(ctrl) {
        c |= b;
    }

    const auto& snap = sturm::detail::garbage_registry::snapshot();
    assert(snap.size() == 2u && "two controlled ops register two records");
    assert(snap[0].tag == sturm::detail::garbage_registry::source_op_tag::AND_ASSIGN);
    assert(snap[1].tag == sturm::detail::garbage_registry::source_op_tag::OR_ASSIGN);
    assert(snap[0].W == static_cast<int>(W));
    assert(snap[1].W == static_cast<int>(W));
    assert(snap[0].ctrl_qubit == ctrl.qubits[0]);
    assert(snap[1].ctrl_qubit == ctrl.qubits[0]);
    assert(snap[0].qubit_indices.size() == W);
    assert(snap[1].qubit_indices.size() == W);

    // The two leaks must be distinct registers.
    for (int q0 : snap[0].qubit_indices) {
        for (int q1 : snap[1].qubit_indices) {
            assert(q0 != q1 &&
                   "distinct controlled ops must leak disjoint registers");
        }
    }

    // After the controlled ops, super_mask is forced to all-W-ones.
    const uint64_t all_ones = (1ULL << W) - 1ULL;
    assert(a.super_mask == all_ones && "controlled &= forces super_mask all-ones");
    assert(c.super_mask == all_ones && "controlled |= forces super_mask all-ones");

    std::puts("  PASS: test_garbage_accounting_and_or");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    // sturm-njul: this file's assertions observe the garbage_registry AFTER
    // the WHEN scope has ended, which is the state *before* the sturm-njul
    // scope-exit consumer was introduced. Disable the consumer for this
    // whole process so the discoverability invariants this file locks down
    // remain observable. Product builds run with the consumer enabled.
    sturm::detail::ScopedGarbageConsumeGuard _njul_off(false);

    std::printf("sturm-h5it.2 &= and |= WHEN CSWAP-and-leak tail tests:\n");
    test_and_uncontrolled_fast_path();
    test_or_uncontrolled_fast_path();
    test_and_ctrl_one();
    test_or_ctrl_one();
    test_and_ctrl_zero_preserves_a();
    test_or_ctrl_zero_preserves_a();
    test_and_nested_when_both_one();
    test_or_nested_when_x_one_y_zero_noop();
    test_garbage_accounting_and_or();
    std::printf("All sturm-h5it.2 tests passed.\n");
    return 0;
}
