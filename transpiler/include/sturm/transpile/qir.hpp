// qir.hpp — Quantum IR for the STURM transpiler (M6).
//
// Purpose
// -------
// The Quantum IR (QIR) is the data contract between three downstream
// modules of the transpiler:
//
//   - M7 AST matcher fills a QUnit from user source code — one QOperation
//     per qbool binary-op assignment (MVP = OR only).
//   - M8 uncompute pass walks QUnit scopes in reverse, emitting an
//     UncomputeInsertion record for each QOperation.
//   - M9 emitter consumes those insertions to rewrite the source.
//
// Shape
// -----
// The IR is a *flat* list of scopes, and each scope holds a flat list of
// operations in source order (the "default" shape noted in the PRD's Open
// Questions section). The transpiler does not reconstruct a tree of
// expressions — each named intermediate (qbool tmp = a | b) is one op, and
// nesting is expressed transparently by operand names pointing back at
// earlier ops' results.
//
// Dependencies
// ------------
// This header is deliberately kept narrow. It includes only the minimum
// Clang type required to represent source positions (SourceLocation /
// SourceRange) plus <string> and <vector>. No AST types (DeclRefExpr,
// VarDecl, ...) leak in, so:
//
//   - Tests can construct a QUnit by hand, without spinning up a ClangTool.
//   - The M8 uncompute pass and M9 emitter compile in <1 second (they do
//     not drag the entire Clang AST library through their headers).
//
// Stability guarantee
// -------------------
// dump() is the single textual representation of a QUnit. The M7 and M8
// test suites compare against golden strings produced by this function, so
// a change to dump()'s format is a breaking change: every golden must be
// regenerated and every reviewer must approve it. If you need a different
// textual form (e.g. JSON for tooling), add a new function — do not
// repurpose dump().
//
// Golden format summary:
//   "QUnit: N scope(s)\n"
//   for each scope:
//     "  Scope[i] braces=[<open>..<close>]\n"
//     for each op:
//       "    Op[j] <KIND> <result>@<loc> = <op1>@<loc>, <op2>@<loc>, ...  range=[<b>..<e>]\n"
//
// Location fields print the raw SourceLocation encoding (a 32-bit unsigned
// integer). A SourceLocation whose isInvalid() returns true renders as the
// literal string "<invalid>" so hand-built test fixtures produce readable
// output without wiring up a SourceManager.

#ifndef STURM_TRANSPILE_QIR_HPP
#define STURM_TRANSPILE_QIR_HPP

#include "clang/Basic/SourceLocation.h"

#include <string>
#include <vector>

namespace sturm::transpile {

/// Kinds of quantum operations representable in the IR.
///
/// MVP includes only OR (`qbool tmp = a | b;`). Post-MVP phases add AND,
/// XOR, NOT, and classical-control forms — each new kind must be handled
/// by every switch in the uncompute pass and emitter, so keep this enum
/// small and intentional.
enum class QOpKind {
    OR,
    // AND, XOR, NOT, ... — added per post-MVP phases.
};

/// Symbolic reference to a named qbool / qint in user code.
///
/// Two QValueRefs compare equal only if both the name AND the declaration
/// location match. Name-only equality would alias shadowed locals across
/// nested scopes, which would silently corrupt the uncompute schedule.
struct QValueRef {
    std::string name;                  // e.g. "a", "tmp"
    clang::SourceLocation decl_loc;    // diagnostics + emitter insertion points
};

/// A single operation in the IR. One QOperation corresponds to one user
/// statement that the M7 matcher recognized (e.g. `qbool tmp = a | b;`).
///
/// - `kind`       : which quantum primitive was used.
/// - `result`     : the produced intermediate (a named qbool in user code).
/// - `operands`   : inputs in source order; references resolve by name + loc.
/// - `stmt_range` : full range of the originating statement, used by the
///                  M9 emitter to decide where to inject the uncompute code.
struct QOperation {
    QOpKind kind;
    QValueRef result;
    std::vector<QValueRef> operands;
    clang::SourceRange stmt_range;
};

/// One compound statement (curly-brace block) in the user's source.
///
/// `open_brace` and `close_brace` identify the scope uniquely — the M7
/// matcher uses them to locate-or-create a QScope when it sees a new op,
/// and the M8 pass uses them to anchor the uncompute-insertion point at
/// `close_brace`.
///
/// Ops are stored in source order. The uncompute pass iterates in reverse
/// to realize LIFO uncomputation.
struct QScope {
    clang::SourceLocation open_brace;
    clang::SourceLocation close_brace;
    std::vector<QOperation> ops;
};

/// Top-level container: one per translation unit. Scopes are stored in
/// source order (by `open_brace` location). The matcher appends as it
/// walks the AST, so no post-hoc sort is required.
struct QUnit {
    std::vector<QScope> scopes;
};

/// Equality on QValueRef: both the name and the decl_loc must match.
/// See the QValueRef struct docstring for why.
bool operator==(const QValueRef& lhs, const QValueRef& rhs);

inline bool operator!=(const QValueRef& lhs, const QValueRef& rhs) {
    return !(lhs == rhs);
}

/// Produce a stable textual representation of `unit`. See the file-level
/// comment for the exact golden format. Output is deterministic: calling
/// dump() twice on the same QUnit yields byte-identical strings.
std::string dump(const QUnit& unit);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QIR_HPP
