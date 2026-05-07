#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/sturm.hpp"
#include "sturm/uncompute/uncompute_api.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

// Bring `qbool` into the global namespace so the MVP OR matcher sees
// the same unqualified typed DeclRefExpr spelling the rest of the
// examples use, and so the injected `uncompute_or(t, a, b);` call the
// PJ-3d hoist matcher relocates to the post-loop anchor resolves
// unqualified at the call site.  Same convention as the Phase J
// `examples/dead_ancilla.cpp`, the Phase J `examples/zero_ancilla_fusion.cpp`,
// the Phase I `examples/user_routine.cpp`, the Phase H
// `examples/control_flow.cpp`, the Phase G `examples/nested_when.cpp`,
// the Phase F `examples/when_integration.cpp`, and the Phase E
// `examples/compound_expression.cpp` templates.  The hermetic snapshot
// fixtures under `tests/transpiler/fixtures/hoist_*` rely on the same
// `using sturm::qbool;` shape so the PJ-3d matcher sees the same typed
// DeclRefExpr spelling as here.
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase J PJ-3: uncompute-hoisting demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises the shape
//
//     for (int i = 0; i < N; ++i) {
//         qbool t = a | b;          // forward OR — operands loop-invariant
//         (void)t;                   // reader (keeps PJ-4a eliminator off)
//     }
//
// when every operand of the OR init is loop-invariant per
// `detail::expr_is_loop_invariant` (operand decl lives OUTSIDE the loop
// body AND no write to it is reachable inside).  In that case the PJ-3d
// matcher (`register_hoist_invariant_matcher` in
// transpiler/src/matcher_hoist_invariant.cpp) fires: it sets
// `op.hoist_to_override` on the OR operation to the close brace of the
// scope that ENCLOSES the loop (one level up from the loop body), and
// sets `op.insert_before_override` to the loop's begin location (for
// future forward-move landing).  The M8 uncompute-synthesis pass then
// plants the `uncompute_or(t, a, b);` call AT THAT POST-LOOP ANCHOR —
// i.e. AFTER the loop's closing `}`, not immediately before it.  Net
// effect: the inverse runs ONCE (not N times), amortizing the adjoint
// cost across the loop iteration count.
//
// PJ-3d only moves the UNCOMPUTE anchor; the forward compute text move
// is a future-phase concern (PJ-3g+).  The forward `qbool t = a | b;`
// stays INSIDE the loop body, still firing per-iteration.  The hermetic
// snapshot fixtures `hoist_or_out_of_for`, `hoist_and_out_of_while`,
// `hoist_multi_op_same_loop`, `hoist_nested_loops_inner_only` pin the
// byte-exact generated layout; this example is the runnable capstone
// that also prints an ASCII circuit diagram.
//
// The `qbool t` OUTER-SCOPE PREDECL inside main() is load-bearing for
// the example: the PJ-3d hoist relocates the `uncompute_or(t, a, b);`
// call to the post-loop anchor, where the inner-loop `qbool t` decl is
// no longer in scope.  The outer predecl keeps the hoisted reference
// resolvable — a pragmatic workaround pinned to the PJ-3d limitation
// that forward text-move is deferred.  At runtime the outer `t` holds
// no ancilla qubit (classical zero), so the hoisted `uncompute_or(t,
// a, b)` takes the all-classical early-return branch in
// src/sturm/uncompute/uncompute_api.cpp (lines 64-66) and emits zero
// gates.  The forward inner-loop `qbool t = a | b;` fires its OR
// decomposition per iteration through the backend sink.
//
// Rule table (see matcher_hoist_invariant.cpp for the full decision
// tree, and the PJ-3f snapshot fixtures under
// `tests/transpiler/fixtures/hoist_*` for every rejected branch):
//
//   Shape                                  | PJ-3d verdict
//   ---------------------------------------|----------------------------
//   `qbool t = a | b;` inside for/while    | HOIST uncompute to post-
//     body, operands invariant             |   loop anchor (happy path)
//   one operand written inside loop body   | REJECT (operand-invariant
//                                          |   probe fails)
//   enclosing scope is if/else (not loop)  | REJECT (LoopBody gate)
//   nested loops, inner-op only            | HOIST to outer-loop body's
//                                          |   close brace (one level up)
//   multiple invariant ops in same loop    | HOIST each in LIFO order
//                                          |   (reverse source order)
//
// This example exercises the HAPPY path: two classical-zero qbools `a`
// and `b` declared in main()'s body, with a for-loop body computing
// `qbool t = a | b;`.  The PJ-3d hoist fires, and the generated
// sibling has the `uncompute_or(t, a, b);` line AFTER the for-loop
// closing `}`, not inside it.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_uncompute_hoisting`, open:
//     build/sturm_gen/examples/uncompute_hoisting.cpp
// main()'s body has the forward `qbool t = a | b;` inside the for-loop
// body (unchanged from source), and the hoisted
// `uncompute_or(t, a, b);` line lands AFTER the for-loop's closing `}`
// — NOT inside the for-body.  This is the PJ-3d semantic: the M8
// synthesis pass consumed `op.hoist_to_override` at uncompute-emission
// time, preferring the post-loop anchor over the default
// `scope.close_brace` (the for-body's `}`).
//
// HOW TO RUN
// ----------
//     ./build/examples/example_uncompute_hoisting
//
// The program prints a gate-count summary followed by an ASCII circuit
// diagram.  Because `a` and `b` are classical zero (no superposition),
// the forward OR's materialization path takes the all-classical early-
// return branch in `bit_proxy.hpp` — no gates are emitted per
// iteration.  The hoisted `uncompute_or(t, a, b)` also early-returns
// (all operands classical).  The gate count therefore stays at zero
// throughout; the diagram is empty modulo any framing lines.  The
// load-bearing observable is the GENERATED FILE layout (checked by
// `check_example_uncompute_hoisting.cmake`), not the run-time gate
// stream.  For a gate-producing variant, pair this example with the
// PJ-3h gate-equivalence harness (tracked under sturm-djqg) which
// wires superposed operands and compares the pre-hoist vs post-hoist
// streams.
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    std::size_t gates_before = 0;
    std::size_t gates_after = 0;

    // ── Case 1: happy-path uncompute-hoisting ──────────────────────────────
    // The inline comments inside `main()` deliberately avoid spelling
    // any of the transpiler-injected fragments verbatim so the PJ-3g
    // byte-identity check and the "did-it-land-in-the-right-place?"
    // search in check_example_uncompute_hoisting.cmake can both anchor
    // on `int main()` without false-positives on inline-comment
    // mentions.  The detailed rewrite shape is described at the top of
    // the file.
    //
    // Case 1 is the PJ-3d happy path: two classical-zero operands
    // declared ABOVE the for-loop (the PJ-3d operand-invariance probe
    // admits outer-scoped decls with no writes inside the body), an
    // outer-scope predecl (pragmatic workaround for the PJ-3d
    // forward-move deferral), and a for-loop body with a VarDecl-init
    // OR.  The exact emitted text is visible in
    //   build/sturm_gen/examples/uncompute_hoisting.cpp.
    {
        qbool a;
        qbool b;
        qbool t;  // outer predecl — see top-of-file prose

        gates_before = ctx->ir.size();

        for (int i = 0; i < 3; ++i) {
            qbool t = a | b;
            (void)t;
        }

        gates_after = ctx->ir.size();
    }

    // Gate-count summary.  Both operands are classical-zero in this
    // example, so the forward OR's materialization path takes the
    // all-classical early-return branch in `bit_proxy.hpp`: no gates
    // are emitted per iteration, and the transpiler-injected post-
    // loop inverse also early-returns (all three operands classical —
    // see src/sturm/uncompute/uncompute_api.cpp lines 64-66).  The
    // delta `gates_after - gates_before` stays at 0; the gate-
    // producing variant is the PJ-3h gate-equivalence harness
    // (tracked under sturm-djqg).
    std::fprintf(stdout, "gates_before = %zu\n", gates_before);
    std::fprintf(stdout, "gates_after  = %zu\n", gates_after);

    // Emit the ASCII circuit diagram AFTER the Phase J PJ-3 scope
    // closes, so both the per-iteration forward OR materializations
    // and the post-loop transpiler-injected inverse have fired before
    // the renderer walks the GateIR.  With classical-zero operands
    // the diagram is effectively empty; the load-bearing observable
    // is the GENERATED FILE layout, not the runtime gate stream.
    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
