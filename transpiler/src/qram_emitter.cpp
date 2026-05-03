// qram_emitter.cpp — sturm-u9ge.15 (Beat D2) implementation.
//
// See `qram_emitter.hpp` for the contract; PRD §8 / §11.1.5 for the
// per-shape emitted call line table; PRD §11.4 / D0d.5 for the
// adjoint registration shape.
//
// Two structural shapes per `Hit.kind` (PRD §11.1.5):
//
//   - StdArray / CArray:
//         sturm::qint_t<W> <b>; ::sturm::QRAM_read(<a>, <i>, <b>);
//   - Pointer:
//         sturm::qint_t<W> <b>; ::sturm::QRAM_read(<a>, <n>, <i>, <b>);
//
// Empty `length_text` on a Pointer hit surfaces a
// `qram-pointer-length-missing` placeholder comment in place of `n`
// per PRD §11.1.6.
//
// Adjoint placement (PRD §D0d.5): when the enclosing function carries
// `[[clang::annotate("sturm::reversible")]]`, the emitter inserts
// `sturm::invert<&::sturm::QRAM_read>()(a, i, b);` just before the
// body's close brace, mirroring how `adjoint_emitter.cpp` plants
// `__fn_adj` body lines.
//
// A null `target_var` / missing `container_expr` / `index_expr`
// yields zero Rewriter mutations — same defensive posture every
// other emitter takes on malformed input.

#include "qram_emitter.hpp"

#include "reversible_attribute.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace {

using clang::CharSourceRange;
using clang::FunctionDecl;
using clang::LangOptions;
using clang::Lexer;
using clang::Rewriter;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::Token;

// ── Source-text recovery helpers ────────────────────────────────────────────
//
// Mirrors the `source_text_of` helper in `adjoint_emitter.cpp` — kept
// local here so the two modules stay independent (the adjoint emitter
// lives in a different layer of the pipeline).
std::string source_text_of(SourceRange range,
                           const SourceManager& sm,
                           const LangOptions& lang) {
    if (range.isInvalid()) return {};
    auto char_range = CharSourceRange::getTokenRange(range);
    auto text = Lexer::getSourceText(char_range, sm, lang);
    return text.str();
}

// Resolve the user's spelling of an `Expr` (container, index) verbatim.
// `IgnoreParens()` is the right peel: we want the source-level
// expression, not the de-decayed type. For `obj.tbl[i]` the recovered
// text is `obj.tbl` for the container and `i` for the index, even when
// Clang inserted an `ImplicitCastExpr` of `ArrayToPointerDecay` around
// the container.
std::string expr_source_text(const clang::Expr* e,
                             const SourceManager& sm,
                             const LangOptions& lang) {
    if (!e) return {};
    return source_text_of(e->IgnoreParens()->getSourceRange(), sm, lang);
}

// Walk up from a `Decl`'s `DeclContext` chain to find the enclosing
// `FunctionDecl`. Returns nullptr for a top-level decl outside any
// function body. Mirrors the parent-walk pattern in
// `matcher_outer_var_guard.cpp::ascend_to_function_decl`.
const FunctionDecl* enclosing_function_of(const clang::Decl* d) {
    if (!d) return nullptr;
    const clang::DeclContext* dc = d->getDeclContext();
    while (dc != nullptr) {
        if (const auto* fd = llvm::dyn_cast<FunctionDecl>(dc)) return fd;
        dc = dc->getParent();
    }
    return nullptr;
}

// ── Pure-string emission shape (mirrors `lossy_rewrite_emitter::render_qint_typename`) ──
//
// `W > 0` ⇒ `sturm::qint_t<W>` so the emitted text compiles in TUs
// without a `using qint = ...;` typedef. `W == 0` falls back to the
// legacy unqualified `qint` typename for hermetic-stub fixtures —
// matches the sturm-czfi posture every other emitter takes.
std::string render_qint_typename(unsigned W) {
    if (W == 0) return "qint";
    std::ostringstream os;
    os << "sturm::qint_t<" << W << ">";
    return os.str();
}

