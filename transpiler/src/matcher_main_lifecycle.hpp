// matcher_main_lifecycle.hpp — Frontend simpl. P7 (sturm-e3ru) public surface.
//
// Plan §3 Phase 7, PRD §5.4 / G4. The matcher fires on two kinds of
// FunctionDecl in any TU whose preprocessor state shows the umbrella
// sentinel `STURM_UMBRELLA_INCLUDED` defined and
// `STURM_NO_AUTO_LIFECYCLE` undefined:
//
//   (a) The unique `int main(...)` FunctionDecl — original P7 path.
//
//   (b) Any FunctionDecl carrying the `[[sturm::entry_point]]`
//       attribute (sturm-0tcv extension — see PRD §5.4 amendment and
//       `entry_point_attribute.hpp`). Library-level fixtures,
//       GoogleTest TEST_F bodies, and other non-`main` top-level
//       entry points can opt into the same auto-injection by writing
//       `[[clang::annotate("sturm::entry_point")]]` (canonical) or
//       `[[sturm::entry_point]]` (plugin-aware short form) on the
//       FunctionDecl declaration.
//
// One `MainLifecycleHit` per matched FunctionDecl; the consumer
// drains the hit vector after `matchAST` returns and converts each
// hit into the appropriate `QReplacement` rewrite via
// `emit_main_lifecycle_replacements`. The emitter chooses the IIFE
// shape based on the hit's `kind` field:
//   - `Main` and `EntryPointReturn`: capture the IIFE result and
//     return it (e.g. `return __sturm_rc;`).
//   - `EntryPointVoid`: invoke the IIFE for side effects and fall
//     through (no `return` after the teardown calls).
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
// Form-of-entry-point coverage (sturm-0tcv):
//   * `void <fn>(<any-params>)` — falls into the EntryPointVoid arm,
//     the IIFE returns void and the teardown completes without a
//     return statement.
//   * `<integral> <fn>(<any-params>)` (including `int`, `int64_t`,
//     `bool`, etc.) — falls into the EntryPointReturn arm, the IIFE
//     captures the result and the teardown ends with
//     `return __sturm_rc;`.
//
//   Non-integral return types (pointer / class / float) are OUT OF
//   SCOPE for this pass — the matcher rejects them at hit time
//   (with a diagnostic) so the rewrite stays safe. Users with
//   exotic return types must fall back to the explicit lifecycle.
//
//   CXXMethodDecl subjects (member functions, static or otherwise)
//   are OUT OF SCOPE: the entry-point auto-injection covers
//   free-function entry points only. Member functions on classes
//   carry an implicit `this` parameter that complicates the IIFE
//   wrapping (the `[&]` capture would not transparently see member
//   names). PRD §3 non-goal "Test-framework auto-injection" is
//   relaxed for GoogleTest TEST_F bodies, which expand to a
//   CXXMethodDecl on a generated fixture class; this is handled
//   separately — see the `is_eligible_entry_point` callback gating
//   below.
//
// Skip conditions (matcher-level — emitter is a pure transform on a
// hit):
//   * `STURM_NO_AUTO_LIFECYCLE` defined → no hit produced (escape
//     hatch for tests / libraries / didactic examples).
//   * `STURM_UMBRELLA_INCLUDED` undefined → no hit (sentinel skip;
//     the user's TU did not transitively include `sturm.h`).
//   * `main`'s first compound-stmt child contains a VarDecl named
//     `__sturm_ctx` → idempotency probe (already rewritten by a
//     prior pass) → no hit. The same probe gates entry-point hits.
//   * Source file's filename contains `gtest` AND the candidate is
//     `int main(...)` → R3 mitigation (the gtest framework provides
//     its own `main` and the transpiler's auto-injection would
//     collide with it). Entry-point hits are NOT skipped by the
//     gtest filename probe — that is precisely the use case the
//     sturm-0tcv attribute targets (a GoogleTest TEST_F body inside
//     a `gtest`-named source file is the canonical example).
//
// Matcher-only contract:
//   * Two bound names: `main_fn` for the `isMain()` anchor's
//     FunctionDecl, `entry_point_fn` for the attribute anchor's
//     FunctionDecl. Only one binding is present per callback run.
//   * `MainLifecycleHit` is plain old data — no AST-bound pointers
//     beyond the FunctionDecl itself, which the consumer drains
//     before `synthesize()` so the AST stays alive.
//
// LoC budget: ≤ 120 (header). Plan budget under sturm-0tcv: extended
// from ≤ 60 to absorb the entry-point doc surface.

#ifndef STURM_TRANSPILE_MATCHER_MAIN_LIFECYCLE_HPP
#define STURM_TRANSPILE_MATCHER_MAIN_LIFECYCLE_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <vector>

namespace clang {
class FunctionDecl;
class Preprocessor;
} // namespace clang

namespace sturm::transpile {

/// Per-hit shape selector. Drives the emitter's choice of IIFE
/// wrapping in `emit_main_lifecycle_replacements`.
enum class MainLifecycleHitKind {
    /// `int main(...)` — the original PRD §5.4 P7 path. The emitter
    /// wraps with `int __sturm_rc = ([&]() -> int { ... })();` and
    /// emits `return __sturm_rc;` after teardown.
    Main = 0,

    /// `[[sturm::entry_point]]` on a non-void-returning FunctionDecl.
    /// The emitter mirrors the Main shape: capture the IIFE result
    /// and return it from the outer function. The return-type spelled
    /// in the IIFE lambda's trailing return is the same as the
    /// FunctionDecl's return type so implicit conversions stay
    /// out of the picture.
    EntryPointReturn = 1,

    /// `[[sturm::entry_point]]` on a void-returning FunctionDecl
    /// (the canonical library-fixture / TEST_F shape). The emitter
    /// wraps with `([&]() -> void { ... })();` and emits no
    /// trailing return statement after teardown — the outer
    /// function falls through to its implicit void return.
    EntryPointVoid = 2,
};

/// One matched FunctionDecl site for auto-lifecycle injection. The
/// consumer rewrites the body of `fn` into the PRD §5.4 IIFE form,
/// with the wrapping shape selected by `kind`. Non-owning pointer;
/// the FunctionDecl is valid only for the MatchFinder's ASTContext
/// lifetime (drained before `synthesize()` returns).
struct MainLifecycleHit {
    const clang::FunctionDecl* main_fn = nullptr;
    MainLifecycleHitKind       kind    = MainLifecycleHitKind::Main;
};

/// Register the main_lifecycle matcher against `finder`. The matcher
/// callback consults `pp` at hit time to gate on the
/// `STURM_NO_AUTO_LIFECYCLE` / `STURM_UMBRELLA_INCLUDED` macros, so
/// `pp` must outlive the finder's run. Hits land in `hits`, which
/// also must outlive the finder's run. At most one hit is published
/// per FunctionDecl (de-duplicated across multiple callback runs).
void register_main_lifecycle_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::Preprocessor& pp,
    std::vector<MainLifecycleHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_MAIN_LIFECYCLE_HPP
