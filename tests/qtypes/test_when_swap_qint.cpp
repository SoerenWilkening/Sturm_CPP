// test_when_swap_qint.cpp — sturm-8dzd: RED-bar unit test for sturm::swap
// (qint_t<W>) under a manually-pushed control stack at depth==1.
//
// Epic: sturm-8aq3 — `sturm::swap(qint_t<W>&, qint_t<W>&)` (defined in
// `include/sturm/detail/qtypes/lossy_oop.hpp`) is not control-aware. On main it
// always relabels indices and emits zero gates regardless of the control
// stack depth, violating PRD §5.1: under WHEN(ctrl) the LO-2 emitter
// inserts `swap(<lhs>, __sturm_tmp_<op>_<N>)` between the forward `*_oop`
// and its adjoint, and that swap MUST lift to a per-bit Fredkin (3 CCX
// per bit) when the control stack is non-empty.
//
// This test pins the contract sturm-arce will turn green:
//   1. depth == 0 (uncontrolled): zero gates; `a.qubits[]`/`b.qubits[]`
//      relabelled (the existing fast-path).
//   2. depth == 1 (one ctrl pushed): 3*W CCX gates, no plain SWAP / CX,
//      operand ordering per the issue spec sturm-8dzd:
//        first  Toffoli (per bit): (ctrl, a_i, b_i)
//        middle Toffoli (per bit): (ctrl, b_i, a_i)
//        third  Toffoli (per bit): (ctrl, a_i, b_i)
//      Both `a.super_mask` and `b.super_mask` end up with bits at indices
//      [0, W) set (BitProxy::ensure_quantum does
//      `*mask_ptr |= (1ULL << bit_pos)` if a bit was unallocated;
//      operands begin pre-allocated + pre-masked here, so the test pins
//      that the controlled branch never CLEARS the per-bit mask).
//
// Status: RED on main (current `sturm::swap(qint_t<W>&, qint_t<W>&)`
//         emits zero gates under control). GREEN after sturm-arce.

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"        // pulls in lossy_oop.hpp under STURM_BACKEND_ENABLED
#include "sturm/detail/qtypes/lossy_oop.hpp"   // sturm::swap(qint_t<W>&, qint_t<W>&)
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/ir.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// ── ScopedAppendCtx ──────────────────────────────────────────────────────────
// APPEND mode records every gate into ctx.ir; the test inspects that buffer
// directly to assert (kind, n, qubits[3]) per record.
struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedAppendCtx(uint32_t mq = 64u) {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, mq);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// Count records of a specific kind in the IR buffer.
static std::size_t count_kind(const sturm::GateIR& ir, sturm_gate_kind_t k) {
    std::size_t n = 0u;
    for (std::size_t i = 0; i < ir.size(); ++i) {
        if (ir.at(i).kind == k) ++n;
    }
    return n;
}

