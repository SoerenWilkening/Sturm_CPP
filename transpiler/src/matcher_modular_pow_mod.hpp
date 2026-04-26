// matcher_modular_pow_mod.hpp — sturm-qzab.5 (Phase 5 beat 5.5) PowMod
// arm registration shim, gated on the `STURM_MODULAR_POW` preprocessor
// define.
//
// Plan §7.1 LoC-budget split: the per-pattern callback + AST pattern
// builder live in `matcher_modular_pow_mod.cpp`; this header exposes
// only the registration helper consumed by `register_modular_op_matcher`
// in the orchestrating `matcher_modular_op.cpp`. The shared
// `ModularOpHit` struct + `ModularOpKind` enum live in
// `matcher_modular_op.hpp`; both this TU and the orchestrator include
// that header.
//
// The matched AST shape is `qint_t<W> r = sturm::pow(a, x) % n;` — a
// VarDecl whose initializer is the qint-overloaded `operator%` whose
// LHS is a `CallExpr` to a function template named `pow` returning a
// `qint_t<W>`. Beat 5.6 (sturm-qzab.6) extends the same TU with the
// `int64_t`-exponent `pow` overload variant; the registration helper
// signature stays stable across both beats.
//
// Gating posture: this header is a no-op include when
// `STURM_MODULAR_POW` is not defined — the registration helper is
// emitted only inside `#ifdef STURM_MODULAR_POW`, so an OFF-mode build
// links a stub TU that does NOT export `register_pow_mod_arm`. The
// orchestrator in `matcher_modular_op.cpp` calls it only inside its
// own `#ifdef STURM_MODULAR_POW` block, so the symbol is requested
// only when it is provided.

#ifndef STURM_TRANSPILE_MATCHER_MODULAR_POW_MOD_HPP
#define STURM_TRANSPILE_MATCHER_MODULAR_POW_MOD_HPP

#ifdef STURM_MODULAR_POW

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <vector>

namespace sturm::transpile {

struct ModularOpHit;

/// Register the beat-5.5 PowMod arm against `finder`. Hits land in
/// the same caller-owned vector the AddMod / MulMod / compound-collapse
/// arms populate; `kind` on each hit is `ModularOpKind::PowMod`,
/// distinguished by the non-null `pow_call` slot (and the null
/// `inner_op_expr` slot — PowMod's inner node is a CallExpr, not a
/// CXXOperatorCallExpr). Idempotent — callers may register the arm at
/// most once per hits vector.
void register_pow_mod_arm(clang::ast_matchers::MatchFinder& finder,
                          std::vector<ModularOpHit>& hits);

} // namespace sturm::transpile

#endif // STURM_MODULAR_POW

#endif // STURM_TRANSPILE_MATCHER_MODULAR_POW_MOD_HPP
