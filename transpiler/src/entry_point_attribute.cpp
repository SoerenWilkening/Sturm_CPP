// entry_point_attribute.cpp — sturm-0tcv: implementation of
// `has_entry_point_attr()`.
//
// Mirrors the `reversible_attribute.cpp` pattern. The detection
// primitive walks every `AnnotateAttr` hanging off the FunctionDecl and
// compares its annotation text byte-exact against
// `kEntryPointAttrAnnotation` ("sturm::entry_point"). A match on any
// one attribute returns true; absence of any match — or a null decl —
// returns false.
//
// Attribute-list shape
// --------------------
// `FunctionDecl::specific_attrs<AnnotateAttr>()` yields an iterator
// range over every `AnnotateAttr` attached to the decl. For the
// opt-in marker written in source as
//
//     [[clang::annotate("sturm::entry_point")]]
//     void test_fixture_setup() { ... }
//
// the iterator visits exactly one `AnnotateAttr` whose
// `getAnnotation()` returns `"sturm::entry_point"`. The check is
// byte-exact — a typo like `"sturm::entry_pont"` yields a different
// StringRef value, the equality test fails, and `has_entry_point_attr`
// returns false. This is deliberate: the attribute is opt-in and the
// typo path must fall through to "no auto-injection".
//
// Plugin-registered short form
// ----------------------------
// `entry_point_parsed_attr_info.cpp` registers a ParsedAttrInfo for
// the plain `[[sturm::entry_point]]` spelling that emits the same
// AnnotateAttr on the underlying FunctionDecl. The detection
// predicate therefore answers true for both spellings as long as the
// host clang loaded the plugin; if the plugin is absent the short
// form silently no-ops (Wunknown-attributes is the diagnostic surface
// for that case — see the registration TU for the trade-off).
//
// Template survival
// -----------------
// Clang attaches `AnnotateAttr` to the FunctionDecl at the point of
// parse, and every instantiation (implicit or explicit) clones the
// attribute list from the primary template's decl. `specific_attrs<>`
// therefore finds the marker on the primary template's
// `FunctionTemplateDecl::getTemplatedDecl()`, on every implicit
// instantiation, and on any explicit specialisation that re-supplies
// the attribute.
//
// Safety
// ------
// Null decls are guarded by an early return; every other shape is
// safe to iterate.

#include "entry_point_attribute.hpp"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/StringRef.h"

namespace sturm::transpile {

bool has_entry_point_attr(const clang::FunctionDecl* fd) {
    // Contract: nullptr returns false. The matcher layer may hand a
    // null decl when an indirect call site fails to resolve; we must
    // not treat that as an implicit entry-point vote.
    if (fd == nullptr) {
        return false;
    }

    // Walk every `AnnotateAttr` attached to the decl. `specific_attrs<>`
    // returns an iterator range; tolerates a decl with no attributes
    // at all (empty range, loop body never runs).
    //
    // We do NOT stop at the first non-matching `AnnotateAttr` — a
    // routine can carry several unrelated annotations (other plugin
    // contracts, third-party tooling, the `sturm::reversible` marker
    // alongside) and only one of them has to match.
    for (const auto* attr : fd->specific_attrs<clang::AnnotateAttr>()) {
        if (attr != nullptr &&
            attr->getAnnotation().equals(llvm::StringRef(
                kEntryPointAttrAnnotation.data(),
                kEntryPointAttrAnnotation.size()))) {
            return true;
        }
    }

    // No matching AnnotateAttr found. This includes:
    //   - Plain functions with no attributes at all.
    //   - Functions annotated only with unrelated `AnnotateAttr`
    //     strings (other plugins, user-defined markers, the
    //     `sturm::reversible` marker without `sturm::entry_point`).
    //   - Functions marked with typos of our marker.
    //   - Functions written with the raw `[[sturm::entry_point]]`
    //     form on a clang that has NOT loaded our ParsedAttrInfo
    //     plugin: the attribute lands as an UnknownAttr (or is
    //     dropped with -Wunknown-attributes), the AnnotateAttr
    //     iteration is empty for THIS marker, and we correctly
    //     return false. Users in that situation must either load
    //     the plugin or fall back to the canonical
    //     `[[clang::annotate("sturm::entry_point")]]` spelling.
    return false;
}

} // namespace sturm::transpile
