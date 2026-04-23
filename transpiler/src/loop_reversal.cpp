// loop_reversal.cpp — Phase S S-A (sturm-ha2k.2) implementation.
//
// See loop_reversal.hpp for the contract. Pipeline (all pure source-
// level, no Rewriter mutation):
//
//  1. Reject-gate: null guard, init/cond/inc presence.
//  2. Init parsing: single integer VarDecl with initializer.
//  3. Cond parsing: single `i <op> bound` comparison where `<op>`
//     is one of `<`, `<=`, `>`, `>=`.
//  4. Inc parsing: `++i` / `i++` / `--i` / `i--` / `i += C` /
//     `i -= C` / `i = i + C` / `i = C + i` / `i = i - C` with `C`
//     an integer literal evaluated via `Expr::EvaluateAsInt` (so
//     unary-minus-wrapped literals like `-3` work).
//  5. Stride validation: stride == 0 → `ZeroStride`.
//  6. Source recovery for init expr + bound via `Lexer::getSourceText`.
//  7. Header assembly: shape is picked from (cmp-op, stride sign):
//
//       - upward forward (`<`/`<=` with positive stride):
//           `<`  last = lo + ((hi-1-lo)/s)*s
//           `<=` last = lo + ((hi-lo)/s)*s
//         reversed: `for (int i = <last>; i >= <lo>; i -= <s>)`
//
//       - downward forward (`>`/`>=` with negative stride):
//           `>`  last = hi - ((hi-lo-1)/s)*s
//           `>=` last = hi - ((hi-lo)/s)*s
//         reversed: `for (int i = <last>; i <= <hi>; i += <s>)`
//
// LOC budget: ~320 (plan §2.4 S-A).

#include "loop_reversal.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace sturm::transpile {

