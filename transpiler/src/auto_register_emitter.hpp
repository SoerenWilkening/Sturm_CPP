// auto_register_emitter.hpp — Phase R R-B (sturm-88d7.3): emission of
// the `STURM_REGISTER_ADJOINT(<fwd>, <adj>);` registration line paired
// with each synthesised adjoint.
//
// Purpose
// -------
// After R-A (sturm-88d7.2) produces a sibling `__<fn>_adj` function
// body, the transpiler must install the forward/adjoint pair in PI-1's
// `RoutineRegistry` so every later `invert(fn)` / `uncompute` site in
// the TU picks it up. The existing PI-1 matcher already scans every
// TU for `STURM_REGISTER_ADJOINT(fn, adj)` invocations — see
// `transpiler/src/routine_registry.cpp`. R-B's job is to emit one of
// those macro invocations per synthesised adjoint, at TU scope, so the
// second pass of PM3's two-pass transpile flow picks up the pair the
// exact same way a hand-registered one does. No new PI-1 matcher
// shape is introduced: R-B piggybacks on the hand-written convention.
//
// Emitted shape
// -------------
// For an R-A synthesis entry with forward name `marked` and emitted
// adjoint name `__marked_adj`, R-B produces the single line
//
//     STURM_REGISTER_ADJOINT(marked, __marked_adj);
//
// followed by a newline. The trailing semicolon is a valid empty
// declaration at TU scope (the `STURM_REGISTER_ADJOINT` macro expands
// to `namespace sturm { namespace _detail { ... } }`, with closing
// braces — the semicolon is a no-op empty declaration). It is
// included for two reasons:
//
//   1. Consistency with the plan `§2.3 R-B` spec which names the line
//      with a trailing `;`.
//   2. Robustness: if the runtime header's expansion ever changes to
//      end with a class-body `}` (no trailing brace pair) — an
//      unlikely but possible future evolution — the semicolon keeps
//      the declaration-list grammar well-formed.
//
// Both arguments are passed verbatim into the macro. The forward
// argument may be a short identifier (`marked`), a nested-name
// (`ns::marked`), or a fully qualified global name (`::foo::bar`);
// R-C is responsible for choosing the right spelling to hand R-B.
// The adjoint argument is by convention `__<fn>_adj` — R-A populates
// `SynthesisEntry::adjoint_name` with that identifier, and R-B reads
// the field verbatim.
//
// Reject-without-diagnostic contract
// ----------------------------------
// Mirrors R-A's and Q-A's posture. When the input does not match the
// expected shape (null forward decl, decl without the
// `[[sturm::reversible]]` marker, empty adjoint_name, template
// instantiation that has no concrete `decltype(&::fn)` to register
// against), the function returns an empty `source` with
// `registered=false` and a machine-readable `reason` tag. Diagnostics
// are deliberately NOT emitted here — R-B is consulted from the
// pipeline driver (R-C) which owns the DiagContext routing.
//
// Pure string / source production
// -------------------------------
// This module NEVER mutates clang IR. The FD-based entry point reads
// the decl via Clang's public const-pointer API (`getNameAsString`,
// `isTemplateInstantiation`, `hasAttr`) and returns a `std::string`
// the driver inlines into the rewritten buffer. The string-based
// entry point does not touch Clang at all — it's a pure
// concatenation, exposed for unit tests and for callers that prefer
// to compute the forward's spelling themselves (e.g. to use a
// qualified name).
//
// Scope (R-B only)
// ----------------
// This module is a *pure source-level emitter* for the registration
// line. It does NOT:
//
//   - Walk the forward body (that is R-A's job, sturm-88d7.2).
//   - Emit the adjoint function definition (R-A).
//   - Run validation (P-C, sturm-z2e8.5).
//   - Normalise the forward's signature (Q-A, sturm-5kgu.2).
//   - Orchestrate the pipeline (R-C, sturm-88d7.4).
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers. The plan
// §2.3 R-B budget is 60 LOC for this header; we stay well under.

#ifndef STURM_TRANSPILE_AUTO_REGISTER_EMITTER_HPP
#define STURM_TRANSPILE_AUTO_REGISTER_EMITTER_HPP

#include <string>
#include <string_view>

namespace clang { class FunctionDecl; }

