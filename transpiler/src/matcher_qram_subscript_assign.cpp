// matcher_qram_subscript_assign.cpp -- sturm-u9ge.6 (Beat H1) implementation.
//
// Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1. See
// `matcher_qram_subscript_assign.hpp` for the contract.
//
// Strategy: anchor on every `CXXOperatorCallExpr` with `OO_Equal`,
// gate run-time on (1) LHS type is frontend `qint`, (2) RHS after
// peel is exactly a subscript with qint-UDC index. On match publish
// one `QramSubscriptAssignHit`. Disjoint from C1 / H4 (those anchor
// on VarDecl initializers; H1 anchors on assignment op-calls).

#include "matcher_qram_subscript_assign.hpp"
#include "width_inference.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// UDC discriminator -- mirrors matcher_qram_subscript.cpp::QintUdcFinder.
class QintUdcFinder : public RecursiveASTVisitor<QintUdcFinder> {
public:
    bool VisitImplicitCastExpr(ImplicitCastExpr* ice) {
        if (found_) return false;
        if (!ice || ice->getCastKind() != CK_UserDefinedConversion) return true;
        const Expr* sub = ice->getSubExpr();
        if (!sub) return true;
        const auto* mce =
            llvm::dyn_cast_or_null<CXXMemberCallExpr>(sub->IgnoreParens());
        if (!mce) return true;
        const auto* fd = mce->getDirectCallee();
        const auto* cd = llvm::dyn_cast_or_null<CXXConversionDecl>(fd);
        if (!cd) return true;
        const CXXRecordDecl* parent = cd->getParent();
        if (!parent) return true;
        if (parent->getNameAsString() != "qint") return true;
        found_ = ice;
        return false;
    }
    const ImplicitCastExpr* find() const { return found_; }
private:
    const ImplicitCastExpr* found_ = nullptr;
};

bool index_has_qint_udc(const Expr* e) {
    if (!e) return false;
    QintUdcFinder f;
    f.TraverseStmt(const_cast<Expr*>(e));
    return f.find() != nullptr;
}

// Container-kind discrimination -- mirror C1.
QramContainerKind discriminate_array_subscript(const ArraySubscriptExpr& e) {
    const Expr* base = e.getBase();
    if (!base) return QramContainerKind::Pointer;
    QualType bt = base->IgnoreParenImpCasts()->getType();
    if (bt.isNull()) return QramContainerKind::Pointer;
    if (bt->isArrayType()) return QramContainerKind::CArray;
    return QramContainerKind::Pointer;
}

// Pointer-arm length recovery -- mirror C1::recover_pointer_length_text.
std::string recover_pointer_length_text(const Expr* container) {
    if (!container) return {};
    const Expr* base = container->IgnoreParenImpCasts();
    const auto* dre = llvm::dyn_cast_or_null<DeclRefExpr>(base);
    if (!dre) return {};
    const auto* container_param =
        llvm::dyn_cast_or_null<ParmVarDecl>(dre->getDecl());
    if (!container_param) return {};
    const auto* fd =
        llvm::dyn_cast_or_null<FunctionDecl>(
            container_param->getDeclContext());
    if (!fd) return {};
    const unsigned n = fd->getNumParams();
    for (unsigned i = 0; i + 1 < n; ++i) {
        if (fd->getParamDecl(i) != container_param) continue;
        const ParmVarDecl* next = fd->getParamDecl(i + 1);
        if (!next) return {};
        QualType qt = next->getType();
        if (qt.isNull() || !qt->isIntegerType()) return {};
        return next->getNameAsString();
    }
    return {};
}

// Frontend qint type predicate -- mirrors C1's hasName("qint").
bool expr_type_is_frontend_qint(const Expr* e) {
    if (!e) return false;
    QualType qt = e->getType();
    if (qt.isNull()) return false;
    const Type* t = qt.getCanonicalType().getTypePtrOrNull();
    if (!t) return false;
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    if (!rd) return false;
    return rd->getNameAsString() == "qint";
}

// Width inference helpers -- mirror width_inference.cpp's element peel.
std::optional<unsigned> peel_qint_t_width_local(QualType qt) {
    qt = qt.getCanonicalType();
    const auto* record = qt->getAsCXXRecordDecl();
    if (!record) return std::nullopt;
    const auto* spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(record);
    if (!spec) return std::nullopt;
    const ClassTemplateDecl* primary = spec->getSpecializedTemplate();
    if (!primary) return std::nullopt;
    if (primary->getQualifiedNameAsString() != "sturm::qint_t") {
        return std::nullopt;
    }
    const TemplateArgumentList& args = spec->getTemplateArgs();
    if (args.size() == 0) return std::nullopt;
    const TemplateArgument& arg0 = args.get(0);
    if (arg0.getKind() != TemplateArgument::Integral) return std::nullopt;
    const llvm::APSInt& aps = arg0.getAsIntegral();
    const int64_t v = aps.getExtValue();
    if (v <= 0) return std::nullopt;
    return static_cast<unsigned>(v);
}

