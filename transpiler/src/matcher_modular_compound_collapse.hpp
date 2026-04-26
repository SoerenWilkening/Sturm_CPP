// matcher_modular_compound_collapse.hpp — sturm-qzab.3 (Phase 5
// beat 5.3) compound peephole-collapsed AddMod arm registration shim.
//
// Plan §7.1 LoC-budget split: the per-pattern callback + AST helpers
// live in `matcher_modular_compound_collapse.cpp`; this header exposes
// only the registration helper consumed by `register_modular_op_matcher`
// in the orchestrating `matcher_modular_op.cpp`. The shared
// `ModularOpHit` struct + `ModularOpKind` enum live in
// `matcher_modular_op.hpp`; both this TU and the orchestrator include
// that header.

#ifndef STURM_TRANSPILE_MATCHER_MODULAR_COMPOUND_COLLAPSE_HPP
#define STURM_TRANSPILE_MATCHER_MODULAR_COMPOUND_COLLAPSE_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <vector>

namespace sturm::transpile {

struct ModularOpHit;

/// Register the beat-5.3 compound peephole-collapsed AddMod arm
/// against `finder`. Hits land in the same caller-owned vector the
/// in-initializer AddMod / MulMod arms (beats 5.1 / 5.2) populate;
/// `kind` on each hit is `ModularOpKind::AddMod`, distinguished from
/// the in-initializer AddMod hits by the non-null `mod_assign_call`
/// slot. Idempotent — callers may register the arm at most once per
/// hits vector.
void register_compound_collapse_addmod_arm(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<ModularOpHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_MODULAR_COMPOUND_COLLAPSE_HPP
