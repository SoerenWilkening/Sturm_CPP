// dead_ancilla_runtime.cpp — Phase J / PJ-4e transpiler INPUT fixture.
//
// Seventh gate-equivalence pair.  Whereas the earlier PG-7 / PH-6a /
// PH-6b / PI-7 / PJ-1i fixtures pin down the OR matcher's per-iteration,
// per-branch, or nested-WHEN scope lowering, the user-routine rewrite,
// and the zero-ancilla fusion, this fixture pins down the Phase J PJ-4
// DEAD-ANCILLA ELIMINATION: a `qbool dead = a | b;` VarDecl whose init
// is a bitwise op-call, but with ZERO readers of `dead` anywhere in the
// enclosing scope.  The PJ-4a peephole matcher
// (`register_dead_ancilla_matcher`) fires on the zero-reader condition
// (`detail::count_readers_in_scope(dead, ...) == 0`), emits one empty-
// text `QReplacement` over the decl's full stmt range (including the
// trailing `;`), and pushes the same range into
// `QUnit::eliminated_stmt_ranges` so the downstream MVP OR matcher
// early-returns on the covered range via `apply_eliminated_stmt_guards`.
// Net effect: the decl vanishes from the rewritten source and no
// `uncompute_or(dead, a, b);` is planted before the demo's closing `}`.
// The hand-written companion `dead_ancilla_reference.cpp` is structurally
// identical — minus the dead decl — so the captured gate streams are
// byte-for-byte identical.
//
// Demo shape
// ----------
// void demo(const qbool& a, const qbool& b, qbool& target) {
//     target.flip();             // sentinel — one X(target) gate
//     qbool dead = a | b;        // ELIMINATED by PJ-4a — zero readers
//     // transpiler rewrites the line above to empty text; no
//     // `uncompute_or(dead, a, b);` is planted before the closing `}`.
// }
//
// Gate-stream witness
// -------------------
// `target.flip()` on a superposed qbool (super_mask=1) emits exactly
// one X(target) record via emit_X_lifted → execute_gate(ctx,
// STURM_GATE_X, ...).  The dead decl contributes ZERO gates because
// PJ-4a strips it before the MVP OR matcher ever sees it — no forward
// OR (CX+CX+CCX), no uncompute OR (CCX+CX+CX).  Total captured stream:
// ONE X gate, three qubits allocated but only `target`'s index touched
// (a and b stay untouched because the OR was eliminated before it could
// fire).  The reference fixture emits the same one X(target) record
// from its own `target.flip()` call — byte-identical.
//
// Why the sentinel flip() is load-bearing
// ---------------------------------------
// The M12 test harness rejects empty-stream captures as "fixture is not
// exercising the quantum ..." (see test_gate_equivalence.cpp's
// `if (ref_stream.empty()) return 1;` guards on every earlier pair).
// A pure PJ-4a-elimination fixture would produce zero gates on BOTH
// sides — a trivially-equal comparison that does not actually verify
// anything.  The `target.flip()` sentinel threads the needle: it
// guarantees the stream is non-empty (so the harness captures a real
// signal), while leaving the dead decl as the ONLY structural
// difference between the runtime input and the reference.  Any
// regression in PJ-4a — a failure to eliminate, a stray `uncompute_or`,
// a byte mismatch in the empty-text replacement — would surface as a
// size mismatch (4 vs 1 gates) or a content mismatch at index 1+ in
// the comparison.
//
// PJ-4a reject branches that DO NOT belong in this fixture
// --------------------------------------------------------
// The snapshot fixtures under `fixtures/dead_ancilla_*` already pin
// down every rejected shape (reader-present, chain-reader, inside-WHEN
// body, plain-copy init); those fall through to the Phase A / MVP OR
// matchers on the runtime path.  This fixture deliberately exercises
// ONLY the happy-path elimination so the gate-equivalence capture is a
// byte-for-byte comparison of the SINGLE shape the PJ-4a peephole
// commits to.
//
// ODR note
// --------
// The harness links this TU together with dead_ancilla_reference.cpp.
// Both define `demo(const qbool&, const qbool&, qbool&)` inside their
// own namespace to avoid an ODR collision.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and
// the `qbool` type would never appear in the AST, which would
// silently disable the PJ-4a matcher.  To make this TU parseable by
// the transpiler AND compilable at runtime, the include is gated on
// `__has_include`: during transpile the preprocessor takes the stub
// branch (which provides a minimal `sturm::qbool` with `operator|`
// and `flip()` the matcher needs to see), and during the downstream
// compile the real headers are available so the `target.flip()` call
// resolves via argument-dependent lookup (the arg is `sturm::qbool&`).
//
// The stub shape mirrors the one used by the PJ-4c snapshot fixtures
// in `fixtures/dead_ancilla_*.cpp`: same class layout, same free
// operator, same namespace.  The transpiler cares about the textual
// pattern of `qbool dead = a | b;` with zero readers — not the
// semantic behaviour.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` pulls in the uncompute free-function
// API (which declares `sturm::uncompute_or`).  The real quantum
// `operator|` + `flip()` live in qbool_ops.hpp / qbool.hpp, so
// include them explicitly here — without them the `a | b` and
// `target.flip()` below would hit the classical short-circuit paths
// (which emit no gates) or fail to compile entirely.
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
    qbool& flip() { return *this; }
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
#endif

namespace m12_dead_ancilla_transpiled {
using sturm::qbool;

// The transpiler's PJ-4a matcher fires on
//     qbool dead = a | b;
// when `dead` has exactly zero readers in scope.  On elimination the
// decl is REPLACED by empty text and no `uncompute_or(dead, a, b);` is
// planted at scope close.  The sentinel `target.flip()` above the dead
// decl keeps the captured gate stream non-empty (one X(target) record),
// so the harness's empty-stream guard does not false-positive this pair
// as a trivially-equal comparison.  `a` / `b` are `const qbool&` so the
// caller's qubit indices are preserved across the demo boundary; `target`
// is a non-const `qbool&` because `flip()` mutates the qbool's state
// (it emits an X gate via emit_X_lifted).
void demo(const qbool& a, const qbool& b, qbool& target) {
    target.flip();
    qbool dead = a | b;
}

} // namespace m12_dead_ancilla_transpiled
