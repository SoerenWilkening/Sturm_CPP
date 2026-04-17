// Phase J / PJ-4c input for the sturm-transpile snapshot test.
//
// Happy-path dead-ancilla elimination: a `qbool t = a | b;` VarDecl
// with NO reader anywhere in the enclosing scope. The PJ-4a matcher
// (`register_dead_ancilla_matcher`) fires on the zero-reader condition
// (`detail::count_readers_in_scope(t, ...) == 0`), emits one empty
// `QReplacement` over the decl's full stmt range (including its
// trailing `;`), and pushes the same range into
// `QUnit::eliminated_stmt_ranges` so the downstream MVP OR matcher
// early-returns on the covered range via `apply_eliminated_stmt_guards`.
//
// Expected transform (see .expected.cpp for the byte-exact golden):
//
//     qbool t = a | b;    // eliminated — no reader, no uncompute
//
// Net effect: the decl vanishes, no `uncompute_or(t, a, b);` is emitted
// at scope close. Zero QOperations land in `QUnit::scopes` for this
// decl — PJ-4a's elimination is silent (no forward op, no inverse).
//
// The stub inlined below is hermetic — the sturm-transpile binary runs
// with a FixedCompilationDatabase that carries no include paths, so
// every symbol the matcher probes must resolve against the fixture's
// own source. Only `operator|` need resolve for the bitwise-init guard
// to fire.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b) {
    qbool t = a | b;
}
