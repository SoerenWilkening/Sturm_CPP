// adjoint_emitter.hpp — Phase R R-1 (sturm-88d7.2): straight-line
// adjoint emission for validated + normalized reversible forward
// routines.
//
// Purpose
// -------
// Phase R of the automatic-adjoint-synthesis roadmap (see
// `docs/implementation_plan_automatic_adjoint_synthesis.md` §2.3 R-A
// and `docs/prd_automatic_adjoint_synthesis.md` §5.1 item 3) emits a
// named sibling function `__<fn>_adj` whose body is the reverse-
// statement-order adjoint of the forward routine's body.
//
// For a forward routine
//
//     [[clang::annotate("sturm::reversible")]]
//     void marked(qbool& a, qint x, int T) { a ^= (x >= T); }
//
// this module produces the sibling source text
//
//     void __marked_adj(qbool& a, qint x, int T) {
//         a ^= (x >= T);
//     }
//
// A second module (R-B `auto_register_emitter`, sturm-88d7.3) later
// appends the matching `STURM_REGISTER_ADJOINT(marked, __marked_adj);`
// line so PI-1's registry picks up the pair on the second transpile
// pass. This module does NOT register anything; its responsibility is
// strictly bounded to "produce the adjoint body source text".
//
// Reverse statement order walk
// ----------------------------
// The adjoint walks the routine's body in REVERSE statement order (per
// B11 — "adjoint synthesis reverses statement order AND loop iteration
// order"). Given forward body `s0; s1; s2;`, the adjoint body is
// `adj(s2); adj(s1); adj(s0);`. For each statement `si`, the per-
// statement inverse renderer is the existing `render_uncompute` helper
// from `transpiler/src/uncompute_pass.cpp` — the same code path the M8
// inline-uncompute pass uses to produce a single inverse line. This
// guarantees that the synthesized adjoint is byte-identical to the
// inline uncompute every Phase A..N user sees today; the only
// difference is the syntactic location (sibling function vs. before
// the enclosing scope's closing brace).
//
// Loops are OUT OF SCOPE for Phase R. The parent plan §2.3 R-A pins
// loops to Phase S (sturm-ha2k.2, `loop_reversal`). Callers that hand
// a body containing a `for`/`while` loop op retain the Phase H PH-3
// `skip_uncompute=true` diagnostic their forward matcher already
// emits; this module produces no adjoint for such ops (i.e. it skips
// them the same way `synthesize()` does, matching the pre-Phase-S
// contract).
//
// PLUGIN kind parity
// ------------------
// Plugin-registered ops (`QOpKind::PLUGIN`) are invertible through
// the same `render_uncompute` dispatch as in-tree kinds; the emitter
// accepts an optional `plugin::Registry*` and threads it through.
// Callers that hand-build ops with only in-tree kinds can pass
// `nullptr` (the default) — `render_uncompute` degrades to an empty
// string on a PLUGIN op without a registry, matching the defensive
// posture every other kind takes on malformed input.
//
// Self-dual and non-self-dual kinds
// ---------------------------------
// Per P9c the emitter unconditionally produces a distinct
// `__<fn>_adj` symbol even for self-inverse bodies. This preserves
// the PI-4 audit trail: a user searching for `__*_adj` at uncompute
// sites finds every synthesized adjoint regardless of whether the
// forward happened to be self-dual. The audit cost of a self-dual
// re-emission is nil — the compiler inlines the call after name
// resolution.
//
// Emission shape
// --------------
// The produced source text follows a deterministic format so
// golden-file tests byte-compare reliably:
//
//   - Leading line:  `void __<fn>_adj(<signature>) {\n`
//   - Body lines:    one per rendered op, in reverse order. Each
//                    line already carries `render_uncompute`'s four-
//                    space leading indent and trailing newline, so
//                    the emitter concatenates them verbatim without
//                    additional whitespace.
//   - Trailing line: `}\n`.
//
// Pure string / source production
// -------------------------------
// This module NEVER mutates clang IR. It consumes a `clang::
// FunctionDecl*` via Clang's public const-pointer API and a vector
// of `QOperation` records (produced by the matcher on the forward's
// body); it emits a `std::string` that the pipeline driver stores
// on the `SynthesisRegistry` entry's `adjoint_source` field via the
// new `set_adjoint_source` setter (sibling to `set_twin_source`).
// R-C's driver matcher later inlines the string into the rewritten
// buffer.
//
// The new `adjoint_source` slot is introduced alongside the existing
// `twin_source` slot (Q-A) because the two fields carry semantically
// distinct blobs:
//
//   - `twin_source`    — the out-param twin `__<fn>_out` body (Q-A).
//   - `adjoint_source` — the sibling adjoint `__<fn>_adj` body (R-A).
//
// Reusing `twin_source` for the adjoint text would confuse downstream
// consumers (R-B reads `adjoint_source` to pick the token that goes
// into `STURM_REGISTER_ADJOINT`; Q-A's `twin_source` is entirely
// unrelated) and would break the `test_synthesis_registry` contract
// pinning the `twin_source` field to the Q-A output.
//
// Reject-without-diagnostic contract
// ----------------------------------
// The function answers "did I produce an adjoint?" via a result
// record. When the input does not match the expected shape (null
// decl, missing `[[sturm::reversible]]` marker, no body, source-text
// recovery failure for the parameter signature), the function
// returns an empty adjoint with `synthesized=false` and a machine-
// readable `reason` tag. Diagnostics are deliberately NOT emitted
// here — R-A is consulted from the pipeline driver (R-C) which owns
// the DiagContext routing. This mirrors Q-A's posture.
//
// LOC budget
// ----------
// CLAUDE.md caps header files at 300 LOC. The plan §2.3 R-A budget
// is 130 LOC for this header; we stay well under.

