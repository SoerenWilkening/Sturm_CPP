// test_destructor_gate.cpp — M2 (transpiler MVP): Gate ~qint_t destructor on
// STURM_AUTO_UNCOMPUTE.
//
// This driver is compiled TWICE via two CMake targets:
//   - test_destructor_gate_on  : built with STURM_AUTO_UNCOMPUTE defined
//   - test_destructor_gate_off : built with STURM_AUTO_UNCOMPUTE undefined
//
// The same source code is used for both; the expected behaviour differs
// based on whether STURM_AUTO_UNCOMPUTE is set at compile time (that is
// the exact contract of M2: the flag toggles the destructor's gate-emission
// and WhenCapture-deferral blocks on/off, while leaving qubit release
// unconditional).
//
// Assertions (per mode):
//   Flag OFF:
//     - A destructor that runs with a non-NONE uncompute op emits ZERO
//       gates to the APPEND-mode IR.
//     - The QubitPool in-use count drops to zero after the owning register
//       is destroyed (release is unconditional).
//   Flag ON:
//     - Same destructor path DOES emit gates (pre-M2 behaviour).
//     - The QubitPool in-use count also drops to zero.
//
// Shared regression (both modes):
//     - A non-owning register (owning_=false) releases nothing in the pool
//       and emits no gates from its own destructor's release step.
//
// Harness: plain assert + main (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using sturm::qint_t;
using sturm::qbool;
using sturm::QubitPool;

// ── Fixture: scoped APPEND context ───────────────────────────────────────────
// Matches the pattern used by tests/backend/test_uncompute_add_const.cpp.
// Installing an APPEND-mode backend makes the destructor's inverse-emission
// path observable via ctx->ir.size().

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
    sturm::BackendContext& bctx() { return *ctx; }
};

// ── Test counters ────────────────────────────────────────────────────────────

static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                    \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while(0)

// ── Test 1: destructor gate emission ─────────────────────────────────────────
// An owning qint_t<W> with a non-NONE uncompute_ is destroyed inside a scope.
// Under STURM_AUTO_UNCOMPUTE, the destructor consults the active context and
// emits inverse gates (IR grows). With the flag OFF, the IR is untouched by
// the destructor.

static void test_destructor_gate_emission() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    // Pre-allocate qubits from the pool so that when the register's
    // destructor releases them we can observe the pool in_use count drop.
    const int q0 = QubitPool::instance().allocate();
    const int q1 = QubitPool::instance().allocate();
    const int q2 = QubitPool::instance().allocate();
    CHECK(QubitPool::instance().in_use() == 3);

    // IR must start empty — we will assert how many records the destructor
    // adds.
    CHECK(sc.ir().size() == 0);

    std::size_t ir_size_at_scope_exit = 0;
    {
        qint_t<3> a;
        a.value      = 7;
        a.super_mask = 0x7u;   // all three bits in superposition
        a.qubits[0]  = q0;
        a.qubits[1]  = q1;
        a.qubits[2]  = q2;
        a.owning_    = true;

        // Stamp an uncompute op. When STURM_AUTO_UNCOMPUTE is ON, the
        // destructor will invoke uncompute_.apply(ctx, view) and the
        // APPEND IR will grow. When the flag is OFF, the destructor skips
        // the entire Strategy-B block and the IR will not grow.
        a.uncompute_ = sturm::uncompute_op::make_add_const(5);

        // Sanity: no records emitted yet — only the destructor can add them
        // (we did not call operator+ here).
        ir_size_at_scope_exit = sc.ir().size();
        CHECK(ir_size_at_scope_exit == 0);
    }

    const std::size_t ir_size_after_dtor = sc.ir().size();

#ifdef STURM_AUTO_UNCOMPUTE
    // Flag ON: destructor emitted at least one inverse gate via the
    // active BackendContext.  The exact count is decided by
    // qint_base::sub_const; we just require strict growth so the
    // regression guard catches any silent disablement.
    CHECK(ir_size_after_dtor > ir_size_at_scope_exit);
#else
    // Flag OFF: destructor is release-only; the APPEND IR must be
    // untouched.
    CHECK(ir_size_after_dtor == ir_size_at_scope_exit);
    CHECK(ir_size_after_dtor == 0);