namespace sturm::transpile {

/// Why an `emit_auto_registration_for_decl` call did NOT produce a
/// registration line. Exposed so callers (pipeline driver, tests) can
/// distinguish expected reject shapes from a bug. The spellings are
/// part of the contract — tests may compare against the exact
/// strings below via `to_string`.
enum class AutoRegisterRejectReason {
    /// Registration line produced successfully — the `source` field
    /// carries it.
    None,
    /// `fd == nullptr`.
    NullDecl,
    /// The decl is not carrying
    /// `[[clang::annotate("sturm::reversible")]]`. R-B is opt-in by
    /// the same P9 contract as R-A.
    NotReversible,
    /// `adjoint_name` was empty — R-A either rejected this forward
    /// or the caller forgot to populate the synthesis-registry slot.
    /// Emitting `STURM_REGISTER_ADJOINT(fn, );` would be ill-formed.
    EmptyAdjointName,
    /// The decl is a template instantiation (non-Undeclared template
    /// specialisation kind). PI-1's matcher keys on
    /// `STURM_REGISTER_ADJOINT(fn, adj)` which takes a concrete
    /// function pointer `&::fn` — template instantiations were never
    /// registry candidates.
    TemplateInstantiation,
};

/// Human-readable spelling of an `AutoRegisterRejectReason`. Stable
/// across runs — the diagnostic surface (when R-C later wires in
/// rejection reporting) and tests compare against these exact strings.
std::string_view to_string(AutoRegisterRejectReason reason);

/// Result of a single `emit_auto_registration_for_decl` invocation.
struct AutoRegisterResult {
    /// The emitted registration line. Non-empty iff
    /// `reason == AutoRegisterRejectReason::None`. Trailing newline
    /// included so the caller concatenates it directly after R-A's
    /// adjoint body text without post-processing.
    std::string source;

    /// `true` iff the registration line was produced and is safe to
    /// inline into the rewritten buffer. Derived from
    /// `reason == None`; kept as a separate bool so test assertions
    /// can be written in the affirmative form.
    bool registered = false;

    /// Machine-readable reason code when `registered == false`. Set
    /// to `AutoRegisterRejectReason::None` on success.
    AutoRegisterRejectReason reason = AutoRegisterRejectReason::NullDecl;
};

/// Produce the TU-scope registration line for a forward/adjoint pair,
/// given the two names verbatim.
///
/// Emission shape:
///
///     STURM_REGISTER_ADJOINT(<forward_name>, <adjoint_name>);\n
///
/// Both names are dropped straight into the macro arguments — no
/// quoting, no mangling. The caller chooses how to qualify the
/// forward (short name, nested-name-specifier, global) based on the
/// emission context; a short name is sufficient at TU scope when the
/// forward lives at namespace scope and the registration line is
/// appended after the forward's definition.
///
/// Contract:
///   - `forward_name.empty()` ⇒ returns empty string (no-op).
///   - `adjoint_name.empty()` ⇒ returns empty string (no-op).
///   - Neither name is validated for being a well-formed identifier.
///     Callers that construct names programmatically (R-C) pass the
///     `getNameAsString()` output directly; bogus input would fail
///     at the later parse step, not here.
std::string emit_auto_registration(std::string_view forward_name,
                                   std::string_view adjoint_name);

/// Produce the registration line for a reversible forward `fd` paired
/// with the adjoint name `adjoint_name` (conventionally R-A's
/// `__<fn>_adj` output).
///
/// The forward's short identifier (`fd->getNameAsString()`) is used
/// as the macro's first argument. R-C handles the qualified-name case
/// separately by calling `emit_auto_registration` directly with the
/// pre-qualified spelling.
///
/// Contract:
///   - `fd == nullptr` ⇒ `registered=false`, `reason=NullDecl`.
///   - `fd` without `[[sturm::reversible]]` ⇒ `registered=false`,
///     `reason=NotReversible`. R-B is opt-in; we never register a
///     routine the user did not mark.
///   - `fd` that is a template instantiation (non-Undeclared
///     `TemplateSpecializationKind`) ⇒ `registered=false`,
///     `reason=TemplateInstantiation`.
///   - `adjoint_name.empty()` ⇒ `registered=false`,
///     `reason=EmptyAdjointName`.
///
/// The function never allocates beyond the returned strings and
/// never mutates any clang IR state. Safe to call speculatively from
/// the driver (R-C) before committing to emission.
AutoRegisterResult emit_auto_registration_for_decl(
    const clang::FunctionDecl* fd, std::string_view adjoint_name);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_AUTO_REGISTER_EMITTER_HPP
