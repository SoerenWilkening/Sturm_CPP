// for_loop_runtime.cpp — Phase H / PH-6a transpiler INPUT fixture.
//
// First gate-equivalence pair for Phase H.  PH-6a pins down the
// classical for-loop control-flow kind: each iteration declares its
// OWN local qbool intermediate via the OR matcher pattern, so the
// transpiler is expected to plant a `sturm::uncompute_or(...)` call
// immediately before the loop body's closing `}` — once per iteration.
// The hand-written companion `for_loop_reference.cpp` spells the same
// per-iteration lowering verbatim; the M12 harness
// `test_gate_equivalence.cpp` byte-compares the two captured gate
// streams.
//
// Demo shape
// ----------
// void demo(const qbool& a, const qbool& b) {
//     for (int i = 0; i < 3; ++i) {
//         qbool tmp = a | b;
//         // transpiler injects: sturm::uncompute_or(tmp, a, b);
//     }
// }
//
// Per-iteration ancilla recycling
// -------------------------------
// At run time each iteration's `qbool tmp = a | b;` allocates a fresh
// ancilla via `QubitPool::allocate()`; the forward OR emits CX+CX+CCX
// against that index, the injected `sturm::uncompute_or(tmp, a, b);`
// emits the three-gate adjoint, and `tmp`'s RAII destructor releases
// the index at the `}`.  Because `QubitPool` is a LIFO free-list the
// NEXT iteration's `allocate()` returns the SAME index, so every
// iteration emits six gates against identical qubit indices.  Three
// iterations × six gates = eighteen gates in the captured stream, and
// the reference fixture produces a byte-identical sequence because it
// uses the same pool ordering.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and the
// `qbool` symbol would never appear in the AST, which would silently
// disable the OR matcher.  To make this TU parseable by the transpiler
// AND compilable at runtime, the include is gated on `__has_include`:
// during transpile the preprocessor takes the stub branch (which
// provides a minimal `sturm::qbool` with the `operator|` overload the
// matcher needs to see), and during the downstream compile the real
// headers are available so the transpiler-injected
// `sturm::uncompute_or(...)` call resolves via ADL.
//
// The stub shape mirrors the one used by or_single_runtime.cpp /
// if_branches_runtime.cpp: same class layout, same free operator, same
// namespace.  The transpiler cares about the textual pattern of
// `qbool tmp = a | b;` — not the semantic behaviour.
//
// Namespace isolation
// -------------------
// The M12 harness links this TU together with for_loop_reference.cpp.
// Both define a `demo(const qbool&, const qbool&)` function.  Wrapping
// each in a dedicated namespace avoids ODR collision at link time;
// inside the namespace a `using sturm::qbool;` brings the qbool type
// into the local scope so the DSL pattern reads as it would to the end
// user.
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
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
#endif

namespace m12_for_loop_transpiled {
using sturm::qbool;

// Per-iteration intermediate.  The PH-1 enclosing_scope refactor keys
// a QScope on the for-body's CompoundStmt, so the M8 uncompute pass
// plants `sturm::uncompute_or(tmp, a, b);` inside the loop body — once
// per iteration.  `a` and `b` cross the loop boundary unchanged — no
// PH-3 outer-variable-mutation diagnostic.  The for-body is BRACED,
// so PH-2 brace-wrap does not fire on this fixture (PH-2 coverage is
// already exercised by fixtures/for_intermediate_or_braceless.cpp).
//
// Why the `(void)tmp;` reader AND the inner-scope wrap are load-bearing
// ---------------------------------------------------------------------
// Phase J PJ-4a (dead-ancilla elimination) removes any `qbool` VarDecl
// whose value is never read.  Without a reader, PJ-4a fires before the
// M7 OR matcher: the entire `qbool tmp = a | b;` line is dropped, the
// for-body collapses to empty, no uncompute is injected, and the
// captured gate stream on the transpiled side is zero-length vs. the
// reference fixture's twenty-seven gates.  The explicit `(void)tmp;`
// bumps the reader count to 1, shutting off PJ-4a so the M7 matcher
// runs as intended.  Same rationale — and same textual shape — as
// or_single_runtime.cpp / hoist_or_out_of_for.cpp.
//
// Additional guard against PJ-3d hoisting: adding `(void)tmp;` alone
// would let PJ-3d inspect the now-alive OR op's operands (`a`, `b`),
// find them loop-invariant (outer-scoped params, no writes inside),
// and hoist the uncompute OUTSIDE the for-loop — producing a 21-gate
// stream instead of the reference's 27-gate per-iteration stream.  We
// wrap the decl + reader in an inner `{ ... }` CompoundStmt whose
// parent is the for-body CompoundStmt (NOT the ForStmt itself) —
// `classify_scope_kind` returns `Other` for a non-loop-direct-body
// scope, so PJ-3d skips and the uncompute_or is planted at the
// inner-scope `}` inside the loop body.  The per-iteration gate
// sequence is preserved.
void demo(const qbool& a, const qbool& b) {
    for (int i = 0; i < 3; ++i) {
        {
            qbool tmp = a | b;
            (void)tmp;
        }
    }
}

} // namespace m12_for_loop_transpiled
