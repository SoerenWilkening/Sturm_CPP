// if_branches_runtime.cpp — Phase H / PH-6b transpiler INPUT fixture.
//
// Second gate-equivalence pair for Phase H.  Whereas PH-6a exercises the
// for-loop control-flow kind, this fixture pins down the classical
// if/else kind: each branch declares its OWN local qbool intermediate
// via the OR matcher pattern, so the transpiler is expected to plant a
// `sturm::uncompute_or(...)` call immediately before each branch's
// closing `}`.  The hand-written companion `if_branches_reference.cpp`
// spells the same pair of lowerings verbatim; the M12 harness
// `test_gate_equivalence.cpp` byte-compares the two captured gate
// streams.
//
// Demo shape
// ----------
// void demo(const qbool& a, const qbool& b, bool cond) {
//     if (cond) {
//         qbool x = a | b;
//         // transpiler injects: sturm::uncompute_or(x, a, b);
//     } else {
//         qbool y = a | b;
//         // transpiler injects: sturm::uncompute_or(y, a, b);
//     }
// }
//
// Runtime branch coverage
// -----------------------
// At run time, `cond` is a classical bool so only one branch fires per
// call.  The harness `run_and_capture_if_branches` calls `demo` TWICE
// in sequence — once with `cond=true`, once with `cond=false` — so the
// captured gate stream exercises BOTH branch lowerings in a single
// BackendContext.  The reference fixture is driven identically.
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
// nested_when_runtime.cpp: same class layout, same free operator, same
// namespace.  The transpiler cares about the textual pattern of
// `qbool var = a | b;` — not the semantic behaviour.
//
// Namespace isolation
// -------------------
// The M12 harness links this TU together with if_branches_reference.cpp.
// Both define a `demo(const qbool&, const qbool&, bool)` function.
// Wrapping each in a dedicated namespace avoids ODR collision at link
// time; inside the namespace a `using sturm::qbool;` brings the qbool
// type into the local scope so the DSL pattern reads as it would to
// the end user.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` only pulls in the uncompute free-
// function API.  The real quantum `operator|` and its lazy-expression
// wrapper live in qbool_ops.hpp / lazy_expr.hpp, so include them
// explicitly here.
#  include "sturm/qtypes/qbool.hpp"
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

namespace m12_if_branches_transpiled {
using sturm::qbool;

// Distinct intermediates per branch.  The PH-1 enclosing_scope
// refactor keys a separate QScope on each CompoundStmt body, so the
// M8 uncompute pass plants one `sturm::uncompute_or(x, a, b);` inside
// the then-arm and one `sturm::uncompute_or(y, a, b);` inside the
// else-arm.  `a` and `b` cross the branch boundary unchanged — no
// PH-3 outer-variable-mutation diagnostic.
//
// Why the `(void)x;` / `(void)y;` readers are load-bearing
// --------------------------------------------------------
// Phase J PJ-4a (dead-ancilla elimination) removes any `qbool` VarDecl
// whose value is never read.  Without readers in each arm, PJ-4a
// fires before the M7 OR matcher: both `qbool {x,y} = a | b;` lines
// are dropped, each arm collapses to empty, no uncompute is injected,
// and the captured gate stream on the transpiled side is zero-length
// vs. the reference fixture's six-gate pair per branch.  The explicit
// `(void)x;` / `(void)y;` readers bump each reader count to 1,
// shutting off PJ-4a so the M7 matcher runs per arm as intended.
// Same rationale — and same textual shape — as or_single_runtime.cpp /
// hoist_or_out_of_for.cpp.
void demo(const qbool& a, const qbool& b, bool cond) {
    if (cond) {
        qbool x = a | b;
        (void)x;
    } else {
        qbool y = a | b;
        (void)y;
    }
}

} // namespace m12_if_branches_transpiled
