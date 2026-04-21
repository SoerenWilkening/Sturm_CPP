// PM3-6 negative-case input fixture for the
// `dropped_quantum_return_diagnostic_bound` CTest.
//
// Purpose
// -------
// Pin the "bound to a named variable" escape for the PM3-6 matcher.
// When the user writes `qbool x = make_qbool();`, the CallExpr's
// parent is a VarDecl (transitively through an InitListExpr /
// DeclStmt), NOT a CompoundStmt — so the matcher's
// `hasParent(stmt(anyOf(compoundStmt(), ...)))` pattern does not
// bind and no Warning is emitted. The user has assigned the return
// value to a handle (`x`) they can later uncompute / measure, so the
// qubit is no longer dropped — the matcher correctly stays silent.
//
// Shape
// -----
// Same minimal `sturm::qbool` stub and `make_qbool()` free function
// as the positive fixture (`dropped_quantum_return_input.cpp`); the
// only delta is the demo() body uses a VarDecl to capture the return
// value.
//
// Acceptance (per PM3-6 issue):
//   - NO diagnostic text on stderr (the test harness asserts the
//     "STURM: discarded quantum return from" substring is absent).
//   - Clang exit code is zero.
namespace sturm {

class qbool {
public:
    qbool() {}
    // Non-trivial destructor, mirroring the real qbool layout.
    ~qbool() {}
    qbool(const qbool&) {}
};

} // namespace sturm
using sturm::qbool;

qbool make_qbool() { return qbool{}; }

void demo() {
    // Bound return: the CallExpr's parent is the VarDecl's init
    // expression chain (not a direct CompoundStmt), so the matcher
    // does not fire. The `(void)x;` suppresses any unused-variable
    // warning from the host compiler so stderr stays empty of any
    // matcher-adjacent noise.
    qbool x = make_qbool();
    (void)x;
}
