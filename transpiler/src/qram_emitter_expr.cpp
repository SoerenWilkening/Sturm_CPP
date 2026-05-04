// qram_emitter_expr.cpp — sturm-u9ge.9 (Beat H4) implementation.
//
// See `qram_emitter_expr.hpp` for the contract; PRD §9 row 4 for the
// "ancilla extraction + uncompute" wording H4 implements; PRD §11.1.5
// for the per-shape `QRAM_read` call line table the emitter mirrors.
//
// Three coordinated rewrites per hit (header §):
//   1. Pre-call extract     — InsertTextBefore at the user's VarDecl
//                             begin loc.
//   2. Subscript replacement — ReplaceText on the matched subscript's
//                              source range with the bare ancilla name.
//   3. Post-call uncompute  — InsertTextAfter at the trailing `;` of
//                             the user's line.
//
// The LHS type rewrite (`qint c` -> `sturm::qint_t<W> c`) is applied
// exactly once per `target_var`; multi-subscript hits sharing a target
// dedupe by VarDecl pointer so the type rewrite fires once.
//
// Defensive posture (mirrors the v1 D2 emitter): a hit with a null
// `target_var` / `subscript_expr` / `container_expr` / `index_expr`
// yields zero Rewriter mutations.

#include "qram_emitter_expr.hpp"

#include "render_qint_typename.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <set>
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
using clang::Token;
using clang::VarDecl;

// ── Source-text recovery helpers (mirror qram_emitter.cpp) ──────────────────
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

// ── Pure-string helpers ─────────────────────────────────────────────────────
// `render_qint_typename` lives in the shared `render_qint_typename.hpp`
// header (sturm-65rs.7 / Beat C0).

std::string render_call(std::string_view fn,
                        QramContainerKind kind,
                        std::string_view container_text,
                        std::string_view index_text,
                        std::string_view length_text,
                        std::string_view ancilla_name) {
    std::ostringstream os;
    os << "::sturm::" << fn << "(" << container_text;
    if (kind == QramContainerKind::Pointer) {
        os << ", "
           << (length_text.empty() ? "/* qram-pointer-length-missing */"
                                   : length_text);
    }
    os << ", " << index_text << ", " << ancilla_name << ")";
    return os.str();
}

// Locate the `;` terminating a VarDecl statement (mirror D2's
// find_var_decl_semi).
SourceLocation find_var_decl_semi(const VarDecl* var,
                                  const SourceManager& sm,
                                  const LangOptions& lang) {
    if (!var) return {};
    const SourceLocation end = var->getEndLoc();
    if (end.isInvalid()) return {};
    std::optional<Token> semi = Lexer::findNextToken(end, sm, lang);
    if (!semi || semi->getKind() != clang::tok::semi) return {};
    return semi->getLocation();
}

// Find the source location of the type-name token in a `qint <name>`
// VarDecl. Used for the LHS type rewrite. The VarDecl's begin loc
// points at the type, so we read its source-range and replace the
// portion up to (but not including) the variable name.
SourceRange type_token_range(const VarDecl* var,
                             const SourceManager& sm,
                             const LangOptions& lang) {
    if (!var) return SourceRange();
    const SourceLocation begin = var->getBeginLoc();
    const SourceLocation name_loc = var->getLocation();
    if (begin.isInvalid() || name_loc.isInvalid()) {
        return SourceRange();
    }
    // Walk back one token from the variable name to the end of the
    // type-name token. The TypeSourceInfo would give us a more
    // structured handle, but for `qint c = ...;` the type is a
    // single `qint` identifier whose end location is the prior token
    // — Lexer::findNextToken from `begin` returns the type token if
    // we lex starting at `begin`.
    std::optional<Token> type_tok = Lexer::findNextToken(
        SourceLocation::getFromRawEncoding(
            begin.getRawEncoding() == 0 ? begin.getRawEncoding()
                                        : begin.getRawEncoding() - 1),
        sm, lang);
    // Defensive: if findNextToken failed or returned a non-identifier,
    // fall back to the begin loc as a single-char range.
    if (!type_tok) return SourceRange(begin, begin);
    return SourceRange(begin, type_tok->getLocation());
}

} // anonymous namespace

// ── Public surface: pure-string emission ───────────────────────────────────

QramExprEmission emit_qram_expr_text(QramContainerKind kind,
                                     std::string_view ancilla_name,
                                     std::string_view container_text,
                                     std::string_view index_text,
                                     std::string_view length_text,
                                     unsigned W) {
    QramExprEmission em;
    em.kind = kind;
    if (ancilla_name.empty() || container_text.empty() ||
        index_text.empty()) {
        return em;
    }
    // Extract: `qint_t<W> __qram_h4_N; ::sturm::QRAM_read(a, [n,] i, ancilla);`
    {
        std::ostringstream os;
        os << render_qint_typename(W) << ' ' << ancilla_name << "; "
           << render_call("QRAM_read", kind, container_text, index_text,
                          length_text, ancilla_name)
           << ";";
        em.extract_text = os.str();
    }
    // Replace: bare ancilla name (the subscript's stand-in).
    em.replace_text = std::string(ancilla_name);
    // Adjoint: `::sturm::__QRAM_read_adj(a, [n,] i, ancilla);`
    {
        std::ostringstream os;
        os << render_call("__QRAM_read_adj", kind, container_text,
                          index_text, length_text, ancilla_name)
           << ";";
        em.adjoint_text = os.str();
    }
    return em;
}

