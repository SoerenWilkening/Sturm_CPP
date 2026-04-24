// test_when_mul_lossy.cpp — sturm-h5it.3
//
// Verifies the CSWAP-and-leak tail for qint_t::operator*= inside WHEN. Matrix:
//   1. Uncontrolled regression — *= outside WHEN keeps the release+relabel
//      fast path. No garbage record is registered.
//   2. Controlled ctrl=|1>  — a *= b produces (old_a * b) & ((1<<W)-1) in the
//      active-ctrl branch (lower W bits of the 2W Cuccaro result).
//   3. Controlled ctrl=|0>  — a stays old_a (the bug this tail fixes).
//   4. Nested WHEN(x){WHEN(y){ a *= b }} — mutates iff x & y = 1. Verified
//      on the (x=1, y=1) and (x=1, y=0) assignments.
//   5. Garbage accounting — detail::garbage_registry grows by exactly one
//      record per controlled *=, with MUL_ASSIGN tag, the right W, and the
//      correct ctrl_qubit.
//
// Superposition-control (ctrl=|+>) coverage with W>=2 would blow past the
// STURM_MAX_QUBITS=17 Orkan budget (operator*= allocates 2W result ancillas
// in addition to the 2W input register and inner-adder carry). We assert
// correctness on the basis assignments of ctrl, which — by linearity —
// suffices to lock down the controlled-branch contract. The generic
// superposition-control bridge is exercised by test_when_control_stack_bridge.
//
// Note (per epic scope, issue sturm-h5it.3): the upper-W bits of the 2W
// Cuccaro result that operator*= releases unconditionally are out of scope
// for this child and are deferred to sturm-pqs0. This test covers only the
// lower-W result-register relabel tail.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/control/garbage_registry.hpp"
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

// ── 1. Uncontrolled regression: *= outside WHEN keeps fast path ─────────────

static void test_mul_uncontrolled_fast_path() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    sturm::detail::garbage_registry::clear();

    sturm::qint_t<3> a(3), b(2);
    const std::uint64_t before = sturm::detail::garbage_registry::snapshot().size();
    a *= b;
    const std::uint64_t after = sturm::detail::garbage_registry::snapshot().size();

    assert(a.value == 6 && "classical fast path: 3 * 2 = 6");
    assert(a.super_mask == 0 && "fast path must not promote");
    for (int q : a.qubits) assert(q == -1 && "fast path allocates no qubits");
    assert(after == before && "uncontrolled *= must NOT register garbage");

    std::puts("  PASS: test_mul_uncontrolled_fast_path");
}

// ── 2. ctrl=|1> simulates to the computed value (old_a * b) mod 2^W ─────────
// W=2: a=3 (|11>), b=2 (|10>), ctrl=1 -> a becomes (3*2) & 3 = 6 & 3 = 2.

static void test_mul_ctrl_one() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    const uint32_t a_base    = 0u;
    const uint32_t b_base    = W;
    const uint32_t ctrl_qidx = 2u * W;   // qubit 4

    // Reserve input registers so the pool won't hand them to scratch.
    int reserved[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    int ctrl_reserved = sturm::QubitPool::instance().allocate();
    assert(ctrl_reserved == static_cast<int>(ctrl_qidx));

    SimCtx sc{17u, 128u};

    // Initialize: a=3 (0b11), b=2 (0b10), ctrl=1.
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
        a *= b;
    }

    const std::size_t reg_after = sturm::detail::garbage_registry::snapshot().size();
    // Post sturm-pqs0: controlled *= registers TWO records per op — the
    // pre-existing MUL_ASSIGN lower-W leak (from sturm-h5it.3) and the
    // MUL_UPPER_W upper-W leak of the 2W Cuccaro result. Ordering: the
    // upper-W record is pushed first (before its release), the MUL_ASSIGN
    // tail record last.
    assert(reg_after == reg_before + 2 &&
           "controlled *=: one MUL_UPPER_W record plus one MUL_ASSIGN record");
    const auto& snap_mul1 = sturm::detail::garbage_registry::snapshot();
    const auto& rec_upper = snap_mul1[reg_before + 0];
    const auto& rec       = snap_mul1[reg_before + 1];
    assert(rec_upper.tag == sturm::detail::garbage_registry::source_op_tag::MUL_UPPER_W);
    assert(rec_upper.W == static_cast<int>(W));
    assert(rec.tag == sturm::detail::garbage_registry::source_op_tag::MUL_ASSIGN);
    assert(rec.W == static_cast<int>(W));
    assert(rec.qubit_indices.size() == W);

    // After the CSWAP, a.qubits[i] still points at the original input register
    // (a_base + i). Those physical qubits now hold (3*2) mod 2^W = 6 & 3 = 2.
    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 2u && "ctrl=|1>: a must become (3*2) mod 4 = 2");

    // Cleanup: prevent double-release.
    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    ctrl.qubits[0] = -1; ctrl.owning_ = false;

    sturm::QubitPool::instance().release(ctrl_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_mul_ctrl_one");
}

