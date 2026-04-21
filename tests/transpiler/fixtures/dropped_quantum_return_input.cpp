// PM3-6 positive-case input fixture for the
// `dropped_quantum_return_diagnostic` CTest.
//
// Purpose
// -------
// Pin the PM3-6 "Class 4 — caller drops returned qbool" contract: a
// bare-statement call to a function returning `sturm::qbool` by value
// (no assignment target, no `(void)` cast) MUST raise the
// `DiagnosticsEngine::Warning` configured by
// `register_dropped_quantum_return_matcher`. The fixture exists to
// exercise the happy path of the diagnostic path; the companion
// `dropped_quantum_return_bound.cpp` and `dropped_quantum_return_void_cast.cpp`
// fixtures pin the negative cases.
//
// Shape
// -----
// A minimal `sturm::qbool` stub with a non-trivial destructor (matching
// the production layout — the real qbool owns a qubit slot through
// qint_t<1>, so its temporary is wrapped in an ExprWithCleanups) plus
// a free function `make_qbool()` that returns a fresh qbool by value.
// The demo() body calls `make_qbool()` as a bare statement so the
// CallExpr's parent chain matches the matcher's AST pattern (either a
// direct CompoundStmt, or an ExprWithCleanups sitting under a
// CompoundStmt).
//
// Expected diagnostic substring on stderr:
//     STURM: discarded quantum return from
//
// The callee qualified name interpolated into the Warning message is
// `make_qbool` (free function, no enclosing namespace), so the locked-
// down text reads:
//
//     STURM: discarded quantum return from 'make_qbool' - the qubit
//     will be released immediately; bind it to a named variable if
//     you intend to use it.
//
// Acceptance (per PM3-6 issue):
//   - Warning on stderr, NOT Error — compilation must continue so the
//     standalone driver's zero-exit is preserved.
//   - Clang exit code is zero (Warning, not Error).
namespace sturm {

class qbool {
public:
    qbool() {}
    // Non-trivial destructor forces Clang to wrap by-value temporaries
    // in an ExprWithCleanups — the exact AST shape the matcher's
    // `exprWithCleanups(hasParent(compoundStmt()))` alternative covers.
    ~qbool() {}
    qbool(const qbool&) {}
};

} // namespace sturm
using sturm::qbool;

// Free function returning qbool by value. Because of qbool's
// non-trivial dtor, the CallExpr at each call site is the child of an
// ExprWithCleanups wrapper when used as a statement — satisfying the
// matcher's EWC alternative.
qbool make_qbool() { return qbool{}; }

void demo() {
    // Dropped-return statement: the matcher fires here. The CallExpr
    // sits under an ExprWithCleanups whose parent is the CompoundStmt
    // of `demo()`'s body.
    make_qbool();
}
