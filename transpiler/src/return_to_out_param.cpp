// return_to_out_param.cpp — Phase Q Q-A (sturm-5kgu.2) implementation.
//
// See return_to_out_param.hpp for the contract. This implementation is a
// pure source-level transformation: every input comes from the
// `clang::FunctionDecl`'s public accessors + `clang::Lexer::getSourceText`,
// and every output is a `std::string` the caller stores on the
// `SynthesisRegistry`. No Clang rewriter touches the AST.
//
// Pipeline
// --------
//
//  1. Reject-gate (`classify_shape`):
//       - nullptr guard,
//       - `is_reversible` guard,
//       - quantum-return-type guard (name-based, same as
//         `matcher_user_routine.cpp`'s `is_output_param`),
//       - body shape guard: must be a `CompoundStmt` with exactly one
//         `ReturnStmt`, and the `ReturnStmt` must carry a non-null
//         return expression.
//
//  2. Source recovery:
//       - verbatim parameter spellings via
//         `Lexer::getSourceText(CharSourceRange::getTokenRange(param->
//         getSourceRange()), ...)`. Preserves const-ness, reference /
//         pointer qualifiers, and default arguments.
//       - return-type source: verbatim spelling from the function's
//         `TypeSourceInfo` / `FunctionTypeLoc` when available, falling
//         back to `QualType::getAsString(PrintingPolicy)` when the
//         decl has no typed source info (e.g. a trailing-return
//         synthesised shape). A failed recovery → `SourceRecoveryFailed`.
//       - return-expression source: same `Lexer::getSourceText` call
//         on the `ReturnStmt`'s `getRetValue()`.
//
//  3. Twin assembly:
//       - Name: `"__<forward-ident>_out"`.
//       - Signature: `void <twin-name>(<orig params>, <ret_type>& <twin_name>)`.
//         When the forward has no parameters the leading comma is
//         dropped; we stitch the joiner conditionally.
//       - Attribute: the forward's `[[clang::annotate("sturm::reversible")]]`
//         marker is re-emitted on the twin so downstream passes
//         (validation, emission) treat the twin as a first-class
//         synthesis candidate.
//       - Body: `{ <twin_name> ^= <return-expr>; }` on its own line.
//         The `^=` operator matches the canonical out-param shape the
//         PRD example uses (`void marked(qbool& a, ...) { a ^= (...); }`).
//
// Formatting
// ----------
// The twin is emitted with deterministic whitespace so the golden-file
// comparison in `test_return_to_out_param.cpp` is byte-stable across
// hosts. Four-space indentation on the body, one statement per line,
// trailing newline. The parameter list joins the original spellings
// with `", "` (comma + single space).
//
// Macro / include-location caveats
// --------------------------------
// Clang routinely places names that originated from macros at
// SourceLocations whose spelling/include stack is not directly
// representable as a token range. `Lexer::getSourceText` returns an
// empty StringRef in that case; we translate that into a
// `SourceRecoveryFailed` reject rather than silently emitting an
// empty token. The test fixtures deliberately avoid macro-expanded
// parameters to keep the success paths stable.

#include "return_to_out_param.hpp"

#include "reversible_attribute.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using clang::CharSourceRange;
using clang::CompoundStmt;
using clang::FunctionDecl;
using clang::LangOptions;
using clang::Lexer;
using clang::ParmVarDecl;
using clang::PrintingPolicy;
using clang::QualType;
using clang::ReturnStmt;
using clang::SourceManager;
using clang::SourceRange;
using clang::Stmt;
using clang::TypeSourceInfo;

// ── Small helpers ──────────────────────────────────────────────────────────

