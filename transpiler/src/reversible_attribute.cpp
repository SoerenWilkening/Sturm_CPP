// reversible_attribute.cpp — Phase P P-A (sturm-z2e8.2): implementation
// of `is_reversible()`.
//
// The detection primitive is a straight read of the `clang::FunctionDecl`'s
// attribute list: we walk every `AnnotateAttr` hanging off the decl and
// compare its annotation text against `kReversibleAttrAnnotation`. A match
// on any one attribute returns true; absence of any match — or a null
// decl — returns false.
//
// Attribute-list shape
// --------------------
// `FunctionDecl::specific_attrs<AnnotateAttr>()` yields an iterator range
// over every `AnnotateAttr` attached to the decl. For the opt-in marker
// written in source as
//
//     [[clang::annotate("sturm::reversible")]]
//     qbool marked(qint x, int T) { return x >= T; }
//
// the iterator visits exactly one `AnnotateAttr` whose
// `getAnnotation()` returns `"sturm::reversible"` (a `llvm::StringRef`
// view into the AST's string arena). The check is byte-exact — a typo
// like `[[clang::annotate("sturm::reversibel")]]` yields a different
// StringRef value, the equality test fails, and `is_reversible` returns
// false. This is deliberate: per PRD §9 Q1, the attribute is opt-in and
// the typo path must fall through to "no synthesis candidate".
//
// Template survival
// -----------------
// Clang attaches `AnnotateAttr` to the `FunctionDecl` at the point of
// parse, and every instantiation (implicit or explicit) clones the
// attribute list from the primary template's decl. `specific_attrs<>`
// therefore finds the marker on:
//
//   1. The primary template's `FunctionTemplateDecl`'s
//      `getTemplatedDecl()` (accessible as an ordinary `FunctionDecl*`).
//   2. Every `FunctionDecl` produced by template instantiation.
//   3. Any explicit specialisation (`template<> void foo<int>() {}`).
//
// All three carry the attribute, so `is_reversible(fd)` answers the
// predicate uniformly regardless of which shape Clang hands us.
//
// Safety
// ------
// `FunctionDecl::specific_attrs<AnnotateAttr>()` is defined on the
// `Decl` base class (DeclBase.h:542). It tolerates decls with no
// attributes at all (empty range). Null decls are guarded by an early
// return; every other shape is safe to iterate.

#include "reversible_attribute.hpp"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/StringRef.h"

namespace sturm::transpile {

bool is_reversible(const clang::FunctionDecl* fd) {
    // Contract: nullptr returns false. The matcher layer may hand a
    // null decl when a CallExpr's callee fails to resolve; we must not
    // treat that as an implicit reversibility vote.
    if (fd == nullptr) {
        return false;
    }

    // Walk every `AnnotateAttr` attached to the decl. `specific_attrs<>`
    // returns an iterator range; tolerates a decl with no attributes at
    // all (empty range, loop body never runs).
    //
    // We do NOT stop at the first `AnnotateAttr` whose text is NOT our
    // marker — a routine can carry several unrelated annotations (plugin
    // contracts, third-party tooling, `[[clang::annotate("foo")]]` from
    // upstream code generators) and only one of them has to match.
    for (const auto* attr : fd->specific_attrs<clang::AnnotateAttr>()) {
        // `getAnnotation()` returns a `llvm::StringRef` view into the
        // AST's string arena. Byte-exact equality against our marker:
        // typos (`sturm::reversibel`), alternative capitalisations
        // (`sturm::Reversible`), and trailing whitespace all fail.
        //
        // Compare via `StringRef::equals(StringRef)` (length + memcmp,
        // no allocation, no case folding, no Unicode normalisation).
        // The `kReversibleAttrAnnotation` constant is a
        // `std::string_view`; StringRef's constructor accepts a pointer
        // + length, so we feed it the view's raw pieces to disambiguate
        // from the reversed-argument `operator==(string_view,
        // basic_string_view)` GCC 15 makes visible under `-std=c++20`.
        if (attr != nullptr &&
            attr->getAnnotation().equals(llvm::StringRef(
                kReversibleAttrAnnotation.data(),
                kReversibleAttrAnnotation.size()))) {
            return true;
        }
    }

    // No matching AnnotateAttr found. This includes:
    //   - Plain functions with no attributes at all.
    //   - Functions annotated only with unrelated `AnnotateAttr`
    //     strings (other plugins, user-defined markers).
    //   - Functions marked with typos of our marker.
    //   - Functions written with the raw `[[sturm::reversible]]` form
    //     that Clang drops with a -Wunknown-attributes warning: the
    //     attribute never lands in the AST, so the iteration is
    //     empty and we correctly return false. Users who want synthesis
    //     must write `[[clang::annotate("sturm::reversible")]]` (the
    //     canonical carrier for our namespace until a first-class
    //     attribute is added).
    return false;
}

} // namespace sturm::transpile
