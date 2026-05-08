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

} // namespace

void emit_main_lifecycle_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<MainLifecycleHit>& hits,
    std::vector<QReplacement>& replacements) {
    if (hits.empty()) return;
    (void)sm;
    (void)lang;
    for (const auto& hit : hits) {
        if (hit.main_fn == nullptr) continue;
        const Stmt* body = hit.main_fn->getBody();
        if (body == nullptr) continue;
        const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
        if (compound == nullptr) continue;

        const SourceLocation lbrace = compound->getLBracLoc();
        const SourceLocation rbrace = compound->getRBracLoc();
        if (lbrace.isInvalid() || rbrace.isInvalid()) continue;

        // Two surgical replacements instead of one whole-body
        // replacement so other matchers' QReplacement entries that
        // target type tokens / sub-statements inside the body
        // (e.g. `matcher_qint_alias_subst` rewriting `qint x;` to
        // `qint_t<W> x;`, or `matcher_qram_subscript` rewriting
        // `qint b = a[i];` to a `QRAM_read` call) compose with this
        // emitter's prologue / epilogue insertions instead of being
        // clobbered. Replacing the whole `{` … `}` body with a
        // lexer-recovered string of the original text would lose
        // those alias-subst / QRAM rewrites because the lexer reads
        // the unmodified source — see sturm-yggr / Phase 8 for the
        // failure mode the previous whole-body replace produced
        // (qram_demo emitted Rz stubs from the un-substituted alias
        // operator bodies instead of the proper QRAM gate stream).
        //
        // (1) Replace the open-brace `{` with the prologue text:
        //     `{` plus the two backend-create lines plus the IIFE
        //     opening `int __sturm_rc = ([&]() -> int {`. The
        //     replacement re-emits the open-brace so the user body
        //     remains lexically inside a CompoundStmt.
        QReplacement open;
        open.range = SourceRange(lbrace, lbrace);
        open.replacement =
            "{\n"
            "    sturm_backend_context_t* __sturm_ctx = "
            "sturm_backend_create(STURM_MODE_DEFAULT);\n"
            "    sturm_set_thread_context(__sturm_ctx);\n"
            "    int __sturm_rc = ([&]() -> int {";
        replacements.push_back(std::move(open));

        // (2) Replace the close-brace `}` with the epilogue text:
        //     IIFE closing `})()` plus the teardown lines plus the
        //     `return __sturm_rc;` plus the final `}` of `main`.
        QReplacement close;
        close.range = SourceRange(rbrace, rbrace);
        close.replacement =
            "})();\n"
            "    sturm_set_thread_context(nullptr);\n"
            "    sturm_backend_destroy(__sturm_ctx);\n"
            "    return __sturm_rc;\n"
            "}";
        replacements.push_back(std::move(close));
    }
}

} // namespace sturm::transpile