/// Return true iff `qt` resolves (after stripping refs + cv) to a
/// CXXRecord named `qbool`, `qint`, or `qint_t`. Mirrors the heuristic
/// `matcher_when_operand_mutation.cpp::is_quantum_type` uses — the
/// public sturm surface types are the only ones reversible routines
/// return.
bool is_quantum_return_type(QualType qt) {
    if (qt.isNull()) return false;
    QualType stripped = qt.getNonReferenceType().getUnqualifiedType();
    const auto* rd = stripped->getAsCXXRecordDecl();
    if (rd == nullptr) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

/// Recover verbatim source text for a `SourceRange`. Returns empty
/// string when the Lexer cannot resolve the range (macro-only, invalid
/// location). Every callsite translates an empty return into a
/// `SourceRecoveryFailed` reject rather than silently emitting an
/// empty token.
std::string source_text_of(SourceRange range,
                           const SourceManager& sm,
                           const LangOptions& lang) {
    if (range.isInvalid()) return {};
    auto char_range = CharSourceRange::getTokenRange(range);
    auto text = Lexer::getSourceText(char_range, sm, lang);
    return text.str();
}

/// Attempt to recover the function's declared return-type source text.
/// Preferred path: walk the function's `TypeSourceInfo` /
/// `FunctionTypeLoc` and read the return TypeLoc's source range.
/// Fallback: pretty-print the canonical `QualType` via the AST's
/// `PrintingPolicy`. Returns empty only when both paths fail (which
/// would indicate a broken decl — we translate it into
/// `SourceRecoveryFailed`).
std::string recover_return_type_text(const FunctionDecl* fd,
                                     const SourceManager& sm,
                                     const LangOptions& lang) {
    if (fd == nullptr) return {};

    // Preferred path: the return-type source range carried by the
    // function's TypeSourceInfo. This preserves the user's exact
    // spelling (`qbool`, `sturm::qbool`, `my::qbool_alias`) rather
    // than the canonical form.
    if (const TypeSourceInfo* tsi = fd->getTypeSourceInfo()) {
        clang::TypeLoc tl = tsi->getTypeLoc();
        if (auto ftl = tl.getAsAdjusted<clang::FunctionTypeLoc>()) {
            clang::TypeLoc rtl = ftl.getReturnLoc();
            auto source = source_text_of(rtl.getSourceRange(), sm, lang);
            if (!source.empty()) return source;
        }
    }

    // Fallback: pretty-print the QualType. The printed form is
    // canonicalised (e.g. `_Bool` for `bool`) but we only hit this
    // branch for generated code where the user had no source
    // spelling in the first place.
    QualType qt = fd->getReturnType();
    if (qt.isNull()) return {};
    PrintingPolicy policy(lang);
    policy.SuppressTagKeyword = 1;
    return qt.getAsString(policy);
}

/// Classify the function's body shape. Writes the surviving
/// `ReturnStmt*` into `*out_ret` on the success path. The classifier
/// enforces: body exists, body is a `CompoundStmt`, exactly one
/// statement, that statement is a `ReturnStmt`, the `ReturnStmt`
/// carries a non-null expression.
TwinRejectReason classify_body(const FunctionDecl* fd,
                               const ReturnStmt** out_ret) {
    if (out_ret != nullptr) *out_ret = nullptr;
    if (!fd->hasBody()) return TwinRejectReason::NoBody;
    const Stmt* body = fd->getBody();
    if (body == nullptr) return TwinRejectReason::NoBody;
    const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
    if (compound == nullptr) return TwinRejectReason::NonCompoundBody;
    if (compound->size() != 1) return TwinRejectReason::MultiStatementBody;
    const Stmt* only = compound->body_front();
    const auto* ret = llvm::dyn_cast_or_null<ReturnStmt>(only);
    if (ret == nullptr) return TwinRejectReason::NotReturnStatement;
    if (ret->getRetValue() == nullptr) {
        return TwinRejectReason::EmptyReturnExpression;
    }
    if (out_ret != nullptr) *out_ret = ret;
    return TwinRejectReason::None;
}

/// Assemble the final twin text. Takes the already-recovered pieces
/// so the caller can short-circuit on any recovery failure before
/// committing to string concatenation.
std::string assemble_twin(
    std::string_view twin_name,
    std::string_view return_type_text,
    const std::vector<std::string>& param_texts,
    std::string_view ret_expr_text) {
    // Canonical spelling of the `[[sturm::reversible]]` marker the
    // twin inherits from the forward. We emit the `clang::annotate`
    // form because that is the one guaranteed to survive as an
    // `AnnotateAttr` on the rewritten buffer's next parse — the
    // shorter `[[sturm::reversible]]` would trigger
    // `-Wunknown-attributes` and never reach the AST.
    std::string out;
    out += "[[clang::annotate(\"sturm::reversible\")]]\n";
    out += "void ";
    out.append(twin_name.data(), twin_name.size());
    out += '(';
    for (std::size_t i = 0; i < param_texts.size(); ++i) {
        if (i > 0) out += ", ";
        out += param_texts[i];
    }
    if (!param_texts.empty()) out += ", ";
    out.append(return_type_text.data(), return_type_text.size());
    out += "& ";
    out.append(twin_name.data(), twin_name.size());
    out += ") {\n    ";
    out.append(twin_name.data(), twin_name.size());
    out += " ^= ";
    out.append(ret_expr_text.data(), ret_expr_text.size());
    out += ";\n}\n";
    return out;
}

} // namespace

