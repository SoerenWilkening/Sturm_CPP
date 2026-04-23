// adjoint_emitter.cpp — Phase R R-1 (sturm-88d7.2) implementation.
//
// See adjoint_emitter.hpp for the contract. This implementation is a
// pure source-level transformation: every input comes from a
// `clang::FunctionDecl` via public accessors + `clang::Lexer::
// getSourceText`, or from a hand-built vector of `QOperation` records
// the caller supplies directly. Every output is a `std::string` the
// pipeline driver stores on the `SynthesisRegistry`. No Clang
// rewriter touches the AST.
//
// Pipeline
// --------
//
//  1. Reject-gate (only in the `emit_adjoint_for_decl` entry point):
//       - nullptr guard,
//       - `is_reversible` guard,
//       - body-exists guard,
//       - per-parameter source-text recovery guard.
//
//  2. Signature recovery (`emit_adjoint_for_decl` only):
//       - verbatim parameter spellings via `Lexer::getSourceText(
//         CharSourceRange::getTokenRange(param->getSourceRange()),
//         ...)`. Preserves const-ness, reference/pointer qualifiers,
//         and default arguments — same recovery shape Q-A uses.
//
//  3. Reverse-statement-order walk:
//       - Iterate `ops` from last to first.
//       - For each op, call the exposed `render_uncompute(op,
//         registry)` helper from `uncompute_pass.cpp`. Skip ops that
//         render to empty (self-disqualifying malformed input,
//         loop-body ops whose `skip_uncompute` flag was set by PH-3 —
//         loops are Phase S's problem).
//       - Concatenate the rendered lines verbatim; each already
//         carries four-space leading indent + trailing newline, so the
//         concatenation is tight.
//
//  4. Assembly:
//       - Emit `void __<fn>_adj(<signature>) {\n`, the concatenated
//         body lines, and `}\n`.
//
// LOC budget
// ----------
// CLAUDE.md caps source files at 400 LOC. The plan §2.3 R-A budget
// is 370 LOC for this implementation; we stay under.

#include "adjoint_emitter.hpp"

#include "reversible_attribute.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using clang::CharSourceRange;
using clang::FunctionDecl;
using clang::LangOptions;
using clang::Lexer;
using clang::ParmVarDecl;
using clang::SourceManager;
using clang::SourceRange;

// ── Small helpers ──────────────────────────────────────────────────────────

/// Recover verbatim source text for a `SourceRange`. Returns empty
/// string when the Lexer cannot resolve the range (macro-only, invalid
/// location). Every callsite translates an empty return into a
/// `SourceRecoveryFailed` reject rather than silently emitting an
/// empty token.
///
/// Same helper shape Q-A uses in `return_to_out_param.cpp`; duplicated
/// here to keep R-A self-contained (the two modules live at sibling
/// peers in the pipeline and sharing a utility header would pull in
/// Q-A's reject enum into R-A's TU).
std::string source_text_of(SourceRange range,
                           const SourceManager& sm,
                           const LangOptions& lang) {
    if (range.isInvalid()) return {};
    auto char_range = CharSourceRange::getTokenRange(range);
    auto text = Lexer::getSourceText(char_range, sm, lang);
    return text.str();
}

/// Walk the forward routine's parameter list and recover each
/// parameter's verbatim source text. Returns true on success, false
/// on any recovery failure (in which case `out_texts` is left in an
/// indeterminate state; the caller should discard it and report a
/// `SourceRecoveryFailed` reject).
bool recover_parameter_texts(const FunctionDecl* fd,
                             const SourceManager& sm,
                             const LangOptions& lang,
                             std::vector<std::string>& out_texts) {
    out_texts.clear();
    out_texts.reserve(fd->getNumParams());
    for (unsigned i = 0; i < fd->getNumParams(); ++i) {
        const ParmVarDecl* p = fd->getParamDecl(i);
        if (p == nullptr) return false;
        std::string text = source_text_of(p->getSourceRange(), sm, lang);
        if (text.empty()) return false;
        out_texts.push_back(std::move(text));
    }
    return true;
}

/// Join the recovered parameter texts into a signature string, comma-
/// separated with a single trailing space between tokens. Returns an
/// empty string when `params` is empty (the adjoint emits `()` for a
/// nullary forward).
std::string join_parameter_texts(const std::vector<std::string>& params) {
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) out += ", ";
        out += params[i];
    }
    return out;
}

