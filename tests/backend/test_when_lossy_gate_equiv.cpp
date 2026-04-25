// test_when_lossy_gate_equiv.cpp -- LO-0.4 (sturm-0j3a) + sturm-hfzc:
// WHEN-lifted lossy-op gate-equivalence under superposed control.
//
// PRD §5 contract for the LO-2 desugar of `WHEN(ctrl) { a &= b; }`:
//   1. Forward `swap(a, tmp_and)` lifts to per-bit Fredkin via BitProxy
//      (`a^=b; b^=a; a^=b` ⇒ 3 CCX per bit ⇒ 3W total).
//   2. Cleanup `invert(lib_c_AND_dsl)(a, b, tmp_and)` lifts to a controlled
//      CCX sweep (`tgt ^= (a & b)` per bit hits BitProxy AndExpr ⇒ 3 CCX
//      via emit_CCX_lifted ⇒ 3W total).
// LO-2 is not yet landed; this test exercises the constituent primitives
// directly to pin the lifting contract LO-2 will rely on.
//
// sturm-hfzc (RED-bar for epic sturm-8aq3): the LO-2 transpiler emits
// unqualified `swap(<lhs>, __sturm_tmp_<op>_<N>);` after every lossy
// compound assignment. ADL resolves that to
// `sturm::swap(qint_t<W>&, qint_t<W>&)` defined in
// `include/sturm/qtypes/lossy_oop.hpp`, which today is NOT control-aware:
// it always relabels indices and emits zero gates regardless of WHEN
// depth. This test pins the equivalence between the transpiler-emitted
// shape (calls `sturm::swap(qint_t<W>&, qint_t<W>&)` under WHEN) and the
// reference shape (per-bit `lib_swap_dsl<BitProxy>` under WHEN). Today
// the two diverge — the LO shape records zero CCX for the swap while
// the reference records 3*W per swap — so this test FAILS on main.
// After sibling sturm-arce makes `sturm::swap(qint_t<W>&, qint_t<W>&)`
// consult `control_stack.depth()` and dispatch to per-bit `lib_swap_dsl`,
// the two IRs become byte-for-byte equal and the test passes.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/c_and_dsl.hpp"
#include "sturm/lib/logic_dsl.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/lib/swap_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/lossy_oop.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx; sturm_backend_context_t* prev;
    explicit ScopedAppendCtx(uint32_t mq = 64u) {
        ctx = sturm_backend_create(STURM_MODE_APPEND, mq); assert(ctx);
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
};

static std::size_t count_kind(const sturm::GateIR& ir, sturm_gate_kind_t k) {
    std::size_t n = 0u;
    for (std::size_t i = 0; i < ir.size(); ++i) if (ir.at(i).kind == k) ++n;
    return n;
}

static constexpr std::size_t W = 2;

static int alloc_regs(int* out, std::size_t n_regs) {
    sturm::QubitPool::instance().reset_for_testing();
    for (std::size_t i = 0; i < n_regs * W; ++i)
        out[i] = sturm::QubitPool::instance().allocate();
    return sturm::QubitPool::instance().allocate();
}
static void release_regs(int* regs, std::size_t n_regs, int qctrl) {
    sturm::QubitPool::instance().release(qctrl);
    for (std::size_t i = n_regs * W; i-- > 0;)
        sturm::QubitPool::instance().release(regs[i]);
}

