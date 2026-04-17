// hoist_reference.cpp — Phase J / PJ-3h hand-written control for the
// uncompute-hoisting gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the transpiler processes
// hoist_runtime.cpp.  The demo body is deliberately byte-similar to
// the transpiler's expected output: an outer predecl `qbool t;`, a
// for-loop body carrying the forward `qbool t = a | b;` +
// `(void)t;` reader pair, and a HOISTED `sturm::uncompute_or(t, a, b);`
// call AFTER the for-loop's closing `}` (at the enclosing scope's
// close brace) — which is exactly the shape the PJ-3d matcher
// produces via `op.hoist_to_override`.
//
// Having a hand-written control checked in directly is what turns
// PJ-3h into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and any
// drift in either the transpiler's PJ-3d hoist anchor selection, the
// M8 synthesis pass's consumption of `op.hoist_to_override`, or the
// runtime OR / uncompute_or decomposition shows up as a mismatch in
// the captured gate stream.
//
// Gate-stream witness
// -------------------
// Identical to the runtime fixture: three iterations of
// `qbool t = a | b;` each emit six gates (three forward CX+CX+CCX
// plus three adjoint CCX+CX+CX via the RAII destructor's auto-
// uncompute under `STURM_AUTO_UNCOMPUTE` ON), totalling eighteen
// gates.  The post-loop `sturm::uncompute_or(t, a, b);` references the
// OUTER predecl `qbool t;` which has had `ensure_qubit()` called on
// it (so `r_q == true` at the uncompute_or call site, avoiding the
// `assert(r_q, ...)` in `src/sturm/uncompute/uncompute_api.cpp` line
// 73); with quantum a, quantum b, quantum outer t the both-quantum
// branch fires and emits the three-gate adjoint CCX+CX+CX against
// (qa, qb, qt_outer).  The runtime fixture performs the same
// `ensure_qubit()` call so both streams match byte-for-byte.  Total
// captured stream: twenty-one gates (eighteen in-loop + three
// post-loop hoisted).
//
// ODR note
// --------
// The harness links this TU together with hoist_runtime.cpp.  Both
// define `demo(const qbool&, const qbool&)` inside their own namespace
// to avoid collisions at the demo level.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` / `sturm::uncompute_or` so each iteration's forward
// three-gate OR (CX+CX+CCX) and its three-gate RAII-destructor
// adjoint actually land in the captured gate stream.  The umbrella
// header is included directly — no `__has_include` guard is necessary
// here because this file never passes through the transpiler (CMake
// compiles it as-is) and the test target always sees `sturm`'s
// include directory.

#include "sturm/sturm.hpp"
// The umbrella `sturm/sturm.hpp` pulls in the uncompute free-function
// API (which declares `sturm::uncompute_or`).  The real quantum
// `operator|` and its lazy-expression wrapper live in qbool_ops.hpp /
// lazy_expr.hpp, so include them explicitly here — without them the
// `a | b` below would hit the classical short-circuit path (which
// emits no gates) or fail to compile entirely.
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_hoist_reference {

// Hand-written realisation of the PJ-3d uncompute-hoisting rewrite.
// The shape mirrors the expected transpiler output verbatim:
//
//   outer predecl            `sturm::qbool t; t.ensure_qubit();`
//   forward inside loop      `sturm::qbool t = a | b; (void)t;`
//                            (per-iteration; six gates via forward OR
//                             + RAII destructor auto-uncompute)
//   HOISTED post-loop        `sturm::uncompute_or(t, a, b);`
//                            (both-quantum branch — three gates
//                             CCX+CX+CX against (qa, qb, qt_outer))
//
// `const sturm::qbool&` arguments preserve the caller's qubit indices;
// see the rationale in test_gate_equivalence.cpp.  Three iterations ×
// six gates = eighteen gates from the loop, plus three more from the
// post-loop hoisted uncompute_or — twenty-one gates total.  The
// outer-t `ensure_qubit()` call is load-bearing: without it the
// hoisted uncompute_or hits `assert(r_q, ...)` in
// `src/sturm/uncompute/uncompute_api.cpp` because quantum a/b alongside
// a classical outer t fall BELOW the all-classical early-return guard.
// The runtime fixture performs the same `ensure_qubit()` call so
// both streams match byte-for-byte.
void demo(const sturm::qbool& a, const sturm::qbool& b) {
    sturm::qbool t;  // outer predecl — see top-of-file prose
    t.ensure_qubit();  // see top-of-file prose
    for (int i = 0; i < 3; ++i) {
        sturm::qbool t = a | b;
        (void)t;
    }
    sturm::uncompute_or(t, a, b);
}

} // namespace m12_hoist_reference
