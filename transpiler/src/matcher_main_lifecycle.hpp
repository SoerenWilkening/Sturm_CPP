// matcher_main_lifecycle.hpp — Frontend simpl. P7 (sturm-e3ru) public surface.
//
// Plan §3 Phase 7, PRD §5.4 / G4. The matcher fires on the unique
// `int main(...)` `FunctionDecl` in any TU whose preprocessor state
// shows the umbrella sentinel `STURM_UMBRELLA_INCLUDED` defined and
// `STURM_NO_AUTO_LIFECYCLE` undefined. It produces ONE
// `MainLifecycleHit` per matched main; the consumer drains the hit
// vector after `matchAST` returns and converts the hit into a single
// `QReplacement` that rewrites main's body into the IIFE form
// (PRD §5.4 listing).
//
// Form-of-main coverage (PRD §5.4):
//   * `int main()`                       — no parameters
//   * `int main(int, char**)`            — argc/argv (KR-style
//                                          `char* argv[]` decays to
//                                          `char**` in the AST and is
//                                          therefore covered by the
//                                          same anchor)
//   * `int main(int, char**, char**)`    — argc/argv/envp
//
// The GCC trailing-return form `auto main() -> int` is OUT OF SCOPE
// per spec (sturm-e3ru notes: "GCC trailing-return form is OUT OF
// SCOPE per spec; flag in fixture comments"). The matcher's anchor
// requires `getReturnType()` to be a non-deduced `int`.
//
// Skip conditions (matcher-level — emitter is a pure transform on a
// hit):
//   * `STURM_NO_AUTO_LIFECYCLE` defined → no hit produced (escape
//     hatch for tests / libraries / didactic examples).
//   * `STURM_UMBRELLA_INCLUDED` undefined → no hit (sentinel skip;
//     the user's TU did not transitively include `sturm.h`).
//   * `main`'s first compound-stmt child contains a VarDecl named
//     `__sturm_ctx` → idempotency probe (already rewritten by a
//     prior pass) → no hit.
//   * Source file's filename contains `gtest` → R3 mitigation (the
//     gtest framework provides its own `main` and the transpiler's
//     auto-injection would collide with it).
//
// Matcher-only contract:
//   * Single bound name `main_fn` for the `FunctionDecl`. The
//     emitter uses the bound decl to recover the body's
//     CompoundStmt source range and the parameter list spelling.
//   * `MainLifecycleHit` is plain old data — no AST-bound pointers
//     beyond the FunctionDecl itself, which the consumer drains
//     before `synthesize()` so the AST stays alive.
//
// LoC budget: ≤ 60 (plan §3 Phase 7 / matcher_main_lifecycle.hpp row).

#ifndef STURM_TRANSPILE_MATCHER_MAIN_LIFECYCLE_HPP
#define STURM_TRANSPILE_MATCHER_MAIN_LIFECYCLE_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <vector>

namespace clang {
class FunctionDecl;
class Preprocessor;
} // namespace clang

namespace sturm::transpile {

/// One matched `int main(...)` site. The consumer rewrites the body
/// of `main_fn` into the PRD §5.4 IIFE form. Non-owning pointer; the
/// FunctionDecl is valid only for the MatchFinder's ASTContext
/// lifetime (drained before `synthesize()` returns).
struct MainLifecycleHit {
    const clang::FunctionDecl* main_fn = nullptr;
};

/// Register the main_lifecycle matcher against `finder`. The matcher
/// callback consults `pp` at hit time to gate on the
/// `STURM_NO_AUTO_LIFECYCLE` / `STURM_UMBRELLA_INCLUDED` macros, so
/// `pp` must outlive the finder's run. Hits land in `hits`, which
/// also must outlive the finder's run. At most one hit is published
/// per TU (the unique `main` FunctionDecl).
void register_main_lifecycle_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::Preprocessor& pp,
    std::vector<MainLifecycleHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_MAIN_LIFECYCLE_HPP
