// Phase H / PH-4 input for the sturm-transpile snapshot test.
//
// Exercises the braced variant of the for-body intermediate OR shape:
// `for (...) { qbool tmp = a | b; }` — the body is already a
// CompoundStmt, so PH-2's auto-brace-wrap matcher MUST NOT fire. The
// only Phase H moving part that's still active is PH-1's scope finder,
// which must return the pre-existing CompoundStmt (`kind=CompoundStmt`)
// so the M8 uncompute-synthesis pass plants a companion
// `uncompute_or(tmp, a, b);` call immediately before the body's closing
// `}` — per-iteration, matching the roadmap requirement that scope-local
// intermediates uncompute on every loop iteration (P9).
//
// Acceptance shape for this fixture:
//   - No `{` / `}` raw insertions from PH-2 (the body is already
//     braced).
//   - The uncompute pass plants `uncompute_or(tmp, a, b);` right before
//     the body's existing close `}`, per-iteration.
//
// The intermediate `tmp` is declared INSIDE the for-body, so PH-3's
// outer-variable-mutation guard does not flag it — this is the normal,
// supported Phase H shape.
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
    for (int i = 0; i < 3; ++i) {
        qbool tmp = a | b;
    }
}