// Render the runtime-call argument list for the rewrite. Pointer
// arm gets `(a, n, i, b)`; the other two arms get `(a, i, b)`. PRD
// §11.1.5 pins the per-shape line table.
std::string render_qram_call(QramContainerKind kind,
                             std::string_view container_text,
                             std::string_view index_text,
                             std::string_view length_text,
                             std::string_view target_name) {
    std::ostringstream os;
    os << "::sturm::QRAM_read(" << container_text;
    if (kind == QramContainerKind::Pointer) {
        // PRD §11.1.6: the pointer overload requires an explicit
        // `n` between `a` and `i`. Empty `length_text` ⇒ surface a
        // placeholder so the rewritten file is unambiguous about the
        // missing argument; the user must fill it in or accept the
        // diagnostic.
        os << ", "
           << (length_text.empty() ? "/* qram-pointer-length-missing */"
                                   : length_text);
    }
    os << ", " << index_text << ", " << target_name << ")";
    return os.str();
}

// Render the adjoint call planted at the reversible scope's close
// brace (PRD §D0d.5). The planted shape is
//
//     ::sturm::invert<&::sturm::QRAM_read<W[, N]>>()(a[, n], i, b);
//
// The template argument list intentionally elides the concrete `(W,
// N)` instantiation: we cannot reliably infer `N` from the AST in the
// general case (the `std::array` arm sometimes has a literal `N`,
// the C-array arm has it in the type, the pointer arm has none).
// `invert<&fn>()` on the *unspecialized* function template name
// matches how `adjoint_emitter.cpp` plants `__fn_adj` calls; the
// `invert.hpp` machinery resolves the registered adjoint by
// NTTP-keyed trait specialisation, not by template-argument
// matching.
std::string render_qram_adjoint(QramContainerKind kind,
                                std::string_view container_text,
                                std::string_view index_text,
                                std::string_view length_text,
                                std::string_view target_name) {
    std::ostringstream os;
    os << "::sturm::invert<&::sturm::QRAM_read>()("
       << container_text;
    if (kind == QramContainerKind::Pointer) {
        os << ", "
           << (length_text.empty() ? "/* qram-pointer-length-missing */"
                                   : length_text);
    }
    os << ", " << index_text << ", " << target_name << ")";
    return os.str();
}

// Locate the `;` terminating a VarDecl statement. Returns an
// invalid `SourceLocation` when the next token is not a `;`
// (defensive — every well-formed VarDecl-as-stmt ends in a `;`,
// but malformed input bails rather than producing a partial
// replacement). Mirrors the pattern in
// `matcher_dead_ancilla.cpp:189`.
SourceLocation find_var_decl_semi(const clang::VarDecl* var,
                                  const SourceManager& sm,
                                  const LangOptions& lang) {
    if (!var) return {};
    const SourceLocation end = var->getEndLoc();
    if (end.isInvalid()) return {};
    std::optional<Token> semi = Lexer::findNextToken(end, sm, lang);
    if (!semi || semi->getKind() != clang::tok::semi) return {};
    return semi->getLocation();
}

} // anonymous namespace

// ── Public surface ──────────────────────────────────────────────────────────

QramEmission emit_qram_forward_text(QramContainerKind kind,
                                    std::string_view target_name,
                                    std::string_view container_text,
                                    std::string_view index_text,
                                    std::string_view length_text,
                                    unsigned W) {
    QramEmission em;
    em.kind = kind;
    if (target_name.empty() || container_text.empty() ||
        index_text.empty()) {
        return em;
    }
    std::ostringstream os;
    os << render_qint_typename(W) << ' ' << target_name << "; "
       << render_qram_call(kind, container_text, index_text, length_text,
                           target_name)
       << ";";
    em.text = os.str();
    return em;
}

