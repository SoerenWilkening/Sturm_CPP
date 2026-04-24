// test_when_lossy_gate_equiv.cpp -- LO-0.4 (sturm-0j3a): WHEN-lifted lossy-op
// gate-equivalence under superposed control.
//
// PRD §5 contract for the LO-2 desugar of `WHEN(ctrl) { a &= b; }`:
//   1. Forward `swap(a, tmp_and)` lifts to per-bit Fredkin via BitProxy
//      (`a^=b; b^=a; a^=b` ⇒ 3 CCX per bit ⇒ 3W total).
//   2. Cleanup `invert(lib_c_AND_dsl)(a, b, tmp_and)` lifts to a controlled
//      CCX sweep (`tgt ^= (a & b)` per bit hits BitProxy AndExpr ⇒ 3 CCX
//      via emit_CCX_lifted ⇒ 3W total).
// LO-2 is not yet landed; this test exercises the constituent primitives
// directly to pin the lifting contract LO-2 will rely on.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/c_and_dsl.hpp"
#include "sturm/lib/swap_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
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

int main() {
    std::printf("sturm-0j3a LO-0.4 WHEN-lifted lossy-op gate-equivalence:\n");
    test_per_bit_fredkin_via_bitproxy();
    test_invert_c_and_dsl_lifts_to_ccx_sweep();
    std::printf("All sturm-0j3a tests passed.\n");
    return 0;
}
