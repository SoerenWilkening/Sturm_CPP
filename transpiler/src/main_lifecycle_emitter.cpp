// main_lifecycle_emitter.cpp — Frontend simpl. P7 (sturm-e3ru) emitter
// implementation.
//
// Plan §3 Phase 7. See `main_lifecycle_emitter.hpp` for the per-hit
// rewrite contract.
//
// Implementation strategy
// -----------------------
// The hit's FunctionDecl points at `int main(...)` with a
// CompoundStmt body. We use the CompoundStmt's brace locations to
// recover (a) the body-content source range — everything between
// `{` and `}` exclusive — and (b) the full body source range
// (`{` loc through `}` loc inclusive) which becomes the
// `QReplacement::range`.
//
// The replacement text is assembled in three sections:
//
//   1. Pre-IIFE prologue — `{` plus the two backend-create lines.
//   2. IIFE wrapper — the `[&]() -> int { <user-body-content> }()`
//      block, with the user body substituted verbatim from
//      Lexer-recovered source text.
//   3. Post-IIFE epilogue — `set_thread_context(nullptr)`,
//      `backend_destroy`, the `return __sturm_rc;`, and the
//      closing `}` of main.
//
// Source-text recovery uses `Lexer::getSourceText(CharSourceRange::
// getCharRange(...))` so the text excludes the brace tokens
// themselves.

#include "main_lifecycle_emitter.hpp"

#include "sturm/transpile/qir.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;

// Render the body-replacement string for one main FunctionDecl.
// Returns an empty string if any source-range recovery fails — the
// caller treats an empty result as "skip this hit".
std::string render_lifecycle_body(const FunctionDecl& fn,
                                  const SourceManager& sm,
                                  const LangOptions& lang) {
    const Stmt* body = fn.getBody();
    if (body == nullptr) return {};
    const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
    if (compound == nullptr) return {};

    const SourceLocation lbrace = compound->getLBracLoc();
    const SourceLocation rbrace = compound->getRBracLoc();
    if (lbrace.isInvalid() || rbrace.isInvalid()) return {};

    // The body-content range is the half-open character range
    // [lbrace+1, rbrace) — everything strictly between the braces.
    // `getLocForEndOfToken(lbrace)` returns the loc immediately
    // after the `{` character.
    const SourceLocation after_lbrace =
        Lexer::getLocForEndOfToken(lbrace, 0, sm, lang);
    if (after_lbrace.isInvalid()) return {};
    const CharSourceRange inner = CharSourceRange::getCharRange(
        after_lbrace, rbrace);
    bool invalid = false;
    const StringRef inner_text =
        Lexer::getSourceText(inner, sm, lang, &invalid);
    if (invalid) return {};

    // Assemble the replacement. The brace tokens themselves are part
    // of `range` (lbrace..rbrace inclusive); the replacement text
    // therefore re-emits both braces around the new body.
    //
    // Indentation note: the user-body text is captured verbatim,
    // including its leading whitespace and trailing whitespace +
    // newline. The lambda's open-brace shares a line with `[&]() -> int`
    // so the user's first body line keeps its original indent; the
    // closing `})()` lands on its own line because the captured text
    // is empirically newline-terminated (every C++ source body of
    // any non-trivial `int main()` ends with a newline before the
    // `}`). For the degenerate single-line empty body
    // `int main() {}` the captured text is the empty string and the
    // emitted lambda body is empty, which is well-formed.
    std::string out;
    out.reserve(inner_text.size() + 256);
    out.append(
        "{\n"
        "    sturm_backend_context_t* __sturm_ctx = "
        "sturm_backend_create(STURM_MODE_DEFAULT);\n"
        "    sturm_set_thread_context(__sturm_ctx);\n"
        "    int __sturm_rc = ([&]() -> int {");
    out.append(inner_text.data(), inner_text.size());
    out.append(
        "})();\n"
        "    sturm_set_thread_context(nullptr);\n"
        "    sturm_backend_destroy(__sturm_ctx);\n"
        "    return __sturm_rc;\n"
        "}");
    return out;
}

} // namespace

void emit_main_lifecycle_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<MainLifecycleHit>& hits,
    std::vector<QReplacement>& replacements) {
    if (hits.empty()) return;
    for (const auto& hit : hits) {
        if (hit.main_fn == nullptr) continue;
        const Stmt* body = hit.main_fn->getBody();
        if (body == nullptr) continue;
        const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
        if (compound == nullptr) continue;

        const SourceLocation lbrace = compound->getLBracLoc();
        const SourceLocation rbrace = compound->getRBracLoc();
        if (lbrace.isInvalid() || rbrace.isInvalid()) continue;

        std::string body_text =
            render_lifecycle_body(*hit.main_fn, sm, lang);
        if (body_text.empty()) continue;

        QReplacement rep;
        // Inclusive-on-both-ends source range covering the open and
        // close braces of main's body. The Rewriter's
        // `ReplaceText(SourceRange, ...)` interprets a SourceRange as
        // a token range, so the replacement covers `{` through `}`
        // inclusive — exactly what we want.
        rep.range = SourceRange(lbrace, rbrace);
        rep.replacement = std::move(body_text);
        replacements.push_back(std::move(rep));
    }
}

} // namespace sturm::transpile