// ── Public surface ──────────────────────────────────────────────────────────

std::string_view to_string(TwinRejectReason reason) {
    switch (reason) {
        case TwinRejectReason::None:                  return "none";
        case TwinRejectReason::NullDecl:              return "null_decl";
        case TwinRejectReason::NotReversible:         return "not_reversible";
        case TwinRejectReason::NonQuantumReturnType:  return "non_quantum_return_type";
        case TwinRejectReason::NoBody:                return "no_body";
        case TwinRejectReason::NonCompoundBody:       return "non_compound_body";
        case TwinRejectReason::MultiStatementBody:    return "multi_statement_body";
        case TwinRejectReason::NotReturnStatement:    return "not_return_statement";
        case TwinRejectReason::EmptyReturnExpression: return "empty_return_expression";
        case TwinRejectReason::SourceRecoveryFailed:  return "source_recovery_failed";
    }
    // Unreachable for a well-formed enum. Empty view is the safe
    // default — we never construct this branch in production code.
    return {};
}

TwinSynthesisResult synthesize_out_param_twin(
    const FunctionDecl* fd,
    const SourceManager& sm,
    const LangOptions& lang) {
    TwinSynthesisResult result;

    // 1. Reject-gate.
    if (fd == nullptr) {
        result.reason = TwinRejectReason::NullDecl;
        return result;
    }
    if (!is_reversible(fd)) {
        result.reason = TwinRejectReason::NotReversible;
        return result;
    }
    if (!is_quantum_return_type(fd->getReturnType())) {
        result.reason = TwinRejectReason::NonQuantumReturnType;
        return result;
    }
    const ReturnStmt* ret = nullptr;
    const TwinRejectReason body_reason = classify_body(fd, &ret);
    if (body_reason != TwinRejectReason::None) {
        result.reason = body_reason;
        return result;
    }

    // 2. Source recovery.
    const std::string return_type_text =
        recover_return_type_text(fd, sm, lang);
    if (return_type_text.empty()) {
        result.reason = TwinRejectReason::SourceRecoveryFailed;
        return result;
    }

    std::vector<std::string> param_texts;
    param_texts.reserve(fd->getNumParams());
    for (unsigned i = 0; i < fd->getNumParams(); ++i) {
        const ParmVarDecl* p = fd->getParamDecl(i);
        if (p == nullptr) {
            result.reason = TwinRejectReason::SourceRecoveryFailed;
            return result;
        }
        std::string text = source_text_of(p->getSourceRange(), sm, lang);
        if (text.empty()) {
            result.reason = TwinRejectReason::SourceRecoveryFailed;
            return result;
        }
        param_texts.push_back(std::move(text));
    }

    const std::string ret_expr_text =
        source_text_of(ret->getRetValue()->getSourceRange(), sm, lang);
    if (ret_expr_text.empty()) {
        result.reason = TwinRejectReason::SourceRecoveryFailed;
        return result;
    }

    // 3. Twin assembly.
    const std::string twin_name = "__" + fd->getNameAsString() + "_out";
    std::string twin_source = assemble_twin(
        twin_name, return_type_text, param_texts, ret_expr_text);

    result.source       = std::move(twin_source);
    result.twin_name    = twin_name;
    result.synthesized  = true;
    result.reason       = TwinRejectReason::None;
    return result;
}

} // namespace sturm::transpile