// ── Public surface: AST-driven rewrites ────────────────────────────────────

void emit_qram_expr_rewrites(Rewriter& rw,
                             const std::vector<QramSubscriptExprHit>& hits) {
    if (hits.empty()) return;
    const SourceManager& sm = rw.getSourceMgr();
    const LangOptions& lang = rw.getLangOpts();

    // Per-VarDecl ancilla counter: `__qram_h4_0`, `__qram_h4_1`, ...
    // numbered globally across hits in the order they arrive. Local
    // counter avoids leaking allocator state between calls.
    std::size_t ancilla_counter = 0;

    // Track which VarDecls have already had their LHS type rewritten
    // — multi-subscript hits sharing a target should rewrite the LHS
    // exactly once.
    std::set<const VarDecl*> lhs_done;

    // Per-hit forward emit + per-hit adjoint queue (LIFO uncompute).
    // We append adjoints in matcher order then plant them in REVERSE
    // matcher order at the same insertion point so LIFO uncompute is
    // preserved on re-read.
    struct PendingAdjoint {
        SourceLocation after_semi;
        std::string text;
    };
    std::vector<PendingAdjoint> pending_adjoints;

    for (const auto& hit : hits) {
        if (hit.target_var == nullptr ||
            hit.subscript_expr == nullptr ||
            hit.container_expr == nullptr ||
            hit.index_expr == nullptr) {
            continue;
        }

        // Recover verbatim source text for `a` and `i`.
        const std::string container_text =
            expr_source_text(hit.container_expr, sm, lang);
        const std::string index_text =
            expr_source_text(hit.index_expr, sm, lang);
        if (container_text.empty() || index_text.empty()) continue;

        // Allocate a fresh ancilla name.
        std::ostringstream name_os;
        name_os << "__qram_h4_" << ancilla_counter++;
        const std::string ancilla_name = name_os.str();

        QramExprEmission em = emit_qram_expr_text(
            hit.kind, ancilla_name, container_text, index_text,
            hit.length_text, hit.W);
        if (em.extract_text.empty()) continue;

        const SourceLocation decl_begin = hit.target_var->getBeginLoc();
        const SourceLocation semi =
            find_var_decl_semi(hit.target_var, sm, lang);
        if (decl_begin.isInvalid() || semi.isInvalid()) continue;

        // 1. Pre-call extract: insert before the VarDecl's begin loc.
        //    Trailing newline + indent so the decl line stays tidy.
        {
            std::string before_text = em.extract_text;
            before_text += "\n    ";
            (void)rw.InsertTextBefore(decl_begin, before_text);
        }

        // 2. Subscript replacement: replace the matched subscript's
        //    source range with the bare ancilla name.
        {
            const SourceRange sub_range =
                hit.subscript_expr->getSourceRange();
            (void)rw.ReplaceText(sub_range, em.replace_text);
        }

        // 3. LHS type rewrite (once per VarDecl).
        if (lhs_done.insert(hit.target_var).second) {
            // Replace the bare `qint` type-name token at the VarDecl
            // begin loc with `sturm::qint_t<W>`. We use ReplaceText on
            // a 4-character range — the literal "qint" identifier.
            // If the user wrote `qint` (4 chars), this works; if they
            // wrote a qualified spelling like `sturm::frontend::qint`,
            // the SourceManager/Lexer-based token-end below will catch
            // the full token range.
            const std::string new_type = render_qint_typename(hit.W);
            // Use Lexer::MeasureTokenLength to get the exact length of
            // the type-name token at the VarDecl's begin loc.
            const unsigned tok_len = Lexer::MeasureTokenLength(
                decl_begin, sm, lang);
            if (tok_len > 0) {
                (void)rw.ReplaceText(decl_begin, tok_len, new_type);
            }
        }

        // 4. Post-call adjoint: defer to after the loop so LIFO order
        //    is preserved across multi-subscript hits.
        std::string adjoint_text = "\n    ";
        adjoint_text += em.adjoint_text;
        pending_adjoints.push_back({semi, std::move(adjoint_text)});
    }

    // Plant adjoints in REVERSE order so LIFO uncompute is preserved.
    // For multi-subscript on the same target_var the semis are equal,
    // so reverse-iterating the queue then InsertTextAfter sequences
    // the adjoints in the correct order in the final buffer.
    for (auto it = pending_adjoints.rbegin(); it != pending_adjoints.rend();
         ++it) {
        // InsertTextAfter at the semicolon location plants the text
        // immediately after the `;` byte — Rewriter handles the
        // offset arithmetic internally.
        (void)rw.InsertTextAfterToken(it->after_semi, it->text);
    }
}

