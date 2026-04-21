// PM3-5 positive-case input fixture for the
// `plugin_diagnostic_quantum_to_classical_cond_fires` CTest.
//
// Purpose
// -------
// Pin the PM3-5 "Class 2 — quantum -> classical in branch condition"
// contract: an explicit cast from a `sturm::qbool` / `sturm::qint_t`
// to a classical `bool` / integral whose ancestor is the condition
// slot of an `IfStmt` / `WhileStmt` / `DoStmt` / `ConditionalOperator`
// MUST raise the `DiagnosticsEngine::Error` configured by
// `DiagContext::report_quantum_to_classical_cond`. Error severity —
// the transpiler cannot honour a source program that collapses a
// quantum value into a classical bit at branch time; compilation
// must abort non-zero.
//
// Why explicit casts only? A bare `if (q)` where `q` is a `qbool`
// is already a C++ error because `qbool::operator bool()` is
// `explicit`, so the compiler refuses the implicit conversion. The
// PM3-5 matcher's job is to catch the user who reached for
// `static_cast<bool>(q)` (or the equivalent C-style / functional
// cast) to silence the error — and, in doing so, collapse the qubit
// state into a classical bit prematurely.
//
// Shape
// -----
// A minimal hermetic `sturm::qbool` stub with an `explicit operator
// bool()` so the only way to reach `bool` from `qbool` is via an
// explicit cast. The `demo(q)` function uses `if (static_cast<bool>(q))`
// — the PM3-5 matcher MUST fire on the `static_cast<bool>` whose
// ancestor (via ASTContext::getParents) is the IfStmt's condition.
//
// Expected diagnostic substring on stderr:
//     branch condition derives from quantum value via explicit cast
// AND exit code non-zero.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    explicit operator bool() const { return false; }
};

} // namespace sturm

using sturm::qbool;

void demo(qbool q) {
    // The PM3-5 diagnostic MUST cite the line below — the
    // `static_cast<bool>(q)` expression sits in the condition slot of
    // the IfStmt, and the source of the cast is a qbool DeclRefExpr.
    if (static_cast<bool>(q)) {
        // body intentionally empty; the matcher's job is to detect the
        // cast in the condition slot, not inspect the body.
    }
}
