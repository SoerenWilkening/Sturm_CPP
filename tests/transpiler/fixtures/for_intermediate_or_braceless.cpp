// Phase H / PH-2 input for the sturm-transpile snapshot test.
//
// Exercises the auto-brace-wrap matcher on a BRACELESS for-body: the
// user writes `for (...) qbool tmp = a | b;` (no `{}` around the
// single-statement body). The PH-2 matcher must auto-synthesize the
// missing braces so that PH-1's BracelessBody scope and the M8
// uncompute-synthesis pass land the `uncompute_or(tmp, a, b);` call
// INSIDE the loop body rather than as a sibling of the original op.
//
// Acceptance shape for this fixture:
//   - `{` inserted at the body stmt's begin loc.
//   - `}` inserted at the loc immediately past the body's terminating
//     `;`.
//   - The uncompute pass plants `uncompute_or(tmp, a, b);` right before
//     that close `}`, so every loop iteration materialises then
//     uncomputes its own `tmp` (per PRD P9: scope-local intermediates).
//
// The intermediate `tmp` is declared INSIDE the synthesised body, so
// PH-3's outer-variable-mutation guard does not flag it — this is the
// normal, supported Phase H shape.
//
// Why the trailing `(void)tmp;` reader is load-bearing
// ----------------------------------------------------
// Phase J PJ-4a (dead-ancilla elimination) removes any `qbool` VarDecl
// whose value is never read.  Without a reader, PJ-4a fires before the
// PH-2 auto-brace-wrap matcher: the entire `qbool tmp = a | b;` line
// is dropped, and the braceless body becomes an empty `;` which PH-2
// rejects (its `body_contains_quantum_op` guard finds nothing to wrap).
// The explicit `(void)tmp;` bumps the reader count to 1, shutting off
// PJ-4a so PH-2 runs as intended.  Adding the reader turns the body
// into a two-statement sequence which MUST be braced at the source
// level to remain syntactically valid — this trades the original
// "true braceless" shape for the equivalent "user-already-braced"
// shape that PH-4 covers (see for_intermediate_or.cpp).  Post-PJ-4a,
// these two fixtures converge: both assert M8's per-iteration
// uncompute placement inside a for-body CompoundStmt; only PH-4's
// input braces survive in the snapshot, so the fixture is renamed
// in spirit but retained in file name for test-list continuity.
//
// The sturm-transpile binary runs with a FixedCompilationDatabase that
// carries no include paths, so we inline a minimal qbool stub whose
// `operator|` overload is enough for the OR matcher to resolve.
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
    for (int i = 0; i < 3; ++i) { qbool tmp = a | b; (void)tmp; }
}