/// Produce the body text: one rendered inverse per op, in REVERSE
/// order. Ops that render to an empty string (malformed, PH-3 skip,
/// PLUGIN without registry) are silently elided — matches the
/// defensive posture of `synthesize()`'s per-op loop. The caller
/// receives the concatenation directly.
std::string render_reverse_body(const std::vector<QOperation>& ops,
                                const plugin::Registry* registry) {
    std::string body;
    // Rough capacity reservation — each rendered line is typically
    // ~30 bytes; pre-reserving avoids mid-loop reallocations on the
    // common small-body case. This mirrors `synthesize()`'s capacity
    // heuristic.
    body.reserve(ops.size() * 48);
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        const QOperation& op = *it;
        // Phase H PH-3 parity: honour the `skip_uncompute` flag the
        // outer-var-guard matcher sets on mutations whose automatic
        // uncomputation would require reverse-loop synthesis (Phase
        // S's problem). The op stays in the matcher's QIR but we
        // emit no adjoint line for it — a caller who wants a
        // diagnostic registers P9d's validation pass (P-C), not R-A.
        if (op.skip_uncompute) continue;
        std::string line = render_uncompute(op, registry);
        if (line.empty()) {
            // Unsupported / malformed op — skip silently; see the
            // rationale in `render_uncompute()`. The MVP matcher
            // never produces such ops.
            continue;
        }
        body.append(line);
    }
    return body;
}

/// Assemble the final adjoint text. Takes the already-recovered
/// pieces so the caller can short-circuit on any recovery failure
/// before committing to string concatenation.
///
/// Emission shape (pinned by golden-file tests):
///
///     void __<fn>_adj(<signature>) {
///     <body>
///     }
///
/// where `<body>` is the concatenation of per-op rendered lines,
/// each carrying four-space indent + trailing newline from
/// `render_uncompute`.
std::string assemble_adjoint(std::string_view adjoint_name,
                             std::string_view signature_text,
                             std::string_view body_text) {
    std::string out;
    out.reserve(32 + adjoint_name.size() + signature_text.size()
                + body_text.size());
    out += "void ";
    out.append(adjoint_name.data(), adjoint_name.size());
    out += '(';
    out.append(signature_text.data(), signature_text.size());
    out += ") {\n";
    out.append(body_text.data(), body_text.size());
    out += "}\n";
    return out;
}

} // namespace

// ── Public surface ──────────────────────────────────────────────────────────

std::string_view to_string(AdjointRejectReason reason) {
    switch (reason) {
        case AdjointRejectReason::None:                 return "none";
        case AdjointRejectReason::NullDecl:             return "null_decl";
        case AdjointRejectReason::NotReversible:        return "not_reversible";
        case AdjointRejectReason::NoBody:               return "no_body";
        case AdjointRejectReason::SourceRecoveryFailed: return "source_recovery_failed";
    }
    // Unreachable for a well-formed enum. Empty view is the safe
    // default — we never construct this branch in production code.
    return {};
}

std::string emit_adjoint_body(std::string_view fn_name,
                              std::string_view signature_text,
                              const std::vector<QOperation>& ops,
                              const plugin::Registry* registry) {
    // Empty fn_name → empty output. A caller with no identifier
    // cannot possibly consume the produced text; return an empty
    // string so the degenerate case is a no-op rather than
    // `void __(<sig>) { ... }` which would collide with future
    // reserved identifiers.
    if (fn_name.empty()) return {};

    const std::string adjoint_name = std::string("__") +
                                     std::string(fn_name) + "_adj";
    const std::string body = render_reverse_body(ops, registry);
    return assemble_adjoint(adjoint_name, signature_text, body);
}

AdjointEmissionResult emit_adjoint_for_decl(
    const FunctionDecl* fd,
    const std::vector<QOperation>& ops,
    const SourceManager& sm,
    const LangOptions& lang,
    const plugin::Registry* registry) {
    AdjointEmissionResult result;

    // 1. Reject-gate.
    if (fd == nullptr) {
        result.reason = AdjointRejectReason::NullDecl;
        return result;
    }
    if (!is_reversible(fd)) {
        result.reason = AdjointRejectReason::NotReversible;
        return result;
    }
    if (!fd->hasBody()) {
        result.reason = AdjointRejectReason::NoBody;
        return result;
    }

    // 2. Signature recovery. We reuse the forward's parameter list
    // verbatim (per P9b the adjoint shares the forward's input
    // immutability rules). Recovery failure on any parameter ⇒
    // `SourceRecoveryFailed`.
    std::vector<std::string> param_texts;
    if (!recover_parameter_texts(fd, sm, lang, param_texts)) {
        result.reason = AdjointRejectReason::SourceRecoveryFailed;
        return result;
    }
    const std::string signature_text = join_parameter_texts(param_texts);

    // 3. Reverse-statement-order body walk + assembly.
    const std::string fn_name = fd->getNameAsString();
    const std::string adjoint_name = "__" + fn_name + "_adj";
    const std::string body = render_reverse_body(ops, registry);
    std::string adjoint_text = assemble_adjoint(
        adjoint_name, signature_text, body);

    result.source        = std::move(adjoint_text);
    result.adjoint_name  = adjoint_name;
    result.synthesized   = true;
    result.reason        = AdjointRejectReason::None;
    return result;
}

} // namespace sturm::transpile
