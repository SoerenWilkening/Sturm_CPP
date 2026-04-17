// hoist_runtime.cpp — Phase J / PJ-3h transpiler INPUT fixture.
//
// Eighth gate-equivalence pair.  Whereas the earlier PG-7 / PH-6a /
// PH-6b / PI-7 / PJ-1i / PJ-4e fixtures pin down the OR matcher's
// per-iteration, per-branch, or nested-WHEN scope lowering, the user-
// routine rewrite, the zero-ancilla fusion, and the dead-ancilla
// elimination, this fixture pins down the Phase J PJ-3
// UNCOMPUTE-HOISTING: a `qbool t = a | b;` VarDecl declared inside a
// for-loop body whose operands (`a`, `b`) are loop-invariant (both are
// outer-scoped function parameters that no statement inside the body
// writes).  The PJ-3d `register_hoist_invariant_matcher` fires on the
// loop-body scope, verifies `classify_scope_kind == LoopBody`, confirms
// every operand is loop-invariant via `detail::expr_is_loop_invariant`,
// and sets `op.hoist_to_override` to the enclosing scope's close brace
// + `op.insert_before_override` to the `for` begin location.  The M8
// uncompute-synthesis pass then consumes `op.hoist_to_override` at
// uncompute-emission time (per the priority ladder in
// `transpiler/src/uncompute_pass.cpp`) — planting the
// `sturm::uncompute_or(t, a, b);` call AT the enclosing scope's close
// brace, i.e. AFTER the for-loop's `}`, not inside the for-body.  The
// hand-written companion `hoist_reference.cpp` spells the same post-
// loop uncompute by hand; the M12 harness byte-compares the two
// captured gate streams.
//
// Demo shape
// ----------
// void demo(const qbool& a, const qbool& b) {
//     qbool t;                          // outer predecl — see below
//     for (int i = 0; i < 3; ++i) {
//         qbool t = a | b;               // forward stays in loop body
//         (void)t;                        //  (PJ-3d only moves the
//     }                                   //   uncompute anchor)
//     // transpiler injects: sturm::uncompute_or(t, a, b);  // hoisted
// }
//
// Why the outer predecl is load-bearing
// -------------------------------------
// The PJ-3d hoist relocates the `uncompute_or(t, a, b);` call to the
// enclosing scope's close brace — AFTER the for-loop's `}`.  At that
// point the INNER loop-body `qbool t` is out of scope, so the hoisted
// call needs an outer `t` to reference or it would fail to compile.
// The outer predecl `qbool t;` is the pragmatic workaround pinned to
// the PJ-3d limitation that forward text-move is deferred (PJ-3g+).
// See also `examples/uncompute_hoisting.cpp` for the same shape in the
// PJ-3g example.
//
// Why `t.ensure_qubit()` on the outer t
// -------------------------------------
// When the harness supplies quantum `a` and `b` (both via
// `qbool::make_non_owning` with `super_mask=1`), the hoisted
// `uncompute_or(t, a, b)` call reaches the assertion
//
//     assert(r_q && "uncompute_or: r must hold an ancilla qubit");
//
// in `src/sturm/uncompute/uncompute_api.cpp` line 73 — the all-classical
// early-return branch ABOVE the assertion requires ALL THREE operands
// (a, b, r) to be classical.  With quantum a/b and a classical outer t,
// the early return does not trigger and the assertion fires.
// `t.ensure_qubit()` allocates a fresh qubit index for the outer t so
// `r_q == true` at the hoisted call site; the uncompute_or's both-quantum
// branch then emits the three-gate adjoint CCX+CX+CX against
// (qa, qb, qt_outer).  The reference fixture performs the same
// `ensure_qubit()` call so the captured streams match byte-for-byte.
// Semantically those three gates are a "spurious" adjoint against the
// outer t (no forward preceded them on that qubit), but gate-stream
// equivalence is what the test verifies — not program correctness on
// the outer t's final state.
//
// Why the `(void)t;` reader is load-bearing
// -----------------------------------------
// Without a reader of `t` in the enclosing scope (the for-body's
// CompoundStmt here), PJ-4a's dead-ancilla eliminator would fire on
// the inner `qbool t = a | b;` decl (reader count == 0) and REMOVE it
// entirely before PJ-3d's hoist matcher gets a chance to look.  With
// the decl gone, the hoist matcher has nothing to hoist and the test
// would fall through to "no forward, no uncompute" — not what we're
// verifying here.  `(void)t;` bumps the reader count to 1, shutting
// PJ-4a off for this slice; PJ-3d then runs cleanly.
//
// Gate-stream witness
// -------------------
// With `STURM_AUTO_UNCOMPUTE` ON (the project default — see
// `CMakeLists.txt` lines 39-43), the inner `qbool t = a | b;` sets
// `t.uncompute_ = make_bitwise_qbool(...)` on the forward OR (per
// `include/sturm/qtypes/qbool_ops.hpp` line 226), and the inner `t`'s
// RAII destructor at the for-body's close brace auto-uncomputes via
// `uncompute_op::apply` — emitting three adjoint gates
// (CCX + CX + CX) AGAINST THE SAME ancilla index.  So each iteration
// emits six gates in total: three forward (CX + CX + CCX) + three
// adjoint (CCX + CX + CX) = 6.  The QubitPool LIFO free-list hands
// the same ancilla index back to the next iteration's `allocate()`,
// so all 3×6 = 18 gates share identical qubit triples
// `(q_a, q_b, q_ancilla)`.
//
// The hoisted `sturm::uncompute_or(t, a, b)` lands after the loop's
// close brace.  Because the outer t has been `ensure_qubit`'d above
// (see "Why `t.ensure_qubit()` on the outer t" above), `r_q == true`
// at the hoisted call site and the uncompute_or's both-quantum branch
// fires — emitting three adjoint gates CCX+CX+CX against
// (q_a, q_b, q_outer_t).  Net captured stream: twenty-one gates
// (eighteen in-loop + three post-loop hoisted), exactly matching
// what the reference fixture produces from its byte-similar shape.
//
// ODR note
// --------
// The harness links this TU together with hoist_reference.cpp.  Both
// define `demo(const qbool&, const qbool&)` inside their own namespace
// to avoid an ODR collision.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and
// the `qbool` type would never appear in the AST, silently disabling
// the MVP OR matcher + PJ-3d's invariance probe.  To make this TU
// parseable by the transpiler AND compilable at runtime, the include
// is gated on `__has_include`: during transpile the preprocessor
// takes the stub branch (which provides a minimal `sturm::qbool` with
// `operator|` the matcher needs to see), and during the downstream
// compile the real headers are available so both the forward `a | b`
// and the injected `sturm::uncompute_or(t, a, b)` resolve to the
// real gate-emitting implementations.
//
// The stub shape mirrors the one used by for_loop_runtime.cpp / the
// hermetic PJ-3f snapshot fixtures under
// `tests/transpiler/fixtures/hoist_*.cpp`: same class layout, same
// free operator, same namespace.  The transpiler cares about the
// textual pattern of `qbool t = a | b;` inside a for-body — not the
// semantic behaviour.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` only pulls in the uncompute free-
// function API.  The real quantum `operator|` and its lazy-expression
// wrapper live in qbool_ops.hpp / lazy_expr.hpp, so include them
// explicitly here.
#  include "sturm/qtypes/qbool.hpp"
#  include "sturm/qtypes/lazy_expr.hpp"
#  include "sturm/qtypes/qbool_ops.hpp"
#else
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    void ensure_qubit() {}
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
#endif