namespace {

using clang::BinaryOperator;
using clang::BinaryOperatorKind;
using clang::CharSourceRange;
using clang::CompoundAssignOperator;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::Expr;
using clang::ForStmt;
using clang::LangOptions;
using clang::Lexer;
using clang::SourceManager;
using clang::SourceRange;
using clang::Stmt;
using clang::UnaryOperator;
using clang::UnaryOperatorKind;
using clang::VarDecl;

// ── Source recovery helpers ────────────────────────────────────────────────

/// Recover verbatim source text. Empty return = Lexer failure (macro-
/// only, invalid location); caller translates to `SourceRecoveryFailed`.
std::string source_text_of(SourceRange range,
                           const SourceManager& sm,
                           const LangOptions& lang) {
    if (range.isInvalid()) return {};
    auto char_range = CharSourceRange::getTokenRange(range);
    auto text = Lexer::getSourceText(char_range, sm, lang);
    return text.str();
}

/// Peel parens + implicit casts so we can inspect shapes without
/// being fooled by `ImplicitCastExpr<LValueToRValue>` wrappers.
const Expr* peel(const Expr* e) {
    if (e == nullptr) return nullptr;
    return e->IgnoreParenImpCasts();
}

// ── Init-clause parsing ────────────────────────────────────────────────────

struct InitInfo {
    const VarDecl* vd = nullptr;
    bool ok = false;
};

/// Require exactly one integer VarDecl with an initializer.
InitInfo parse_init(const Stmt* init) {
    InitInfo out;
    if (init == nullptr) return out;
    const auto* ds = llvm::dyn_cast<DeclStmt>(init);
    if (ds == nullptr) return out;
    if (!ds->isSingleDecl()) return out;
    const auto* vd = llvm::dyn_cast<VarDecl>(ds->getSingleDecl());
    if (vd == nullptr) return out;
    if (!vd->hasInit()) return out;
    const clang::QualType qt = vd->getType();
    if (qt.isNull()) return out;
    if (!qt->isIntegerType()) return out;
    out.vd = vd;
    out.ok = true;
    return out;
}

// ── Cond-clause parsing ────────────────────────────────────────────────────

enum class CmpOp { LT, LE, GT, GE };

struct CondInfo {
    CmpOp op = CmpOp::LT;
    const Expr* bound = nullptr;
    bool ok = false;
};

/// Expect `i <op> bound` with `<op>` ∈ {<, <=, >, >=} and LHS that
/// references the induction variable. Everything else rejects —
/// compound (`i < N && flag`), unary (`!done`), calls (`check(i)`).
CondInfo parse_cond(const Expr* cond, const VarDecl* iv) {
    CondInfo out;
    if (cond == nullptr || iv == nullptr) return out;
    const auto* bo = llvm::dyn_cast<BinaryOperator>(peel(cond));
    if (bo == nullptr) return out;
    switch (bo->getOpcode()) {
        case BinaryOperatorKind::BO_LT: out.op = CmpOp::LT; break;
        case BinaryOperatorKind::BO_LE: out.op = CmpOp::LE; break;
        case BinaryOperatorKind::BO_GT: out.op = CmpOp::GT; break;
        case BinaryOperatorKind::BO_GE: out.op = CmpOp::GE; break;
        default: return out;
    }
    const auto* lhs = llvm::dyn_cast<DeclRefExpr>(peel(bo->getLHS()));
    if (lhs == nullptr) return out;
    if (lhs->getDecl() != iv) return out;
    out.bound = bo->getRHS();
    out.ok = true;
    return out;
}

// ── Inc-clause parsing ─────────────────────────────────────────────────────

struct IncInfo {
    std::int64_t stride = 0;
    bool ok = false;
};

/// Evaluate `e` as a compile-time signed int literal. Uses
/// `EvaluateAsInt` so wrapped-in-paren / implicit-cast / unary-
/// minus-wrapped literals all work.
bool eval_int_literal(const Expr* e, clang::ASTContext& ctx,
                      std::int64_t& out) {
    if (e == nullptr) return false;
    clang::Expr::EvalResult r;
    if (!e->EvaluateAsInt(r, ctx)) return false;
    if (!r.Val.isInt()) return false;
    const llvm::APSInt v = r.Val.getInt();
    // Reject magnitudes that overflow int64 — any user loop with a
    // literal stride this large is pathological and falls back to the
    // caller's own error path.
    if (v.getSignificantBits() > 64) return false;
    out = v.getExtValue();
    return true;
}

/// Recognise constant-stride increments. See header-level doc for
/// the full shape list.
IncInfo parse_inc(const Expr* inc, const VarDecl* iv,
                  clang::ASTContext& ctx) {
    IncInfo out;
    if (inc == nullptr || iv == nullptr) return out;
    const Expr* e = peel(inc);

    // (1) `++i` / `--i` / `i++` / `i--`
    if (const auto* uo = llvm::dyn_cast<UnaryOperator>(e)) {
        const auto* dre =
            llvm::dyn_cast<DeclRefExpr>(peel(uo->getSubExpr()));
        if (dre == nullptr || dre->getDecl() != iv) return out;
        switch (uo->getOpcode()) {
            case UnaryOperatorKind::UO_PreInc:
            case UnaryOperatorKind::UO_PostInc:
                out.stride = 1; out.ok = true; return out;
            case UnaryOperatorKind::UO_PreDec:
            case UnaryOperatorKind::UO_PostDec:
                out.stride = -1; out.ok = true; return out;
            default:
                return out;
        }
    }

    // (2) `i += C` / `i -= C`
    if (const auto* cao = llvm::dyn_cast<CompoundAssignOperator>(e)) {
        const auto* lhs =
            llvm::dyn_cast<DeclRefExpr>(peel(cao->getLHS()));
        if (lhs == nullptr || lhs->getDecl() != iv) return out;
        std::int64_t v = 0;
        if (!eval_int_literal(cao->getRHS(), ctx, v)) return out;
        switch (cao->getOpcode()) {
            case BinaryOperatorKind::BO_AddAssign:
                out.stride = v; out.ok = true; return out;
            case BinaryOperatorKind::BO_SubAssign:
                out.stride = -v; out.ok = true; return out;
            default:
                return out;
        }
    }

    // (3) `i = i + C` / `i = C + i` / `i = i - C`
    if (const auto* bo = llvm::dyn_cast<BinaryOperator>(e)) {
        if (bo->getOpcode() != BinaryOperatorKind::BO_Assign)
            return out;
        const auto* lhs =
            llvm::dyn_cast<DeclRefExpr>(peel(bo->getLHS()));
        if (lhs == nullptr || lhs->getDecl() != iv) return out;
        const auto* rhs =
            llvm::dyn_cast<BinaryOperator>(peel(bo->getRHS()));
        if (rhs == nullptr) return out;
        const bool is_add = rhs->getOpcode() == BinaryOperatorKind::BO_Add;
        const bool is_sub = rhs->getOpcode() == BinaryOperatorKind::BO_Sub;
        if (!is_add && !is_sub) return out;
        const auto* lhs_ref =
            llvm::dyn_cast<DeclRefExpr>(peel(rhs->getLHS()));
        const auto* rhs_ref =
            llvm::dyn_cast<DeclRefExpr>(peel(rhs->getRHS()));
        std::int64_t v = 0;
        if (lhs_ref != nullptr && lhs_ref->getDecl() == iv) {
            // `i + C` / `i - C`
            if (!eval_int_literal(rhs->getRHS(), ctx, v)) return out;
            out.stride = is_add ? v : -v;
            out.ok = true;
            return out;
        }
        if (rhs_ref != nullptr && rhs_ref->getDecl() == iv && is_add) {
            // `C + i` — only valid for `+`; `C - i` would negate
            // the induction direction and is not a stride shape.
            if (!eval_int_literal(rhs->getLHS(), ctx, v)) return out;
            out.stride = v;
            out.ok = true;
            return out;
        }
        return out;
    }

    return out;
}

// ── Reversed-header assembly ───────────────────────────────────────────────

/// Upward forward (`<` / `<=` + positive stride) → downward reversed.
std::string build_upward_reverse(std::string_view iv,
                                 std::string_view lo,
                                 std::string_view hi,
                                 std::string_view s_text,
                                 bool inclusive) {
    std::string last;
    last.reserve(lo.size() + hi.size() + s_text.size() + 32);
    last += '(';
    last.append(lo.data(), lo.size());
    last += ") + (((";
    last.append(hi.data(), hi.size());
    last += ")";
    if (!inclusive) last += " - 1";
    last += " - (";
    last.append(lo.data(), lo.size());
    last += ")) / (";
    last.append(s_text.data(), s_text.size());
    last += ")) * (";
    last.append(s_text.data(), s_text.size());
    last += ")";

    std::string out;
    out.reserve(last.size() + lo.size() + s_text.size() + 48);
    out += "for (int ";
    out.append(iv.data(), iv.size());
    out += " = ";
    out += last;
    out += "; ";
    out.append(iv.data(), iv.size());
    out += " >= (";
    out.append(lo.data(), lo.size());
    out += "); ";
    out.append(iv.data(), iv.size());
    out += " -= (";
    out.append(s_text.data(), s_text.size());
    out += "))";
    return out;
}

/// Downward forward (`>` / `>=` + negative stride) → upward reversed.
std::string build_downward_reverse(std::string_view iv,
                                   std::string_view hi,
                                   std::string_view lo,
                                   std::string_view s_text,
                                   bool inclusive) {
    std::string last;
    last.reserve(lo.size() + hi.size() + s_text.size() + 32);
    last += '(';
    last.append(hi.data(), hi.size());
    last += ") - (((";
    last.append(hi.data(), hi.size());
    last += ") - (";
    last.append(lo.data(), lo.size());
    last += ")";
    if (!inclusive) last += " - 1";
    last += ") / (";
    last.append(s_text.data(), s_text.size());
    last += ")) * (";
    last.append(s_text.data(), s_text.size());
    last += ")";

    std::string out;
    out.reserve(last.size() + hi.size() + s_text.size() + 48);
    out += "for (int ";
    out.append(iv.data(), iv.size());
    out += " = ";
    out += last;
    out += "; ";
    out.append(iv.data(), iv.size());
    out += " <= (";
    out.append(hi.data(), hi.size());
    out += "); ";
    out.append(iv.data(), iv.size());
    out += " += (";
    out.append(s_text.data(), s_text.size());
    out += "))";
    return out;
}

} // namespace