void emit_qram_rewrites(Rewriter& rw,
                        const std::vector<QramSubscriptHit>& hits) {
    if (hits.empty()) return;
    const SourceManager& sm = rw.getSourceMgr();
    const LangOptions& lang = rw.getLangOpts();

    for (const auto& hit : hits) {
        if (hit.target_var == nullptr ||
            hit.container_expr == nullptr ||
            hit.index_expr == nullptr) {
            continue;
        }

        // Recover the user's verbatim spellings of `a`, `i`, and `b`.
        const std::string container_text =
            expr_source_text(hit.container_expr, sm, lang);
        const std::string index_text =
            expr_source_text(hit.index_expr, sm, lang);
        const std::string target_name = hit.target_var->getNameAsString();
        if (container_text.empty() || index_text.empty() ||
            target_name.empty()) {
            continue;
        }

        // Forward emission: replace `qint b = a[i];` (begin → trailing
        // `;`) with the two-line `sturm::qint_t<W> b;
        // ::sturm::QRAM_read(a, i, b);` sequence per PRD §8.
        QramEmission em = emit_qram_forward_text(
            hit.kind, target_name, container_text, index_text,
            hit.length_text, hit.W);
        if (em.text.empty()) continue;

        const SourceLocation decl_begin = hit.target_var->getBeginLoc();
        const SourceLocation decl_end =
            find_var_decl_semi(hit.target_var, sm, lang);
        if (decl_begin.isInvalid() || decl_end.isInvalid()) continue;

        // ReplaceText takes a token-end SourceRange — the matching
        // shape modular_rewrite_emitter wires up in the consumer
        // drain (transpile_consumer.cpp:1071).
        (void)rw.ReplaceText(SourceRange(decl_begin, decl_end), em.text);

        // Adjoint placement (PRD §D0d.5): plant the matching
        // `sturm::invert<&::sturm::QRAM_read>()(a, i, b);` call just
        // before the enclosing reversible scope's close brace.
        // Non-reversible enclosing functions get no adjoint —
        // PRD §10.2 only mandates an adjoint inside reversible
        // routines.
        const FunctionDecl* enclosing =
            enclosing_function_of(hit.target_var);
        if (enclosing == nullptr) continue;
        if (!is_reversible(enclosing)) continue;
        const clang::Stmt* body = enclosing->getBody();
        if (body == nullptr) continue;
        const auto* compound =
            llvm::dyn_cast<clang::CompoundStmt>(body);
        if (compound == nullptr) continue;
        const SourceLocation rbrace = compound->getRBracLoc();
        if (rbrace.isInvalid()) continue;

        std::string adjoint_text = "    ";
        adjoint_text += render_qram_adjoint(hit.kind, container_text,
                                            index_text, hit.length_text,
                                            target_name);
        adjoint_text += ";\n";
        // InsertTextBefore at the close brace plants the call as the
        // last statement of the function body — same posture
        // `adjoint_emitter.cpp` takes for `__fn_adj` body lines.
        (void)rw.InsertTextBefore(rbrace, adjoint_text);
    }
}

// sturm-ddgo: produce QReplacement / UncomputeInsertion records
// without touching a Rewriter. Mirrors `emit_qram_rewrites` but
// flows through the consumer's `QUnit::replacements` /
// `QUnit::raw_insertions` channels so the production
// `transpile_consumer.cpp` can integrate the QRAM rewrite alongside
// the modular / lossy / per-op-uncompute pipelines.
void emit_qram_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<QramSubscriptHit>& hits,
    std::vector<QReplacement>& replacements,
    std::vector<UncomputeInsertion>& insertions) {
    if (hits.empty()) return;
    for (const auto& hit : hits) {
        if (hit.target_var == nullptr ||
            hit.container_expr == nullptr ||
            hit.index_expr == nullptr) {
            continue;
        }

        const std::string container_text =
            expr_source_text(hit.container_expr, sm, lang);
        const std::string index_text =
            expr_source_text(hit.index_expr, sm, lang);
        const std::string target_name = hit.target_var->getNameAsString();
        if (container_text.empty() || index_text.empty() ||
            target_name.empty()) {
            continue;
        }

        QramEmission em = emit_qram_forward_text(
            hit.kind, target_name, container_text, index_text,
            hit.length_text, hit.W);
        if (em.text.empty()) continue;

        const SourceLocation decl_begin = hit.target_var->getBeginLoc();
        const SourceLocation decl_end =
            find_var_decl_semi(hit.target_var, sm, lang);
        if (decl_begin.isInvalid() || decl_end.isInvalid()) continue;

        QReplacement rep;
        rep.range = SourceRange(decl_begin, decl_end);
        rep.replacement = em.text;
        replacements.push_back(std::move(rep));

        const FunctionDecl* enclosing =
            enclosing_function_of(hit.target_var);
        if (enclosing == nullptr) continue;
        if (!is_reversible(enclosing)) continue;
        const clang::Stmt* body = enclosing->getBody();
        if (body == nullptr) continue;
        const auto* compound = llvm::dyn_cast<clang::CompoundStmt>(body);
        if (compound == nullptr) continue;
        const SourceLocation rbrace = compound->getRBracLoc();
        if (rbrace.isInvalid()) continue;

        std::string adjoint_text = "    ";
        adjoint_text += render_qram_adjoint(hit.kind, container_text,
                                            index_text, hit.length_text,
                                            target_name);
        adjoint_text += ";\n";

        UncomputeInsertion ins;
        ins.insert_before = rbrace;
        ins.code = std::move(adjoint_text);
        insertions.push_back(std::move(ins));
    }
}

} // namespace sturm::transpile
