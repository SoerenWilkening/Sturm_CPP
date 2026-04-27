// depth_invariant_test.cpp -- sturm-a3t4.7 P6
//
// Regression net for the depth-1 control stack invariant (sturm-a3t4 epic).
// Exercises every public op (add_mod, mul_mod, pow_mod, div, mul, plus a
// trivial qbool::flip) both bare and inside WHEN(c), and asserts that
// `current_control_stack().depth()` never exceeds 1 across any invocation.
//
// Mechanism: the test installs the `set_execute_gate_hook` observer added in
// sturm-a3t4.7 (see include/sturm/core/context.hpp).  execute_gate fires the
// hook after the gate-count increment and before mode dispatch, so the hook
// sees the control stack at every gate emission site.  A thread-local
// `g_max_depth` is updated to max(g_max_depth, depth()) on each call; we
// reset it before each op and assert <= 1 after.
//
// Coverage rationale (issue body): "modular-arithmetic test surface in this
// file should be reduced -- one operand triple per op, one width, with/
// without WHEN.  Goal is invariant coverage, not exhaustive correctness
// (existing functional tests cover correctness)."
//
// Failure mode: if a future op were tempted to call
// ControlStack::push_control() while a control was already live (e.g. by
// regressing the lift pattern back to raw push/pop), depth() would observe
// 2 inside the body, max_depth would record 2, and the assert below would
// trip with a clear depth>1 message pointing back at this file.  The
// runtime push_control assert (control_stack.hpp) gives the same diagnostic
// when assertions are enabled, but this test gives an explicit trip-wire
// regardless of NDEBUG status (it aborts via std::abort() if max>1).

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/add_mod_dsl.hpp"
#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/lib/pow_mod_dsl.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/control_stack.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ── Hook state ───────────────────────────────────────────────────────────────
//
// The hook runs inside execute_gate after the gate_count increment.  We
// poll the active control stack and update the running maximum.  Both the
// max and the call counter are file-local so the hook is reentrancy-safe
// for our single-threaded test driver (the test never spawns threads).

static std::uint32_t g_max_depth = 0u;
static std::uint64_t g_hook_calls = 0u;

static void depth_polling_hook(sturm::BackendContext& /*ctx*/,
                               sturm_gate_kind_t      /*kind*/,
                               const std::uint32_t*   /*qubits*/,
                               std::uint8_t           /*n*/,
                               double                 /*param*/) {
    const std::uint32_t d = sturm::current_control_stack().depth();
    if (d > g_max_depth) g_max_depth = d;
    ++g_hook_calls;
}

static void reset_hook_state() {
    g_max_depth  = 0u;
    g_hook_calls = 0u;
}

// ── Test harness ─────────────────────────────────────────────────────────────
//
// Each op runs inside its own context to keep allocator state independent.
// All ops run in APPEND mode -- we only care about depth observation, not
// correctness, so we don't need an Orkan statevector.  The hook fires
// before mode dispatch, so APPEND vs SIMULATE is irrelevant for this test.

static constexpr std::size_t W = 2;

namespace {

struct CtxScope {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    CtxScope() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 256u);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~CtxScope() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// Allocate a W-bit register from the pool and wrap it in BitProxy entries.
struct Reg {
    int               qi[W];
    sturm::qbool      owners[W];
    sturm::BitProxy   bits[W];
    void allocate() {
        for (std::size_t i = 0; i < W; ++i) {
            qi[i]      = sturm::QubitPool::instance().allocate();
            owners[i]  = sturm::qbool::make_non_owning(qi[i]);
            bits[i]    = sturm::BitProxy(owners[i]);
        }
    }
    void release() {
        for (std::size_t i = W; i-- > 0;)
            sturm::QubitPool::instance().release(qi[i]);
    }
};

// Build the outer control `c` for the WHEN-wrapped variant.  super_mask=1
// forces WhenGuard down the superposed branch so the lift's active_control()
// path runs; value=1 keeps the body live for any APPEND-mode gate emission.
struct OuterControl {
    int qi{-1};
    sturm::qbool owner;
    void allocate() {
        qi    = sturm::QubitPool::instance().allocate();
        owner = sturm::qbool::make_non_owning(qi, /*val=*/1, /*mask=*/1ULL);
    }
    void release() {
        if (qi >= 0) sturm::QubitPool::instance().release(qi);
    }
};

}  // namespace