namespace m12_hoist_transpiled {
using sturm::qbool;

// Per-iteration intermediate with loop-invariant operands.  The PJ-3d
// matcher fires on the loop-body scope and sets `op.hoist_to_override`
// to the demo's close brace, so the M8 synthesis pass plants
// `sturm::uncompute_or(t, a, b);` AFTER the for-loop's `}` — resolving
// to the OUTER predecl `qbool t;`.  The inner `qbool t`'s RAII
// destructor still fires per-iteration; with `STURM_AUTO_UNCOMPUTE` ON
// (the project default) the destructor auto-uncomputes via the
// `uncompute_` field set by the forward OR, so the per-iteration
// gate budget is six (3 forward + 3 destructor adjoint) × 3 iterations
// = 18 gates.  The post-loop hoisted call adds three more gates
// (both-quantum CCX+CX+CX against the outer t's ensure_qubit'd
// index).
//
// `const qbool&` arguments preserve the caller's qubit indices across
// the demo boundary (the MVP OR matcher's invariance probe admits
// function-scoped parameters with no writes inside the loop body).
void demo(const qbool& a, const qbool& b) {
    qbool t;  // outer predecl — see top-of-file prose
    t.ensure_qubit();  // allocate outer qubit so the hoisted
                       // uncompute_or does not assert on `r` classical
    for (int i = 0; i < 3; ++i) {
        qbool t = a | b;
        (void)t;
    }
}

} // namespace m12_hoist_transpiled
