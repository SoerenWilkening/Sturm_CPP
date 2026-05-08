#include "sturm.h"
#include "sturm/draw_ascii.h"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Phase H: classical control flow around quantum ops (for / if-else)
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises quantum ops (here: the MVP `qbool r = a | b;`
// initializer) nested inside classical control-flow constructs.  Phase H
// fills three gaps (see docs/implementation_plan_transpiler_phase_h.md):
//
//   PH-1: scope-finder refactor — `enclosing_scope(node, ctx)` returns
//         either a `CompoundStmt` anchor (braced bodies) or a
//         `BracelessBody` anchor (single-stmt bodies).  Every pre-existing
//         Phase A–G matcher switched to the new API with no behavioural
//         change on braced fixtures.
//   PH-2: `matcher_brace_wrap.cpp` auto-synthesises `{ ... }` around
//         braceless for/while/if/else bodies that contain a quantum op,
//         so the M8 uncompute-synthesis pass can land its
//         `uncompute_*(...)` call INSIDE the body rather than as a
//         sibling of the loop/branch.
//   PH-3: `matcher_outer_var_guard.cpp` diagnostic rejects outer-scoped
//         qbool/qint mutations inside a loop/branch/WHEN body — those
//         would require reverse-loop synthesis, which violates P9 ("the
//         user writes the adjoint, not the transpiler") and is out of
//         scope for Phase H.
//
// This example exercises the happy path for both control-flow kinds:
//
//   Source pattern                                | Phase | Rewrite
//   ----------------------------------------------|-------|-------------------
//   for (int i = 0; i < N; ++i) {                 | PH-1  | `uncompute_or(tmp,
//       qbool tmp = a | b;                        |       | a, b);` planted
//   }                                             |       | before the loop
//                                                 |       | body's closing `}`
//                                                 |       | — once per
//                                                 |       | iteration.
//   if (cond) {                                   | PH-1  | Distinct
//       qbool x = a | b;                          |       | per-branch
//   } else {                                      |       | intermediates,
//       qbool y = a | b;                          |       | each with its own
//   }                                             |       | `uncompute_or(...)`
//                                                 |       | planted before
//                                                 |       | that branch's
//                                                 |       | closing `}`.
//
// The hermetic Phase H M12 gate-equivalence fixtures
// (`tests/transpiler/fixtures/for_loop_{runtime,reference}.cpp` and
// `if_branches_{runtime,reference}.cpp`) pin down the exact emitted
// gate stream for both shapes; this example is the runnable capstone
// that also prints an ASCII circuit diagram.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_control_flow`, open:
//     build/sturm_gen/examples/control_flow.cpp
// The for-loop body has a single `uncompute_or(tmp, a, b);` call planted
// directly before its closing `}` — so every iteration materialises then
// uncomputes its own `tmp`.  The if/else each have their own
// `uncompute_or(x, a, b);` / `uncompute_or(y, a, b);` call planted
// directly before their respective branch closing `}` — so the inverse
// for whichever branch fires runs before the qbool destructors at
// scope exit.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_control_flow
//
// The program prints an ASCII circuit diagram showing the forward
// three-gate OR pattern (CX + CX + CCX) and its injected three-gate
// adjoint (CCX + CX + CX) for each iteration of the for-loop and for
// the if/else branch whose classical condition fires.  Per-iteration
// `QubitPool` LIFO reuse means every loop iteration reuses the same
// ancilla index — the diagram shows repeated forward/inverse pairs on
// identical qubit indices.
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API (including `qbool` at namespace
// scope) and the auto-injected lifecycle wraps `main` with
// `sturm_backend_create` / `destroy`. The opt-in `sturm/draw_ascii.h`
// exposes the no-arg renderer entry point.

int main() {
    // Case 1 and Case 2 below demonstrate the Phase H per-iteration and
    // per-branch uncompute lowerings.  The inline comments inside
    // `main()` deliberately avoid spelling any of the transpiler-
    // injected fragments verbatim so the PH-5 byte-identity check and
    // the "did-it-land-in-the-right-place?" search in
    // check_example_control_flow.cmake can both anchor on
    // `int main()` without false-positives.  The detailed rewrite
    // shapes are described at the top of the file.

    // ── Case 1: for-loop with per-iteration intermediate OR ────────────────
    // `a` and `b` are superposed (allocated + prepared at p=0.5) so
    // the forward OR emits a real CX+CX+CCX sequence, and the injected
    // adjoint emits its three-gate inverse.  Each iteration allocates
    // its own per-iteration intermediate ancilla; the transpiler plants
    // the OR uncompute call before the loop body's `}`, so the
    // intermediate is uncomputed BEFORE its destructor releases the
    // qubit index back to the LIFO `QubitPool`.  The next iteration's
    // allocate() then returns the SAME index — the diagram shows
    // three adjacent forward/inverse pairs on identical qubit lanes.
    // The inner scope ensures the a/b destructors fire AFTER the
    // entire loop completes.  The exact emitted text is visible in
    //   build/sturm_gen/examples/control_flow.cpp.
    //
    // The `(void)tmp;` reader is load-bearing: Phase J PJ-4a
    // (dead-ancilla elimination) strips any `qbool` VarDecl with no
    // readers, so without it the forward `qbool tmp = a | b;` line
    // would be removed before the M7 OR matcher runs.  The inner
    // `{ ... }` wrap around the decl + reader additionally prevents
    // PJ-3d from hoisting the uncompute OUT of the for-loop — the
    // inner compound's parent is the for-body CompoundStmt (not the
    // ForStmt itself), so `classify_scope_kind` returns `Other`
    // rather than `LoopBody`, and the uncompute lands per-iteration
    // inside the inner scope (PH-5's PH-5 contract remains: forward
    // + uncompute together per iteration).
    {
        qbool a(0.5);
        qbool b(0.5);

        for (int i = 0; i < 3; ++i) {
            {
                qbool tmp = a | b;
                (void)tmp;
            }
        }
    }

    // ── Case 2: if/else with distinct per-branch intermediates ─────────────
    // `cond` is a classical bool so only ONE branch fires per program
    // invocation.  The transpiler plants a DIFFERENT `uncompute_or`
    // call inside each branch — one against `x` in the then-arm,
    // one against `y` in the else-arm — so whichever branch the
    // classical dispatch chooses carries its own forward/inverse pair
    // through the backend.  Two inner scopes around each call so the
    // `a` / `b` pair is re-superposed per case and destructors fire
    // deterministically.
    //
    // The `(void)x;` / `(void)y;` readers follow the same PJ-4a
    // rationale as Case 1 above.  An extra inner scope is NOT needed
    // per branch: if/else arms are not LoopBody, so PJ-3d does not
    // hoist out of them (its `classify_scope_kind == LoopBody` guard
    // rejects BranchBody directly).
    {
        qbool a(0.5);
        qbool b(0.5);
        const bool cond = true;

        if (cond) {
            qbool x = a | b;
            (void)x;
        } else {
            qbool y = a | b;
            (void)y;
        }
    }

    // Emit the ASCII circuit diagram AFTER the two Phase H scopes
    // close, so every transpiler-injected `uncompute_or(...)` call has
    // fired before the renderer walks the GateIR.  The diagram shows
    // the three for-loop iteration pairs and the single if-branch
    // pair in source / LIFO order.
    sturm::print_ascii();
    return 0;
}
