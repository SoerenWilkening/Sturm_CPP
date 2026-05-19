// matcher_dropped_quantum_return.cpp — PM3-6 Class 4 diagnostic.
//
// Detects CallExpr returning `sturm::qbool` or `sturm::qint_t` by value
// whose result is discarded at statement scope. Dropping a returned
// quantum value leaks the qubit indices it owns — the caller has no
// handle to uncompute / measure the returned object, so the released
// qubits remain tangled in whatever state the callee left them in.
//
// Detection shape (AST pattern)
// -----------------------------
//     callExpr(
//       hasType(hasDeclaration(namedDecl(anyOf(
//           hasName("sturm::qbool"),
//           hasName("sturm::qint_t"))))),
//       hasParent(stmt(anyOf(
//           compoundStmt(),
//           exprWithCleanups(hasParent(compoundStmt()))))))
//
// The `exprWithCleanups(hasParent(compoundStmt()))` alternative captures
// the common case where the AST inserts an `ExprWithCleanups` node above
// a temporary-producing call before the CompoundStmt parent — Clang
// wraps any call whose return type has a non-trivial destructor (which
// `sturm::qbool` does, via the `qint_t<1>` base owning a qubit slot).
//
// Shapes that correctly escape the matcher
// -----------------------------------------
//   - `qbool x = make_qbool();` — the parent of the CallExpr is a
//     VarDecl / DeclStmt, not a CompoundStmt. The matcher does not fire.
//   - `(void)make_qbool();`    — the parent of the CallExpr is a
//     `CStyleCastExpr` (the `(void)` cast), which breaks the
//     `hasParent(compoundStmt())` chain. The user has signalled an
//     explicit discard and the matcher respects that.
//
// Severity: `DiagnosticIDs::Warning`, NOT `Error` — the transpiler must
// keep compiling so the user sees every diagnostic for one TU, not just
// the first one to fire.
//
// Message format (locked-down by the PM3-6 issue description):
//     STURM: discarded quantum return from '<callee-qualified-name>' —
//     the qubit will be released immediately; bind it to a named
//     variable if you intend to use it.

#include "sturm/transpile/matcher.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/SourceLocation.h"

#include <memory>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_dropped_quantum_return_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// Callback that reports the Class 4 diagnostic once per matched
// CallExpr. The callback carries a `DiagnosticsEngine*` handed in at
// registration time so the report path does not have to go through
// `r.Context->getDiagnostics()` on every fire — the shared
// DiagnosticsEngine threaded from the parent CompilerInstance is the
// authoritative sink and matches the PM3-1 `TextDiagnosticPrinter`
// wiring on the standalone driver.
class DroppedQuantumReturnCallback : public MatchFinder::MatchCallback {
public:
    explicit DroppedQuantumReturnCallback(clang::DiagnosticsEngine* diag)
        : diag_(diag) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CallExpr>("call");
        if (!call || !diag_) return;

        // Resolve the callee's qualified name. We prefer the direct
        // FunctionDecl on the CallExpr so template specializations and
        // overload resolution produce a stable, user-visible name.
        // Fallback to "<unknown-callee>" defensively if the AST shape
        // does not expose a FunctionDecl (e.g. an indirect call through
        // a function pointer — not a production shape for this
        // diagnostic but kept so the matcher never crashes).
        std::string callee_name = "<unknown-callee>";
        if (const auto* fd = call->getDirectCallee()) {
            callee_name = fd->getQualifiedNameAsString();
        }

        // Lazy-cached diag ID. The message uses `%0` for the callee
        // qualified name; the Warning level ensures compilation
        // continues and the user can see every leak in one pass.
        const unsigned id = diag_->getCustomDiagID(
            clang::DiagnosticsEngine::Warning,
            "STURM: discarded quantum return from '%0' - the qubit "
            "will be released immediately; bind it to a named variable "
            "if you intend to use it.");

        diag_->Report(call->getBeginLoc(), id) << callee_name;
    }

private:
    clang::DiagnosticsEngine* diag_;
};

// Keep-alive pool for the callback instance. We allocate once per
// `register_dropped_quantum_return_matcher` call and stash the pointer
// here — MatchFinder does not own the callback, so it must outlive the
// finder's run. This mirrors the static-pool pattern used by every
// other matcher in this directory (matcher_qbool_assign.cpp,
// matcher_dead_ancilla.cpp, ...).
std::vector<std::unique_ptr<DroppedQuantumReturnCallback>>&
dropped_quantum_return_callback_pool() {
    static std::vector<std::unique_ptr<DroppedQuantumReturnCallback>> pool;
    return pool;
}

} // namespace sturm_matcher_dropped_quantum_return_anon_ns
using namespace sturm_matcher_dropped_quantum_return_anon_ns;

void register_dropped_quantum_return_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::DiagnosticsEngine& diag) {
    // AST pattern: a CallExpr whose return type resolves (through
    // `hasDeclaration`) to a NamedDecl whose name is `sturm::qbool` or
    // `sturm::qint_t`, AND whose parent-chain leads to a CompoundStmt
    // without passing through any "use-site" wrapper (VarDecl init,
    // assignment target, `(void)` cast, return stmt, function
    // argument, etc.).
    //
    // The parent-chain alternatives we accept:
    //
    //   - `callExpr → CompoundStmt`
    //     The trivially-destructible case: CallExpr is a direct
    //     statement child of the enclosing block. Rare in practice
    //     because `sturm::qbool` / `sturm::qint_t` both own a qubit
    //     slot and therefore have non-trivial destructors.
    //
    //   - `callExpr → ExprWithCleanups → CompoundStmt`
    //     The direct-EWC case: Clang inserts an ExprWithCleanups
    //     above a by-value temporary returned from a call whose return
    //     type has a non-trivial destructor, but only wraps the
    //     CallExpr directly when no CXXBindTemporaryExpr is needed.
    //
    //   - `callExpr → CXXBindTemporaryExpr → ExprWithCleanups →
    //      CompoundStmt`
    //     The realistic case: `sturm::qbool` has a non-trivial
    //     destructor, so Clang emits a `CXXBindTemporaryExpr` above
    //     the CallExpr and wraps the whole tree in an
    //     `ExprWithCleanups` before parenting under the CompoundStmt.
    //     Every production drop-site has this shape, per the AST dump
    //     in the PM3-6 fixture design notes.
    //
    // Shapes that correctly do NOT match this pattern:
    //
    //   - `qbool x = make_qbool();`  — CallExpr's parent-chain leads
    //     through a VarDecl init, not a CompoundStmt.
    //   - `(void)make_qbool();`      — the `(void)` cast inserts a
    //     `CStyleCastExpr` between the CallExpr (or the EWC wrapper)
    //     and the CompoundStmt. `CStyleCastExpr` matches none of our
    //     stmt alternatives, so the chain is broken and the matcher
    //     stays silent — the user-signalled explicit discard.
    auto pattern = callExpr(
        hasType(qualType(hasDeclaration(namedDecl(anyOf(
            hasName("sturm::qbool"),
            hasName("sturm::qint_t")))))),
        hasParent(stmt(anyOf(
            compoundStmt(),
            exprWithCleanups(hasParent(compoundStmt())),
            cxxBindTemporaryExpr(hasParent(
                exprWithCleanups(hasParent(compoundStmt())))))))
    ).bind("call");

    auto& pool = dropped_quantum_return_callback_pool();
    pool.push_back(std::make_unique<DroppedQuantumReturnCallback>(&diag));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
