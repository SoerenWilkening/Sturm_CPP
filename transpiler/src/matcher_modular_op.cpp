// matcher_modular_op.cpp — sturm-r5pn.4 (Phase 0.4) implementation.
// SHELL: registers NO patterns. See matcher_modular_op.hpp for the
// scaffold rationale and Phase 5's plan §7 contract.
//
// The function body below intentionally references neither `finder` nor
// `hits` beyond the no-op `(void)` casts that suppress unused-parameter
// warnings — Phase 5 (the per-pattern register_one calls modeled on
// `matcher_lossy_op.cpp`) replaces those casts with the actual matcher
// pool registration. Keeping the function defined now ensures the
// linker resolves `register_modular_op_matcher` at the consumer wiring
// site without an `#ifdef PHASE5_LANDED` shim.

#include "matcher_modular_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <vector>

namespace sturm::transpile {

void register_modular_op_matcher(clang::ast_matchers::MatchFinder& finder,
                                 std::vector<ModularOpHit>& hits) {
    // SHELL: no patterns registered. Phase 5 (P5
    // transpiler-modular-rewrite) replaces this body with the per-
    // pattern `register_one<...>(finder, hits, ...)` calls modeled on
    // `register_lossy_op_matcher` in `matcher_lossy_op.cpp`.
    //
    // The unused-parameter casts below mirror the pattern other
    // shell-stage matchers in the codebase use (e.g. the early stub
    // generations of `matcher_qbool_prep.cpp` before its diagnostic
    // arms landed).
    (void)finder;
    (void)hits;
}

} // namespace sturm::transpile
