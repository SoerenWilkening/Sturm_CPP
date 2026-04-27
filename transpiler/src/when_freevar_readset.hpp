// when_freevar_readset.hpp — E7.M1 WHEN free-variable read-set extractor.
//
// Given the `expr` of a `WHEN(expr) { body }` invocation, compute the set
// of `VarDecl`s read by `expr`, transitively through any function call
// inside `expr` whose body the transpiler can see. If a callee's body is
// not available (e.g. `extern` declaration, library symbol), the result
// is flagged "conservative" — the read-set is potentially incomplete and
// downstream consumers (the E7.M2 write-set checker) must decide how to
// handle that.
//
// Design notes
// ------------
// - The extractor walks the expression with a `RecursiveASTVisitor`,
//   collecting every `DeclRefExpr` whose referenced declaration is a
//   `VarDecl`. Function references, type references, etc. are ignored.
// - For every `CallExpr`, the extractor looks at the direct callee's
//   `FunctionDecl`. If the callee has a definition with a body in this
//   TU, the body is visited recursively (transitive read-set). A
//   visited-functions guard prevents infinite recursion through self-
//   or mutually-recursive callees.
// - Calls without a direct callee (function-pointer dispatch, virtual
//   resolution we cannot resolve) and calls to declarations without a
//   visible definition both flip `conservative = true`.
// - The result is a `SmallPtrSet<const VarDecl*, ...>` so callers can
//   union / intersect against the body's write-set via simple set
//   operations on `VarDecl*` identity (this matches the E7.M2 plan).
//
// This module is intentionally side-effect free: no diagnostics, no
// QUnit mutation. M1 is pure analysis; M2 ties the result into the
// matcher pipeline and emits the user-facing error.

#ifndef STURM_TRANSPILE_WHEN_FREEVAR_READSET_HPP
#define STURM_TRANSPILE_WHEN_FREEVAR_READSET_HPP

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace sturm::transpile {

struct WhenFreeVarReadSet {
    // Every VarDecl read by the expression (or by any transitively
    // visited callee body). Stored as raw `VarDecl*` so callers can
    // intersect against an analogously-typed write-set.
    llvm::SmallPtrSet<const clang::VarDecl*, 8> vars;

    // True iff at least one CallExpr along the way had no analyzable
    // body (no direct callee, or the callee declaration has no
    // definition in this TU). When true, the `vars` set is a *lower
    // bound* on the actual read-set — there may be additional reads
    // hidden behind the unanalyzed call.
    bool conservative = false;
};

// Compute the read-set of `expr`. A null `expr` returns an empty,
// non-conservative result.
WhenFreeVarReadSet compute_when_freevar_readset(const clang::Expr* expr);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_WHEN_FREEVAR_READSET_HPP
