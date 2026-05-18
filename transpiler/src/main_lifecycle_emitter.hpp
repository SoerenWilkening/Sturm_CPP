// main_lifecycle_emitter.hpp — Frontend simpl. P7 (sturm-e3ru) +
// sturm-0tcv (entry-point attribute extension) emitter surface.
//
// Plan §3 Phase 7 split target. Companion to
// `matcher_main_lifecycle.{hpp,cpp}`. The matcher publishes one
// `MainLifecycleHit` per matched entry-point function (the unique
// `int main(...)` of any TU OR any FunctionDecl carrying
// `[[sturm::entry_point]]`); this emitter turns each hit into
// per-hit `QReplacement`s over the body's CompoundStmt source range
// (everything between `{` and `}` inclusive).
//
// Per-hit transformation depends on `MainLifecycleHit::kind`:
//
//   * `Main` and `EntryPointReturn` — capture + return shape:
//
//         <ret-type> fn(/* user-args */) {
//             /* user-body */
//         }
//
//       →
//
//         <ret-type> fn(/* user-args */) {
//             sturm_backend_context_t* __sturm_ctx =
//                 sturm_backend_create(STURM_MODE_DEFAULT);
//             sturm_set_thread_context(__sturm_ctx);
//             <ret-type> __sturm_rc = ([&]() -> <ret-type> {
//                 /* user-body */
//             })();
//             sturm_set_thread_context(nullptr);
//             sturm_backend_destroy(__sturm_ctx);
//             return __sturm_rc;
//         }
//
//     For `Main` the return type is always `int` (hardcoded); for
//     `EntryPointReturn` the return type is derived from the
//     FunctionDecl via `getReturnType()` and a PrintingPolicy spell.
//
//   * `EntryPointVoid` — fire-and-forget shape (used by library
//     fixtures, GoogleTest TEST_F bodies, any non-result-producing
//     entry point):
//
//         void fn(/* user-args */) {
//             /* user-body */
//         }
//
//       →
//
//         void fn(/* user-args */) {
//             sturm_backend_context_t* __sturm_ctx =
//                 sturm_backend_create(STURM_MODE_DEFAULT);
//             sturm_set_thread_context(__sturm_ctx);
//             ([&]() -> void {
//                 /* user-body */
//             })();
//             sturm_set_thread_context(nullptr);
//             sturm_backend_destroy(__sturm_ctx);
//         }
//
// The emitter rewrites only the body's source range (open-brace
// through close-brace, inclusive). The function declarator (return
// type, name, parameter list) is left untouched — parameters
// (`argc`, `argv`, fixture state, etc.) are visible to the IIFE
// through the `[&]` capture clause because the lambda lexically
// nests inside the function's body.
//
// Empty hit vector ⇒ no-op (matches the posture of every other
// rewrite emitter in this directory).

#ifndef STURM_TRANSPILE_MAIN_LIFECYCLE_EMITTER_HPP
#define STURM_TRANSPILE_MAIN_LIFECYCLE_EMITTER_HPP

#include "matcher_main_lifecycle.hpp"
#include "sturm/transpile/qir.hpp"

#include <vector>

namespace clang {
class LangOptions;
class SourceManager;
} // namespace clang

namespace sturm::transpile {

/// Per-hit body rewrite. For each hit, the function appends one
/// `QReplacement` to `replacements`:
///
///   - `range`: the body CompoundStmt's source range (open-brace
///     loc through close-brace loc, inclusive).
///   - `replacement`: the IIFE-form text shown in the file header
///     comment, with the original body content (everything between
///     `{` and `}` exclusive of the braces themselves) substituted
///     verbatim into the lambda's body.
///
/// The function is a no-op when `hits` is empty, when a hit's
/// FunctionDecl is null, when the body source range is invalid, or
/// when the body's source text cannot be recovered from the
/// SourceManager. Each of those cases logs no diagnostic — the
/// matcher is the authoritative gate for "this main is rewriteable",
/// and degenerate hits are silently dropped (parity with
/// `emit_qram_replacements` in `qram_emitter.cpp`).
void emit_main_lifecycle_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<MainLifecycleHit>& hits,
    std::vector<QReplacement>& replacements);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MAIN_LIFECYCLE_EMITTER_HPP
