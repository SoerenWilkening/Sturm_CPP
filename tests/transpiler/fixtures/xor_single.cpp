// Phase A / PA-2 input for the sturm-transpile snapshot test.
//
// Exercises the two-operand self-inverse form: `qbool tmp = a ^ b;` must
// be paired with `tmp ^= a;` then `tmp ^= b;` injected before the scope's
// close brace. XOR is self-inverse because (a^b)^a^b == 0, so applying
// the same operands via `^=` to the result qubit uncomputes it without
// any additional library helper.
//
// The sturm-transpile binary runs with a FixedCompilationDatabase that
// carries no include paths, so we inline a minimal qbool stub whose
// `operator^` overload is sufficient for the matcher to resolve the
// binary XOR call.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
};
inline qbool operator^(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b) { qbool tmp = a ^ b; }