// Container element type walk -- same three-shape posture as
// width_inference.cpp::element_qint_t_width.
std::optional<unsigned> element_qint_t_width(const Expr* container) {
    if (!container) return std::nullopt;
    QualType ct = container->IgnoreParenImpCasts()->getType();
    if (ct.isNull()) return std::nullopt;
    QualType elem;
    if (ct->isPointerType()) {
        elem = ct->getPointeeType();
    } else if (ct->isArrayType()) {
        const auto* at = ct->getAsArrayTypeUnsafe();
        if (!at) return std::nullopt;
        elem = at->getElementType();
    } else {
        const auto* record = ct.getCanonicalType()->getAsCXXRecordDecl();
        if (!record) return std::nullopt;
        const auto* spec =
            llvm::dyn_cast<ClassTemplateSpecializationDecl>(record);
        if (!spec) return std::nullopt;
        const TemplateArgumentList& args = spec->getTemplateArgs();
        if (args.size() == 0) return std::nullopt;
        const TemplateArgument& arg0 = args.get(0);
        if (arg0.getKind() != TemplateArgument::Type) return std::nullopt;
        elem = arg0.getAsType();
    }
    return peel_qint_t_width_local(elem);
}

// Recover the VarDecl bound by an LHS DRE. Returns nullptr if the LHS
// is not a direct DRE (e.g. member access). Used as a hint only.
const VarDecl* lhs_var_decl(const Expr* lhs) {
    if (!lhs) return nullptr;
    const Expr* peeled = lhs->IgnoreParenImpCasts();
    const auto* dre = llvm::dyn_cast_or_null<DeclRefExpr>(peeled);
    if (!dre) return nullptr;
    return llvm::dyn_cast_or_null<VarDecl>(dre->getDecl());
}

// Subscript-shape discrimination on RHS. Returns the subscript triple
// if the RHS, after peel, is exactly a subscript expression with
// qint-UDC index. Returns std::nullopt otherwise (RHS is a larger
// expression, classical-int index, etc.).
struct SubscriptTriple {
    const Expr* subscript_expr = nullptr;
    const Expr* container_expr = nullptr;
    const Expr* index_expr     = nullptr;
    QramContainerKind kind     = QramContainerKind::StdArray;
};

// Peel the implicit-conversion chain that may wrap a `qint_t<W>` rvalue
// when the production frontend `qint` lacks a direct
// `qint& operator=(const qint_t<W>&)` overload (sturm-ddgo). In that
// shape, `b = a[i];` parses as
//
//     CXXOperatorCallExpr '='
//       arg(0): DeclRefExpr 'b'
//       arg(1): MaterializeTemporaryExpr
//                 ImplicitCastExpr <ConstructorConversion>
//                   CXXConstructExpr (qint(const qint_t<W>&))
//                     [ImplicitCastExpr <NoOp>]
//                       [CXXBindTemporaryExpr]
//                         <CXXOperatorCallExpr '[]' or ArraySubscriptExpr>
//
// vs the hermetic-fixture shape where the qint class declares the
// overload directly and the RHS is the bare subscript after
// `IgnoreParenImpCasts()`. We accept both: peel through any combination
// of MaterializeTemporaryExpr / ExprWithCleanups / CXXBindTemporaryExpr
// / CXXConstructExpr (whose constructed type is the frontend qint AND
// whose first ctor argument is the qint_t<W>) until we hit the bare
// subscript.
const Expr* peel_to_subscript(const Expr* e) {
    if (!e) return nullptr;
    for (int guard = 0; guard < 16; ++guard) {
        if (!e) return nullptr;
        const Expr* before = e;
        e = e->IgnoreParenImpCasts();
        if (!e) return nullptr;
        if (const auto* mte = llvm::dyn_cast<MaterializeTemporaryExpr>(e)) {
            e = mte->getSubExpr();
            continue;
        }
        if (const auto* ewc = llvm::dyn_cast<ExprWithCleanups>(e)) {
            e = ewc->getSubExpr();
            continue;
        }
        if (const auto* btx = llvm::dyn_cast<CXXBindTemporaryExpr>(e)) {
            e = btx->getSubExpr();
            continue;
        }
        if (const auto* cxe = llvm::dyn_cast<CXXConstructExpr>(e)) {
            // Only peel through the converting ctor that wraps a
            // qint_t<W> rvalue into a frontend qint — i.e. the
            // CXXConstructExpr's constructed type is the frontend qint
            // class and it has at least one argument (the source
            // qint_t<W>). Without this guard a subscript-of-record-of-
            // record-of-record initializer chain might be peeled in
            // ways that are not what we want.
            if (cxe->getNumArgs() == 0) return nullptr;
            // Type guard: the constructed type must be (canonically) a
            // CXXRecordDecl named "qint" — mirrors the LHS-type gate
            // above.
            QualType ct = cxe->getType();
            if (ct.isNull()) return nullptr;
            const CXXRecordDecl* rd =
                ct.getCanonicalType()->getAsCXXRecordDecl();
            if (!rd || rd->getNameAsString() != "qint") return nullptr;
            e = cxe->getArg(0);
            continue;
        }
        // No more peel layers we recognise; stop. If we hit the same
        // node twice the loop guard prevents infinite progress.
        if (e == before) break;
    }
    return e;
}