// Per-bit Fredkin via BitProxy under WHEN(ctrl): 3W CCX.
static void test_per_bit_fredkin_via_bitproxy() {
    int regs[2 * W];
    const int qctrl = alloc_regs(regs, 2);
    int* qa = regs; int* qtmp = regs + W;
    ScopedAppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qctrl);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    WHEN(ctrl) {
        for (std::size_t i = 0; i < W; ++i) {
            sturm::qbool aq = sturm::qbool::make_non_owning(qa[i]);
            sturm::qbool tq = sturm::qbool::make_non_owning(qtmp[i]);
            sturm::BitProxy a(aq), t(tq);
            sturm::lib_swap_dsl<sturm::BitProxy>(a, t);
        }
    }
    assert(count_kind(sc.ctx->ir, STURM_GATE_CCX) == 3u * W &&
           "WHEN-lifted swap must emit 3*W CCX (per-bit Fredkin via BitProxy)");
    release_regs(regs, 2, qctrl);
    std::puts("  PASS: test_per_bit_fredkin_via_bitproxy");
}

// invert(lib_c_AND_dsl) per-bit under WHEN(ctrl): 3W CCX via emit_CCX_lifted.
static void test_invert_c_and_dsl_lifts_to_ccx_sweep() {
    int regs[3 * W];
    const int qctrl = alloc_regs(regs, 3);
    int* qa = regs; int* qb = regs + W; int* qtmp = regs + 2 * W;
    ScopedAppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qctrl);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    constexpr auto adj = sturm::invert<&sturm::lib_c_AND_dsl<sturm::BitProxy>>();
    static_assert(adj != nullptr, "invert<&lib_c_AND_dsl<BitProxy>>() must resolve");
    WHEN(ctrl) {
        for (std::size_t i = 0; i < W; ++i) {
            sturm::qbool aq = sturm::qbool::make_non_owning(qa[i]);
            sturm::qbool bq = sturm::qbool::make_non_owning(qb[i]);
            sturm::qbool tq = sturm::qbool::make_non_owning(qtmp[i]);
            sturm::BitProxy a(aq), b(bq), t(tq);
            adj(a, b, t);
        }
    }
    assert(count_kind(sc.ctx->ir, STURM_GATE_CCX) == 3u * W &&
           "WHEN-lifted invert(lib_c_AND_dsl) per bit must emit 3 CCX");
    release_regs(regs, 3, qctrl);
    std::puts("  PASS: test_invert_c_and_dsl_lifts_to_ccx_sweep");
}

// ───────────────────────────────────────────────────────────────────────────
// sturm-hfzc: end-to-end gate-equivalence under WHEN(ctrl) for the LO-2
// emitter-text shape vs a per-bit Fredkin reference. The LO-2 transpiler
// emits unqualified `swap(a, tmp)` after each lossy compound assignment;
// that resolves via ADL to `sturm::swap(qint_t<W>&, qint_t<W>&)`. Today
// that overload emits zero gates inside WHEN(ctrl), violating PRD §5.1.
// The reference shape uses per-bit `lib_swap_dsl<BitProxy>` under the
// SAME WhenGuard, which lifts each XOR triple into a 3-CCX Fredkin.
//
// Test strategy: APPEND mode (gate IR recording, no state vector). For
// each run we reset the qubit pool so allocator indices align across the
// two runs; we then capture the IR vector from `sc.ctx->ir`. Byte-for-
// byte equality of the recorded gate lists is the assertion (matching
// the existing `test_when_lossy_gate_equiv_full.cpp` style for `tests/
// backend/`).
//
// Coverage: `WHEN(ctrl) { a *= b; }` (the marquee mul case) + `WHEN(ctrl)
// { a &= b; }` (the single-ancilla family). Both fail before sturm-arce
// lands; both pass after. ctrl is given super_mask=1 (treated as
// superposed by WhenGuard), matching the (|0>+|1>)/sqrt(2) pattern the
// LO-2 desugar must support.

using IR = std::vector<sturm::GateRecord>;

static IR snap_ir(const sturm::GateIR& ir) {
    IR v; v.reserve(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i) v.push_back(ir.at(i));
    return v;
}

static bool ir_equal(const IR& a, const IR& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.kind != y.kind || x.n != y.n) return false;
        if (x.qubits[0] != y.qubits[0] || x.qubits[1] != y.qubits[1]
            || x.qubits[2] != y.qubits[2]) return false;
    }
    return true;
}