// sturm-ddgo: produce QReplacement / UncomputeInsertion records
// without touching a Rewriter. Mirrors `emit_qram_expr_rewrites`'s
// per-hit logic exactly. Three records per hit:
//   - One `UncomputeInsertion` at decl_begin carrying the extract.
//   - One `QReplacement` for the subscript replacement.
//   - One `UncomputeInsertion` at the location AFTER the semi (via
//     `Lexer::getLocForEndOfToken(semi, ...)`) carrying the adjoint.
// The LHS type rewrite, when needed, is one extra `QReplacement` per
// VarDecl (deduped by pointer).
void emit_qram_expr_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<QramSubscriptExprHit>& hits,
    std::vector<QReplacement>& replacements,
    std::vector<UncomputeInsertion>& insertions) {
    if (hits.empty()) return;

    std::size_t ancilla_counter = 0;
    std::set<const VarDecl*> lhs_done;

    // Pending adjoints: identical reasoning to the Rewriter path —
    // queue per hit, plant in REVERSE matcher order so a multi-
    // subscript line uncomputes LIFO.
    struct PendingAdjoint {
        SourceLocation after_semi;
        std::string text;
    };
    std::vector<PendingAdjoint> pending_adjoints;

    for (const auto& hit : hits) {
        if (hit.target_var == nullptr ||
            hit.subscript_expr == nullptr ||
            hit.container_expr == nullptr ||
            hit.index_expr == nullptr) {
            continue;
        }

        const std::string container_text =
            expr_source_text(hit.container_expr, sm, lang);
        const std::string index_text =
            expr_source_text(hit.index_expr, sm, lang);
        if (container_text.empty() || index_text.empty()) continue;

        std::ostringstream name_os;
        name_os << "__qram_h4_" << ancilla_counter++;
        const std::string ancilla_name = name_os.str();

        QramExprEmission em = emit_qram_expr_text(
            hit.kind, ancilla_name, container_text, index_text,
            hit.length_text, hit.W);
        if (em.extract_text.empty()) continue;

        const SourceLocation decl_begin = hit.target_var->getBeginLoc();
        const SourceLocation semi =
            find_var_decl_semi(hit.target_var, sm, lang);
        if (decl_begin.isInvalid() || semi.isInvalid()) continue;

        // Pre-call extract.
        {
            UncomputeInsertion ins;
            ins.insert_before = decl_begin;
            ins.code = em.extract_text + "\n    ";
            insertions.push_back(std::move(ins));
        }

        // Subscript replacement.
        {
            const SourceRange sub_range =
                hit.subscript_expr->getSourceRange();
            if (sub_range.isValid()) {
                QReplacement rep;
                rep.range = sub_range;
                rep.replacement = em.replace_text;
                replacements.push_back(std::move(rep));
            }
        }

        // LHS type rewrite (once per VarDecl).
        if (lhs_done.insert(hit.target_var).second) {
            const std::string new_type = render_qint_typename(hit.W);
            const unsigned tok_len = Lexer::MeasureTokenLength(
                decl_begin, sm, lang);
            if (tok_len > 0) {
                QReplacement rep;
                rep.range = SourceRange(
                    decl_begin, decl_begin.getLocWithOffset(tok_len - 1));
                rep.replacement = new_type;
                replacements.push_back(std::move(rep));
            }
        }

        // Defer the post-call adjoint to after the loop.
        std::string adjoint_text = "\n    ";
        adjoint_text += em.adjoint_text;
        pending_adjoints.push_back({semi, std::move(adjoint_text)});
    }

    // Plant adjoints. We want each adjoint to land AFTER the
    // semicolon byte. `getLocForEndOfToken(L, 0, sm, lang)` returns
    // the location one byte past the end of the token at L; for a
    // single-character `;` token this is exactly the byte right after
    // it. We push in REVERSE matcher order so the M9 emitter's reverse
    // iteration over `insertions` (`emitter.cpp`'s LIFO loop) plants
    // them in matcher-forward order at the same location, preserving
    // the original `InsertTextAfterToken` ordering. (See "Why reverse
    // iteration?" in `emitter.cpp`.)
    for (auto it = pending_adjoints.begin(); it != pending_adjoints.end();
         ++it) {
        const SourceLocation after =
            Lexer::getLocForEndOfToken(it->after_semi, 0, sm, lang);
        if (after.isInvalid()) continue;
        UncomputeInsertion ins;
        ins.insert_before = after;
        ins.code = std::move(it->text);
        insertions.push_back(std::move(ins));
    }
}

} // namespace sturm::transpile