// Run `body` (bare and WHEN(c)-wrapped) and assert the polled max stack
// depth never exceeds 1.  `label` shows up in the per-case PASS line.
template <class Body>
static void run_op(const char* label, Body&& body) {
    // ── Bare ────────────────────────────────────────────────────────────
    {
        sturm::QubitPool::instance().reset_for_testing();
        CtxScope sc;
        reset_hook_state();
        sturm::set_execute_gate_hook(&depth_polling_hook);
        body(/*wrap_in_when=*/false);
        sturm::set_execute_gate_hook(nullptr);
        if (g_hook_calls == 0u) {
            std::fprintf(stderr,
                         "  FAIL: %s [bare]: hook never fired (op emitted no "
                         "gates) -- test cannot prove the invariant\n", label);
            std::abort();
        }
        if (g_max_depth > 1u) {
            std::fprintf(stderr,
                         "  FAIL: %s [bare]: max control_stack depth = %u "
                         "(expected <= 1; depth-1 invariant breached)\n",
                         label, g_max_depth);
            std::abort();
        }
        std::printf("  PASS: %s [bare]: max_depth=%u over %llu gate(s)\n",
                    label, g_max_depth,
                    static_cast<unsigned long long>(g_hook_calls));
    }

    // ── WHEN(c)-wrapped ─────────────────────────────────────────────────
    {
        sturm::QubitPool::instance().reset_for_testing();
        CtxScope sc;
        reset_hook_state();
        sturm::set_execute_gate_hook(&depth_polling_hook);
        body(/*wrap_in_when=*/true);
        sturm::set_execute_gate_hook(nullptr);
        if (g_hook_calls == 0u) {
            std::fprintf(stderr,
                         "  FAIL: %s [WHEN(c)]: hook never fired -- test "
                         "cannot prove the invariant\n", label);
            std::abort();
        }
        if (g_max_depth > 1u) {
            std::fprintf(stderr,
                         "  FAIL: %s [WHEN(c)]: max control_stack depth = %u "
                         "(expected <= 1; depth-1 invariant breached -- "
                         "library op skipped the outer & flag + WHEN lift)\n",
                         label, g_max_depth);
            std::abort();
        }
        std::printf("  PASS: %s [WHEN(c)]: max_depth=%u over %llu gate(s)\n",
                    label, g_max_depth,
                    static_cast<unsigned long long>(g_hook_calls));
    }
}

// ── Per-op bodies ────────────────────────────────────────────────────────────
//
// One operand triple per op (per the issue: "one operand triple per op,
// one width, with/without WHEN").  All bodies allocate fresh registers
// from the pool (reset by run_op), wrap them in BitProxy, and -- when
// wrap_in_when -- invoke the op inside a WHEN(c) scope where c carries
// super_mask=1 so the lift's active_control() path runs.

static void body_add_mod(bool wrap_in_when) {
    Reg a, b, n, r; a.allocate(); b.allocate(); n.allocate(); r.allocate();
    OuterControl c; if (wrap_in_when) c.allocate();
    if (wrap_in_when) {
        WHEN(c.owner) {
            sturm::lib_add_mod_dsl<sturm::BitProxy>(
                a.bits, b.bits, n.bits, W, r.bits);
        }
    } else {
        sturm::lib_add_mod_dsl<sturm::BitProxy>(
            a.bits, b.bits, n.bits, W, r.bits);
    }
    if (wrap_in_when) c.release();
    r.release(); n.release(); b.release(); a.release();
}

