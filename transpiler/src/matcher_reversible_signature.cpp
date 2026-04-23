// matcher_reversible_signature.cpp — Phase Q Q-B (sturm-5kgu.3):
// parameter-list signature enforcement for `[[sturm::reversible]]`
// user routines. See the sibling header for the contract.
//
// The module walks the parameter list of every reversible forward
// routine and rejects three classes per PRD §5.3 / §9 P9b:
//
//   1. PointerParam         — `qbool*` / `qint*` / `qint_t*` in any
//                             cv-qualification. Rejected on sight
//                             (body is not consulted).
//   2. ValueParamMutated    — by-value non-`const` quantum parameter
//                             whose VarDecl is the target of an
//                             assignment-shape op inside the body.
//   3. ConstRefParamMutated — `const`-qualified reference-to-quantum
//                             parameter whose VarDecl is the target
//                             of an assignment-shape op inside the
//                             body.
//
// Per-parameter classification order (so a parameter votes for at
// most one reject reason):
//
//   pointer → const-ref-mutated → value-mutated
//
// A pointer parameter short-circuits the mutation scan (the pointer
// shape is already enough to reject). The const-ref / value checks
// only fire when the body actually contains a matching mutation.
//
// First reject wins on `.reason`; every subsequent reject still fires
// via `DiagContext` so the user sees every site in one pass. All
// `SourceLocation`s funnel through `getFileLoc`.

#include "matcher_reversible_signature.hpp"

#include "diag_context.hpp"
#include "reversible_attribute.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Casting.h"

#include <string>
#include <string_view>

