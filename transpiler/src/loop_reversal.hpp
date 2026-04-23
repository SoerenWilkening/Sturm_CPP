// loop_reversal.hpp — Phase S S-A (sturm-ha2k.2): reversed-iteration
// adjoint loop-header synthesis for `[[sturm::reversible]]` routines.
//
// Purpose
// -------
// Turns a canonical forward classical `for`-loop into the header of
// its reversed-iteration adjoint, per B11 ("adjoint synthesis
// reverses statement order AND loop iteration order"). For a forward
//
//     for (int i = lo; i < hi; i += s)    // s > 0
//
// this module produces the reversed header text
//
//     for (int i = (lo) + (((hi) - 1 - (lo)) / (s)) * (s); i >= (lo); i -= (s))
//
// Symmetric shapes handle inclusive bounds (`<=` / `>=`) and
// decrementing forward loops (`i -= s`, `--i`). The body descent
// (delegation to `adjoint_emitter`'s statement-level renderer) is
// Phase R R-1's responsibility; S-A emits only the header.
//
// Supported forward shapes
// ------------------------
//   - Init: `int i = <integer-expr>;` (single VarDecl, integer type).
//   - Cond: `i <op> <expr>` with `<op>` in `{ <, <=, >, >= }`.
//   - Inc:  `++i` / `i++` / `--i` / `i--` / `i += C` / `i -= C` /
//           `i = i + C` / `i = C + i` / `i = i - C` with C an
//           integer literal (possibly unary-minus-wrapped).
//
// Rejected shapes produce an empty `header` + a machine-readable
// `reason` code:
//
//   - Null ForStmt.
//   - Missing init / cond / inc (bare `for (;;)`).
//   - Multi-decl or non-integer init.
//   - Compound / non-comparison condition.
//   - Side-effecting / non-constant-literal increment.
//   - Stride evaluates to zero (no-progress forward; not reversible).
//   - Lexer source-text recovery failure for a literal token.
//
// S-A does NOT emit diagnostics — the caller (driver / S-B) routes
// the reason code through `DiagContext::report_*`. Mirrors Q-A /
// R-A's reject-without-diagnostic posture.
//
// Scope (S-A only)
// ----------------
//   - No body descent: the caller owns body rendering via R-A.
//   - No `while`-loop handling: P-C rejects those at validation.
//   - No nested-loop recursion: the driver applies S-A recursively.
//   - No `clang::Rewriter` mutation: pure string production.
//
// LOC budget: header ~80 (plan §2.4 S-A).

#ifndef STURM_TRANSPILE_LOOP_REVERSAL_HPP
#define STURM_TRANSPILE_LOOP_REVERSAL_HPP

#include <string>
#include <string_view>

namespace clang {
class ForStmt;
class LangOptions;
class SourceManager;
} // namespace clang

namespace sturm::transpile {

/// Why a `reverse_for_header` call did NOT produce a reversed header.
/// Spellings are part of the public contract — tests compare against
/// these exact strings via `to_string`.
enum class LoopRejectReason {
    None,                    ///< Success — see result fields.
    NullStmt,                ///< `fs == nullptr`.
    MissingClause,           ///< Missing init / cond / inc.
    NonCanonicalInit,        ///< Not a single integer VarDecl w/ init.
    NonCanonicalCond,        ///< Not a single comparison `i <op> bound`.
    NonCanonicalInc,         ///< Not a recognised constant-stride shape.
    ZeroStride,              ///< Stride evaluates to zero.
    SourceRecoveryFailed,    ///< Lexer failed to recover a source token.
};

/// Stable human-readable spelling of a `LoopRejectReason`.
std::string_view to_string(LoopRejectReason reason);

/// Result of a `reverse_for_header` invocation.
struct LoopReversalResult {
    /// Reversed for-header text `for (<init>; <cond>; <inc>)`. Does
    /// NOT include the body or opening brace. Non-empty iff
    /// `reason == None`.
    std::string header;

    /// The induction variable's identifier (verbatim). Exposed so
    /// the driver can match per-iteration mutations to the loop
    /// variable without re-parsing the reversed header.
    std::string induction_var;

    /// Success flag — derived from `reason == None`. Kept as a
    /// separate bool so assertions read in the affirmative.
    bool reversed = false;

    /// Machine-readable reason code.
    LoopRejectReason reason = LoopRejectReason::NullStmt;
};

/// Produce the reversed-iteration adjoint header for a canonical
/// forward `for`-loop. See header-level docs for the supported +
/// rejected shapes. The function never allocates beyond the returned
/// strings and never mutates clang IR state.
LoopReversalResult reverse_for_header(const clang::ForStmt* fs,
                                      const clang::SourceManager& sm,
                                      const clang::LangOptions& lang);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_LOOP_REVERSAL_HPP
