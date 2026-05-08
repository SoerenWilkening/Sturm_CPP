#include "sturm.h"

#include <cstdint>
#include <cstdio>

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
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API (including `qbool` at namespace
// scope) and the auto-injected lifecycle wraps `main` with
// `sturm_backend_create` / `destroy`.

int main() {
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

    return 0;
}