namespace sturm::transpile {

// ── Public: human-readable reason spellings ────────────────────────

std::string_view to_string(ReversibleSigRejectReason reason) {
    switch (reason) {
        case ReversibleSigRejectReason::None:               return "none";
        case ReversibleSigRejectReason::NullDecl:           return "null_decl";
        case ReversibleSigRejectReason::NotReversible:      return "not_reversible";
        case ReversibleSigRejectReason::PointerParam:       return "pointer_param";
        case ReversibleSigRejectReason::ValueParamMutated:  return "value_param_mutated";
        case ReversibleSigRejectReason::ConstRefParamMutated:
            return "const_ref_param_mutated";
    }
    return {}; // unreachable for a well-formed enum
}

namespace {

using namespace clang;

// Return true iff `qt` resolves (after stripping top-level references
// + qualifications + pointer) to a CXXRecord named `qbool`, `qint`, or
// `qint_t`. Mirrors the heuristic used across the other validators
// and matchers.
bool type_points_or_refers_to_quantum(QualType qt) {
    if (qt.isNull()) return false;
    QualType stripped = qt;
    // Peel a single pointer (the Q-B pointer check peeks one level
    // down so `qbool*` and `const qbool*` both classify as quantum).
    if (stripped->isPointerType()) {
        stripped = stripped->getPointeeType();
    }
    stripped = stripped.getNonReferenceType().getUnqualifiedType();
    if (stripped.isNull()) return false;
    const CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// Return true iff `qt` is a bare quantum record — no reference, no
// pointer, any cv qualification. Used to classify by-value
// parameters.
bool is_bare_quantum(QualType qt) {
    if (qt.isNull()) return false;
    if (qt->isReferenceType()) return false;
    if (qt->isPointerType()) return false;
    QualType stripped = qt.getUnqualifiedType();
    const CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// Return true iff `qt` is a reference to a quantum record (regardless
// of the reference's cv qualification on the pointee).
bool is_quantum_ref(QualType qt) {
    if (qt.isNull()) return false;
    if (!qt->isReferenceType()) return false;
    QualType pointee = qt.getNonReferenceType();
    if (pointee.isNull()) return false;
    const CXXRecordDecl* rd = pointee.getUnqualifiedType()->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// Return true iff `qt` is a reference whose pointee is `const`-
// qualified. For `const qbool&` this is true; for `qbool&` it is
// false.
bool is_const_ref(QualType qt) {
    if (qt.isNull()) return false;
    if (!qt->isReferenceType()) return false;
    QualType pointee = qt.getNonReferenceType();
    if (pointee.isNull()) return false;
    return pointee.isConstQualified();
}

// Return true iff `qt` is a pointer type to a quantum record, at any
// cv-qualification of either the pointer itself or the pointee.
bool is_quantum_pointer(QualType qt) {
    if (qt.isNull()) return false;
    if (!qt->isPointerType()) return false;
    QualType pointee = qt->getPointeeType();
    if (pointee.isNull()) return false;
    const CXXRecordDecl* rd =
        pointee.getUnqualifiedType()->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// ── Body walker — collect every ParmVarDecl that the body mutates ──
//
// The walker scans for assignment-shape `CXXOperatorCallExpr` and
// inc/dec `UnaryOperator` nodes. For each match it peels the LHS (or
// operand) to a `DeclRefExpr`; if the referenced decl is a
// `ParmVarDecl`, the parameter is inserted into `muted_params_`.
//
// Peeling unwraps `ImplicitCastExpr` (l-to-r, etc.), `ParenExpr`, and
// `CXXConstCastExpr` — the last one is important for the P9b
// const-ref enforcement path, where a user might `const_cast` a
// const-ref to fake mutation on a custom quantum type. The cast's
// source expression still names the original parameter, and that is
// what Q-B must fire on.
class ParameterMutationCollector
    : public RecursiveASTVisitor<ParameterMutationCollector> {
public:
    using Set = llvm::SmallPtrSetImpl<const ParmVarDecl*>;

    explicit ParameterMutationCollector(Set* out) : out_(out) {}

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* call) {
        if (!call || !out_) return true;
        const OverloadedOperatorKind op = call->getOperator();
        const bool is_assign_shape =
            (op == OO_Equal)         ||
            (op == OO_CaretEqual)    ||
            (op == OO_PlusEqual)     ||
            (op == OO_MinusEqual)    ||
            (op == OO_StarEqual)     ||
            (op == OO_SlashEqual)    ||
            (op == OO_PercentEqual)  ||
            (op == OO_PipeEqual)     ||
            (op == OO_AmpEqual)      ||
            (op == OO_PlusPlus)      ||
            (op == OO_MinusMinus);
        if (!is_assign_shape) return true;
        if (call->getNumArgs() < 1) return true;
        check_lhs(call->getArg(0));
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* uop) {
        if (!uop || !out_) return true;
        const UnaryOperatorKind k = uop->getOpcode();
        const bool is_inc_dec =
            k == UO_PreInc || k == UO_PreDec ||
            k == UO_PostInc || k == UO_PostDec;
        if (!is_inc_dec) return true;
        check_lhs(uop->getSubExpr());
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator* bop) {
        if (!bop || !out_) return true;
        if (!bop->isAssignmentOp() && !bop->isCompoundAssignmentOp()) {
            return true;
        }
        check_lhs(bop->getLHS());
        return true;
    }

private:
    // Strip paren / implicit-cast / const-cast / material-temp
    // wrappers to reach the inner DeclRefExpr. The const-cast peel is
    // important for the P9b const-ref enforcement path.
    const Expr* peel(const Expr* e) const {
        if (!e) return nullptr;
        const Expr* cur = e->IgnoreParenImpCasts();
        while (true) {
            if (const auto* cce = llvm::dyn_cast<CXXConstCastExpr>(cur)) {
                if (const Expr* sub = cce->getSubExpr()) {
                    cur = sub->IgnoreParenImpCasts();
                    continue;
                }
            }
            if (const auto* mte =
                    llvm::dyn_cast<MaterializeTemporaryExpr>(cur)) {
                if (const Expr* sub = mte->getSubExpr()) {
                    cur = sub->IgnoreParenImpCasts();
                    continue;
                }
            }
            break;
        }
        return cur;
    }

    void check_lhs(const Expr* lhs) {
        const Expr* inner = peel(lhs);
        if (!inner) return;
        const auto* dre = llvm::dyn_cast_or_null<DeclRefExpr>(inner);
        if (!dre) return;
        const auto* pvd = llvm::dyn_cast_or_null<ParmVarDecl>(dre->getDecl());
        if (!pvd) return;
        out_->insert(pvd);
    }

    Set* out_;
};

// ── Per-parameter classifier — decide which Q-B class (if any) a
//    given parameter falls into, then fire the matching diagnostic.
//
// Returns a `ReversibleSigRejectReason` enum:
//   - `None`                  → parameter is canonical, no fire.
//   - `PointerParam`          → pointer-to-quantum, fires.
//   - `ValueParamMutated`     → by-value non-const quantum, mutated.
//   - `ConstRefParamMutated`  → const-qualified ref-to-quantum, mutated.
//
// Called once per parameter after the mutation scan has built the
// body's mutated-parameter set.
ReversibleSigRejectReason classify_param(
    const ParmVarDecl* p,
    const llvm::SmallPtrSetImpl<const ParmVarDecl*>& muted) {
    if (!p) return ReversibleSigRejectReason::None;
    const QualType qt = p->getType();

    // 1. Pointer-to-quantum, unconditional reject. The body is not
    //    consulted — the shape itself is illegal.
    if (is_quantum_pointer(qt)) {
        return ReversibleSigRejectReason::PointerParam;
    }

    const bool mutated_in_body = muted.count(p) != 0;

    // 2. Const-qualified reference to quantum + mutated → reject.
    if (is_quantum_ref(qt) && is_const_ref(qt) && mutated_in_body) {
        return ReversibleSigRejectReason::ConstRefParamMutated;
    }

    // 3. By-value non-const quantum + mutated → reject. We only fire
    //    on the mutated case — a non-const by-value parameter that
    //    nobody touches is a legitimate "read-only copy" per P9b.
    if (is_bare_quantum(qt) && !qt.isConstQualified() && mutated_in_body) {
        return ReversibleSigRejectReason::ValueParamMutated;
    }

    return ReversibleSigRejectReason::None;
}

} // namespace

// ── Public: entry point ────────────────────────────────────────────

ReversibleSignatureResult validate_reversible_signature(
    const clang::FunctionDecl* fd,
    clang::ASTContext& ctx,
    DiagContext& diag) {
    ReversibleSignatureResult result;

    // Silent rejects: null / non-reversible. Opt-in contract — no
    // diagnostic fires on these paths.
    if (fd == nullptr) {
        result.valid = false;
        result.reason = ReversibleSigRejectReason::NullDecl;
        return result;
    }
    if (!is_reversible(fd)) {
        result.valid = false;
        result.reason = ReversibleSigRejectReason::NotReversible;
        return result;
    }

    const SourceManager& sm = ctx.getSourceManager();
    const std::string fn_name = fd->getNameAsString();

    // Build the body's mutated-parameter set. When the FD has no
    // body (forward declaration), the set is empty — by-value /
    // const-ref parameters are tolerated in that case since we have
    // no evidence of mutation. Pointer parameters still reject on
    // sight.
    llvm::SmallPtrSet<const ParmVarDecl*, 4> muted;
    if (const Stmt* body = fd->getBody()) {
        ParameterMutationCollector collector(&muted);
        collector.TraverseStmt(const_cast<Stmt*>(body));
    }

    // Walk parameters in declaration order. Every offending
    // parameter fires one diagnostic; the first reject wins on
    // `.reason`.
    ReversibleSigRejectReason first_reason =
        ReversibleSigRejectReason::None;
    unsigned fired = 0;

    for (unsigned i = 0; i < fd->getNumParams(); ++i) {
        const ParmVarDecl* p = fd->getParamDecl(i);
        if (!p) continue;
        const ReversibleSigRejectReason r = classify_param(p, muted);
        if (r == ReversibleSigRejectReason::None) continue;

        // First reject wins on `.reason`.
        if (first_reason == ReversibleSigRejectReason::None) {
            first_reason = r;
        }

        // Funnel the param's location through `getFileLoc` so macro-
        // expanded parameters still cite the user's file.
        const SourceLocation loc = sm.getFileLoc(p->getLocation());
        const std::string pname = p->getNameAsString();

        switch (r) {
            case ReversibleSigRejectReason::PointerParam:
                diag.report_reversible_pointer_param(
                    loc,
                    std::string_view(fn_name),
                    std::string_view(pname));
                break;
            case ReversibleSigRejectReason::ValueParamMutated:
                diag.report_reversible_value_param_mutated(
                    loc,
                    std::string_view(fn_name),
                    std::string_view(pname));
                break;
            case ReversibleSigRejectReason::ConstRefParamMutated:
                diag.report_reversible_const_ref_mutated(
                    loc,
                    std::string_view(fn_name),
                    std::string_view(pname));
                break;
            default:
                break;
        }
        ++fired;
    }

    result.diagnostics_fired = fired;
    result.reason = first_reason;
    result.valid = (first_reason == ReversibleSigRejectReason::None);
    return result;
}

} // namespace sturm::transpile
