// reversible_attribute.hpp — Phase P P-A (sturm-z2e8.2): detection of
// the `[[sturm::reversible]]` opt-in marker on user routines.
//
// Purpose
// -------
// Phase P of the automatic-adjoint-synthesis roadmap begins by tagging
// user forward routines that the transpiler is allowed to synthesize
// adjoints for. Per the PRD §9 Q1 locked decision (opt-in via
// attribute), the marker is the source-level spelling
// `[[sturm::reversible]]`. Clang does not ship a first-class attribute
// under the `sturm::` namespace, so the canonical carrier is
// `[[clang::annotate("sturm::reversible")]]` — Clang's general-purpose
// string annotation attribute. Any user who writes the shorter
// `[[sturm::reversible]]` form sees a `-Wunknown-attributes` warning
// and the attribute is dropped; the `clang::annotate` form is the one
// the transpiler is guaranteed to see in the AST.
//
// This module exposes the single read-side predicate
//
//     bool is_reversible(const clang::FunctionDecl* fd)
//
// which returns true iff `fd` carries at least one `AnnotateAttr`
// whose annotation text exactly matches `"sturm::reversible"`. A null
// `fd` returns false. Typos (e.g. `"sturm::reversibel"`) return false
// because the match is byte-exact, pinning the P9 contract that the
// marker is opt-in — a forward routine is only a synthesis candidate
// when the user spelled the intent correctly.
//
// Template survival
// -----------------
// When a user writes `[[sturm::reversible]]` on a function template
// (primary declaration), Clang preserves the `AnnotateAttr` on the
// template's `FunctionDecl` AND on every `FunctionDecl` produced by
// instantiation. `is_reversible` therefore answers the predicate for
// the primary template, for an explicit specialization, and for an
// implicit instantiation alike. Downstream consumers (synthesis
// registry, validation matcher) can call it on whichever `FunctionDecl`
// the MatchFinder hands them without worrying about which of the three
// shapes it is.
//
// Scope (P-A only)
// ----------------
// This header is the detection primitive. It does NOT:
//   - Traverse the function body (that is P-C's validation pass).
//   - Track or register forward/adjoint pairs (that is P-B's
//     `SynthesisRegistry`, sturm-z2e8.3).
//   - Emit diagnostics (that is P-D's `DiagContext` extension,
//     sturm-z2e8.4).
//
// Header-only vs translation-unit split
// -------------------------------------
// The predicate is small enough to inline, but the `AnnotateAttr`
// header surface is nontrivial (drags `clang/AST/Attr.h` and its
// transitively-included `Attrs.inc`). Keeping the implementation in
// `reversible_attribute.cpp` lets callers include this header with
// only a forward declaration of `clang::FunctionDecl`, matching the
// pattern `routine_registry.hpp` uses for its registration helper.
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers / 400 LOC for
// implementation files. This header targets ~80 LOC (matcher
// implementation plan §2.1 P-A).

#ifndef STURM_TRANSPILE_REVERSIBLE_ATTRIBUTE_HPP
#define STURM_TRANSPILE_REVERSIBLE_ATTRIBUTE_HPP

#include <string_view>

namespace clang { class FunctionDecl; }

namespace sturm::transpile {

/// The exact annotation text the transpiler matches. Any other spelling
/// — including common typos like `sturm::reversibel` — fails detection.
/// Exposed as a constant so tests can pin the contract without hand-
/// copying the magic string.
inline constexpr std::string_view kReversibleAttrAnnotation =
    "sturm::reversible";

/// Return true iff `fd` carries the `[[sturm::reversible]]` opt-in
/// marker, recognised through the Clang `AnnotateAttr` whose
/// annotation string equals `kReversibleAttrAnnotation`.
///
/// Contract:
///   - `fd == nullptr` returns false.
///   - Multiple AnnotateAttrs on the same decl are tolerated: a match
///     on any one suffices. Other annotations (unrelated plugins,
///     `[[clang::annotate("foo")]]`) are ignored.
///   - Byte-exact match against `kReversibleAttrAnnotation`. Typos,
///     trailing whitespace, and alternative capitalisations all fail.
///   - Template survival: callable on the primary template's
///     `FunctionDecl`, on explicit specialisations, and on implicit
///     instantiations; all three carry the attribute when the source
///     marked the primary with `[[sturm::reversible]]`.
bool is_reversible(const clang::FunctionDecl* fd);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_REVERSIBLE_ATTRIBUTE_HPP
