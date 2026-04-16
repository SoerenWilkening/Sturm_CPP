// Phase H / PH-4 input for the sturm-transpile snapshot test.
//
// Exercises the braced variant of the if-else intermediate OR shape:
// BOTH arms are already CompoundStmts and EACH declares a distinct
// per-branch intermediate — `qbool tmp_then = a | b;` in the then-arm
// and `qbool tmp_else = a | b;` in the else-arm. PH-2's auto-brace-wrap
// matcher MUST NOT fire on either arm. The only Phase H moving part
// still active is PH-1's scope finder, which must return the pre-
// existing CompoundStmt for each arm (`kind=CompoundStmt`) so the M8
// uncompute-synthesis pass plants a companion `uncompute_or(...)` call
// immediately before EACH arm's closing `}` — each guarded by whichever
// branch is taken, matching the roadmap requirement that scope-local
// intermediates uncompute iff the owning branch is entered (P9).
//
// Acceptance shape for this fixture:
//   - No `{` / `}` raw insertions from PH-2 (both arms are already
//     braced).
//   - The uncompute pass plants `uncompute_or(tmp_then, a, b);` right
//     before the then-arm's existing close `}`.
//   - The uncompute pass plants `uncompute_or(tmp_else, a, b);` right
//     before the else-arm's existing close `}`.
//   - The two scopes are independent — each intermediate uncomputes
//     only in its own arm, demonstrating the per-op, per-scope
//     granularity of the uncompute synthesis pass.
//
// Both intermediates are declared INSIDE their respective arms, so
// PH-3's outer-variable-mutation guard does not flag them.
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
        qbool tmp_then = a | b;
    } else {
        qbool tmp_else = a | b;
    }
}
