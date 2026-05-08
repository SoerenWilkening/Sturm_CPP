#include "sturm.h"
#include "sturm/draw_ascii.h"
#include "sturm/uncompute/uncompute_api.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Phase J PJ-4: dead-ancilla elimination demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises the shape
//
//     qbool t = <qbool-operand bitwise expr>;   // no reader of `t` in scope
//
// and, when `detail::count_readers_in_scope(t, ...) == 0`, the PJ-4a
// matcher (`register_dead_ancilla_matcher` in
// transpiler/src/matcher_dead_ancilla.cpp) fires: it stages one
// empty-text `QReplacement` over the decl's full stmt range (including
// the trailing `;`), and pushes the same range into
// `QUnit::eliminated_stmt_ranges`.  The downstream Phase A / Phase E /
// MVP OR matchers consult that list via `apply_eliminated_stmt_guards`
// and early-return on any match whose own `stmt_range` lies inside an
// eliminated entry.  Net effect: the decl vanishes and no
// `uncompute_or(t, a, b);` is planted at scope close — zero gates
// emitted, zero ancilla qubits allocated.  The PJ-4a matcher is
// registered BEFORE the Phase A/E matchers in `transpiler/src/main.cpp`
// (the PJ-4b ordering rationale) so its `eliminated_stmt_ranges`
// entries are published before the downstream callbacks run.
//
// Rule table (see matcher_dead_ancilla.cpp for the full decision tree,
// and the PJ-4c snapshot fixtures under
// `tests/transpiler/fixtures/dead_ancilla_*` for every rejected
// branch):
//
//   Shape                                  | PJ-4a verdict
//   ---------------------------------------|----------------------------
//   `qbool t = a | b;` with 0 readers      | ELIMINATE (happy path)
//   `qbool t = a | b; x ^= t;` (1 reader)  | REJECT (reader-count guard)
//   `qbool t = a | b; qbool r = t & d;`    | REJECT `t` (reader via `r`
//                                          |   init); ELIMINATE `r`
//                                          |   independently (0 readers)
//   `qbool t = a;` (plain copy / non-bitwise| REJECT (init-shape guard —
//     init)                                |   not the PJ-4a territory)
//
// This example exercises BOTH the happy path and the reader-reject
// path in a single `main()` so the generated sibling shows the
// asymmetry directly: the dead decl vanishes from its scope, and the
// live (reader-present) decl gets the full Phase A / MVP OR rewrite
// fired unchanged.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_dead_ancilla`, open:
//     build/sturm_gen/examples/dead_ancilla.cpp
// main()'s `dead` scope contains NO `qbool dead = a | b;` line — the
// PJ-4a QReplacement stripped the decl verbatim — AND no
// `uncompute_or(dead, a, b);` at scope close.  main()'s `live` scope,
// by contrast, contains the preserved forward pair
// (`qbool live = a | b; x ^= live;`), a PA-3 LIFO self-adjoint
// `x ^= live;` inverse, and an MVP OR `uncompute_or(live, a, b);`
// planted before the scope's closing `}`.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_dead_ancilla
//
// The program prints a two-line gate-count summary followed by an
// ASCII circuit diagram:
//
//     gates_after_dead = <N1>
//     gates_after_live = <N2>
//
// Because the PJ-4a matcher eliminates the `dead` decl entirely — no
// forward OR gates, no uncompute OR gates — the `dead` scope
// contributes zero gates, so `N1 == gates_before_dead`.  The `live`
// scope, by contrast, emits the full forward OR three-gate sequence
// (CX + CX + CCX) + a PA-3 forward X on `x` + a PA-3 self-adjoint X
// on `x` + an MVP OR three-gate uncompute (CCX + CX + CX), for a net
// delta of `N2 - N1 == 8` (Phase K removed RAII auto-uncompute, so
// the transpiler's injection is the sole source of uncompute gates).
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API (including `qbool` at namespace
// scope) and the auto-injected lifecycle wraps `main` with
// `sturm_backend_create` / `destroy`. The opt-in `sturm/draw_ascii.h`
// exposes the no-arg renderer entry point.

int main() {
    std::size_t gates_after_dead = 0;
    std::size_t gates_after_live = 0;

    // ── Case 1: happy-path dead-ancilla elimination ────────────────────────
    // The PJ-4a matcher eliminates the `dead` decl entirely.  In the
    // generated sibling, the body of this inner scope is empty; no
    // forward OR gates are emitted and no `uncompute_or(...)` is
    // planted at scope close.  The inline comment deliberately avoids
    // spelling any of the PJ-4a-eliminated fragments verbatim so the
    // check_example_dead_ancilla.cmake "decl gone" assertion cannot
    // false-positive on a comment-only mention.  The detailed rewrite
    // shape is described in the top-of-file prose above.
    {
        qbool a(0.5);    // superposed — gives the OR a concrete
                         // forward gate to emit under real ancilla.
        qbool b(0.5);

        // PJ-4a — the canonical dead-decl elimination.  The transpiler
        // REPLACES the line below with empty text in the generated
        // sibling; no `uncompute_or(...)` is planted before the inner-
        // scope close brace.  The exact emitted shape is visible in
        //   build/sturm_gen/examples/dead_ancilla.cpp.
        qbool dead = a | b;
    }
    gates_after_dead = sturm::gate_count();

    // ── Case 2: reader-reject path — normal Phase A + MVP OR rewrite ───────
    // The PJ-4a matcher bails because `live` has one reader (the
    // adjacent compound-assign's RHS), so `live` passes through to
    // the Phase A PA-3 + MVP OR matchers unchanged.  In the generated
    // sibling the forward pair is preserved verbatim, a PA-3 LIFO
    // self-adjoint compound-assign inverse is planted before scope
    // close, and the MVP OR uncompute free-function call follows (LIFO
    // order).  The inline comment deliberately avoids spelling the
    // injected fragments verbatim AFTER this anchor comment so the
    // check_example_dead_ancilla.cmake "no-leak" assertion cannot
    // false-positive on a source-comment-only mention.  The detailed
    // rewrite shape is described in the top-of-file prose above.
    {
        qbool a(0.5);
        qbool b(0.5);
        qbool x;         // classical zero — PA-3 forward flip emits
        x.ensure_qubit();// one X on `x`, the LIFO inverse emits a
                         // second X that cancels it exactly.

        // PJ-4a rejects elimination here: `live` is read by the
        // adjacent compound-assign stmt, so the decl passes through
        // to the downstream matchers unchanged.  The exact emitted
        // text for the Phase A PA-3 inverse and the MVP OR uncompute
        // is visible in build/sturm_gen/examples/dead_ancilla.cpp.
        qbool live = a | b;
        x ^= live;
    }
    gates_after_live = sturm::gate_count();

    // Gate-count summary.  The Case 1 dead-decl elimination emits ZERO
    // gates (no forward, no uncompute) because the PJ-4a QReplacement
    // strips the decl verbatim and its `eliminated_stmt_ranges` entry
    // makes the MVP OR matcher early-return on the covered range; the
    // delta `gates_after_dead - gates_before_dead` is therefore 0.
    //
    // Case 2, by contrast, runs the full Phase A + MVP OR path:
    //     forward OR (CX + CX + CCX)                 = 3 gates
    //     PA-3 forward  `x ^= live`  (X)             = 1 gate
    //     PA-3 LIFO inv `x ^= live`  (X)             = 1 gate
    //     MVP OR uncompute (CCX + CX + CX)           = 3 gates
    //     ------------------------------------------ ------------
    //     total delta `gates_after_live - gates_after_dead`  = 8 gates
    //
    // Phase K removed RAII auto-uncompute; the transpiler's injected
    // inverse is the sole source of uncompute gates, so the totals
    // above are exactly what gets emitted.
    std::fprintf(stdout, "gates_after_dead = %zu\n",
                 gates_after_dead);
    std::fprintf(stdout, "gates_after_live = %zu\n",
                 gates_after_live);

    // Emit the ASCII circuit diagram AFTER both Phase J scopes close,
    // so both the eliminated Case-1 decl's (absent) gates and the
    // Case-2 forward / injected-inverse pairs show up side by side.
    // The diagram's net effect is an identity on the live qubits —
    // every forward gate has its matching inverse — plus the qbool
    // preparation gates for the four `qbool(0.5)` superposed operands
    // (two per scope, four total).
    sturm::print_ascii();
    return 0;
}