#ifndef STURM_TRANSPILE_ADJOINT_EMITTER_HPP
#define STURM_TRANSPILE_ADJOINT_EMITTER_HPP

#include "sturm/transpile/qir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace clang {
class FunctionDecl;
class LangOptions;
class SourceManager;
} // namespace clang

namespace sturm::transpile {

namespace plugin {
class Registry;
} // namespace plugin

/// Why an `emit_adjoint_for_decl` call did NOT produce an adjoint.
/// Exposed so callers (pipeline driver, tests) can distinguish the
/// expected reject shapes from a bug. The spellings are part of the
/// contract — tests may compare against the exact strings below.
enum class AdjointRejectReason {
    /// Adjoint produced successfully — the `source` field carries it.
    None,
    /// `fd == nullptr`.
    NullDecl,
    /// The decl is not carrying `[[clang::annotate("sturm::reversible")]]`.
    NotReversible,
    /// The decl has no body (forward declaration) — cannot synthesise
    /// from an unseen definition.
    NoBody,
    /// The `clang::Lexer` failed to recover verbatim source text for
    /// one of the parameters the adjoint signature needs.
    SourceRecoveryFailed,
};

/// Result of a single `emit_adjoint_for_decl` / `emit_adjoint_body`
/// invocation.
struct AdjointEmissionResult {
    /// The emitted adjoint's complete source text. Non-empty iff
    /// `reason == AdjointRejectReason::None`.
    std::string source;

    /// The adjoint's source-level identifier, of the form
    /// `__<forward>_adj`. Populated whenever `reason == None`; empty
    /// on every reject path. Exposed so the pipeline driver can
    /// record the name into the `SynthesisRegistry` without
    /// re-deriving it from the forward's decl.
    std::string adjoint_name;

    /// `true` iff the adjoint source was produced and is safe to
    /// attach to the synthesis registry. Derived from
    /// `reason == None`; kept as a separate bool so test assertions
    /// can be written in the affirmative form.
    bool synthesized = false;

