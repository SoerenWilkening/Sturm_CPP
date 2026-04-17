#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// Bring `qbool` into the global namespace so the flat decls the
// transpiler emits resolve unqualified — same convention as the
// Phase E snapshot fixtures under tests/transpiler/fixtures/compound_*.
// The transpiler's render_decl_line() helper does not re-qualify by
// namespace because the original VarDecl's spelling can be either
// `qbool` or `sturm::qbool` depending on the user's preference; the
// fixture convention is to bring the type in via `using`.
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase E: compound qbool expression demo (`qbool r = (b | c) & d;`)
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises a compound qbool VarDecl whose initializer
// nests `|` / `&` operator calls (depth >= 2). The Phase E matcher
// flattens the expression tree into a sequence of single-operator
// VarDecls — one fresh `__stu_t<N>` per interior node, the original
// VarDecl name on the outermost — then injects the LIFO uncompute
// chain just before the enclosing scope's closing brace:
//
//   Source pattern              | Phase | Flattened decls + injected inverses
//   ----------------------------|-------|--------------------------------------
//   qbool r = (b | c) & d;      | PE-5  | one fresh `__stu_t<N>` decl for the
//                               |       | inner OR, the original VarDecl name
//                               |       | on the outer AND, and a LIFO pair of
//                               |       | `uncompute_and` / `uncompute_or`
//                               |       | calls injected before scope close.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_compound_expression`, open:
//     build/sturm_gen/examples/compound_expression.cpp
// The original `qbool r = (b | c) & d;` has been REPLACED in-place with
// the two flat decls (this is the first transpiler phase that mutates
// original source — Phases A–D only INSERTED before the close brace),
// and the two LIFO `uncompute_and` / `uncompute_or` calls appear just
// before the inner-block closing brace inside main().
//
// HOW TO RUN
// ----------
//     ./build/examples/example_compound_expression
//
// NOTE on STURM_AUTO_UNCOMPUTE: same caveat as the other Phase A–D
// examples — the build defaults to ON, which layers the destructor's
// auto-uncompute on top of the transpiler's injection. To see ONLY the
// transpiler's contribution, configure with `-DSTURM_AUTO_UNCOMPUTE=OFF`.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Inner scope so the transpiler-injected inverses fire BEFORE the
    // qbool destructors run. All three inputs are left on the classical
    // short-circuit path (super_mask = 0, qubits[0] < 0 throughout) so
    // the forward `(b | c) & d` evaluates purely classically — no gates
    // are emitted by either the forward op or the injected uncomputes,
    // matching the comparison.cpp / qint_arith.cpp idiom.
    //
    // Truth-table check: b = 1, c = 0, d = 1 → (1 | 0) & 1 = 1.
    {
        sturm::qbool b;
        sturm::qbool c;
        sturm::qbool d;
        b.value = 1;
        c.value = 0;
        d.value = 1;

        // PE-5 — the roadmap compound shape. The transpiler REPLACES this
        // VarDecl in the generated sibling with a flat two-decl
        // sequence: one fresh `__stu_t<N>` intermediate for the inner
        // OR sub-expression, then the outer AND consuming it. Two
        // inverses (uncompute_and on the outer, uncompute_or on the
        // intermediate) are injected just before the inner-block close
        // brace in LIFO order. The exact emitted text is visible in
        //   build/sturm_gen/examples/compound_expression.cpp.
        //
        // The trailing `(void)r;` is load-bearing: Phase J PJ-4a
        // (dead-ancilla elimination) strips any `qbool` VarDecl whose
        // value is never read, including the whole compound flatten's
        // outer `r` and its `__stu_t0` intermediate.  Without a reader,
        // PJ-4a fires before the Phase E compound matcher and the
        // generated file would have no flat decls and no uncompute
        // injections — same as the `or_single_runtime.cpp` rationale.
        sturm::qbool r = (b | c) & d;
        (void)r;
    }

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
