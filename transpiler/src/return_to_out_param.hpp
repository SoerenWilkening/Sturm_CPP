// return_to_out_param.hpp — Phase Q Q-A (sturm-5kgu.2): synthesis of
// out-param twin source text for reversible forwards that return a
// quantum type via a single `return <expr>;` statement.
//
// Purpose
// -------
// Phase Q of the automatic-adjoint-synthesis roadmap normalises the
// signatures of reversible user routines so the downstream straight-
// line adjoint emitter (R-A) only has to reason about one canonical
// shape. Per PRD §4.1 and §5.1, both return-style and out-param-style
// forwards are accepted by the user-facing API:
//
//     // Return style — the sugar this module normalises.
//     [[clang::annotate("sturm::reversible")]]
//     qbool marked(qint x, int T) { return x >= T; }
//
//     // Out-param style — already canonical, no twin needed.
//     void marked(qbool& a, qint x, int T) { a ^= (x >= T); }
//
// For the return-style case this module produces the source text of a
// sibling out-param companion. For the canonical-in-place marked
// example above the synthesised twin reads:
//
//     [[clang::annotate("sturm::reversible")]]
//     void __marked_out(qint x, int T, qbool& __marked_out) {
//         __marked_out ^= x >= T;
//     }
//
// The twin's name is `__<fn>_out`, the returned value is threaded
// through an `&`-reference out-parameter appended to the forward's
// existing parameter list, and the single return expression is
// rewritten into an `^=` XOR-assign against the out-parameter (matching
// the canonical out-param-style body the PRD example shows).
//
// Pure string / source production
// -------------------------------
// This module NEVER mutates clang IR. It walks the `clang::FunctionDecl`
// via Clang's public const-pointer API (`getReturnType`, `parameters`,
// `getBody`) and uses `clang::Lexer::getSourceText` to recover the
// verbatim spelling of parameter types, the return-type node, and the
// return expression. The output is an in-memory `std::string` the
// caller attaches to the `SynthesisRegistry` entry via its new
// `set_twin_source` setter — Q-A does not register anything in
// Clang's AST itself; that is R-A's job on a later transpile pass once
// the twin text has been inlined into the rewritten buffer.
//
// Decision: the twin source lives on the synthesis registry as a
// separate `twin_source` string field (not `adjoint_name`). The
// `adjoint_name` slot documents a *source-level identifier* in the
// emitted buffer (conventionally `__<fwd>_adj`), which is semantically
// unrelated to a multi-line twin body. Reusing the name slot for a
// body blob would confuse Phase R's downstream consumer and break the
// `adjoint_name` contract already pinned by `test_synthesis_registry`.
// The new field is added alongside the existing one, set by Q-A,
// read-only after that.
//
// Scope (Q-A only)
// ----------------
// This module is a *pure source-level normaliser* for one narrow
// syntactic shape. It does NOT:
//
//   - Enforce parameter constness or reject mutation patterns — that
//     is Q-B (`matcher_reversible_signature.cpp`, sturm-5kgu.3).
//   - Walk the body for measurement / I/O / unregistered-callee
//     rejection — that is P-C (`matcher_reversible_validate.cpp`).
//   - Emit an adjoint — that is R-A (`adjoint_emitter.cpp`,
//     sturm-88d7.2).
//   - Inject a `STURM_REGISTER_ADJOINT(...)` expansion — that is R-B
//     (`auto_register_emitter.cpp`).
//
// Reject-without-diagnostic contract
// ----------------------------------
// The function answers "did I synthesise a twin?" via a result record.
// When the input does not match the expected syntactic shape (null
// decl, missing `[[sturm::reversible]]` marker, non-quantum return
// type, no body, body with more than one statement, body whose sole
// statement is not a `ReturnStmt` with an expression, a `ReturnStmt`
// without an expression, or a failed source-text recovery via the
// Lexer), the function returns an empty twin with `synthesized=false`
// and a machine-readable `reason` tag. Diagnostics are deliberately
// not emitted here — Q-A is consulted speculatively by the pipeline
// driver (R-C) which needs a cheap way to ask "is this forward in
// return-style shape?" without fanning out into `DiagContext`.
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers. The plan
// §2.2 Q-A budget is 110 LOC for this header; we stay well under.

#ifndef STURM_TRANSPILE_RETURN_TO_OUT_PARAM_HPP
#define STURM_TRANSPILE_RETURN_TO_OUT_PARAM_HPP

#include <string>
#include <string_view>

namespace clang {
class FunctionDecl;
class LangOptions;
class SourceManager;
} // namespace clang