std::optional<SubscriptTriple> subscript_triple_from_rhs(const Expr* rhs) {
    if (!rhs) return std::nullopt;
    const Expr* peeled = peel_to_subscript(rhs);
    if (!peeled) return std::nullopt;
    if (const auto* ase = llvm::dyn_cast<ArraySubscriptExpr>(peeled)) {
        if (!index_has_qint_udc(ase->getIdx())) return std::nullopt;
        SubscriptTriple t;
        t.subscript_expr = ase;
        t.container_expr = ase->getBase();
        t.index_expr     = ase->getIdx();
        t.kind           = discriminate_array_subscript(*ase);
        return t;
    }
    if (const auto* coe = llvm::dyn_cast<CXXOperatorCallExpr>(peeled)) {
        if (coe->getOperator() != OO_Subscript) return std::nullopt;
        if (coe->getNumArgs() != 2) return std::nullopt;
        const Expr* idx = coe->getArg(1);
        if (!index_has_qint_udc(idx)) return std::nullopt;
        SubscriptTriple t;
        t.subscript_expr = coe;
        t.container_expr = coe->getArg(0);
        t.index_expr     = idx;
        t.kind           = QramContainerKind::StdArray;
        return t;
    }
    return std::nullopt;
}

// Anchor callback: assignment op-call.
class AssignCallback : public MatchFinder::MatchCallback {
public:
    explicit AssignCallback(std::vector<QramSubscriptAssignHit>* hits)
        : hits_(hits) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* op = r.Nodes.getNodeAs<CXXOperatorCallExpr>("assign");
        if (!op || !hits_) return;
        if (op->getOperator() != OO_Equal) return;
        if (op->getNumArgs() != 2) return;
        const Expr* lhs = op->getArg(0);
        const Expr* rhs = op->getArg(1);
        if (!lhs || !rhs) return;
        // Gate (1): LHS must be a frontend qint lvalue. Filters out
        // the OOS write shape `a[i] = b;` (LHS subscript yields the
        // backend element type, NOT frontend qint).
        if (!expr_type_is_frontend_qint(lhs)) return;
        // Gate (2): RHS is exactly a subscript with qint-UDC index.
        auto triple = subscript_triple_from_rhs(rhs);
        if (!triple) return;
        QramSubscriptAssignHit hit;
        hit.kind           = triple->kind;
        hit.assign_expr    = op;
        hit.target_expr    = lhs;
        hit.target_var     = lhs_var_decl(lhs);
        hit.subscript_expr = triple->subscript_expr;
        hit.container_expr = triple->container_expr;
        hit.index_expr     = triple->index_expr;
        if (auto w = element_qint_t_width(triple->container_expr)) {
            hit.W = *w;
        } else {
            hit.W = kDefaultWidth;
        }
        if (hit.kind == QramContainerKind::Pointer) {
            hit.length_text = recover_pointer_length_text(triple->container_expr);
        }
        hits_->push_back(hit);
    }
private:
    std::vector<QramSubscriptAssignHit>* hits_;
};

template <class T>
std::vector<std::unique_ptr<T>>& callback_pool() {
    static std::vector<std::unique_ptr<T>> pool;
    return pool;
}

} // anonymous namespace

void register_qram_subscript_assign_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QramSubscriptAssignHit>& hits) {
    auto& pool = callback_pool<AssignCallback>();
    pool.push_back(std::make_unique<AssignCallback>(&hits));
    // Anchor: every CXXOperatorCallExpr in the TU. The callback's
    // OO_Equal + LHS-type + RHS-shape gates filter to the bare
    // existing-target shape.
    finder.addMatcher(
        cxxOperatorCallExpr().bind("assign"),
        pool.back().get());
}

} // namespace sturm::transpile
