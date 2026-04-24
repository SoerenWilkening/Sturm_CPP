// test_c_and_dsl_adjoint.cpp -- LO-1c (sturm-eum8): lib_c_AND_dsl adjoint roundtrip.
//
// Contract: after `invert(lib_c_AND_dsl)(c0, c1, tgt)` runs in the PRD §2.2
// swap-then-uncompute shape, the target register `tgt` (tmp_and post-swap-undo)
// is returned to |0>. See docs/plan_lossy_compound_reversibility.md §3 LO-1c.
//
// The CCX sweep `tgt ^= (c0 & c1)` is self-inverse (CCX is its own adjoint),
// so __lib_c_AND_dsl_adj is structurally identical to the forward. We still
// register an explicit adjoint so `invert(lib_c_AND_dsl)(…)` resolves at the
// cleanup call site the LO-2 rewrite will emit.
//
// Invariants pinned:
//   (1) `lib_c_AND_dsl(c0, c1, tgt)` with tgt starting at |0> puts tgt in
//       state (c0 & c1).
//   (2) `sturm::invert<&sturm::lib_c_AND_dsl<BitProxy>>()` resolves to the
//       registered `__lib_c_AND_dsl_adj`.
//   (3) Calling the adjoint on (c0, c1, tgt) after the forward zeros tgt.
//   (4) Double-apply the sweep also zeros tgt — self-inverse property.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/c_and_dsl.hpp"
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

// invert(lib_c_AND_dsl) resolves and zeros tgt after forward.
static void run_adjoint_case(uint32_t c0v, uint32_t c1v) {
    sturm::QubitPool::instance().reset_for_testing();
    int res[3];
    for (int i = 0; i < 3; ++i) res[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{3u, 64u};
    if (c0v) orkan::apply_x(sc.sv(), 0u);
    if (c1v) orkan::apply_x(sc.sv(), 1u);

    sturm::qbool c0q = sturm::qbool::make_non_owning(0);
    sturm::qbool c1q = sturm::qbool::make_non_owning(1);
    sturm::qbool tq  = sturm::qbool::make_non_owning(2);
    sturm::BitProxy c0(c0q), c1(c1q), tgt(tq);

    sturm::lib_c_AND_dsl(c0, c1, tgt);
    const uint32_t expect_and = (c0v & c1v) & 1u;
    assert(read_qubit(sc.sv(), 2u, 3u) == expect_and && "forward: tgt == c0 & c1");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_c_AND_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_c_AND_dsl<BitProxy>>() must resolve to a registered adjoint");
    adj_ptr(c0, c1, tgt);
    assert(read_qubit(sc.sv(), 2u, 3u) == 0u
           && "adjoint: tgt register returned to |0> post-swap-undo");

    assert(read_qubit(sc.sv(), 0u, 3u) == c0v && "c0 preserved");
    assert(read_qubit(sc.sv(), 1u, 3u) == c1v && "c1 preserved");

    for (int i = 0; i < 3; ++i) sturm::QubitPool::instance().release(res[i]);
}

// Double-apply the forward sweep — self-inverse property.
static void run_double_apply_case(uint32_t c0v, uint32_t c1v) {
    sturm::QubitPool::instance().reset_for_testing();
    int res[3];
    for (int i = 0; i < 3; ++i) res[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{3u, 64u};
    if (c0v) orkan::apply_x(sc.sv(), 0u);
    if (c1v) orkan::apply_x(sc.sv(), 1u);

    sturm::qbool c0q = sturm::qbool::make_non_owning(0);
    sturm::qbool c1q = sturm::qbool::make_non_owning(1);
    sturm::qbool tq  = sturm::qbool::make_non_owning(2);
    sturm::BitProxy c0(c0q), c1(c1q), tgt(tq);

    sturm::lib_c_AND_dsl(c0, c1, tgt);
    sturm::lib_c_AND_dsl(c0, c1, tgt);
    assert(read_qubit(sc.sv(), 2u, 3u) == 0u
           && "double-apply: CCX is self-inverse");
    assert(read_qubit(sc.sv(), 0u, 3u) == c0v && "c0 preserved after double-apply");
    assert(read_qubit(sc.sv(), 1u, 3u) == c1v && "c1 preserved after double-apply");

    for (int i = 0; i < 3; ++i) sturm::QubitPool::instance().release(res[i]);
}

int main() {
    std::printf("sturm-eum8 LO-1c: lib_c_AND_dsl adjoint tests:\n");
    run_adjoint_case(0u, 0u); std::puts("  PASS: c_and_dsl_adjoint 00");
    run_adjoint_case(0u, 1u); std::puts("  PASS: c_and_dsl_adjoint 01");
    run_adjoint_case(1u, 0u); std::puts("  PASS: c_and_dsl_adjoint 10");
    run_adjoint_case(1u, 1u); std::puts("  PASS: c_and_dsl_adjoint 11");
    run_double_apply_case(0u, 0u); std::puts("  PASS: c_and_dsl_double 00");
    run_double_apply_case(0u, 1u); std::puts("  PASS: c_and_dsl_double 01");
    run_double_apply_case(1u, 0u); std::puts("  PASS: c_and_dsl_double 10");
    run_double_apply_case(1u, 1u); std::puts("  PASS: c_and_dsl_double 11");
    std::printf("All sturm-eum8 tests passed.\n");
    return 0;
}