namespace sturm::transpile {

/// Why a `synthesize_out_param_twin` call did NOT produce a twin.
/// Exposed so callers (pipeline driver, tests) can distinguish the
/// expected reject shapes from a bug. The spellings are part of the
/// contract — tests may compare against the exact strings below.
enum class TwinRejectReason {
    /// Twin produced successfully — the `source` field carries it.
    None,
    /// `fd == nullptr`.
    NullDecl,
    /// The decl is not carrying `[[clang::annotate("sturm::reversible")]]`.
    NotReversible,
    /// The declared return type is not a quantum type
    /// (not `qbool` / `qint` / `qint_t`). `void` returns fall here.
    NonQuantumReturnType,
    /// The decl has no body (forward declaration) — cannot synthesise
    /// from an unseen definition.
    NoBody,
    /// The body is not a `CompoundStmt` (should be unreachable for a
    /// well-formed C++ function, but we defend).
    NonCompoundBody,
    /// The body contains zero or more than one statement. Q-A only
    /// handles the exact `{ return <expr>; }` shape.
    MultiStatementBody,
    /// The single body statement is not a `return`.
    NotReturnStatement,
    /// `return;` without an expression — cannot rewrite into `^=`.
    EmptyReturnExpression,
    /// The `clang::Lexer` failed to recover verbatim source text for
    /// one of the tokens the twin needs (parameter, return expr).
    /// Typically a macro expansion with no file-backed SourceRange.
    SourceRecoveryFailed,
};

/// Result of a single `synthesize_out_param_twin` invocation.
struct TwinSynthesisResult {
    /// The emitted twin's source text. Non-empty iff
    /// `reason == TwinRejectReason::None`.
    std::string source;

    /// The twin's source-level identifier, of the form
    /// `__<forward>_out`. Populated whenever `reason == None`; empty
    /// on every reject path. Exposed so the pipeline driver can
    /// record the name into the `SynthesisRegistry` without
    /// re-deriving it from the forward's decl.
    std::string twin_name;

    /// `true` iff the twin source was produced and is safe to attach
    /// to the synthesis registry. Derived from `reason == None`;
    /// kept as a separate bool so test assertions can be written in
    /// the affirmative form.
    bool synthesized = false;

    /// Machine-readable reason code when `synthesized == false`.
    /// Set to `TwinRejectReason::None` on success.
    TwinRejectReason reason = TwinRejectReason::NullDecl;
};

/// Human-readable spelling of a `TwinRejectReason`. Stable across
/// runs — the diagnostic surface (when R-C later wires in rejection
/// reporting) and tests compare against these exact strings.
std::string_view to_string(TwinRejectReason reason);

/// Synthesise the out-param twin source text for a reversible forward
/// routine whose return type is a quantum type and whose body is a
/// single `return <expr>;` statement.
///
/// Shape produced (matching PRD §4.1 / §5.1):
///
///     // Forward (kept unchanged by this module):
///     [[clang::annotate("sturm::reversible")]]
///     qbool marked(qint x, int T) { return x >= T; }
///
///     // Twin (what this function returns as text):
///     [[clang::annotate("sturm::reversible")]]
///     void __marked_out(qint x, int T, qbool& __marked_out) {
///         __marked_out ^= x >= T;
///     }
///
/// The forward's `[[sturm::reversible]]` annotation is propagated so
/// the twin is itself a synthesis candidate (R-A walks it next
/// pass). Each parameter is re-emitted verbatim — same const-ness,
/// same reference / value category, same name — followed by a new
/// out-parameter `<ret_type>& <twin_name>` whose name matches the
/// twin's own identifier. The return expression is lifted into a
/// `<twin_name> ^= <expr>;` assignment (matching the canonical
/// out-param shape of `void marked(qbool& a, ...) { a ^= (...); }`).
///
/// Contract:
///   - `fd == nullptr` ⇒ `synthesized=false`, `reason=NullDecl`.
///   - `fd` without `[[sturm::reversible]]` ⇒ `synthesized=false`,
///     `reason=NotReversible`. Q-A is opt-in; we never rewrite a
///     routine the user did not mark.
///   - `fd` returning a non-quantum type (`void`, `int`, etc.) ⇒
///     `synthesized=false`, `reason=NonQuantumReturnType`.
///   - `fd` without a body (forward declaration with no definition)
///     ⇒ `synthesized=false`, `reason=NoBody`.
///   - `fd` whose body is not exactly `{ return <expr>; }` ⇒ one of
///     `NonCompoundBody`, `MultiStatementBody`, `NotReturnStatement`,
///     `EmptyReturnExpression` depending on the specific deviation.
///   - Any parameter / expression the `clang::Lexer` cannot recover
///     verbatim source text for ⇒ `synthesized=false`,
///     `reason=SourceRecoveryFailed`.
///
/// The function never allocates beyond the returned strings and never
/// mutates any clang IR state. It is safe to call speculatively from
/// the driver (R-C) before committing to emission.
TwinSynthesisResult synthesize_out_param_twin(
    const clang::FunctionDecl* fd,
    const clang::SourceManager& sm,
    const clang::LangOptions& lang);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_RETURN_TO_OUT_PARAM_HPP
