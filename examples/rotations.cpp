#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

// Bring `qbool` and `qint_t` into the global namespace so the PN-2
// rotation matchers (`register_theta_add_matcher`,
// `register_theta_sub_matcher`, `register_phi_add_matcher`,
// `register_phi_sub_matcher`) see the same unqualified typed
// DeclRefExpr spelling the hermetic PN-3 snapshot fixtures under
// `tests/transpiler/fixtures/{theta,phi}_{add,sub}_const.cpp` use, and
// so the injected `q.theta() -= d;` / `q.phi() += d;` lines the
// PN-4 uncompute pass emits resolve unqualified at the call site.
// Same convention as the Phase M `examples/peephole_reorder.cpp`, the
// Phase J `examples/zero_ancilla_fusion.cpp`, the Phase I
// `examples/user_routine.cpp`, the Phase H `examples/control_flow.cpp`,
// the Phase G `examples/nested_when.cpp`, the Phase F
// `examples/when_integration.cpp`, and the Phase E
// `examples/compound_expression.cpp` templates.
using sturm::qbool;
using sturm::qint_t;

// ─────────────────────────────────────────────────────────────────────────────
// Phase N: amplitude + phase rotation compound-assign demo (P5 items 2 & 3)
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognises each of the four continuous-parameter
// rotation compound-assigns on qint_t<W>'s nested proxies when the RHS
// is a `double`-valued expression, and injects the sign-flipped inverse
// over the same verbatim RHS token before the enclosing scope closes
// (LIFO order).  The runtime's self-dual ThetaProxy / PhiProxy
// `operator-=` (at `include/sturm/qtypes/qint_core.hpp:305,372`)
// dispatches `-delta` through the same Ry(θ) / Rz(θ) emission path,
// so the forward + uncompute GateRecord stream cancels to identity:
//
//   Source pattern           | Phase | Injected inverse
//   -------------------------|-------|------------------------
//   q.theta() += <rhs>;      | PN-2a | q.theta() -= <rhs>;
//   q.theta() -= <rhs>;      | PN-2b | q.theta() += <rhs>;
//   q.phi()   += <rhs>;      | PN-2c | q.phi()   -= <rhs>;
//   q.phi()   -= <rhs>;      | PN-2d | q.phi()   += <rhs>;
//
// DEPTH-1 WHEN-GUARDED ROTATION (B5 invariant)
// --------------------------------------------
// Principle B5 ("Primitives have uncontrolled and singly-controlled
// forms only.") combined with Phase G's AND-fold (which collapses
// nested WHEN chains to depth ≤ 1 before the rotation matcher ever
// sees them) means the transpiler only ever emits an uncontrolled
// inverse `q.theta() -= d;` or a depth-1 WHEN-guarded inverse
// `WHEN(c) { q.theta() -= d; }`.  A rotation inside two nested WHENs
// is forbidden by construction: Phase G AND-fold collapses
// `WHEN(outer) WHEN(inner)` into a single `WHEN(outer & inner)` via
// an explicit `uncompute_and` temporary, so depth-2 cannot reach the
// rotation matcher.
//
// This example exercises exactly ONE depth-1 WHEN-guarded rotation
// (on `a.theta()` controlled by `c`).  A depth-2 nesting would be a
// test that this example is wrong — the Phase G AND-fold would refuse
// to fire on a rotation body, and the PN-2 matcher would see a
// depth-1 control at the rotation site.  The single-depth construction
// is the load-bearing invariant; adding a nested WHEN here is a bug.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_rotations`, open:
//     build/sturm_gen/examples/rotations.cpp
// main()'s inner scope has been rewritten through the pipeline:
//   - Four forward rotations on `a.theta()` / `a.theta()` / `b.phi()`
//     / `b.phi()` pass through PA-3 / PB-1..4 matchers untouched and
//     stage four `QOperation{kind=THETA_ADD_ASSIGN_CONST,...}` /
//     etc. on the enclosing QScope.
//   - The depth-1 WHEN-guarded `a.theta() += 0.5;` inside
//     `WHEN(c) { ... }` stages a fifth QOperation whose enclosing
//     scope is the inner WHEN body's CompoundStmt.
//   - The M8 uncompute-synthesis pass (PN-4 arms in
//     `transpiler/src/uncompute_pass.cpp`) plants five sign-flipped
//     LIFO inverses:
//       * `a.theta() -= 0.5;` AFTER the WHEN body's existing op
//         (still inside the WHEN body so the control scope covers
//         both directions — depth-1 symmetric).
//       * `b.phi()   += 0.2;` (dual of `b.phi()   -= 0.2;`)
//       * `b.phi()   -= 0.7;` (dual of `b.phi()   += 0.7;`)
//       * `a.theta() += 0.1;` (dual of `a.theta() -= 0.1;`)
//       * `a.theta() -= 0.3;` (dual of `a.theta() += 0.3;`)
//     in LIFO / reverse-source order.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_rotations
//
// The program prints an ASCII circuit diagram showing the Ry / Rz
// rotation records acting on the qint_t's auto-promoted qubit lanes
// (and the CRy on the depth-1 WHEN-controlled rotation).  Each
// forward rotation's injected inverse emits the sign-flipped delta
// through the runtime's self-dual operator-=, so every Ry/Rz pair
// cancels to identity — the diagram's net quantum state effect is
// the identity operator on the register.  The load-bearing
// observable is the GENERATED FILE layout, not the runtime stream.
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    // The Phase N rotation happy path.  The inline comments inside
    // `main()` deliberately avoid spelling any of the transpiler-
    // injected fragments verbatim so the PN-7 byte-identity check
    // and the "did-it-land-in-the-right-place?" search in
    // check_example_rotations.cmake can both anchor on `int main()`
    // without false-positives.  The detailed rewrite shape is
    // described at the top of the file.
    //
    // Two qint_t<1> registers and one qbool control.  The qint_t
    // registers are fully-classical on entry (qubits[0] < 0); the
    // first rotation auto-promotes via the M15/M16 auto-promote
    // path in ThetaProxy / PhiProxy `operator+=`, allocating one
    // qubit per register and emitting a real Ry / Rz record.  The
    // qbool `c` is superposed so the depth-1 WHEN-guard puts a
    // real CRy on the inner rotation.  The inner scope ensures
    // every PN-4 injected inverse fires BEFORE the qint / qbool
    // destructors run.
    {
        qint_t<1> a;
        qint_t<1> b;
        qbool c(0.5);

        // PN-2a / PN-2b — amplitude rotation compound-assigns.
        // Each line stages a THETA_ADD_ASSIGN_CONST /
        // THETA_SUB_ASSIGN_CONST QOperation on the enclosing QScope;
        // the PN-4 uncompute arms plant the sign-flipped inverse
        // on the same proxy before the inner scope's close brace.
        // The exact emitted text is visible in
        //   build/sturm_gen/examples/rotations.cpp.
        a.theta() += 0.3;
        a.theta() -= 0.1;

        // PN-2c / PN-2d — phase rotation compound-assigns, same
        // inversion shape on the phi proxy.  The PN-4 uncompute
        // arms emit the sign-flipped duals in LIFO order before
        // the inner scope's close brace.  The exact emitted text
        // is visible in
        //   build/sturm_gen/examples/rotations.cpp
        // (and is spelled verbatim in the top-of-file schematic
        // prose — we omit it here so the PN-7 no-leak check does
        // not false-positive on this comment).
        b.phi() += 0.7;
        b.phi() -= 0.2;

        // Depth-1 WHEN-guarded rotation — B5 invariant anchor.
        // The inner rotation stages a THETA_ADD_ASSIGN_CONST
        // QOperation on the WHEN body's QScope (not the outer
        // inner-scope), so the PN-4 uncompute emits the sign-
        // flipped dual INSIDE the WHEN body, not after it (the
        // exact emitted text is spelled verbatim in the top-of-
        // file schematic prose — we omit it here so the PN-7
        // no-leak check does not false-positive on this comment).
        // Phase G AND-fold would collapse nested WHEN chains into
        // a single `WHEN(...)` via a synthetic control temp plus
        // matching AND uncompute, so a second nested WHEN here
        // would be a test that this example is wrong (depth-2
        // guards never reach the rotation matcher by construction).
        WHEN(c) {
            a.theta() += 0.5;
        }
    }

    // Emit the ASCII circuit diagram AFTER the Phase N scope closes,
    // so every PN-4 injected inverse has fired before the renderer
    // walks the GateIR.  The diagram shows the Ry / Rz (and CRy)
    // records with their sign-flipped duals cancelling to identity;
    // the load-bearing observable is the GENERATED FILE layout
    // checked by `check_example_rotations.cmake`, not the runtime
    // gate stream.
    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
