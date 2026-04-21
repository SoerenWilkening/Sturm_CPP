// PM3-6 negative-case input fixture for the
// `dropped_quantum_return_diagnostic_void_cast` CTest.
//
// Purpose
// -------
// Pin the `(void)call()` explicit-discard escape hatch. When the user
// writes `(void)make_qbool();`, the `(void)` cast inserts a
// `CStyleCastExpr` between the CallExpr and the enclosing
// CompoundStmt. The matcher's `hasParent(stmt(anyOf(compoundStmt(),
// exprWithCleanups(hasParent(compoundStmt())))))` pattern does NOT
// match a `CStyleCastExpr`, so the AST binding fails at the parent
// step and no Warning is emitted. This is the documented user-
// signalled escape: `(void)` communicates "I acknowledge the qubit
// will be released; stop warning me."
//
// Shape
// -----
// Same minimal `sturm::qbool` stub and `make_qbool()` free function
// as the positive fixture; the only delta is the `(void)` cast in
// front of the call.
//
// Acceptance (per PM3-6 issue):
//   - NO diagnostic text on stderr.
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
    // Explicit discard: the `(void)` cast wraps the CallExpr in a
    // CStyleCastExpr, breaking the `hasParent(stmt(anyOf(compoundStmt(),
    // exprWithCleanups(hasParent(compoundStmt())))))` chain. The
    // matcher does not fire.
    (void)make_qbool();
}