// ── 3. ctrl=|0>: a must retain old value (the bug being fixed) ──────────────

static void test_mul_ctrl_zero_preserves_a() {
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
    ctrl.super_mask = 1ULL;  // still "quantum" so WhenGuard takes the superposed branch
    ctrl.value      = 0;

    WHEN(ctrl) {
        a *= b;
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 3u &&
           "ctrl=|0>: *= must leave a unchanged (the CSWAP+leak tail's raison d'etre)");

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    ctrl.qubits[0] = -1; ctrl.owning_ = false;

    sturm::QubitPool::instance().release(ctrl_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_mul_ctrl_zero_preserves_a");
}

// ── 4. Nested WHEN(x){WHEN(y){ a *= b }} — gated by x & y ───────────────────
// Verifies the tail works at depth 1 (WhenGuard's swap-path lowering keeps the
// active depth at 1 per B5). Only test the (x=1,y=1) and (x=1,y=0) cases — one
// where the op fires, one where it doesn't — since the guard's contract has
// the same shape as the flat ctrl=|1> / ctrl=|0> cases.

static void test_mul_nested_when_both_one() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    // Nested WHEN adds a 2nd control qubit. WhenGuard's depth-1 swap-path
    // means runtime control depth stays 1 (the outer is popped), but the
    // extra register adds qubit pressure. operator*= allocates 2W result
    // ancillas on top of the 2W input register, so use W=1 to fit under
    // STURM_MAX_QUBITS (17). 1 * 1 = 1.
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

    // a=1, b=1, x=1, y=1 -> 1*1 = 1.
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
            a *= b;
        }
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 1u && "nested (x=1,y=1): a must equal (1*1) mod 2 = 1");

    // Post sturm-pqs0: two garbage records per inner *= — MUL_UPPER_W + MUL_ASSIGN.
    assert(sturm::detail::garbage_registry::snapshot().size() == 2u);

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    xq.qubits[0] = -1; xq.owning_ = false;
    yq.qubits[0] = -1; yq.owning_ = false;

    sturm::QubitPool::instance().release(x_reserved);
    sturm::QubitPool::instance().release(y_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_mul_nested_when_both_one");
}

