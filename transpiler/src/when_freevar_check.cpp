// when_freevar_check.cpp — E7.M2 implementation. See header for the
// public contract. The walk maintains three accumulators: the
// (immutable) read-set, the body-local set (every VarDecl declared
// inside the walked subtree), and a visited-callees set keyed on
// `getCanonicalDecl()` to break cycles. On each assignment-shape node
// we peel the LHS to a `DeclRefExpr`, require its VarDecl be in the
// read-set AND not body-local, and emit one hard-error diagnostic.

#include "when_freevar_check.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <string>

namespace sturm::transpile {

namespace {

using VisitedSet =
    llvm::SmallPtrSet<const clang::FunctionDecl*, 8>;
using LocalSet =
    llvm::SmallPtrSet<const clang::VarDecl*, 8>;

// Peel implicit casts, parens, and temporary-materialize wrappers.
// Inlined from `matcher_common.hpp::peel_to_payload` so this TU does
// not pull in the full matcher header (and its qbool/qint deps).
const clang::Expr* peel(const clang::Expr* e) {
    if (!e) return nullptr;
    const clang::Expr* p = e->IgnoreParenImpCasts();
    if (const auto* mte =
            llvm::dyn_cast_or_null<clang::MaterializeTemporaryExpr>(p)) {
        const clang::Expr* sub = mte->getSubExpr();
        if (sub) p = sub->IgnoreParenImpCasts();
    }
    return p;
}

// Return the VarDecl the LHS references, or nullptr if it is not a
// bare variable (member access / subscript / call return are skipped).
const clang::VarDecl* lhs_var(const clang::Expr* lhs) {
    const clang::Expr* peeled = peel(lhs);
    const auto* dre =
        llvm::dyn_cast_or_null<clang::DeclRefExpr>(peeled);
    if (!dre) return nullptr;
    return llvm::dyn_cast_or_null<clang::VarDecl>(dre->getDecl());
}

// Assignment-shape OverloadedOperatorKind for CXXOperatorCallExpr.
// Mirrors `matcher_when_operand_mutation.cpp`'s set.
bool is_assign_shape_op(clang::OverloadedOperatorKind op) {
    switch (op) {
        case clang::OO_Equal:
        case clang::OO_PlusEqual:
        case clang::OO_MinusEqual:
        case clang::OO_StarEqual:
        case clang::OO_SlashEqual:
        case clang::OO_PercentEqual:
        case clang::OO_LessLessEqual:
        case clang::OO_GreaterGreaterEqual:
        case clang::OO_AmpEqual:
        case clang::OO_PipeEqual:
        case clang::OO_CaretEqual:
            return true;
        default:
            return false;
    }
}

class WriteFinder;

// Walk `root` with a fresh body-local set; emit one diagnostic per
// detected write through `engine.Report(...)`.
void walk_subtree(const clang::Stmt*                 root,
                  const WhenFreeVarReadSet&          readset,
                  clang::DiagnosticsEngine&          engine,
                  unsigned                           diag_id,
                  const clang::SourceManager&        sm,
                  VisitedSet&                        visited,
                  unsigned&                          writes_reported);

class WriteFinder
    : public clang::RecursiveASTVisitor<WriteFinder> {
public:
    WriteFinder(const WhenFreeVarReadSet*    readset,
                clang::DiagnosticsEngine*    engine,
                unsigned                     diag_id,
                const clang::SourceManager*  sm,
                VisitedSet*                  visited,
                unsigned*                    writes_reported)
        : readset_(readset), engine_(engine), diag_id_(diag_id),
          sm_(sm), visited_(visited),
          writes_reported_(writes_reported) {}

    // Body-local exclusion: every VarDecl visited inside the walked
    // subtree is recorded here so mutations targeting it are skipped.
    // Callee locals do not bleed into the WHEN body's exclusion set
    // because each callee descent constructs a fresh WriteFinder.
    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd) return true;
        body_locals_.insert(vd);
        return true;
    }

    // Plain `=` (compound forms parse as CompoundAssignOperator).
    bool VisitBinaryOperator(clang::BinaryOperator* bo) {
        if (!bo) return true;
        if (bo->getOpcode() != clang::BO_Assign) return true;
        check(bo->getLHS(), bo->getOperatorLoc());
        return true;
    }

    // `+=` / `-=` / `*=` / `/=` / `%=` / `<<=` / `>>=` / `&=` / `^=` / `|=`.
    bool VisitCompoundAssignOperator(clang::CompoundAssignOperator* cao) {
        if (!cao) return true;
        check(cao->getLHS(), cao->getOperatorLoc());
        return true;
    }