template <std::size_t Wa>
static void wire_qint_view(sturm::qint_t<Wa>& q, const int* idx) {
    for (std::size_t i = 0; i < Wa; ++i) q.qubits[i] = idx[i];
    q.super_mask = (Wa >= 64) ? ~0ULL : ((1ULL << Wa) - 1ULL);
    q.owning_ = false;
}

// Per-bit Fredkin via BitProxy, calling lib_swap_dsl<BitProxy> per bit.
// Used by the reference shape; the WhenGuard at the call site lifts each
// `^=` inside lib_swap_dsl into a controlled CCX.
template <std::size_t Wa>
static void per_bit_fredkin(sturm::qint_t<Wa>& a, sturm::qint_t<Wa>& b) {
    for (std::size_t i = 0; i < Wa; ++i) {
        sturm::BitProxy x(a, i), y(b, i);
        sturm::lib_swap_dsl<sturm::BitProxy>(x, y);
    }
}

// LO transpiler-emitted shape for `WHEN(ctrl) { a *= b; }`:
//   mul_oop(a, b, tmp); swap(a, tmp); swap(a, tmp); mul_oop_adj(a, b, tmp);
// The unqualified `swap` resolves to `sturm::swap(qint_t<W>&, qint_t<W>&)`
// via ADL — the buggy zero-gate path under WHEN(ctrl) on main.
template <std::size_t Wa>
static IR run_lo_emitter_mul(int qctrl, const int* qa, const int* qb) {
    ScopedAppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qctrl);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    sturm::qint_t<Wa> a, b; wire_qint_view(a, qa); wire_qint_view(b, qb);
    sturm::qint_t<Wa> tmp;  // owning_=true: will allocate fresh tmp.qubits in mul_oop
    WHEN(ctrl) {
        sturm::mul_oop(a, b, tmp);
        sturm::swap(a, tmp);   // BUGGY: zero gates under WHEN today.
        sturm::swap(a, tmp);   // Reverse swap.
        sturm::mul_oop_adj(a, b, tmp);
    }
    return snap_ir(sc.ctx->ir);
}

// Reference shape: same forward op, but the two swaps are per-bit Fredkin
// via BitProxy under the SAME WhenGuard. Each bit's `lib_swap_dsl<BitProxy>`
// emits 3 CCX -> 3*W CCX per swap -> 6*W extra CCX vs the LO shape.
template <std::size_t Wa>
static IR run_ref_mul(int qctrl, const int* qa, const int* qb) {
    ScopedAppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qctrl);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    sturm::qint_t<Wa> a, b; wire_qint_view(a, qa); wire_qint_view(b, qb);
    sturm::qint_t<Wa> tmp;
    WHEN(ctrl) {
        sturm::mul_oop(a, b, tmp);
        per_bit_fredkin(a, tmp);
        per_bit_fredkin(a, tmp);
        sturm::mul_oop_adj(a, b, tmp);
    }
    return snap_ir(sc.ctx->ir);
}

// LO shape and reference for `WHEN(ctrl) { a &= b; }`. Same structure as
// mul, but and_oop is single-ancilla (low-W tmp only, no upper-half stash).
template <std::size_t Wa>
static IR run_lo_emitter_and(int qctrl, const int* qa, const int* qb) {
    ScopedAppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qctrl);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    sturm::qint_t<Wa> a, b; wire_qint_view(a, qa); wire_qint_view(b, qb);
    sturm::qint_t<Wa> tmp;
    WHEN(ctrl) {
        sturm::and_oop(a, b, tmp);
        sturm::swap(a, tmp);
        sturm::swap(a, tmp);
        sturm::and_oop_adj(a, b, tmp);
    }
    return snap_ir(sc.ctx->ir);
}
template <std::size_t Wa>
static IR run_ref_and(int qctrl, const int* qa, const int* qb) {
    ScopedAppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qctrl);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    sturm::qint_t<Wa> a, b; wire_qint_view(a, qa); wire_qint_view(b, qb);
    sturm::qint_t<Wa> tmp;
    WHEN(ctrl) {
        sturm::and_oop(a, b, tmp);
        per_bit_fredkin(a, tmp);
        per_bit_fredkin(a, tmp);
        sturm::and_oop_adj(a, b, tmp);
    }
    return snap_ir(sc.ctx->ir);
}

