// when_freevar_check.hpp — E7.M2 WHEN free-variable write-set checker.
//
// Companion to E7.M1 (`when_freevar_readset.{hpp,cpp}`). M1 computes
// the set of `VarDecl`s read by the WHEN control expression `expr`;
// M2 — this module — walks the WHEN body looking for assignment-shape
// writes whose target sits inside that read-set, and emits a hard-
// error `clang::DiagnosticsEngine` diagnostic at the offending write's
// source location for each match. Writes to vars *declared inside* the
// body are body-local and ignored.
//
// Per principle P4 ("quantum control is lexical scope"), mutating any
// free variable of a WHEN control expression inside the WHEN body
// produces an incorrect adjoint. Per packaging-export PRD §3.7 / D6,
// the violation is a hard compile-time diagnostic — auto-snapshotting
// to silently make user code correct was rejected because it would
// hide gate-count costs from algorithm authors.
//
// Detection shape (algorithm)
// ---------------------------
// Three write shapes the issue spec (§E7.M2) explicitly lists:
//   1. `BinaryOperator` with an assignment opcode (`=`, the only one
//      since `+=` etc. parse as `CompoundAssignOperator`).
//   2. `CompoundAssignOperator` (`+=`, `-=`, `*=`, `/=`, `%=`,
//      `<<=`, `>>=`, `&=`, `|=`, `^=`).
//   3. `CXXOperatorCallExpr` for any assignment-shape operator
//      (`operator=`, `operator^=`, etc.) — the user-defined-overload
//      shape that `qbool::operator^=` and friends take.
//
// We additionally cover `UnaryOperator` increment / decrement (`++`,
// `--`, pre and post). The issue's bullet-list is non-exhaustive but
// the prose is "mutation"; inc / dec is a mutation by any reasonable
// definition and the sibling matcher in
// `matcher_when_operand_mutation.cpp` already covers it. Treating it
// as a write here keeps the two surfaces user-consistent.
//
// Transitive coverage of writes inside callees works the same way
// E7.M1 walks reads: when the body calls a function whose body is
// available in this TU, we recurse into it and treat its writes as
// part of the WHEN body's effective write-set. A visited-functions
// guard breaks cycles.
//
// Body-local exclusion
// --------------------
// The walker maintains a stack-discipline `SmallPtrSet` of `VarDecl`s
// declared inside the walked subtree (recorded at every `VarDecl`
// visit). A write whose target is in that set is body-local and
// SKIPPED. Crucially, the body-local set is *reset* each time we
// recurse into a callee body (a callee's locals are not the original
// body's locals — what we care about is whether the write target is
// declared inside the lexical span of the WHEN body, not of an
// arbitrary function the WHEN happens to call).
//
// Diagnostic surface
// ------------------
// Each match calls `engine.Report(loc, id) << var_name` where:
//   - `loc` is the offending write's `getBeginLoc()` funnelled
//     through `SourceManager::getFileLoc(...)` so the diagnostic
//     cites user source rather than a memory buffer (matches the
//     PM3-2 / PM3-3 plumbing; required for the plugin's nested
//     CompilerInvocation).
//   - `id` is a custom diag-ID resolved once per call to
//     `check_when_freevar_writes`. The format string mentions both
//     the variable name and the WHEN scope:
//       "STURM: WHEN free variable '%0' is mutated inside the WHEN
//        body — mutation of a control free-variable corrupts the
//        adjoint (P4)."
//
// This module is intentionally side-effect-free apart from the
// diagnostic stream: it does not touch a `QUnit`, does not register
// matchers, and does not depend on `DiagContext` (so unit tests can
// drive it against a throwaway `DiagnosticsEngine` directly). E7.M3
// will wire this into the matcher pipeline; E7.M2 is the pure check.

#ifndef STURM_TRANSPILE_WHEN_FREEVAR_CHECK_HPP
#define STURM_TRANSPILE_WHEN_FREEVAR_CHECK_HPP

#include "when_freevar_readset.hpp"

namespace clang {
class DiagnosticsEngine;
class SourceLocation;
class SourceManager;
class Stmt;
} // namespace clang

namespace sturm::transpile {

/// Result bundle returned by `check_when_freevar_writes`. Pure
/// reporting summary — the diagnostics themselves land on the
/// `DiagnosticsEngine` passed in by the caller.
struct WhenFreeVarWriteCheck {
    /// Number of distinct write sites in the body (including
    /// transitive sites in callee bodies) that targeted a VarDecl in
    /// the read-set. One hard-error diagnostic is emitted per write.
    unsigned writes_reported = 0;
};

/// Walk `body` looking for writes (assignment-shape `BinaryOperator`,
/// `CompoundAssignOperator`, `CXXOperatorCallExpr` with an assignment
/// opcode, plus `UnaryOperator` inc / dec) whose target VarDecl lives
/// in `readset.vars`. For each match, emit a hard-error diagnostic
/// through `engine` at the write's source location, naming the
/// variable. Writes to variables declared inside `body` itself are
/// body-local and silently skipped.
///
/// `when_scope_loc` is currently unused by the diagnostic format
/// (the offending-write location is sufficient context) but is kept
/// in the signature so a future reformatting that wants to cite the
/// WHEN entry can wire it up without an API break.
///
/// A null `body` is a no-op. If `readset.vars` is empty the function
/// also returns immediately — the body cannot intersect an empty set
/// and there is no point walking it.
WhenFreeVarWriteCheck check_when_freevar_writes(
    const clang::Stmt*           body,
    const WhenFreeVarReadSet&    readset,
    clang::DiagnosticsEngine&    engine,
    const clang::SourceManager&  sm,
    clang::SourceLocation        when_scope_loc);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_WHEN_FREEVAR_CHECK_HPP
