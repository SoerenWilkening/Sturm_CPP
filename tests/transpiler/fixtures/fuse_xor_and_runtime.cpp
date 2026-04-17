// fuse_xor_and_runtime.cpp — Phase J / PJ-1i transpiler INPUT fixture.
//
// Sixth gate-equivalence pair.  Whereas the earlier PG-7 / PH-6a /
// PH-6b / PI-7 fixtures pin down the OR matcher's per-iteration, per-
// branch, or nested-WHEN scope lowering and the user-routine rewrite,
// this fixture pins down the Phase J PJ-1 zero-ancilla fusion
// peephole: a `qbool __t = a & b;` VarDecl whose init is a bare `&`
// op-call with two DeclRefExpr operands, immediately followed by an
// `x ^= __t;` compound-assign whose RHS is exactly that temporary.
// `__t` has exactly one reader in the enclosing scope (the `^=` RHS
// itself), so the PJ-1d peephole matcher fuses the pair into a single
// `ccnot_inplace(x, a, b);` call — a single CCX acting in-place on
// `x` with NO intermediate ancilla.  The PJ-1c render case in
// transpiler/src/uncompute_pass.cpp plants a matching self-adjoint
// `ccnot_inplace(x, a, b);` before the enclosing scope's close brace.
// The hand-written companion `fuse_xor_and_reference.cpp` spells both
// calls by hand via the primitive_AND sink (NOT via lazy_expr — the
// pre-K lazy path would emit `a & b` as the four-gate AndExpr
// materialization which does NOT match the single-CCX fused shape).
//
// Demo shape
// ----------
// void demo(const qbool& a, const qbool& b, qbool& x) {
//     qbool __t = a & b;
//     x ^= __t;
//     // transpiler replaces both stmts with:
//     //     ccnot_inplace(x, a, b);
//     // and plants a self-adjoint
//     //     ccnot_inplace(x, a, b);
//     // before the demo's closing `}`.
// }
//
// Gate-stream witness
// -------------------
// Forward fused call (`ccnot_inplace(x, a, b);`) ⇒ one CCX(a, b, x)
// record on the three caller-supplied qubit indices.
// Self-adjoint uncompute (`ccnot_inplace(x, a, b);`) ⇒ one more
// CCX(a, b, x) record on the same three indices — CCX is its own
// inverse, so running the helper a second time on the live state
// cancels the forward flip exactly.  Total captured stream: TWO CCX
// gates, three qubits touched (no ancilla allocation — that's the
// defining invariant of the PJ-1 fusion).
//
// PJ-1d reject branches that DO NOT belong in this fixture
// --------------------------------------------------------
// The snapshot fixtures under `fixtures/fuse_xor_and_reject_*`
// already pin down every rejected shape (extra reader, classical
// RHS, nested RHS, intervening stmt); those fall through to the
// Phase E compound-flatten / PA-3/PA-4 matchers on the runtime
// path.  This fixture deliberately exercises ONLY the happy-path
// fuse so the gate-equivalence capture is a byte-for-byte
// comparison of the SINGLE shape the PJ-1d peephole commits to.
//
// ODR note
// --------
// The harness links this TU together with fuse_xor_and_reference.cpp.
// Both define `demo(const qbool&, const qbool&, qbool&)` inside their
// own namespace to avoid an ODR collision.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and
// the `qbool` type would never appear in the AST, which would
// silently disable the PJ-1d matcher.  To make this TU parseable by
// the transpiler AND compilable at runtime, the include is gated on
// `__has_include`: during transpile the preprocessor takes the stub
// branch (which provides a minimal `sturm::qbool` with `operator&`
// and `operator^=` the matcher needs to see), and during the
// downstream compile the real headers are available so the
// transpiler-injected `ccnot_inplace(x, a, b);` call resolves via
// argument-dependent lookup (one of the args is `sturm::qbool&`).
//
// The stub shape mirrors the one used by the PJ-1d unit tests in
// `transpiler/tests/test_matcher_ccnot_fuse.cpp` and the PJ-1g
// snapshot fixture `fixtures/fuse_xor_and.cpp`: same class layout,
// same free operator, same namespace.  The transpiler cares about
// the textual pattern of `qbool __t = a & b; x ^= __t;` — not the
// semantic behaviour.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` pulls in the uncompute free-function
// API (which declares `sturm::ccnot_inplace`).  The real quantum
// `operator&` + `operator^=` live in qbool_ops.hpp / qbool.hpp, so
// include them explicitly here — without them the `a & b` and
// `x ^= __t` below would hit the classical short-circuit paths (which
// emit no gates) or fail to compile entirely.
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
    qbool& operator^=(const qbool&) { return *this; }
};
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
#endif

namespace m12_fused_transpiled {
using sturm::qbool;

// The transpiler's PJ-1d matcher fires on
//     qbool __t = a & b;
//     x ^= __t;
// when `__t` has exactly one reader in scope.  On fusion the pair is
// REPLACED by a single `ccnot_inplace(x, a, b);` call, and the PJ-1c
// render case plants a matching self-adjoint `ccnot_inplace(x, a, b);`
// before this demo's closing `}`.  `a` / `b` are `const qbool&` so the
// caller's qubit indices are preserved across the demo boundary; `x`
// is a non-const `qbool&` since the `^=` target must be a non-const
// lvalue, and the `ccnot_inplace` helper takes its target by non-const
// reference (see uncompute_api.hpp).
void demo(const qbool& a, const qbool& b, qbool& x) {
    qbool __t = a & b;
    x ^= __t;
}

} // namespace m12_fused_transpiled