#endif

    // Qubit release is UNCONDITIONAL on STURM_AUTO_UNCOMPUTE.  All three
    // owning qubits must have been returned to the pool regardless of mode.
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 2: non-owning view releases nothing in either mode ──────────────────
// Shared regression: destroying a non-owning qint_t view must NOT release
// qubits, and must NOT emit gates from its destructor's release step (it has
// no uncompute op stamped and owning_=false disables the release loop).

static void test_non_owning_no_release_either_mode() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    const int q0 = QubitPool::instance().allocate();
    const int q1 = QubitPool::instance().allocate();
    CHECK(QubitPool::instance().in_use() == 2);

    const std::size_t ir_before = sc.ir().size();

    {
        std::array<int, 2> idx = {q0, q1};
        auto view = qint_t<2>::make_non_owning(idx, /*value=*/0,
                                               /*mask=*/0ULL);
        CHECK(view.owning_ == false);
        // No uncompute op, no qubit release: destructor is a no-op in
        // both modes.
    }

    // Pool unchanged — the view did not release the qubits.
    CHECK(QubitPool::instance().in_use() == 2);
    // IR unchanged — the view did not emit anything from its destructor.
    CHECK(sc.ir().size() == ir_before);

    // Clean up.
    QubitPool::instance().release(q0);
    QubitPool::instance().release(q1);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 3: non-owning qbool releases nothing in either mode ─────────────────
// qbool inherits through qint_t<1>'s default destructor — M2 requires
// that qbool.hpp is NOT modified.  This test is the acceptance-criterion
// regression that qbool::make_non_owning behaves consistently.

static void test_non_owning_qbool_no_release_either_mode() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    const int q0 = QubitPool::instance().allocate();
    CHECK(QubitPool::instance().in_use() == 1);

    const std::size_t ir_before = sc.ir().size();

    {
        auto b = qbool::make_non_owning(q0);
        CHECK(b.owning_ == false);
        // b goes out of scope — qint_t<1>::~qint_t runs.  Non-owning +
        // no uncompute op → destructor is a no-op in both modes.
    }

    CHECK(QubitPool::instance().in_use() == 1);
    CHECK(sc.ir().size() == ir_before);

    // Clean up.
    QubitPool::instance().release(q0);
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Test 4: pool release count matches owning-qubit count ────────────────────
// Acceptance criterion: "QubitPool release count matches owning qubits" in
// both modes.  We allocate N qubits, let a single owning register destruct,
// and verify that exactly N qubits come back into the free-list.  This is
// the invariant that makes the release path "unconditional" meaningful.

static void test_release_count_matches_owning_qubits() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    const int q0 = QubitPool::instance().allocate();
    const int q1 = QubitPool::instance().allocate();
    const int q2 = QubitPool::instance().allocate();
    const int q3 = QubitPool::instance().allocate();
    CHECK(QubitPool::instance().in_use() == 4);

    {
        qint_t<4> a;
        a.qubits[0] = q0;
        a.qubits[1] = q1;
        a.qubits[2] = q2;
        a.qubits[3] = q3;
        a.owning_   = true;
        // No uncompute op stamped here — ensures release is the only
        // thing that should happen in OFF mode, and release + (no-op
        // Strategy-B because tag==NONE) in ON mode.
    }

    // Independent of the flag, all 4 owning qubits returned to the pool.
    CHECK(QubitPool::instance().in_use() == 0);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
#ifdef STURM_AUTO_UNCOMPUTE
    std::printf("M2 destructor-gate tests (STURM_AUTO_UNCOMPUTE=ON):\n");
#else
    std::printf("M2 destructor-gate tests (STURM_AUTO_UNCOMPUTE=OFF):\n");
#endif
    test_destructor_gate_emission();
    test_non_owning_no_release_either_mode();
    test_non_owning_qbool_no_release_either_mode();
    test_release_count_matches_owning_qubits();

    std::printf("%s  (%d/%d passed)\n",
                tests_pass == tests_run ? "PASS" : "FAIL",
                tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
