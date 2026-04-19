// or_single_runtime.cpp — M12 transpiler INPUT fixture.
//
// Intended to be processed by `sturm-transpile` via the
// `add_quantum_executable()` build-system helper.  The transpiler is
// expected to inject an `uncompute_or(tmp, a, b);` call immediately
// before the enclosing scope's close brace — mirroring the PRD-MVP
// canonical hello-world in docs/prd_transpiler_uncompute.md.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp line 243).  A
// bare `#include <sturm/sturm.hpp>` would therefore fail to resolve
// and the `qbool` type would never appear in the AST, which would
// silently disable the M7 matcher.  To make this TU parseable by the
// transpiler AND compilable at runtime, the include is gated on
// `__has_include`: during transpile the preprocessor takes the stub
// branch (which provides a minimal `sturm::qbool` with the operator|
// the matcher needs to see), and during the downstream `add_quantum_
// executable` compile the real header is available so `a | b` emits
// the full quantum OR circuit and the injected `uncompute_or` resolves
// to `sturm::uncompute_or` via argument-dependent lookup.
//
// The stub's shape mirrors the one used by `or_single.cpp` / the
// transpiler's own test_emitter snapshots: same class layout, same
// free operator, same namespace.  The transpiler cares about the
// textual pattern of `qbool tmp = a | b;` — not the semantic behavior.
//
// Namespace isolation
// -------------------
// The M12 harness links this TU together with `or_single_reference.cpp`.
// Both define a `demo(const qbool&, const qbool&)` function.  Wrapping
// each in a dedicated namespace avoids ODR collision at link time.
// Inside the namespace a `using sturm::qbool;` brings the qbool type
// into the local scope so the DSL pattern reads as it would to the
// end user.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` only pulls in the uncompute free-
// function API.  The real quantum `operator|` lives in
// `qbool_ops.hpp`, so include it explicitly here — without it the
// `a | b` below would hit the eager `qbool_logic.hpp` path (which
// does not emit gates) or fail to compile entirely.
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

namespace m12_transpiled {
using sturm::qbool;

// The transpiler's matcher fires on `qbool tmp = a | b;`.  At runtime
// `a | b` allocates a fresh ancilla and emits the three-gate OR
// circuit (CX + CX + CCX).  Without an injected `uncompute_or`, the
// gate stream would be missing the three-gate adjoint this fixture is
// supposed to produce — the M12 test_gate_equivalence harness would
// then see a length mismatch against the hand-written reference.
// That is the contract the transpiler must uphold.
//
// Why the `(void)tmp;` reader is load-bearing
// -------------------------------------------
// Phase J PJ-4a (dead-ancilla elimination) removes any `qbool` VarDecl
// whose value is never read.  Without a reader, PJ-4a fires before the
// M7 uncompute-injection matcher: the entire `qbool tmp = a | b;` line
// is dropped, the function body becomes empty, and the M12 harness
// sees a zero-length transpiled gate stream vs. the 9-gate reference.
// The explicit `(void)tmp;` bumps the reader count to 1, shutting off
// PJ-4a so the M7 matcher runs as intended.  Same rationale — and
// same textual shape — as the PJ-3f / PJ-4c snapshot fixtures (see
// hoist_or_out_of_for.cpp and hoist_runtime.cpp).
void demo(const qbool& a, const qbool& b) { qbool tmp = a | b; (void)tmp; }

// LP7: second demo that mirrors `examples/or_circuit.cpp`'s VarDecl
// (variable name `c` instead of `tmp`) so the M12 harness can bind
// PRD acceptance #5 to the real example's pattern. Transpiler is
// expected to inject `uncompute_or(c, a, b);` before the closing brace.
// The `(void)c;` reader is required for the same PJ-4a reason as the
// `demo(...)` above.
void demo_or_circuit(const qbool& a, const qbool& b) { qbool c = a | b; (void)c; }

} // namespace m12_transpiled
