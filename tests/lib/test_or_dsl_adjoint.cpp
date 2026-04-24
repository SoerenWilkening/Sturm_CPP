// test_or_dsl_adjoint.cpp -- LO-1d (sturm-le6w): lib_or_dsl adjoint roundtrip.
//
// Contract: after `invert(lib_or_dsl)(a, b, c)` runs in the PRD §2.2
// swap-then-uncompute shape, the target register `c` (tmp_or post-swap-undo)
// is returned to |0>. See docs/plan_lossy_compound_reversibility.md §3 LO-1d.
//
// Invariants pinned:
//   (1) `lib_or_dsl(a, b, c)` with c starting at |0> puts c in state (a | b).
//   (2) `sturm::invert(&sturm::lib_or_dsl<BitProxy>)` resolves to the
//       registered `__lib_or_dsl_adj`.
//   (3) Calling the adjoint on (a, b, c) after the forward zeros c
//       (statevector readout == 0).
//
// The canonical LO-2 rewrite for `a |= b` emits the forward as
//   or_oop(a, b, tmp_or); swap(a, tmp_or);
// and the scope-exit cleanup as
//   swap(a, tmp_or); invert(lib_or_dsl)(a, b, tmp_or);
// so the adjoint is invoked with `tmp_or` already holding `a | b` (the swap
// has been undone by the cleanup's first line). This test models that by
// preparing c to hold a|b and then asserting the adjoint zeros c.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/logic_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
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

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
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

static uint32_t read_qubit(orkan::state_t& sv, uint32_t q, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

static void run_truth_case(uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();
    int res[3];
    for (int i = 0; i < 3; ++i) res[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{3u, 64u};
    if (a_val) orkan::apply_x(sc.sv(), 0u);
    if (b_val) orkan::apply_x(sc.sv(), 1u);
    // c starts in |0>.

    sturm::qbool aq = sturm::qbool::make_non_owning(0);
    sturm::qbool bq = sturm::qbool::make_non_owning(1);
    sturm::qbool cq = sturm::qbool::make_non_owning(2);
    sturm::BitProxy a(aq), b(bq), c(cq);

    // Forward: c = a | b via lib_or_dsl (BitProxy path: CX + CX + CCX, no anc).
    sturm::lib_or_dsl(a, b, c);
    const uint32_t expect_or = (a_val | b_val) & 1u;
    assert(read_qubit(sc.sv(), 2u, 3u) == expect_or && "forward: c == a|b");

    // Adjoint: invert(lib_or_dsl)(a, b, c) must zero c (PRD §3.1 equivalent
    // for OR). The swap-and-uncompute scope-exit shape is modelled by calling
    // the adjoint on the same (a, b, c) with c already holding a|b.
    constexpr auto adj_ptr =
        sturm::invert(&sturm::lib_or_dsl<sturm::BitProxy>);
    static_assert(adj_ptr != nullptr,
                  "invert(lib_or_dsl<BitProxy>) must resolve to a registered adjoint");
    adj_ptr(a, b, c);
    assert(read_qubit(sc.sv(), 2u, 3u) == 0u
           && "adjoint: c register returned to |0> post-swap-undo");

    // Inputs a, b must be preserved by both forward and adjoint.
    assert(read_qubit(sc.sv(), 0u, 3u) == a_val && "a preserved");
    assert(read_qubit(sc.sv(), 1u, 3u) == b_val && "b preserved");

    for (int i = 0; i < 3; ++i) sturm::QubitPool::instance().release(res[i]);
}

static void test_roundtrip_00() { run_truth_case(0u, 0u);
    std::puts("  PASS: or_dsl_adjoint a=0 b=0"); }
static void test_roundtrip_01() { run_truth_case(0u, 1u);
    std::puts("  PASS: or_dsl_adjoint a=0 b=1"); }
static void test_roundtrip_10() { run_truth_case(1u, 0u);
    std::puts("  PASS: or_dsl_adjoint a=1 b=0"); }
static void test_roundtrip_11() { run_truth_case(1u, 1u);
    std::puts("  PASS: or_dsl_adjoint a=1 b=1"); }

int main() {
    std::printf("sturm-le6w LO-1d: lib_or_dsl adjoint tests:\n");
    test_roundtrip_00();
    test_roundtrip_01();
    test_roundtrip_10();
    test_roundtrip_11();
    std::printf("All sturm-le6w tests passed.\n");
    return 0;
}
