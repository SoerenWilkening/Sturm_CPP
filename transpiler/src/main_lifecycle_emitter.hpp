// main_lifecycle_emitter.hpp — Frontend simpl. P7 (sturm-e3ru) emitter
// surface.
//
// Plan §3 Phase 7 split target. Companion to
// `matcher_main_lifecycle.{hpp,cpp}`. The matcher publishes one
// `MainLifecycleHit` per matched main; this emitter turns each hit
// into a single `QReplacement` over the body's CompoundStmt source
// range (everything between `{` and `}` inclusive).
//
// Per-hit transformation (PRD §5.4 listing):
//
//     int main(/* user-args */) {
//         /* user-body */
//     }
//
//   →
//
//     int main(/* user-args */) {
//         sturm_backend_context_t* __sturm_ctx =
//             sturm_backend_create(STURM_MODE_DEFAULT);
//         sturm_set_thread_context(__sturm_ctx);
//         int __sturm_rc = ([&]() -> int {
//             /* user-body */
//         })();
//         sturm_set_thread_context(nullptr);
//         sturm_backend_destroy(__sturm_ctx);
//         return __sturm_rc;
//     }
//
// The emitter rewrites only the body's source range (open-brace
// through close-brace, inclusive). The function declarator (return
// type, name, parameter list) is left untouched — `argc` / `argv` /
// `envp` are visible to the IIFE through the `[&]` capture clause
// because the lambda lexically nests inside `main`'s body and C++'s
// reference-capture semantics make all enclosing automatic variables
// visible by reference.
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