// ── Test 1: depth == 1 — 3*W CCX with the issue-specified operand ordering ───
//
// Setup: pre-allocate W qubits for `a`, W qubits for `b`, plus one ctrl
// qubit. Pre-set both super_masks to (1<<W)-1 (the qubits are deliberately
// already promoted; the contract under control is "preserve mask"). Push
// the ctrl onto `ctx.control_stack`. Call `sturm::swap(a, b)`.
//
// Expected gate stream (per bit i in [0, W)):
//   CCX(ctrl, a_i, b_i)
//   CCX(ctrl, b_i, a_i)
//   CCX(ctrl, a_i, b_i)
// Total: 3*W CCX. No SWAP / CX / X / RZ / RY / etc.
template <std::size_t W>
static void test_depth1_emits_3W_ccx_with_ordering() {
    sturm::QubitPool::instance().reset_for_testing();

    std::array<int, W> qa{}, qb{};
    for (std::size_t i = 0; i < W; ++i) qa[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i) qb[i] = sturm::QubitPool::instance().allocate();
    const int qctrl_int = sturm::QubitPool::instance().allocate();
    const auto qctrl = static_cast<uint32_t>(qctrl_int);

    ScopedAppendCtx sc;

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = qa[i];
        b.qubits[i] = qb[i];
    }
    a.value = 0; b.value = 0;
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.owning_ = false;  // QubitPool integers are owned by `qa[]` array — manual release below.
    b.owning_ = false;

    // Manually push one ctrl onto the control stack (no WHEN involved).
    sc.ctx->control_stack.push_control(qctrl);
    assert(sc.ctx->control_stack.depth() == 1u
           && "control_stack depth must be 1 before calling sturm::swap");

    // Snapshot IR size pre-call (should be 0).
    const std::size_t pre = sc.ctx->ir.size();
    assert(pre == 0u && "APPEND mode IR must be empty before sturm::swap");

    sturm::swap(a, b);

    // Pop the ctrl now that the call is done; matches the WhenGuard
    // pop-on-scope-exit ordering.
    sc.ctx->control_stack.pop_control();

    const std::size_t total = sc.ctx->ir.size() - pre;

    // (A) Total gate count: exactly 3*W records.
    assert(total == 3u * W
           && "sturm::swap(qint_t<W>&, qint_t<W>&) under depth==1 must emit 3*W gates "
              "(per-bit Fredkin = 3 CCX per bit)");

    // (B) All records are CCX — no plain SWAP, no CX, no X, no rotations.
    assert(count_kind(sc.ctx->ir, STURM_GATE_CCX) == 3u * W
           && "all 3*W gates emitted must be CCX (per-bit Fredkin under one control)");
    assert(count_kind(sc.ctx->ir, STURM_GATE_SWAP) == 0u
           && "sturm::swap(qint_t<W>&) must NOT emit a plain SWAP under control");
    assert(count_kind(sc.ctx->ir, STURM_GATE_CX) == 0u
           && "sturm::swap under depth==1 must NOT emit bare CX (must be lifted to CCX)");
    assert(count_kind(sc.ctx->ir, STURM_GATE_X) == 0u
           && "sturm::swap under depth==1 must NOT emit bare X");

    // (C) Operand ordering per bit. Issue sturm-8dzd:
    //   first/third Toffoli: (ctrl, a_i, b_i)
    //   middle      Toffoli: (ctrl, b_i, a_i)
    for (std::size_t i = 0; i < W; ++i) {
        const auto& r0 = sc.ctx->ir.at(pre + 3u * i + 0u);
        const auto& r1 = sc.ctx->ir.at(pre + 3u * i + 1u);
        const auto& r2 = sc.ctx->ir.at(pre + 3u * i + 2u);

        assert(r0.kind == STURM_GATE_CCX && r0.n == 3u
               && "Fredkin gate 0 must be CCX with arity 3");
        assert(r1.kind == STURM_GATE_CCX && r1.n == 3u
               && "Fredkin gate 1 must be CCX with arity 3");
        assert(r2.kind == STURM_GATE_CCX && r2.n == 3u
               && "Fredkin gate 2 must be CCX with arity 3");

        // First Toffoli: (ctrl, b_i, a_i) — lib_swap_dsl(a,b) starts with a^=b
        // → CX(b→a) → at depth 1 lifts to CCX(ctrl, b_i, a_i).
        assert(r0.qubits[0] == qctrl
               && "Fredkin gate 0: qubits[0] must be ctrl");
        assert(r0.qubits[1] == static_cast<uint32_t>(qb[i])
               && "Fredkin gate 0: qubits[1] must be b_i");
        assert(r0.qubits[2] == static_cast<uint32_t>(qa[i])
               && "Fredkin gate 0: qubits[2] must be a_i");

        // Middle Toffoli: (ctrl, a_i, b_i) — b^=a → CX(a→b) → CCX(ctrl,a_i,b_i).
        assert(r1.qubits[0] == qctrl
               && "Fredkin gate 1: qubits[0] must be ctrl");
        assert(r1.qubits[1] == static_cast<uint32_t>(qa[i])
               && "Fredkin gate 1: qubits[1] must be a_i");
        assert(r1.qubits[2] == static_cast<uint32_t>(qb[i])
               && "Fredkin gate 1: qubits[2] must be b_i");

        // Third Toffoli: (ctrl, b_i, a_i) — final a^=b → CCX(ctrl,b_i,a_i).
        assert(r2.qubits[0] == qctrl
               && "Fredkin gate 2: qubits[0] must be ctrl");
        assert(r2.qubits[1] == static_cast<uint32_t>(qb[i])
               && "Fredkin gate 2: qubits[1] must be b_i");
        assert(r2.qubits[2] == static_cast<uint32_t>(qa[i])
               && "Fredkin gate 2: qubits[2] must be a_i");
    }

    // (D) Per-bit super_mask propagation: both operands' super_mask must
    // have bits [0, W) set. Pre-set masks should remain set; if the impl
    // hands an unallocated bit to BitProxy::ensure_quantum, that helper
    // will OR `1<<bit_pos` into mask_ptr. Either way, post-call the bits
    // [0,W) must be set.
    const uint64_t expected_mask = (1ULL << W) - 1ULL;
    assert((a.super_mask & expected_mask) == expected_mask
           && "after depth==1 swap: a.super_mask must have bits [0, W) set");
    assert((b.super_mask & expected_mask) == expected_mask
           && "after depth==1 swap: b.super_mask must have bits [0, W) set");

    // Detach borrowed views before destruction (we manage the qubit pool
    // entries below).
    a.qubits.fill(-1); b.qubits.fill(-1);

    // Release the reserved qubits.
    sturm::QubitPool::instance().release(qctrl_int);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qb[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qa[i]);

    std::printf("  PASS: test_depth1_emits_3W_ccx_with_ordering<W=%zu>\n", W);
}

