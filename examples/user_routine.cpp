#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// Bring `qbool` into the global namespace so the forward routine
// signature + the transpiler-emitted `invert(rotate_by_k)(tmp, a, 3);`
// call both resolve their qbool spelling unqualified — same convention
// as the Phase G `examples/nested_when.cpp`, the Phase F
// `examples/when_integration.cpp`, the Phase E
// `examples/compound_expression.cpp`, and the Phase H
// `examples/control_flow.cpp` templates.  The PI-2 matcher keys on the
// callee's type signature (non-const qbool& → OUTPUT slot) so the
// generated file's `invert(rotate_by_k)(...)` call needs `qbool` and
// `invert` resolvable at the call site.
using sturm::invert;
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase I: user-defined routine + automatic adjoint dispatch demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises every `callExpr` whose callee FunctionDecl
// has been paired with a hand-written adjoint via
// `STURM_REGISTER_ADJOINT(fwd, adj)`.  On each match the PI-2 matcher
// records a `QOpKind::USER_ROUTINE` QOperation; PI-3 classifies every
// output-parameter slot; and PI-4's uncompute pass renders the op as
// a bare `invert(<name>)(<args...>);` call planted before the
// enclosing scope's close brace — unless PI-3 told it otherwise.
//
// Rule table (see include/sturm/routines/invert.hpp and the PI-0..PI-5
// plan bullets in docs/roadmap_transpiler_post_mvp.md):
//
//   Output backing VarDecl    | PI-3 class        | Injection
//   --------------------------|-------------------|-----------------------
//   qbool local to call scope | Intermediate      | `invert(fn)(...)`
//                             |                   | before scope close
//   qbool in an ancestor scope| IntermediateOuter | `invert(fn)(...)` at
//                             |                   | declaring scope close
//   function parameter        | Final             | NO injection (escape)
//   file / namespace scope    | Final             | NO injection (escape)
//   ancestor + barrier in     | SkipWithDiagnostic| NO injection + stderr
//     for/while/if/WHEN body  |                   | diagnostic (mirrors
//                             |                   | PH-3 / outer-var guard)
//
// This example exercises the two most important rows:
//
//   Case 1 (in main())      — local-intermediate output.  A fresh
//                             `qbool tmp` declared in main()'s inner
//                             scope backs the call's output slot; the
//                             transpiler injects
//                             `invert(rotate_by_k)(tmp, a, 3);` before
//                             the scope's close brace.
//
//   Case 2 (in apply_rotation) — escaping output.  The helper takes
//                             `qbool& out` as a parameter; PI-3 flags
//                             that as `Final` (the caller owns the
//                             uncompute policy per principle P9) and
//                             PI-4 emits nothing.  The helper's body
//                             is byte-identical in the generated
//                             sibling apart from the transpile-
//                             unrelated header preamble.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_user_routine`, open:
//     build/sturm_gen/examples/user_routine.cpp
// main()'s inner scope contains the forward `rotate_by_k(tmp, a, 3);`
// call followed — before the `}` — by the injected
// `invert(rotate_by_k)(tmp, a, 3);` line.  apply_rotation's body
// contains ONLY the forward `rotate_by_k(out, a, 3);` call — no
// inverse is emitted because `out` is a ParmVarDecl (escapes).
//
// HOW TO RUN
// ----------
//     ./build/examples/example_user_routine
//
// The program prints a two-line gate-count summary to stdout:
//
//     gates_after_forward = <N1>
//     gates_after_adjoint = <N2>
//
// Because `rotate_by_k_adj` is the manual adjoint of `rotate_by_k`
// (see the bodies below — each forward flip is cancelled by a
// matching flip in the adjoint), and each routine emits exactly
// `k` gates on `out`, the summary satisfies
//     N2 - N1 == k
// for the Case-1 local-intermediate injection that fires inside
// main()'s inner scope.  The Case-2 call (apply_rotation) contributes
// only its forward-side gates to both N1 and N2 (its output escapes;
// no inverse is injected).
//
// NOTE on STURM_AUTO_UNCOMPUTE: the build defaults to ON for this
// flag, layering destructor-driven auto-uncompute on top of the
// transpiler's injection for qbool/qint locals that the transpiler
// touches.  The `qbool tmp` in Case 1 is the only qbool local of
// this example whose destructor could double-uncompute; with
// STURM_AUTO_UNCOMPUTE=ON that's harmless because the second
// application of the adjoint is a no-op by construction (rotate_by_k
// is a classical X-chain — applying the adjoint twice is identity).
// To see ONLY the transpiler's contribution, configure with
// `-DSTURM_AUTO_UNCOMPUTE=OFF`.

// ── Forward user routine ─────────────────────────────────────────────────────
// Rotates `out` by `k` bit-flips controlled classically on `in`'s
// current value.  With `in` on the classical short-circuit path
// (qubits[0] < 0, super_mask bit 0 clear) the `flip()` calls run
// unconditionally on `out` — which is enough to emit `k` gates and
// demonstrate the forward/adjoint pairing at run time.  The exact
// mechanics of `flip()` live in include/sturm/qtypes/qbool.hpp / the
// qbool_ops implementation; what matters here is that it's a single
// gate per call, so the before/after gate-count subtraction is a
// concrete witness of "adjoint fired".
//
// Both `rotate_by_k` and its adjoint live at namespace scope
// (unqualified / global in this TU) because `STURM_REGISTER_ADJOINT`
// expands to a specialization of `sturm::_detail::adjoint_of<decltype(
// &::rotate_by_k)>` — the `::rotate_by_k` nested-name-specifier
// requires a namespace-scope declaration.
void rotate_by_k(qbool& out, const qbool& in, int k) {
    // Reference `in` so the signature genuinely uses it (keeps the
    // compiler from warning about an unused const-ref parameter AND
    // makes the routine's classical behaviour trivially testable).
    // `in` is NOT an output — it's const, so the PI-2 matcher will
    // leave its outputs_mask bit clear.
    (void)in;
    for (int i = 0; i < k; ++i) {
        out.flip();
    }
}

