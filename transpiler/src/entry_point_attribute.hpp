// entry_point_attribute.hpp — sturm-0tcv: detection of the
// `[[sturm::entry_point]]` opt-in marker for auto-lifecycle injection
// on non-main entry points.
//
// Purpose
// -------
// PRD §5.4 (auto-injected lifecycle) initially fired only on the unique
// `int main(...)` FunctionDecl of any TU that includes the umbrella
// `sturm.h`. PRD §7's out-of-scope follow-up entry called for a
// `[[sturm::entry_point]]` attribute that flags an arbitrary
// FunctionDecl for the same auto-injection treatment — covering
// library-level fixtures, test-framework SetUp routines, and any
// non-main top-level entry point that would otherwise need a manual
// lifecycle.
//
// This module is the read-side predicate. It mirrors the
// `is_reversible(const FunctionDecl*)` pattern from
// `reversible_attribute.hpp`: a free function over a FunctionDecl
// that returns true iff the decl carries the
// `[[clang::annotate("sturm::entry_point")]]` marker.
//
// Attribute spelling
// ------------------
// The canonical carrier is `[[clang::annotate("sturm::entry_point")]]`
// because Clang does not ship a first-class attribute under the
// `sturm::` namespace and the plain `[[sturm::entry_point]]` form is
// dropped with a `-Wunknown-attributes` warning before the AST sees
// it (same situation as `[[sturm::reversible]]` in
// `reversible_attribute.hpp`). Detection is byte-exact: typos, wrong
// case, and leading/trailing whitespace all fail the predicate.
//
// A `ParsedAttrInfo` plugin registration that teaches Clang about the
// plain `[[sturm::entry_point]]` spelling (forwarding to the same
// annotate carrier) is registered in
// `entry_point_parsed_attr_info.cpp` as a best-effort convenience for
// users who prefer the shorter form. The `is_entry_point()` predicate
// only inspects the AnnotateAttr because that is the single shape
// guaranteed to land in the AST regardless of plugin registration
// status (the ParsedAttrInfo forwards to the same annotation string at
// parse time, so the AnnotateAttr is what every downstream walker
// sees).
//
// Scope (this module only)
// ------------------------
// This header is the detection primitive. It does NOT:
//   - Decide what the matcher does on a hit (that is
//     `matcher_main_lifecycle.cpp`'s job — see the matcher's
//     extension comment for the IIFE shape choice).
//   - Emit code (that is `main_lifecycle_emitter.cpp`'s job).
//   - Diagnose ill-typed entry points (out of scope; PRD §7's
//     follow-up entry intentionally leaves type validation to the
//     normal C++ overload resolution machinery — a `qint`-taking
//     entry point that tries to return from inside an IIFE is no
//     worse than the same code outside an IIFE).
//
// LoC budget
// ----------
// CLAUDE.md caps headers at 300 LOC. This header targets ~80 LOC.

#ifndef STURM_TRANSPILE_ENTRY_POINT_ATTRIBUTE_HPP
#define STURM_TRANSPILE_ENTRY_POINT_ATTRIBUTE_HPP

#include <string_view>

namespace clang { class FunctionDecl; }

namespace sturm::transpile {

/// The exact annotation text the transpiler matches. Any other spelling
/// — including typos, wrong case, or surrounding whitespace — fails
/// detection. Exposed as a constant so tests can pin the contract
/// without hand-copying the magic string.
inline constexpr std::string_view kEntryPointAttrAnnotation =
    "sturm::entry_point";

/// Return true iff `fd` carries the `[[sturm::entry_point]]` opt-in
/// marker, recognised through the Clang `AnnotateAttr` whose
/// annotation string equals `kEntryPointAttrAnnotation`.
///
/// Contract:
///   - `fd == nullptr` returns false.
///   - Multiple AnnotateAttrs on the same decl are tolerated: a match
///     on any one suffices. Other annotations (unrelated plugins,
///     `[[clang::annotate("foo")]]`, `[[clang::annotate("sturm::
///     reversible")]]`) are ignored.
///   - Byte-exact match against `kEntryPointAttrAnnotation`. Typos
///     ("entry_pont"), trailing whitespace, and alternative
///     capitalisations all fail.
///   - Template survival: callable on the primary template's
///     `FunctionDecl`, on explicit specialisations, and on implicit
///     instantiations; all three carry the attribute when the source
///     marked the primary with `[[sturm::entry_point]]`.
bool has_entry_point_attr(const clang::FunctionDecl* fd);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_ENTRY_POINT_ATTRIBUTE_HPP