// ── Public surface ──────────────────────────────────────────────────────────

std::string_view to_string(LoopRejectReason reason) {
    switch (reason) {
        case LoopRejectReason::None:                 return "none";
        case LoopRejectReason::NullStmt:             return "null_stmt";
        case LoopRejectReason::MissingClause:        return "missing_clause";
        case LoopRejectReason::NonCanonicalInit:     return "non_canonical_init";
        case LoopRejectReason::NonCanonicalCond:     return "non_canonical_cond";
        case LoopRejectReason::NonCanonicalInc:      return "non_canonical_inc";
        case LoopRejectReason::ZeroStride:           return "zero_stride";
        case LoopRejectReason::SourceRecoveryFailed: return "source_recovery_failed";
    }
    return {};
}

LoopReversalResult reverse_for_header(const ForStmt* fs,
                                      const SourceManager& sm,
                                      const LangOptions& lang) {
    LoopReversalResult result;

    if (fs == nullptr) {
        result.reason = LoopRejectReason::NullStmt;
        return result;
    }
    const Stmt* init = fs->getInit();
    const Expr* cond = fs->getCond();
    const Expr* inc  = fs->getInc();
    if (init == nullptr || cond == nullptr || inc == nullptr) {
        result.reason = LoopRejectReason::MissingClause;
        return result;
    }

    InitInfo ii = parse_init(init);
    if (!ii.ok) {
        result.reason = LoopRejectReason::NonCanonicalInit;
        return result;
    }
    const VarDecl* iv = ii.vd;
    clang::ASTContext& ctx = iv->getASTContext();

    CondInfo ci = parse_cond(cond, iv);
    if (!ci.ok) {
        result.reason = LoopRejectReason::NonCanonicalCond;
        return result;
    }

    IncInfo inci = parse_inc(inc, iv, ctx);
    if (!inci.ok) {
        result.reason = LoopRejectReason::NonCanonicalInc;
        return result;
    }
    if (inci.stride == 0) {
        result.reason = LoopRejectReason::ZeroStride;
        return result;
    }

    const std::string iv_name = iv->getNameAsString();
    if (iv_name.empty()) {
        result.reason = LoopRejectReason::SourceRecoveryFailed;
        return result;
    }
    const Expr* init_expr = iv->getInit();
    const std::string lo_text =
        source_text_of(init_expr->getSourceRange(), sm, lang);
    const std::string bound_text =
        source_text_of(ci.bound->getSourceRange(), sm, lang);
    if (lo_text.empty() || bound_text.empty()) {
        result.reason = LoopRejectReason::SourceRecoveryFailed;
        return result;
    }
    // Absolute-value stride spelling — sign is baked into the
    // reversed shape (`-=` for upward → downward, `+=` for
    // downward → upward).
    const std::int64_t abs_stride =
        inci.stride < 0 ? -inci.stride : inci.stride;
    const std::string s_text = std::to_string(abs_stride);

    // Pick the reversed shape from (cmp op, stride sign). The four
    // mismatched combinations (upward cmp + negative stride, etc.)
    // describe zero-iteration forward loops; we still emit a
    // syntactically-well-formed reversed header whose loop body
    // executes zero times, so the splice logic is uniform.
    const bool forward_is_up = (inci.stride > 0);
    const bool cmp_is_up = (ci.op == CmpOp::LT || ci.op == CmpOp::LE);
    const bool inclusive =
        (ci.op == CmpOp::LE || ci.op == CmpOp::GE);

    if (forward_is_up && cmp_is_up) {
        result.header = build_upward_reverse(
            iv_name, lo_text, bound_text, s_text, inclusive);
    } else if (!forward_is_up && !cmp_is_up) {
        result.header = build_downward_reverse(
            iv_name, lo_text, bound_text, s_text, inclusive);
    } else if (forward_is_up && !cmp_is_up) {
        result.header = build_downward_reverse(
            iv_name, lo_text, bound_text, s_text, inclusive);
    } else {
        result.header = build_upward_reverse(
            iv_name, lo_text, bound_text, s_text, inclusive);
    }

    result.induction_var = iv_name;
    result.reversed = true;
    result.reason = LoopRejectReason::None;
    return result;
}

} // namespace sturm::transpile