// ── Hand-written adjoint ─────────────────────────────────────────────────────
// For a self-inverse X-chain the adjoint is literally the forward
// routine run again — applying `flip()` `k` times twice returns `out`
// to its starting value.  The runtime-level contract is that
// `invert(rotate_by_k)(out, in, k); rotate_by_k(out, in, k);` is the
// identity on `out` (and vice-versa).
void rotate_by_k_adj(qbool& out, const qbool& in, int k) {
    (void)in;
    for (int i = 0; i < k; ++i) {
        out.flip();
    }
}

// ── Register the (forward, adjoint) pair ────────────────────────────────────
// Expands to a full specialization of
// `sturm::_detail::adjoint_of<decltype(&::rotate_by_k)>` whose `value`
// member is `&::rotate_by_k_adj`.  The PI-1 AST matcher picks this
// specialization up and seeds the RoutineRegistry with a canonical
// FunctionDecl* key pointing at `rotate_by_k`; the PI-2 callExpr
// matcher then uses that registry to decide whether a given call is
// "a user routine" and — if so — emits a `QOpKind::USER_ROUTINE`
// QOperation the PI-4 render case lowers to `invert(...)(...)` text.
STURM_REGISTER_ADJOINT(rotate_by_k, rotate_by_k_adj)

// ── Case 2 helper: escaping-output routine call ─────────────────────────────
// Phase I PI-3 classifies the output-parameter `out` as `Final` (it's
// a ParmVarDecl), so the PI-4 uncompute pass emits nothing for this
// call.  The caller (main() below) is responsible for the adjoint
// policy of whatever backing qbool it passed in — principle P9 in
// docs/01_principles.md.  The helper's body is therefore transpile-
// invariant: the generated sibling's `apply_rotation` contains only
// the forward `rotate_by_k(out, a, 3);` call, with no injected
// `invert(...)` companion.
void apply_rotation(qbool& out, const qbool& a) {
    rotate_by_k(out, a, 3);
}

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Inner scope so the transpiler-injected adjoint dispatch fires
    // BEFORE we sample the gate-count summary and before `tmp`'s
    // destructor runs.  The inline comments inside `main()`
    // deliberately avoid spelling any of the transpiler-injected
    // fragments verbatim so the PI-6 byte-identity check and the
    // "did-it-land-in-the-right-place?" search in
    // check_example_user_routine.cmake can both anchor on
    // `int main()` without false-positives.  The detailed rewrite
    // shape is described in the top-of-file prose above.
    std::size_t gates_after_forward = 0;
    std::size_t gates_after_adjoint = 0;
    {
        qbool a(0.5);    // superposed — gives `flip()` a concrete
                         // gate to emit under a real ancilla.
        qbool tmp;       // local-intermediate — Case 1 output slot.
        tmp.ensure_qubit();

        // Case 1: local-intermediate output.  `tmp` is declared in
        // THIS scope so PI-3 classifies its backing VarDecl as
        // `Intermediate`; PI-4 plants the routine's adjoint dispatch
        // before the `}` below (the exact emitted form is described
        // in the top-of-file prose to keep this comment free of
        // false-positive anchors).
        rotate_by_k(tmp, a, 3);
        gates_after_forward = ctx->ir.size();

        // Case 2: escaping-output routine call.  `apply_rotation` is
        // called with a local qbool, but INSIDE `apply_rotation` the
        // output backs a ParmVarDecl — so PI-3 classifies it as
        // `Final` and PI-4 emits no inverse for that call site.  The
        // helper's forward side still contributes gates to the IR.
        qbool escape;
        escape.ensure_qubit();
        apply_rotation(escape, a);

        // ↳ Transpiler injects the adjoint dispatch for Case 1 just
        //    before the `}` below.  The exact emitted text is visible
        //    in build/sturm_gen/examples/user_routine.cpp.
    }
    gates_after_adjoint = ctx->ir.size();

    // Gate-count summary.  The Case-1 injection runs `rotate_by_k_adj`
    // on `(tmp, a, 3)` — three `flip()` calls, three gates — so the
    // delta between the post-forward sample and the post-scope sample
    // pins the adjoint firing at run time.  Case 2's forward
    // `apply_rotation` emits between those two samples as well (it's
    // the second forward-only call in source order), so the exact
    // numerical delta is
    //     gates_after_adjoint - gates_after_forward
    //         == apply_rotation.forward + invert(rotate_by_k)
    //         == 3 + 3
    //         == 6
    // assuming the destructor auto-uncompute layer adds no extra
    // gates for `tmp` / `escape` (they're classical at this point).
    std::fprintf(stdout, "gates_after_forward = %zu\n",
                 gates_after_forward);
    std::fprintf(stdout, "gates_after_adjoint = %zu\n",
                 gates_after_adjoint);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
