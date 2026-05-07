#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// Bring `qbool` into the global namespace so the flat decls the
// transpiler emits (`qbool __stu_t0 = b | c;` etc.) resolve unqualified
// — same convention as the Phase E `examples/compound_expression.cpp`
// and the hermetic Phase F fixtures under
// `tests/transpiler/fixtures/when_*`.
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase F: compound-`WHEN` integration demo (`WHEN((b | c) & d) { ... }`)
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises a `WHEN(expr)` macro invocation whose peeled
// argument is a nested qbool bitwise compound expression (`|` / `&`) and
// performs the Phase F three-point rewrite:
//
//   Source pattern                  | Phase | Rewrite
//   --------------------------------|-------|------------------------------------
//   WHEN((b | c) & d) { body }      | PF-4  | two flat `qbool __stu_tN = ...;`
//                                   |       | decls injected BEFORE the WHEN,
//                                   |       | argument substituted to the top
//                                   |       | temp, LIFO `uncompute_and` /
//                                   |       | `uncompute_or` calls planted
//                                   |       | AFTER the WHEN body's `}`.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_when_integration`, open:
//     build/sturm_gen/examples/when_integration.cpp
// The original `WHEN((b | c) & d) { ... }` has been rewritten: two flat
// decls sit immediately above the `WHEN(__stu_t1) { ... }` line, and
// the `uncompute_and(__stu_t1, __stu_t0, d);` /
// `uncompute_or(__stu_t0, b, c);` LIFO pair is planted directly after
// the WHEN body's closing brace — still inside the enclosing inner
// scope so the temporaries' destructors fire AFTER their inverses.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_when_integration
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    // Inner scope so the transpiler-injected inverses fire BEFORE any
    // qbool destructors run. All three WHEN-control inputs (b, c, d)
    // are left on the classical short-circuit path (super_mask = 0,
    // qubits[0] < 0 throughout) so the forward `(b | c) & d` and its
    // injected `uncompute_{and,or}` inverses evaluate purely
    // classically — no gates are emitted, matching the
    // compound_expression.cpp / comparison.cpp idiom.
    //
    // Truth-table check: b = 1, c = 0, d = 1 → (1 | 0) & 1 = 1, so the
    // WHEN body runs and the 4th qbool `a` is toggled from 0 to 1.
    {
        sturm::qbool a;
        sturm::qbool b;
        sturm::qbool c;
        sturm::qbool d;
        a.value = 0;
        b.value = 1;
        c.value = 0;
        d.value = 1;

        // PF-4 — the roadmap compound-WHEN shape. The transpiler
        // REPLACES this `WHEN(...)` argument in the generated sibling
        // with two flat `__stu_t` decls planted immediately above the
        // WHEN line, rewrites the macro argument to the outer flat
        // temp, and injects a LIFO `uncompute_and` / `uncompute_or`
        // inverse pair directly after the WHEN body's `}`. The exact
        // emitted text is visible in
        //   build/sturm_gen/examples/when_integration.cpp.
        WHEN((b | c) & d) {
            // Classical toggle — the qbool `a` is on the short-circuit
            // path so `a.value ^= 1` flips bit 0 with zero gate emission.
            // Using the `.value` field directly avoids a Phase A
            // `qbool::operator^=` match on this write (the forward op
            // would assert `qubits[0] >= 0`, which is not true on the
            // classical path). The transpiler's Phase F rewrite is the
            // load-bearing transformation this example demonstrates;
            // the body itself is intentionally trivial.
            a.value ^= 1;
        }
    }

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
