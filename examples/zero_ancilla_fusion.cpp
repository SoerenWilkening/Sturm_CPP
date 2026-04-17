#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/sturm.hpp"
#include "sturm/uncompute/uncompute_api.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

// Bring `qbool` into the global namespace so the fused
// `ccnot_inplace(x, a, b);` call the transpiler emits — both as the
// forward replacement over `qbool __t = a & b; x ^= __t;` AND as the
// self-adjoint uncompute planted before the enclosing scope closes —
// resolves unqualified at the call site.  Same convention as the Phase H
// `examples/control_flow.cpp`, the Phase G `examples/nested_when.cpp`,
// the Phase F `examples/when_integration.cpp`, the Phase E
// `examples/compound_expression.cpp`, and the Phase I
// `examples/user_routine.cpp` templates.  The hermetic snapshot
// fixtures under `tests/transpiler/fixtures/fuse_xor_and*` rely on the
// same `using sturm::qbool;` shape so the PJ-1d matcher sees the same
// typed DeclRefExpr spelling.
using sturm::qbool;

// ─────────────────────────────────────────────────────────────────────────────
// Phase J PJ-1: zero-ancilla fusion demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises the canonical two-statement peephole
//
//     qbool __t = a & b;        // VarDecl init = bare `&` op-call
//     x ^= __t;                 // adjacent `^=` whose RHS is __t
//
// when `__t` has exactly one reader in its enclosing scope (the `^=`
// RHS itself).  On each matched pair the PJ-1d peephole matcher
// (transpiler/src/matcher_ccnot_fuse.cpp) performs a two-point rewrite:
//
//   1. The ORIGINAL pair is REPLACED by a single
//      `ccnot_inplace(x, a, b);` call — a QReplacement whose source
//      range spans both statements, starting at the VarDecl's begin
//      and ending at the `^=` op-call's terminating `;`.  The fused
//      call collapses the forward CCX(a, b, __t) + CX(__t, x) +
//      uncompute CCX sequence into a single CCX(a, b, x) acting
//      in-place on `x` with NO intermediate ancilla qubit.
//
//   2. A matching `QOperation{kind=CCNOT_INPLACE}` is staged on the
//      enclosing QScope so the M8 uncompute-synthesis pass (via the
//      PJ-1c render case in transpiler/src/uncompute_pass.cpp) emits
//      a SECOND `ccnot_inplace(x, a, b);` call directly before the
//      scope's closing `}`.  CCX is self-adjoint — running the helper
//      a second time on the live state undoes the forward flip — so
//      the forward and uncompute emissions share a single identifier.
//
// Rule table (see transpiler/src/matcher_ccnot_fuse.cpp for the full
// decision tree, and the PJ-1g snapshot fixtures under
// `tests/transpiler/fixtures/fuse_xor_and*` for every rejected branch):
//
//   Shape                                  | PJ-1d verdict
//   ---------------------------------------|----------------------------
//   bare DREs, one reader, adjacent `^=`   | FUSE (happy path)
//   two-or-more readers in scope           | REJECT (reader-count guard)
//   nested op-call in `&` init             | REJECT (bare-DRE guard) →
//                                          |   Phase E PE-4 flattens
//   next stmt is `^=` on a DIFFERENT qbool | REJECT (consumer guard) →
//                                          |   Phase A PA-3 schedules
//                                          |   its self-adjoint inverse
//   next stmt is `x ^= <classical int>`    | REJECT (RHS-DRE guard) →
//                                          |   Phase A PA-4 schedules
//                                          |   its self-adjoint inverse
//
// This example exercises the HAPPY path: two superposed qbools `a` and
// `b` flow into the `&` init of a local intermediate `__t`, which is
// consumed exactly once by an adjacent `x ^= __t;` flip.  The PJ-1d
// matcher fuses the pair into a forward `ccnot_inplace(x, a, b);`, the
// uncompute pass plants a second `ccnot_inplace(x, a, b);` before the
// inner scope's close brace, and the ASCII circuit renderer confirms
// exactly two CCX records acting on `(a, b, x)` with NO ancilla
// allocation anywhere — zero intermediate qubits, hence the name.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_zero_ancilla_fusion`, open:
//     build/sturm_gen/examples/zero_ancilla_fusion.cpp
// The original `qbool __t = a & b;` + `x ^= __t;` pair inside main()'s
// inner scope has been REPLACED in-place by a single
// `ccnot_inplace(x, a, b);` call; a SECOND `ccnot_inplace(x, a, b);`
// line sits directly before the inner scope's closing `}`.  No
// `__t` decl or `^=` stmt survives in the generated body — the
// QReplacement span covers both statements and the Rewriter strips
// the original text.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_zero_ancilla_fusion
//
// The program prints an ASCII circuit diagram showing exactly TWO CCX
// records acting on the same three qubit lanes (`a`, `b`, `x`).  No
// intermediate qubit index appears in the diagram — the PJ-1 fusion
// erased the ancilla allocation that the unfused path would have
// done for `__t`.  The two CCX records cancel exactly (CCX is
// self-inverse), so the net effect on the live state is identity.
// The diagram is the concrete witness that the fusion fired: before
// PJ-1, the same source would have produced a forward CCX(a, b, __t)
// + CX(__t, x) + uncompute CCX(a, b, __t) triple on FOUR qubit lanes;
// after PJ-1 the triple collapses to TWO CCX(a, b, x) records on
// THREE lanes.
//
// NOTE on STURM_AUTO_UNCOMPUTE: same caveat as the earlier Phase A–I
// examples — the build defaults to ON, which layers the destructor's
// auto-uncompute on top of the transpiler's injection.  `x` is the
// only qbool of this example whose destructor could double-uncompute,
// and because CCX is self-inverse, running the adjoint twice is the
// identity.  To see ONLY the transpiler's contribution, configure
// with `-DSTURM_AUTO_UNCOMPUTE=OFF`.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // The Phase J PJ-1 zero-ancilla fusion happy path.  The inline
    // comments inside `main()` deliberately avoid spelling any of the
    // transpiler-injected fragments verbatim so the PJ-1h byte-identity
    // check and the "did-it-land-in-the-right-place?" search in
    // check_example_zero_ancilla_fusion.cmake can both anchor on
    // `int main()` without false-positives.  The detailed rewrite
    // shape is described at the top of the file.
    //
    // All three qbools are superposed (allocated + prepared at p=0.5)
    // so the fused forward `ccnot_inplace(x, a, b)` and its self-adjoint
    // uncompute both emit a real CCX record — the ASCII diagram below
    // shows the pair on three distinct qubit lanes (a, b, x).  The
    // inner scope ensures the transpiler's injected inverse fires
    // BEFORE the qbool destructors run.
    {
        qbool a(0.5);
        qbool b(0.5);
        qbool x(0.5);

        // PJ-1 — the canonical fusion pair.  The transpiler REPLACES
        // both statements below in the generated sibling with a
        // single forward fused call, and plants a matching self-
        // adjoint inverse just before the inner-scope close brace.
        // The exact emitted text is visible in
        //   build/sturm_gen/examples/zero_ancilla_fusion.cpp.
        qbool __t = a & b;
        x ^= __t;
    }

    // Emit the ASCII circuit diagram AFTER the Phase J scope closes,
    // so both the transpiler-injected forward fused call and its
    // self-adjoint inverse have fired before the renderer walks the
    // GateIR.  The diagram shows two CCX records on the same (a, b, x)
    // lanes — the PJ-1 fusion erased the ancilla that the unfused
    // path would have allocated for `__t`, so no fourth qubit lane
    // appears in the diagram.
    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
