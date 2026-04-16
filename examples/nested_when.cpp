#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// Bring `qbool` into the global namespace so the `__stu_ctrl<N>` decls
// the Phase G transpiler emits (`qbool __stu_ctrl0 = a & b;` etc.)
// resolve unqualified — same convention as the Phase F
// `examples/when_integration.cpp` and the Phase E
// `examples/compound_expression.cpp` templates.  The hermetic snapshot
// fixtures under `tests/transpiler/fixtures/when_nested_*` rely on the
// same `using sturm::qbool;` shape so the matcher sees the same typed
// DeclRefExpr spelling.
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase G: named-named nested `WHEN` AND-fold demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises a nested `WHEN(outer) { WHEN(inner) { ... } }`
// pair whose outer and inner peeled arguments are BOTH bare
// `DeclRefExpr` to named qbools.  On each matched (outer, inner) pair
// it performs the Phase G three-point rewrite (see
// `docs/implementation_plan_transpiler_phase_g.md` §Design):
//
//   Source pattern                           | Phase | Rewrite
//   -----------------------------------------|-------|----------------------------
//   WHEN(a) { WHEN(b) { body } }             | PG-2  | `qbool __stu_ctrl0 = a & b;`
//                                            |       | injected immediately before
//                                            |       | the inner `WHEN`; the inner
//                                            |       | WHEN's argument is rewritten
//                                            |       | to `__stu_ctrl0`; and
//                                            |       | `uncompute_and(__stu_ctrl0,
//                                            |       | a, b);` is planted directly
//                                            |       | after the inner WHEN body's
//                                            |       | `}`, still inside the outer
//                                            |       | scope.
//   WHEN(x) { WHEN(y) { WHEN(z) { body } } } | PG-3  | Pairwise cascade: one
//                                            |       | `__stu_ctrl0 = x & y` pair
//                                            |       | lifts the (x, y) level and
//                                            |       | one `__stu_ctrl1 = y & z`
//                                            |       | pair lifts the (y, z)
//                                            |       | level; two
//                                            |       | `uncompute_and(...)` calls
//                                            |       | in cascade order.
//
// This lowering moves the historical runtime `WhenGuard::AND-fold`
// ancilla+CCX pair (retired in PG-4) into an explicit compile-time
// AND temporary plus `uncompute_and` — the runtime now only ever sees
// a single named control qbool per `WHEN`.  Gate semantics are
// identical (the forward `__stu_ctrl0 = a & b` emits the same
// CCX(a, b, __stu_ctrl0) the former ancilla+CCX emitted), but the
// complexity is now transparent: every inserted fragment is visible
// in the generated sibling file.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_nested_when`, open:
//     build/sturm_gen/examples/nested_when.cpp
// The original `WHEN(a) { WHEN(b) { ... } }` has been rewritten: a
// `qbool __stu_ctrl0 = a & b;` decl sits immediately above the inner
// `WHEN(__stu_ctrl0) { ... }` line, and
// `uncompute_and(__stu_ctrl0, a, b);` is planted directly after the
// inner WHEN body's closing brace.  The three-level cascade shows
// both `__stu_ctrl0` and `__stu_ctrl1` decls and their corresponding
// `uncompute_and` calls in LIFO cascade order.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_nested_when
//
// NOTE on STURM_AUTO_UNCOMPUTE: same caveat as the earlier Phase A–F
// examples — the build defaults to ON, which layers the destructor's
// auto-uncompute on top of the transpiler's injection.  To see ONLY the
// transpiler's contribution, configure with `-DSTURM_AUTO_UNCOMPUTE=OFF`.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Case 1 and Case 2 below demonstrate the named-named nested-WHEN
    // AND-fold lowering.  The inline comments inside `main()` deliberately
    // avoid spelling any of the transpiler-injected fragments verbatim
    // so the PG-8 byte-identity check and the "did-it-land-in-the-right-
    // place?" search in check_example_nested_when.cmake can both
    // anchor on `int main()` without false-positives.  The detailed
    // rewrite shape is described at the top of the file.

    // Case 1: two-level named-named nested WHEN.  Each qbool is
    // superposed (allocated + prepared at p=0.5) so the forward AND
    // in the injected sibling-file decl emits a real CCX, and the
    // inner WHEN body's lifted flip emits a CX under the active
    // single control.  The inner scope ensures the transpiler's
    // injected inverse fires BEFORE the qbool destructors run.
    {
        qbool a(0.5);
        qbool b(0.5);
        qbool target(0.5);

        WHEN(a) {
            WHEN(b) {
                target.flip();
            }
        }
    }

    // Case 2: three-level named-named nested WHEN (pairwise
    // cascade).  The Phase G matcher fires once per adjacent pair,
    // never transitively; the two cascade levels share one
    // persistent fresh-name counter so their control-temp names
    // never collide.  See the top-of-file overview for the exact
    // rewrite shape.  Inner scope again so the injected inverses
    // fire before the qbool destructors run.
    {
        qbool x(0.5);
        qbool y(0.5);
        qbool z(0.5);
        qbool target(0.5);

        WHEN(x) {
            WHEN(y) {
                WHEN(z) {
                    target.flip();
                }
            }
        }
    }

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