// Allocate `2*Wa` operand qubits + 1 ctrl qubit deterministically; the
// caller resets the pool first so both LO and ref runs see identical
// operand indices and identical allocator state for tmp + ancillas.
template <std::size_t Wa>
static int alloc_2reg(int* out_a, int* out_b) {
    sturm::QubitPool::instance().reset_for_testing();
    for (std::size_t i = 0; i < Wa; ++i) out_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wa; ++i) out_b[i] = sturm::QubitPool::instance().allocate();
    return sturm::QubitPool::instance().allocate();
}

// sturm-hfzc: WHEN(ctrl){ a *= b; } LO shape vs reference. RED on main.
static void test_when_mul_assign_swap_equivalence() {
    constexpr std::size_t Wa = 1u;  // mul_oop allocates 2W tmp ancillas + scratch
    int qa[Wa], qb[Wa]; int qctrl;
    qctrl = alloc_2reg<Wa>(qa, qb);
    IR lo = run_lo_emitter_mul<Wa>(qctrl, qa, qb);
    qctrl = alloc_2reg<Wa>(qa, qb);
    IR ref = run_ref_mul<Wa>(qctrl, qa, qb);
    if (!ir_equal(lo, ref)) {
        std::fprintf(stderr,
            "  FAIL: WHEN(ctrl){ a *= b; } -- LO emitter shape (sturm::swap "
            "qint_t<W>) IR differs from per-bit Fredkin reference. "
            "lo.size=%zu ref.size=%zu (expected %zu extra CCX in ref for "
            "two W=%zu Fredkin swaps)\n",
            lo.size(), ref.size(), 6u * Wa, Wa);
        std::abort();
    }
    std::puts("  PASS: test_when_mul_assign_swap_equivalence");
}

// sturm-hfzc: WHEN(ctrl){ a &= b; } LO shape vs reference. RED on main.
static void test_when_and_assign_swap_equivalence() {
    constexpr std::size_t Wa = 2u;
    int qa[Wa], qb[Wa]; int qctrl;
    qctrl = alloc_2reg<Wa>(qa, qb);
    IR lo = run_lo_emitter_and<Wa>(qctrl, qa, qb);
    qctrl = alloc_2reg<Wa>(qa, qb);
    IR ref = run_ref_and<Wa>(qctrl, qa, qb);
    if (!ir_equal(lo, ref)) {
        std::fprintf(stderr,
            "  FAIL: WHEN(ctrl){ a &= b; } -- LO emitter shape (sturm::swap "
            "qint_t<W>) IR differs from per-bit Fredkin reference. "
            "lo.size=%zu ref.size=%zu (expected %zu extra CCX in ref for "
            "two W=%zu Fredkin swaps)\n",
            lo.size(), ref.size(), 6u * Wa, Wa);
        std::abort();
    }
    std::puts("  PASS: test_when_and_assign_swap_equivalence");
}

int main() {
    std::printf("sturm-0j3a LO-0.4 + sturm-hfzc WHEN-lifted lossy-op "
                "gate-equivalence:\n");
    test_per_bit_fredkin_via_bitproxy();
    test_invert_c_and_dsl_lifts_to_ccx_sweep();
    test_when_mul_assign_swap_equivalence();
    test_when_and_assign_swap_equivalence();
    std::printf("All sturm-0j3a + sturm-hfzc tests passed.\n");
    return 0;
}
