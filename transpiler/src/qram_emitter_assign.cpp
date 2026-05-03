// qram_emitter_assign.cpp -- sturm-u9ge.6 (Beat H1) implementation.
//
// See `qram_emitter_assign.hpp` for the contract; PRD §9 row 1 for the
// "uncompute of old `b` before QRAM writes" wording H1 implements;
// PRD §11.1.5 for the per-shape `QRAM_read` call line table the
// emitter mirrors.
//
// One coordinated rewrite per hit (header §): replace the assignment
// expression `b = a[i]` with a two-statement sequence
// `::sturm::__QRAM_target_uncompute(b); ::sturm::QRAM_read(a, [n,] i, b)`
// using `Rewriter::ReplaceText` on the op-call's source range. The
// trailing `;` that originally terminated `b = a[i];` remains
// untouched and acts as the terminator for the final forward call,
// matching the v1 D2 emitter's posture (begin-loc => trailing-`;`
// inclusive replacement is intentionally split here so the user's
// trailing comment after the `;` survives).
//
// Defensive posture: a hit with a null `target_expr` /
// `container_expr` / `index_expr` yields zero Rewriter mutations.

#include "qram_emitter_assign.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringRef.h"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace {

using clang::CharSourceRange;
using clang::LangOptions;
using clang::Lexer;
using clang::Rewriter;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;

// -- Source-text recovery helpers (mirror qram_emitter.cpp / _expr) ------
std::string source_text_of(SourceRange range,
                           const SourceManager& sm,
                           const LangOptions& lang) {
    if (range.isInvalid()) return {};
    auto char_range = CharSourceRange::getTokenRange(range);
    auto text = Lexer::getSourceText(char_range, sm, lang);
    return text.str();
}

std::string expr_source_text(const clang::Expr* e,
                             const SourceManager& sm,
                             const LangOptions& lang) {
    if (!e) return {};
    return source_text_of(e->IgnoreParens()->getSourceRange(), sm, lang);
}

// -- Pure-string helpers -------------------------------------------------
std::string ns_prefix(unsigned W) {
    return W == 0 ? std::string("") : std::string("::sturm::");
}

std::string render_uncompute_call(unsigned W,
                                  std::string_view target_text) {
    std::ostringstream os;
    os << ns_prefix(W) << "__QRAM_target_uncompute(" << target_text << ")";
    return os.str();
}

std::string render_forward_call(unsigned W,
                                QramContainerKind kind,
                                std::string_view container_text,
                                std::string_view index_text,
                                std::string_view length_text,
                                std::string_view target_text) {
    std::ostringstream os;
    os << ns_prefix(W) << "QRAM_read(" << container_text;
    if (kind == QramContainerKind::Pointer) {
        os << ", "
           << (length_text.empty() ? "/* qram-pointer-length-missing */"
                                   : length_text);
    }
    os << ", " << index_text << ", " << target_text << ")";
    return os.str();
}

} // anonymous namespace

// -- Public surface: pure-string emission --------------------------------

QramAssignEmission emit_qram_assign_text(QramContainerKind kind,
                                         std::string_view target_text,
                                         std::string_view container_text,
                                         std::string_view index_text,
                                         std::string_view length_text,
                                         unsigned W) {
    QramAssignEmission em;
    em.kind = kind;
    if (target_text.empty() || container_text.empty() ||
        index_text.empty()) {
        return em;
    }
    std::ostringstream os;
    // Two-statement sequence: uncompute of old target, then forward.
    // The trailing `;` of the SECOND call is intentionally omitted
    // here -- the caller's existing `;` after the assignment supplies
    // that terminator. The FIRST statement's trailing `;` IS emitted
    // since it has no pre-existing terminator in source.
    os << render_uncompute_call(W, target_text) << "; "
       << render_forward_call(W, kind, container_text, index_text,
                              length_text, target_text);
    em.text = os.str();
    return em;
}

// -- Public surface: AST-driven rewrites ---------------------------------

void emit_qram_assign_rewrites(
    Rewriter& rw,
    const std::vector<QramSubscriptAssignHit>& hits) {
    if (hits.empty()) return;
    const SourceManager& sm = rw.getSourceMgr();
    const LangOptions& lang = rw.getLangOpts();

    for (const auto& hit : hits) {
        if (hit.assign_expr   == nullptr ||
            hit.target_expr   == nullptr ||
            hit.container_expr == nullptr ||
            hit.index_expr     == nullptr) {
            continue;
        }

        // Recover verbatim source text for `b`, `a`, and `i`.
        const std::string target_text =
            expr_source_text(hit.target_expr, sm, lang);
        const std::string container_text =
            expr_source_text(hit.container_expr, sm, lang);
        const std::string index_text =
            expr_source_text(hit.index_expr, sm, lang);
        if (target_text.empty() || container_text.empty() ||
            index_text.empty()) continue;

        QramAssignEmission em = emit_qram_assign_text(
            hit.kind, target_text, container_text, index_text,
            hit.length_text, hit.W);
        if (em.text.empty()) continue;

        const SourceRange assign_range =
            hit.assign_expr->getSourceRange();
        if (assign_range.isInvalid()) continue;

        // ReplaceText on the op-call's source range. The user's
        // trailing `;` after `b = a[i];` remains untouched and acts
        // as the terminator for the planted forward call.
        (void)rw.ReplaceText(assign_range, em.text);
    }
}

} // namespace sturm::transpile