    // User-defined overloaded operators (e.g. qbool::operator^=).
    bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call) {
        if (!call) return true;
        if (!is_assign_shape_op(call->getOperator())) return true;
        if (call->getNumArgs() < 1) return true;
        check(call->getArg(0), call->getBeginLoc());
        return true;
    }

    // ++/-- (pre and post). Spec lists three core write shapes; the
    // sibling matcher in matcher_when_operand_mutation.cpp also covers
    // inc/dec, so we mirror it for user-consistency.
    bool VisitUnaryOperator(clang::UnaryOperator* uo) {
        if (!uo) return true;
        const auto op = uo->getOpcode();
        const bool is_inc_dec =
            op == clang::UO_PreInc  || op == clang::UO_PreDec ||
            op == clang::UO_PostInc || op == clang::UO_PostDec;
        if (!is_inc_dec) return true;
        check(uo->getSubExpr(), uo->getOperatorLoc());
        return true;
    }

    // Recurse into callee bodies. Indirect calls and unanalyzed
    // declarations are skipped (no source location to cite).
    bool VisitCallExpr(clang::CallExpr* ce) {
        if (!ce || !visited_) return true;
        const clang::FunctionDecl* fd = ce->getDirectCallee();
        if (!fd) return true;
        const clang::FunctionDecl* canon = fd->getCanonicalDecl();
        if (!canon) canon = fd;
        if (!visited_->insert(canon).second) return true;
        const clang::FunctionDecl* def = fd->getDefinition();
        const clang::Stmt*         body = def ? def->getBody() : nullptr;
        if (!body) return true;
        walk_subtree(body, *readset_, *engine_, diag_id_, *sm_,
                     *visited_, *writes_reported_);
        return true;
    }

private:
    // Common LHS-classifier + emission path. `report_loc` is the
    // operator loc for binary/compound/unary shapes; call begin for
    // the C++ operator-call shape.
    void check(const clang::Expr* lhs, clang::SourceLocation report_loc) {
        if (!lhs || !engine_ || !sm_ || !readset_ || !writes_reported_) {
            return;
        }
        const clang::VarDecl* vd = lhs_var(lhs);
        if (!vd) return;
        if (body_locals_.count(vd)) return;
        if (!readset_->vars.count(vd)) return;

        // `getFileLoc` makes the diagnostic cite user source under
        // both the standalone driver and the plugin's nested
        // CompilerInvocation. Matches PM3-2 / PM3-3 plumbing.
        const clang::SourceLocation file_loc = sm_->getFileLoc(report_loc);
        engine_->Report(file_loc, diag_id_)
            << std::string(vd->getNameAsString());
        ++(*writes_reported_);
    }

    const WhenFreeVarReadSet*    readset_;
    clang::DiagnosticsEngine*    engine_;
    unsigned                     diag_id_;
    const clang::SourceManager*  sm_;
    VisitedSet*                  visited_;
    unsigned*                    writes_reported_;
    LocalSet                     body_locals_;
};

void walk_subtree(const clang::Stmt*                 root,
                  const WhenFreeVarReadSet&          readset,
                  clang::DiagnosticsEngine&          engine,
                  unsigned                           diag_id,
                  const clang::SourceManager&        sm,
                  VisitedSet&                        visited,
                  unsigned&                          writes_reported) {
    if (!root) return;
    WriteFinder finder(&readset, &engine, diag_id, &sm, &visited,
                       &writes_reported);
    finder.TraverseStmt(const_cast<clang::Stmt*>(root));
}

// Hard-error diag-ID. `%0` is the variable identifier; literal "WHEN"
// in the prose lets a future reformatting add a richer scope slot
// (`%1`) without breaking existing substring assertions.
unsigned register_diag_id(clang::DiagnosticsEngine& engine) {
    constexpr const char kFmt[] =
        "STURM: WHEN free variable '%0' is mutated inside the WHEN "
        "body - mutation of a control free-variable corrupts the "
        "adjoint (P4).";
    return engine.getDiagnosticIDs()->getCustomDiagID(
        clang::DiagnosticIDs::Error,
        llvm::StringRef(kFmt, sizeof(kFmt) - 1));
}

} // namespace

WhenFreeVarWriteCheck check_when_freevar_writes(
    const clang::Stmt*           body,
    const WhenFreeVarReadSet&    readset,
    clang::DiagnosticsEngine&    engine,
    const clang::SourceManager&  sm,
    clang::SourceLocation        when_scope_loc) {
    (void)when_scope_loc; // reserved for a future scope-citation slot.
    WhenFreeVarWriteCheck out;
    if (!body) return out;
    if (readset.vars.empty()) return out;

    const unsigned diag_id = register_diag_id(engine);
    VisitedSet visited;
    walk_subtree(body, readset, engine, diag_id, sm, visited,
                 out.writes_reported);
    return out;
}

} // namespace sturm::transpile
