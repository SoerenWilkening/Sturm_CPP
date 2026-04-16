// Phase H / PH-2 input for the sturm-transpile snapshot test.
//
// Exercises the auto-brace-wrap matcher on a BRACELESS while-body:
// `while (cond) qbool tmp = a | b;` with no `{}` around the single
// statement. The PH-2 matcher must auto-synthesize the missing braces
// so the M8 uncompute-synthesis pass plants `uncompute_or(tmp, a, b);`
// INSIDE the while body (once per iteration) rather than as a sibling
// of the original op at function scope.
//
// Acceptance shape for this fixture:
//   - `{` inserted at the body stmt's begin loc.
//   - `}` inserted at the loc immediately past the body's terminating
//     `;`.
//   - The uncompute pass plants `uncompute_or(tmp, a, b);` right before
//     that close `}`, per-iteration.
//
// The intermediate `tmp` is declared INSIDE the synthesised body, so
// PH-3's outer-variable-mutation guard does not flag it.
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
    int i = 0;
    while (i < 3) qbool tmp = a | b;
}
