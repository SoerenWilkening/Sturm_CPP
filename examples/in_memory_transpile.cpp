#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/sturm.hpp"
#include "sturm/uncompute/uncompute_api.hpp"

#include <cstdint>
#include <cstdio>

// Bring `qbool` into the global namespace so every flat decl the
// transpiler emits — `qbool __stu_tN = ...;` from the Phase E compound
// flatten and the Phase F WHEN-lift, plus the fused
// `ccnot_inplace(x, a, b);` call from the Phase J PJ-1 peephole —
// resolves unqualified at the call site. Same convention as the
// single-phase templates (examples/compound_expression.cpp,
// examples/when_integration.cpp, examples/zero_ancilla_fusion.cpp).
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase M PM1-8: in-memory transpile tutorial
// ─────────────────────────────────────────────────────────────────────────────
//
// This example is the first Phase M tutorial that exercises THREE
// transpiler rewrites in a single translation unit, built through the
// default **plugin mode** added in PM1-5. The plugin loads into the
// clang++ compile of this file via `-fplugin=$<TARGET_FILE:...>`,
// runs the shared TranspileConsumer against the AST in memory, and
// then drives a nested CompilerInvocation + EmitObjAction over the
// rewritten buffer to produce the object file. No intermediate .cpp
// round-trips through the filesystem during the compile.
//
// For the injected-observability CTest below we additionally mirror
// the rewritten buffer to `${CMAKE_BINARY_DIR}/sturm_gen/<relpath>`
// via the plugin's `dump-to=` flag — same disk layout as the legacy
// dump mode, so the per-example check_example_*.cmake grep pattern
// continues to work unchanged. Developers can request the same mirror
// ad-hoc with the `--dump-transpiled=<path>` flag on the standalone
// binary (PM1-6). The two flags converge on a single file-write path
// in `transpiler/src/io.cpp`.
//
// Rewrites exercised (one per inner scope below):
//
//   Inner scope | Phase  | Forward shape                       | Rewrite
//   ------------|--------|-------------------------------------|--------------------------------
//   E           | PE-5   | qbool r = (b | c) & d;              | flat `__stu_tN` decls replace
//               |        |                                     | the VarDecl; LIFO `uncompute_`
//               |        |                                     | chain before scope close.
//   F           | PF-4   | WHEN((wb | wc) & wd) { body }       | flat `__stu_tN` decls ABOVE
//               |        |                                     | WHEN, arg substituted to outer
//               |        |                                     | temp, LIFO uncompute AFTER body.
//   J PJ-1      | PJ-1d  | qbool __t = fa & fb;                | pair COLLAPSES to one fused
//               |        | fx ^= __t;                          | `ccnot_inplace(fx, fa, fb);`
//               |        |                                     | plus a self-adjoint uncompute
//               |        |                                     | before scope close.
//
// Each rewrite lives in its own block scope so its injections do not
// interleave with the others' — the three rewrites compose cleanly
// because Phase E / F allocate `__stu_t<N>` on distinct allocators
// (Phase E: one per QUnit; Phase F: one per WHEN invocation) and the
// Phase J PJ-1 peephole keys on the literal `__t` name plus a
// single-reader guard, neither of which the other blocks trigger.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_in_memory_transpile`, open:
//     build/sturm_gen/examples/in_memory_transpile.cpp
// The file contains the rewritten source mirrored from the plugin's
// in-memory buffer. Each inner scope shows its phase-specific
// injection: flat decls for E/F, a pair of `ccnot_inplace(fx, fa, fb);`
// calls for J PJ-1.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_in_memory_transpile
//
// All qbools are left on the classical short-circuit path (no
// `ensure_qubit()`, no `theta()` promotion) so the forward ops and
// their injected inverses evaluate purely classically — no gates
// emit, matching the idiom of compound_expression.cpp /
// when_integration.cpp.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    // ── Phase E: compound qbool expression ──────────────────────────────────
    // `qbool r = (b | c) & d;` → the Phase E matcher flattens the
    // two-deep expression tree into one fresh `__stu_tN` decl for the
    // inner OR plus the outer AND on the original VarDecl name, and
    // injects a LIFO `uncompute_and` / `uncompute_or` pair before the
    // inner scope's `}`.  The `(void)r;` bottom-anchor keeps the outer
    // `r` decl alive against the Phase J PJ-4a dead-ancilla elimination
    // pass, mirroring the rationale documented in
    // examples/compound_expression.cpp.
    {
        qbool b;
        qbool c;
        qbool d;
        b.value = 1;
        c.value = 0;
        d.value = 1;

        qbool r = (b | c) & d;
        (void)r;
    }

    // ── Phase F: compound-WHEN integration ──────────────────────────────────
    // The `WHEN` macro below wraps a two-deep `|` / `&` compound over
    // four qbool operands.  The Phase F matcher lifts that compound
    // argument into two flat temporaries planted ABOVE the WHEN line,
    // substitutes the macro argument to the outer temp, and plants a
    // LIFO inverse pair AFTER the WHEN body's `}`.  Allocator scope:
    // Phase F mints a fresh per-invocation allocator inside
    // `flatten_arg`, independent of the Phase E block above, so the
    // emitted temp names restart at 0 for this block.  The inline
    // comments below deliberately avoid spelling the compound WHEN
    // argument verbatim so the PM1-8 injected CTest's "original
    // spelling gone" regex cannot false-positive on comment prose.
    {
        qbool wa;
        qbool wb;
        qbool wc;
        qbool wd;
        wa.value = 0;
        wb.value = 1;
        wc.value = 0;
        wd.value = 1;

        WHEN((wb | wc) & wd) {
            // Classical toggle via `.value` direct write — same idiom
            // as when_integration.cpp.  The Phase F rewrite is the
            // load-bearing transformation this block demonstrates;
            // the body itself is intentionally trivial.
            wa.value ^= 1;
        }
    }

    // ── Phase J PJ-1: zero-ancilla fusion ───────────────────────────────────
    // The two statements below spell the canonical PJ-1 peephole shape
    // — a bare `&` VarDecl consumed by an adjacent `^=` with exactly
    // one reader in scope.  The PJ-1d peephole (QReplacement spanning
    // BOTH stmts) collapses the pair into a single forward
    // `ccnot_inplace(...)` call, and the PJ-1c render case plants a
    // matching self-adjoint uncompute before the inner scope's `}`.
    // CCX is self-inverse so forward + uncompute share the same
    // identifier — same rewrite shape as
    // examples/zero_ancilla_fusion.cpp.  The inline comments here
    // deliberately avoid spelling the original pair verbatim so the
    // PM1-8 injected CTest's "original pair gone" regex cannot
    // false-positive on comment prose.
    {
        qbool fa;
        qbool fb;
        qbool fx;
        fa.value = 1;
        fb.value = 1;
        fx.value = 0;

        qbool __t = fa & fb;
        fx ^= __t;
    }

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
