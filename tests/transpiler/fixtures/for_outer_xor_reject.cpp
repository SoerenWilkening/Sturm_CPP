// Phase H / PH-3 input for the sturm-transpile snapshot test.
//
// Exercises the outer-variable-mutation guard: `a ^= b;` where the
// qbool `a` is declared in the enclosing function body (outside the
// for-body) but mutated inside the for-body. Automatic uncomputation
// of this pattern would require reverse-loop synthesis, which
// contradicts P9 ("routines are invertible by explicit adjoint").
// The transpiler MUST refuse to inject an inverse here and instead
// emit a stderr diagnostic pointing the user at the offending line;
// the generated file is otherwise byte-identical to the input (modulo
// the AUTO-GENERATED header the emitter prepends).
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        a ^= b;
    }
}