// ── Test 2: depth == 0 — fast path: zero gates, qubits[] swapped ─────────────
//
// Verifies the existing fast-path contract: with no controls active,
// `sturm::swap(a, b)` is a pure index relabel — zero gates emitted, and
// the qubits[] arrays of the two qint_t are swapped.
template <std::size_t W>
static void test_depth0_zero_gates_swap_qubits() {
    sturm::QubitPool::instance().reset_for_testing();

    std::array<int, W> qa{}, qb{};
    for (std::size_t i = 0; i < W; ++i) qa[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i) qb[i] = sturm::QubitPool::instance().allocate();

    ScopedAppendCtx sc;

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = qa[i];
        b.qubits[i] = qb[i];
    }
    a.value = 0xA; b.value = 0xB;
    a.super_mask = 0x3; b.super_mask = 0xC;
    a.owning_ = false; b.owning_ = false;

    assert(sc.ctx->control_stack.depth() == 0u
           && "control_stack depth must be 0 for the fast-path test");

    const std::size_t pre = sc.ctx->ir.size();
    sturm::swap(a, b);
    const std::size_t emitted = sc.ctx->ir.size() - pre;

    // Zero gates emitted on the depth==0 fast path.
    assert(emitted == 0u
           && "sturm::swap on depth==0 must emit zero gates (index relabel only)");

    // a.qubits[] and b.qubits[] must be swapped.
    for (std::size_t i = 0; i < W; ++i) {
        assert(a.qubits[i] == qb[i]
               && "depth==0 fast-path: a.qubits[i] must hold the original b qubit");
        assert(b.qubits[i] == qa[i]
               && "depth==0 fast-path: b.qubits[i] must hold the original a qubit");
    }

    // Detach borrowed views before destruction.
    a.qubits.fill(-1); b.qubits.fill(-1);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qb[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qa[i]);

    std::printf("  PASS: test_depth0_zero_gates_swap_qubits<W=%zu>\n", W);
}

int main() {
    std::printf("sturm-8dzd: test_when_swap_qint (RED on main, GREEN after sturm-arce)\n");

    // Pin both behaviors at W=2 (small enough for fast read, large enough
    // to confirm per-bit iteration). The depth==0 fast-path is also
    // exercised post-call so a single test binary covers both branches.
    test_depth1_emits_3W_ccx_with_ordering<2>();
    test_depth1_emits_3W_ccx_with_ordering<3>();
    test_depth0_zero_gates_swap_qubits<2>();
    test_depth0_zero_gates_swap_qubits<3>();

    std::printf("All sturm-8dzd tests passed.\n");
    return 0;
}
