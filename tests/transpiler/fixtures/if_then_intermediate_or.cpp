// Phase H / PH-4 input for the sturm-transpile snapshot test.
//
// Exercises the braced variant of the if-then intermediate OR shape:
// `if (cond) { qbool tmp = a | b; }` — the then-arm is already a
// CompoundStmt, so PH-2's auto-brace-wrap matcher MUST NOT fire. The
// only Phase H moving part still active is PH-1's scope finder, which
// must return the pre-existing CompoundStmt (`kind=CompoundStmt`) so
// the M8 uncompute-synthesis pass plants a companion
// `uncompute_or(tmp, a, b);` call immediately before the then-arm's
// closing `}` — guarded by `cond`, matching the roadmap requirement
// that scope-local intermediates uncompute iff the branch is taken
// (P9).
//
// Acceptance shape for this fixture:
//   - No `{` / `}` raw insertions from PH-2 (the body is already
//     braced).
//   - The uncompute pass plants `uncompute_or(tmp, a, b);` right before
//     the body's existing close `}`, so the uncompute runs iff the
//     branch is taken.
//
// The intermediate `tmp` is declared INSIDE the then-body, so PH-3's
// outer-variable-mutation guard does not flag it.
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

void demo(qbool a, qbool b, bool cond) {
    if (cond) {
        qbool tmp = a | b;
    }
}