    /// Machine-readable reason code when `synthesized == false`.
    /// Set to `AdjointRejectReason::None` on success.
    AdjointRejectReason reason = AdjointRejectReason::NullDecl;
};

/// Human-readable spelling of an `AdjointRejectReason`. Stable across
/// runs — the diagnostic surface (when R-C later wires in rejection
/// reporting) and tests compare against these exact strings.
std::string_view to_string(AdjointRejectReason reason);

/// Produce the adjoint body text for a sequence of forward
/// `QOperation`s, in reverse statement order.
///
/// This is the pure-string variant: no `FunctionDecl` involvement,
/// no `SourceManager` touching. The caller provides the signature
/// text verbatim (parameter list inside the parentheses, same
/// spelling the forward's signature uses — the adjoint shares the
/// forward's parameter shape per P9b). Exposed for unit tests that
/// hand-build IR fragments via `test_matcher_harness.hpp` and
/// cannot conveniently instantiate a FunctionDecl.
///
/// Emission shape:
///
///     void __<fn>_adj(<signature>) {
///         <rendered inverse of ops[N-1]>
///         <rendered inverse of ops[N-2]>
///         ...
///         <rendered inverse of ops[0]>
///     }
///
/// Each rendered line carries `render_uncompute`'s four-space leading
/// indent and trailing newline, so the concatenation is tight.
///
/// Contract:
///   - `fn_name` empty ⇒ returns empty string (no-op).
///   - `ops` empty ⇒ returns a body with zero statements (an adjoint
///     stub — matches the shape R-C needs for an edge-case forward
///     whose only op was elided by an earlier optimization pass).
///   - Per-op `render_uncompute` failure (malformed op, empty render)
///     ⇒ the line is skipped verbatim; the surrounding shape still
///     emits. Matches `synthesize()`'s posture: the MVP matcher never
///     produces malformed ops, but defense is cheap.
///   - `QOpKind::PLUGIN` ops without a `registry` render to empty
///     (skipped) — same posture as `render_uncompute`.
std::string emit_adjoint_body(
    std::string_view fn_name,
    std::string_view signature_text,
    const std::vector<QOperation>& ops,
    const plugin::Registry* registry = nullptr);

/// Produce the complete sibling-adjoint source text for a validated
/// + normalized `[[sturm::reversible]]` forward routine.
///
/// Walks the forward's body in reverse statement order, calling
/// `render_uncompute` for each statement, and emits
///
///     void __<fn>_adj(<signature>) {
///         <rendered adjoint body>
///     }
///
/// where `<signature>` is recovered verbatim from the forward's
/// parameter list (same spelling, same const-ness, same ref/value
/// category — per P9b the adjoint and forward share input
/// immutability rules).
///
/// The caller must separately populate `ops` with the matcher's
/// per-statement `QOperation` records for the forward body. R-C's
/// driver matcher is responsible for this; unit tests construct
/// `ops` by hand via the plain-string overload above.
///
/// Contract:
///   - `fd == nullptr` ⇒ `synthesized=false`, `reason=NullDecl`.
///   - `fd` without `[[sturm::reversible]]` ⇒ `synthesized=false`,
///     `reason=NotReversible`. R-A is opt-in; we never emit an
///     adjoint for a routine the user did not mark.
///   - `fd` without a body ⇒ `synthesized=false`, `reason=NoBody`.
///   - Any parameter `clang::Lexer` cannot recover source text for
///     ⇒ `synthesized=false`, `reason=SourceRecoveryFailed`.
///
/// The function never allocates beyond the returned strings and
/// never mutates any clang IR state. Safe to call speculatively
/// from the driver (R-C) before committing to emission.
AdjointEmissionResult emit_adjoint_for_decl(
    const clang::FunctionDecl* fd,
    const std::vector<QOperation>& ops,
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const plugin::Registry* registry = nullptr);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_ADJOINT_EMITTER_HPP
