// Phase H / PH-2 input for the sturm-transpile snapshot test.
//
// Exercises the auto-brace-wrap matcher on a BRACELESS if-else body:
// the then-arm is already braced (`{ (void)a; }` — a classical no-op
// that keeps the branch syntactically well-formed without introducing a
// quantum op for PH-2 to wrap), and the else-arm is a single-statement
// braceless body `qbool tmp = a | b;`. The PH-2 matcher must wrap ONLY
// the else-arm — the braced then-arm is a CompoundStmt and is left
// alone.
//
// Acceptance shape for this fixture:
//   - `{` inserted at the else body stmt's begin loc.
//   - `}` inserted at the loc immediately past the else body's
//     terminating `;`.
//   - The uncompute pass plants `uncompute_or(tmp, a, b);` right before
//     that close `}`, so the uncompute runs iff the else branch is
//     taken.
//   - The then-arm stays byte-identical (no raw_insertions for it).
//
// The intermediate `tmp` is declared INSIDE the synthesised else-body,
// so PH-3's outer-variable-mutation guard does not flag it.
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
    if (cond) { (void)a; } else qbool tmp = a | b;
}
