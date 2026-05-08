#include "sturm.h"
#include "sturm/draw_ascii.h"
#include "sturm/routines/invert.hpp"

#include <cstdint>
#include <cstdio>

// Bring `invert` into the global namespace so the transpiler-emitted
// `invert(rotate_by_k)(tmp, a, 3);` call resolves the helper unqualified.
using sturm::invert;

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
//                             that INSIDE the helper's body as `Final`
//                             (the caller owns the uncompute policy
//                             per principle P9) so the inner
//                             `rotate_by_k(out, a, 3);` call gets NO
//                             injected `invert(...)` companion inside
//                             the helper's body.  The helper ITSELF is
//                             a quantum routine (non-const qbool&
//                             output), so P9 still demands a
//                             hand-written adjoint at TU scope — see
//                             `STURM_REGISTER_ADJOINT(apply_rotation,
//                             apply_rotation_adj)` below.  At main()'s
//                             `apply_rotation(escape, a);` call-site
//                             the output argument `escape` is a local
//                             qbool (Intermediate class), so PI-4
//                             plants `invert(apply_rotation)(escape,
//                             a);` before main's inner scope closes.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_user_routine`, open:
//     build/sturm_gen/examples/user_routine.cpp
// main()'s inner scope contains the forward `rotate_by_k(tmp, a, 3);`
// and `apply_rotation(escape, a);` calls followed — before the `}`,
// in LIFO order — by the injected `invert(apply_rotation)(escape,
// a);` and `invert(rotate_by_k)(tmp, a, 3);` lines.
// apply_rotation's body contains ONLY the forward
// `rotate_by_k(out, a, 3);` call — no inverse is emitted THERE
// because `out` is a ParmVarDecl (escapes); the caller's scope is
// where the inverse lands.
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
// AND `apply_rotation_adj` is the manual adjoint of
// `apply_rotation` (see the bodies below — each forward flip is
// cancelled by a matching flip in the adjoint), and each routine
// emits exactly `k` gates on its output, the summary satisfies
//     N2 - N1 == apply_rotation.forward
//              + invert(apply_rotation)
//              + invert(rotate_by_k)
//              == k + k + k
//              == 3k
// for k=3, that is 9.  The three contributions are:
//   - apply_rotation's forward body (runs between the N1 sample
//     and scope close),
//   - the PI-4-injected `invert(apply_rotation)(escape, a);`
//     before main's inner `}` (LIFO — runs first of the two
//     injected inverses),
//   - the PI-4-injected `invert(rotate_by_k)(tmp, a, 3);` before
//     main's inner `}` (runs second, cancelling the Case-1
//     forward).
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API (including `qbool` at namespace
// scope) and the auto-injected lifecycle wraps `main` with
// `sturm_backend_create` / `destroy`.

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
// Phase I PI-3 classifies the output-parameter `out` INSIDE this
// function's body as `Final` (it's a ParmVarDecl), so the PI-4
// uncompute pass emits nothing for the inner
// `rotate_by_k(out, a, 3);` call.  The caller (main() below) is
// responsible for the adjoint policy of whatever backing qbool it
// passed in — principle P9 in docs/01_principles.md.  The helper's
// body is therefore transpile-invariant: the generated sibling's
// `apply_rotation` contains only the forward `rotate_by_k(out, a, 3);`
// call, with no injected `invert(...)` companion.
//
// `apply_rotation` is itself a quantum routine with a non-const
// `qbool&` output parameter, so principle P9 (and the PM3-3
// missing-adjoint diagnostic that enforces it) demands a
// hand-written adjoint at TU scope paired through
// `STURM_REGISTER_ADJOINT(apply_rotation, apply_rotation_adj)`.
// The adjoint below runs the same `rotate_by_k` body — a length-3
// X-chain composes with another length-3 X-chain to identity on
// the same qubit, so `apply_rotation` is self-inverse.
void apply_rotation(qbool& out, const qbool& a) {
    rotate_by_k(out, a, 3);
}

// Hand-written adjoint for `apply_rotation`.  Runs the same
// `rotate_by_k(out, a, 3)` body as the forward — `apply_rotation`
// is self-inverse because its body is a length-3 X-chain, and
// two length-3 X-chains on the same qubit compose to identity.
// Calling `rotate_by_k` (rather than `rotate_by_k_adj`) on the
// adjoint side keeps the TU free of a trivial "self-inverse
// wrapper adjoint" register for `rotate_by_k_adj` — the inner
// call resolves through the already-registered `rotate_by_k`
// adjoint pair just like the forward does.  PI-3 classifies the
// output slot `out` of the inner `rotate_by_k(out, a, 3);` call
// as `Final` (ParmVarDecl) so the PI-4 uncompute pass emits no
// injection inside this adjoint's body — the caller of
// `apply_rotation_adj` owns the uncompute policy per P9.
void apply_rotation_adj(qbool& out, const qbool& a) {
    rotate_by_k(out, a, 3);
}

// Register `apply_rotation`'s (forward, adjoint) pair so the PI-1
// routine-registry matcher picks up the specialization of
// `sturm::_detail::adjoint_of<decltype(&::apply_rotation)>`.  This
// silences the PM3-3 missing-adjoint Error that would otherwise
// fire on the `apply_rotation(escape, a);` call in main() (where
// the output argument `escape` is a local qbool classified as
// `Intermediate` — from main's vantage point the output is NOT
// Final, so P9 demands that main be able to plant the routine's
// adjoint before its scope closes).
STURM_REGISTER_ADJOINT(apply_rotation, apply_rotation_adj)

int main() {
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
        gates_after_forward = sturm::gate_count();

        // Case 2: `apply_rotation` is called with a local qbool
        // `escape` — from main's vantage point the output is
        // `Intermediate` and PI-4 plants `invert(apply_rotation)(
        // escape, a);` before the `}` below.  Inside
        // `apply_rotation`'s own body the `rotate_by_k(out, a, 3);`
        // call has `out` backing a ParmVarDecl, so PI-3 classifies
        // THAT as `Final` and PI-4 emits no inverse inside the
        // helper itself — the caller owns the uncompute policy per
        // principle P9.
        qbool escape;
        escape.ensure_qubit();
        apply_rotation(escape, a);

        // ↳ Transpiler injects the adjoint dispatch for Case 1 just
        //    before the `}` below.  The exact emitted text is visible
        //    in build/sturm_gen/examples/user_routine.cpp.
    }
    gates_after_adjoint = sturm::gate_count();

    // Gate-count summary.  Three contributions land between N1 and
    // N2 at run time; see the top-of-file prose for the detailed
    // per-call accounting.  The per-routine bookkeeping resolves to
    //     gates_after_adjoint - gates_after_forward == 3k == 9
    // for k=3, assuming the destructor auto-uncompute layer adds no
    // extra gates for `tmp` / `escape` (they're classical at this
    // point).  The exact emitted forms of the two PI-4 inverse
    // injections live in build/sturm_gen/examples/user_routine.cpp.
    std::fprintf(stdout, "gates_after_forward = %zu\n",
                 gates_after_forward);
    std::fprintf(stdout, "gates_after_adjoint = %zu\n",
                 gates_after_adjoint);

    return 0;
}
