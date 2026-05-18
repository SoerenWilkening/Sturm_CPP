// main_lifecycle_emitter.cpp — Frontend simpl. P7 (sturm-e3ru) +
// sturm-0tcv (entry-point attribute extension) emitter implementation.
//
// Plan §3 Phase 7. See `main_lifecycle_emitter.hpp` for the per-hit
// rewrite contract.
//
// Implementation strategy
// -----------------------
// The hit's FunctionDecl points at an entry function (`main` or one
// carrying `[[sturm::entry_point]]`) with a CompoundStmt body. We
// use the CompoundStmt's brace locations to recover (a) the body-
// content source range — everything between `{` and `}` exclusive —
// and (b) the full body source range (`{` loc through `}` loc
// inclusive) which becomes the `QReplacement::range`.
//
// The replacement text is assembled per-hit based on the hit's
// `kind`:
//
//   - Main / EntryPointReturn: capture the IIFE result into an
//     `int __sturm_rc` and emit `return __sturm_rc;` after the
//     teardown. The IIFE's trailing return type is the same as the
//     outer FunctionDecl's return type so implicit conversions stay
//     out of the picture (for `Main` this is `int`; for
//     `EntryPointReturn` this is whatever integral type the user
//     declared).
//
//   - EntryPointVoid: invoke the IIFE for side effects with a
//     `void` trailing return type, and emit no return statement
//     after the teardown — the outer function falls through to
//     its implicit void return.
//
// Two surgical replacements instead of one whole-body replacement
// so other matchers' QReplacement entries that target type tokens
// inside the body compose with this emitter's prologue / epilogue
// insertions (see the in-line comment below the open-brace
// replacement for the rationale).

#include "main_lifecycle_emitter.hpp"

#include "sturm/transpile/qir.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
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

// Produce a printable text spelling of a FunctionDecl's return type
// (used as the IIFE's trailing-return-type for entry-point hits).
// We use the canonical type's `getAsString()` over a PrintingPolicy
// so spelling stays in canonical form (no typedef expansions issues).
//
// For the `Main` case the type is always `int` so we hardcode rather
// than re-derive — the legacy P7 code path emitted the literal "int"
// trailing return type.
std::string spell_return_type(const FunctionDecl& fn,
                              const LangOptions& lang) {
    const QualType rt = fn.getReturnType();
    if (rt.isNull()) return "int";
    PrintingPolicy policy(lang);
    policy.SuppressTagKeyword = true;
    policy.SuppressUnwrittenScope = true;
    // Use desugared spelling so platform-specific typedefs
    // (`int64_t`, `uint32_t`) render as canonical types (or stay
    // typedef-spelled if Clang's printer chooses).
    return rt.getAsString(policy);
}

} // namespace

void emit_main_lifecycle_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<MainLifecycleHit>& hits,
    std::vector<QReplacement>& replacements) {
    if (hits.empty()) return;
    (void)sm;
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

        switch (hit.kind) {
        case MainLifecycleHitKind::Main:
        case MainLifecycleHitKind::EntryPointReturn: {
            // Capture + return shape. The IIFE's trailing return
            // type spells the outer function's return type so the
            // outer `return __sturm_rc;` stays well-typed under
            // implicit conversions on the IIFE invocation site
            // (e.g. `int` capture + `int` outer return = no
            // conversion needed; `int64_t` outer return + `int`
            // capture would need `static_cast<int64_t>`, but we
            // emit the trailing type to match the outer so the
            // capture variable's type is also `int64_t`).
            const std::string ret_type =
                (hit.kind == MainLifecycleHitKind::Main)
                    ? "int"
                    : spell_return_type(*hit.main_fn, lang);

            // (1) Replace `{` with prologue + IIFE opener.
            QReplacement open;
            open.range = SourceRange(lbrace, lbrace);
            open.replacement =
                "{\n"
                "    sturm_backend_context_t* __sturm_ctx = "
                "sturm_backend_create(STURM_MODE_DEFAULT);\n"
                "    sturm_set_thread_context(__sturm_ctx);\n"
                "    " + ret_type + " __sturm_rc = ([&]() -> " +
                ret_type + " {";
            replacements.push_back(std::move(open));

            // (2) Replace `}` with IIFE closer + teardown + return.
            QReplacement close;
            close.range = SourceRange(rbrace, rbrace);
            close.replacement =
                "})();\n"
                "    sturm_set_thread_context(nullptr);\n"
                "    sturm_backend_destroy(__sturm_ctx);\n"
                "    return __sturm_rc;\n"
                "}";
            replacements.push_back(std::move(close));
            break;
        }
        case MainLifecycleHitKind::EntryPointVoid: {
            // Void-return shape. The IIFE has a `-> void` trailing
            // return type, no result is captured, and the outer
            // teardown emits no return statement — the outer
            // function falls through to its implicit void return.
            //
            // (1) Replace `{` with prologue + IIFE opener.
            QReplacement open;
            open.range = SourceRange(lbrace, lbrace);
            open.replacement =
                "{\n"
                "    sturm_backend_context_t* __sturm_ctx = "
                "sturm_backend_create(STURM_MODE_DEFAULT);\n"
                "    sturm_set_thread_context(__sturm_ctx);\n"
                "    ([&]() -> void {";
            replacements.push_back(std::move(open));

            // (2) Replace `}` with IIFE closer + teardown.
            //     No `return` — the outer function is void.
            QReplacement close;
            close.range = SourceRange(rbrace, rbrace);
            close.replacement =
                "})();\n"
                "    sturm_set_thread_context(nullptr);\n"
                "    sturm_backend_destroy(__sturm_ctx);\n"
                "}";
            replacements.push_back(std::move(close));
            break;
        }
        }
    }
}

} // namespace sturm::transpile