static void body_mul_mod(bool wrap_in_when) {
    Reg a, b, n, r; a.allocate(); b.allocate(); n.allocate(); r.allocate();
    OuterControl c; if (wrap_in_when) c.allocate();
    if (wrap_in_when) {
        WHEN(c.owner) {
            sturm::lib_mul_mod_dsl<sturm::BitProxy>(
                a.bits, b.bits, n.bits, W, r.bits);
        }
    } else {
        sturm::lib_mul_mod_dsl<sturm::BitProxy>(
            a.bits, b.bits, n.bits, W, r.bits);
    }
    if (wrap_in_when) c.release();
    r.release(); n.release(); b.release(); a.release();
}

static void body_pow_mod(bool wrap_in_when) {
    Reg base, exp, n, r;
    base.allocate(); exp.allocate(); n.allocate(); r.allocate();
    OuterControl c; if (wrap_in_when) c.allocate();
    if (wrap_in_when) {
        WHEN(c.owner) {
            sturm::lib_pow_mod_dsl<sturm::BitProxy>(
                base.bits, exp.bits, n.bits, W, r.bits);
        }
    } else {
        sturm::lib_pow_mod_dsl<sturm::BitProxy>(
            base.bits, exp.bits, n.bits, W, r.bits);
    }
    if (wrap_in_when) c.release();
    r.release(); n.release(); exp.release(); base.release();
}

static void body_mul(bool wrap_in_when) {
    Reg a, b;
    a.allocate(); b.allocate();
    int qi_r[2 * W];
    sturm::qbool      r_own[2 * W];
    sturm::BitProxy   r_bits[2 * W];
    for (std::size_t i = 0; i < 2 * W; ++i) {
        qi_r[i]   = sturm::QubitPool::instance().allocate();
        r_own[i]  = sturm::qbool::make_non_owning(qi_r[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }
    OuterControl c; if (wrap_in_when) c.allocate();
    if (wrap_in_when) {
        WHEN(c.owner) {
            sturm::lib_mul_dsl<sturm::BitProxy>(
                a.bits, W, b.bits, W, r_bits, 2 * W);
        }
    } else {
        sturm::lib_mul_dsl<sturm::BitProxy>(
            a.bits, W, b.bits, W, r_bits, 2 * W);
    }
    if (wrap_in_when) c.release();
    for (std::size_t i = 2 * W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    b.release(); a.release();
}

static void body_div(bool wrap_in_when) {
    Reg a, b, q, r;
    a.allocate(); b.allocate(); q.allocate(); r.allocate();
    OuterControl c; if (wrap_in_when) c.allocate();
    if (wrap_in_when) {
        WHEN(c.owner) {
            sturm::lib_div_dsl<sturm::BitProxy>(
                a.bits, W, b.bits, W, q.bits, r.bits);
        }
    } else {
        sturm::lib_div_dsl<sturm::BitProxy>(
            a.bits, W, b.bits, W, q.bits, r.bits);
    }
    if (wrap_in_when) c.release();
    r.release(); q.release(); b.release(); a.release();
}

// qbool::flip() emits a single X (or CX, when an outer control is live via
// emit_X_lifted).  Bare WHEN-less it produces depth=0; under WHEN(c) the
// active control sits at depth=1 and the emit_X_lifted path produces a CX
// against ctrls[0].  Both must keep depth <= 1.
static void body_qbool_flip(bool wrap_in_when) {
    int qi_t = sturm::QubitPool::instance().allocate();
    sturm::qbool t = sturm::qbool::make_non_owning(qi_t);
    OuterControl c; if (wrap_in_when) c.allocate();
    if (wrap_in_when) {
        WHEN(c.owner) {
            t.flip();
        }
    } else {
        t.flip();
    }
    if (wrap_in_when) c.release();
    sturm::QubitPool::instance().release(qi_t);
}

// ── main ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("sturm-a3t4.7: depth-1 control stack invariant regression "
                "net (W=%zu, with/without enclosing WHEN(c)):\n", W);

    run_op("add_mod",     &body_add_mod);
    run_op("mul_mod",     &body_mul_mod);
    run_op("pow_mod",     &body_pow_mod);
    run_op("mul",         &body_mul);
    run_op("div",         &body_div);
    run_op("qbool::flip", &body_qbool_flip);

    std::printf("All sturm-a3t4.7 depth-1 invariant regression checks "
                "passed.\n");
    return 0;
}