static void test_mul_nested_when_x_one_y_zero_noop() {
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

    // a=1, b=1, x=1, y=0 -> a must stay 1.
    orkan::apply_x(sc.sv(), a_base + 0);
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
    a.value = 1; b.value = 1;

    sturm::qbool xq = sturm::qbool::make_non_owning(static_cast<int>(x_qidx));
    xq.super_mask = 1ULL; xq.value = 0;
    sturm::qbool yq = sturm::qbool::make_non_owning(static_cast<int>(y_qidx));
    yq.super_mask = 1ULL; yq.value = 0;

    WHEN(xq) {
        WHEN(yq) {
            a *= b;
        }
    }

    uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == 1u && "nested (x=1,y=0): *= must leave a unchanged (old value 1)");

    // Garbage records still emitted at the gate-emission level (the tail
    // doesn't know that y=|0> — it trusts the control stack). Post
    // sturm-pqs0 this is two records per inner *=: MUL_UPPER_W + MUL_ASSIGN.
    assert(sturm::detail::garbage_registry::snapshot().size() == 2u);

    for (int& q : a.qubits) q = -1;
    for (int& q : b.qubits) q = -1;
    xq.qubits[0] = -1; xq.owning_ = false;
    yq.qubits[0] = -1; yq.owning_ = false;

    sturm::QubitPool::instance().release(x_reserved);
    sturm::QubitPool::instance().release(y_reserved);
    for (std::size_t i = 0; i < 2 * W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::puts("  PASS: test_mul_nested_when_x_one_y_zero_noop");
}

// ── 5. Garbage accounting ────────────────────────────────────────────────────
// Two controlled *= ops in sequence register two records, each with the right
// tag / W / ctrl_qubit. The leaked register indices are distinct across records.

static void test_garbage_accounting_two_ops() {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::detail::garbage_registry::clear();

    static constexpr std::size_t W = 2;
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    sturm::qint_t<W> a(3), b(2), c(1);

    // Promote a, b, c to quantum so they survive the *= tail without forcing
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
        a *= b;
    }
    WHEN(ctrl) {
        c *= b;
    }

    const auto& snap = sturm::detail::garbage_registry::snapshot();
    // Post sturm-pqs0: each controlled *= registers TWO records
    // (MUL_UPPER_W then MUL_ASSIGN), so two ops register four total.
    // Ordering per op: snap[0+2k] = MUL_UPPER_W, snap[1+2k] = MUL_ASSIGN.
    assert(snap.size() == 4u && "two controlled *= ops now register four records");
    assert(snap[0].tag == sturm::detail::garbage_registry::source_op_tag::MUL_UPPER_W);
    assert(snap[1].tag == sturm::detail::garbage_registry::source_op_tag::MUL_ASSIGN);
    assert(snap[2].tag == sturm::detail::garbage_registry::source_op_tag::MUL_UPPER_W);
    assert(snap[3].tag == sturm::detail::garbage_registry::source_op_tag::MUL_ASSIGN);
    for (const auto& r : snap) {
        assert(r.W == static_cast<int>(W));
        assert(r.ctrl_qubit == ctrl.qubits[0]);
    }

    // The lower-W leaks of the two ops (MUL_ASSIGN records) must be
    // disjoint registers; likewise the upper-W leaks.
    for (int q0 : snap[1].qubit_indices) {
        for (int q1 : snap[3].qubit_indices) {
            assert(q0 != q1 &&
                   "distinct controlled ops must leak disjoint MUL_ASSIGN registers");
        }
    }
    for (int q0 : snap[0].qubit_indices) {
        for (int q1 : snap[2].qubit_indices) {
            assert(q0 != q1 &&
                   "distinct controlled ops must leak disjoint MUL_UPPER_W registers");
        }
    }

    // After the controlled ops, super_mask is forced to all-W-ones.
    const uint64_t all_ones = (1ULL << W) - 1ULL;
    assert(a.super_mask == all_ones && "controlled *= forces super_mask all-ones");
    assert(c.super_mask == all_ones && "controlled *= forces super_mask all-ones");

    std::puts("  PASS: test_garbage_accounting_two_ops");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::printf("sturm-h5it.3 *= WHEN CSWAP-and-leak tail tests:\n");
    test_mul_uncontrolled_fast_path();
    test_mul_ctrl_one();
    test_mul_ctrl_zero_preserves_a();
    test_mul_nested_when_both_one();
    test_mul_nested_when_x_one_y_zero_noop();
    test_garbage_accounting_two_ops();
    std::printf("All sturm-h5it.3 tests passed.\n");
    return 0;
}
