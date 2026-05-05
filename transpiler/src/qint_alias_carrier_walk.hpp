// qint_alias_carrier_walk.hpp -- sturm-7t85.1 (Beat G1) helper.
//
// Plan §19b / PRD §9.3.1. The carrier walk lives next to its sole
// caller (`matcher_qint_alias_subst.cpp`). It exists as its own TU
// because folding it back into the matcher pushes that file past its
// 300-LoC budget (plan §19b: "If the budget is tight, lift the helper
// into a sibling `qint_alias_carrier_walk.{hpp,cpp}` ≤ 100 LoC").
//
// What it does
// ------------
// `declarator_typeloc_range(dd)` returns the source range of a
// DeclaratorDecl's type-spelling, walking one level into
// `ArrayTypeLoc::getElementLoc()` or `PointerTypeLoc::getPointeeLoc()`
// when the outer carrier is an array or pointer. The walk is
// deliberately single-level — multi-dim arrays and multi-level
// pointers stay out of scope in v1 (R6, plan §19b; bd
// `sturm-7t85.6` follow-up). On hitting a `TypedefTypeLoc` the
// helper DECLINES (returns invalid `SourceRange`) so user typedefs
// like `using QArr = qint[4];` round-trip byte-identical (R5).

#ifndef STURM_TRANSPILE_QINT_ALIAS_CARRIER_WALK_HPP
#define STURM_TRANSPILE_QINT_ALIAS_CARRIER_WALK_HPP

#include "clang/Basic/SourceLocation.h"

namespace clang { class DeclaratorDecl; }

namespace sturm::transpile {

// Returns the TypeLoc source range of `dd`, single-level-walked into
// any array element / pointer pointee. Returns an invalid SourceRange
// on null input, missing TypeSourceInfo, or a TypedefTypeLoc on the
// inner walked TypeLoc.
clang::SourceRange declarator_typeloc_range(const clang::DeclaratorDecl* dd);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QINT_ALIAS_CARRIER_WALK_HPP
