// matcher_qram_subscript_expr.cpp — sturm-u9ge.9 (Beat H4) implementation.
//
// Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4. See
// `matcher_qram_subscript_expr.hpp` for the contract.
//
// Implementation strategy
// -----------------------
// We anchor on every `varDecl` of frontend `qint` and walk the
// initializer subtree collecting subscript sites. The walk is
// deliberately *less* restrictive than C1's "subscript IS the
// immediate initializer" gate — H4 fires precisely when the subscript
// is a *strict sub-expression* of the initializer (e.g. inside a
// binary op, a paren, a cast). The disjointness with C1 is enforced
// by checking that the matched subscript is NOT the immediate
// initializer of the VarDecl: if it is, this is C1's territory and
// we skip.
//
// For each surviving subscript site:
//   - kind: discriminate from the AST node class plus base-expression
//     type (StdArray for op-call, CArray vs Pointer for built-in).
//   - target_var: the enclosing VarDecl (the same for every subscript
//     in a given initializer, so a multi-subscript initializer
//     publishes multiple hits sharing this field).
//   - subscript_expr / container_expr / index_expr: per-site triple
//     the emitter pivots on for in-place rewrite.
//   - W: B1's `infer_width()` (single source of truth).
//   - length_text: pointer-arm sibling-parameter heuristic, identical
//     to C1's `recover_pointer_length_text`.
//
// Disjointness vs C1 (the immediate-initializer gate)
// ---------------------------------------------------
// Clang wraps a copy-initialised frontend `qint c = a[i];` with
// `<ConstructorConversion>` ICE + `CXXConstructExpr` around the
// subscript. The C1 matcher peels both layers and matches only when
// the subscript is the immediate construct-arg. H4 mirrors that
// peeling and *rejects* sites where the subscript IS the immediate
// construct-arg — those go to C1. Anything else (binary op, paren,
// cast, function call, ternary, etc. wrapping the subscript) is H4's.

#include "matcher_qram_subscript_expr.hpp"
#include "width_inference.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// ── UDC discriminator (mirrors matcher_qram_subscript.cpp::QintUdcFinder) ──
class QintUdcFinder : public RecursiveASTVisitor<QintUdcFinder> {
public:
    bool VisitImplicitCastExpr(ImplicitCastExpr* ice) {
        if (found_) return false;
        if (!ice || ice->getCastKind() != CK_UserDefinedConversion) {
            return true;
        }
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

// ── Container-kind discrimination (mirror C1) ──────────────────────────────
QramContainerKind discriminate_array_subscript(
    const ArraySubscriptExpr& e) {
    const Expr* base = e.getBase();
    if (!base) return QramContainerKind::Pointer;
    QualType bt = base->IgnoreParenImpCasts()->getType();
    if (bt.isNull()) return QramContainerKind::Pointer;
    if (bt->isArrayType()) return QramContainerKind::CArray;
    return QramContainerKind::Pointer;
}

// ── Pointer-arm length recovery (mirror C1::recover_pointer_length_text) ───
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

// ── "Is the subscript the immediate initializer?" gate (C1's territory) ────
//
// C1 anchors when the subscript IS the immediate copy-init expression.
// Specifically, Clang wraps `qint c = a[i];` as
//   VarDecl
//     `-CXXConstructExpr (frontend qint copy-ctor)
//        `-ImplicitCastExpr <ConstructorConversion>
//           `-<the subscript node>
// Walk the VarDecl's initializer through any `<ConstructorConversion>`
// ICEs + CXXConstructExpr layers. If the unwrapped node IS the
// subscript we're checking, this is C1's case and H4 must skip.
//
// Returns true iff `sub` is the immediate (post-peel) initializer of
// `vd` — i.e. C1 would handle this site.
bool is_immediate_init(const VarDecl* vd, const Expr* sub) {
    if (!vd || !sub) return false;
    const Expr* init = vd->getInit();
    if (!init) return false;
    const Expr* peeled = init->IgnoreImplicit();
    if (peeled == nullptr) return false;
    // Peel through CXXConstructExpr (the frontend qint copy-ctor) and
    // its inner argument; mirrors C1's `cxxConstructExpr(hasArgument(
    // 0, ignoringParenImpCasts(<subscript>)))` pattern.
    if (const auto* ce = llvm::dyn_cast<CXXConstructExpr>(peeled)) {
        if (ce->getNumArgs() == 0) return false;
        const Expr* arg0 = ce->getArg(0);
        if (!arg0) return false;
        const Expr* inner = arg0->IgnoreParenImpCasts();
        return inner == sub;
    }
    // Defensive: if no construct wrapper (e.g. a future v2 form
    // where copy-init elides the construct expression), treat the
    // peeled init itself as the immediate node.
    return peeled->IgnoreParenImpCasts() == sub;
}

// ── Hit-collection visitor ──────────────────────────────────────────────────
//
// Walk the VarDecl's initializer subtree gathering every subscript site
// that:
//   * carries the qint UDC at its index, AND
//   * is NOT the immediate initializer (those are C1's).
// One hit is published per subscript site.
class HitCollector : public RecursiveASTVisitor<HitCollector> {
public:
    HitCollector(const VarDecl* vd,
                 std::vector<QramSubscriptExprHit>* hits)
        : vd_(vd), hits_(hits) {}

    bool VisitArraySubscriptExpr(ArraySubscriptExpr* e) {
        if (!e) return true;
        const Expr* idx = e->getIdx();
        if (!index_has_qint_udc(idx)) return true;
        if (is_immediate_init(vd_, e)) return true;  // C1's case
        publish(e, e->getBase(), idx,
                discriminate_array_subscript(*e));
        return true;
    }

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* e) {
        if (!e) return true;
        if (e->getOperator() != OO_Subscript) return true;
        if (e->getNumArgs() != 2) return true;
        const Expr* container = e->getArg(0);
        const Expr* index     = e->getArg(1);
        if (!container || !index) return true;
        if (!index_has_qint_udc(index)) return true;
        if (is_immediate_init(vd_, e)) return true;  // C1's case
        publish(e, container, index, QramContainerKind::StdArray);
        return true;
    }

private:
    void publish(const Expr* subscript,
                 const Expr* container,
                 const Expr* index,
                 QramContainerKind kind) {
        if (!hits_ || !subscript || !container || !index) return;
        QramSubscriptExprHit hit;
        hit.kind = kind;
        hit.target_var = vd_;
        hit.subscript_expr = subscript;
        hit.container_expr = container;
        hit.index_expr = index;
        // PRD §11.3 / D0c.3 single-source-of-truth: B1's infer_width
        // is the only place width is decided.
        InferContext ctx{};
        hit.W = vd_ ? infer_width(*vd_, ctx) : ctx.default_width;
        if (kind == QramContainerKind::Pointer) {
            hit.length_text = recover_pointer_length_text(container);
        }
        hits_->push_back(hit);
    }

    const VarDecl* vd_;
    std::vector<QramSubscriptExprHit>* hits_;
};

// ── Anchor callback: VarDecl of frontend qint with an initializer ──────────
class VarDeclCallback : public MatchFinder::MatchCallback {
public:
    explicit VarDeclCallback(std::vector<QramSubscriptExprHit>* hits)
        : hits_(hits) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        if (!var) return;
        const Expr* init = var->getInit();
        if (!init) return;
        HitCollector hc(var, hits_);
        hc.TraverseStmt(const_cast<Expr*>(init));
    }
private:
    std::vector<QramSubscriptExprHit>* hits_;
};

template <class T>
std::vector<std::unique_ptr<T>>& callback_pool() {
    static std::vector<std::unique_ptr<T>> pool;
    return pool;
}

} // anonymous namespace

void register_qram_subscript_expr_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QramSubscriptExprHit>& hits) {
    auto& pool = callback_pool<VarDeclCallback>();
    pool.push_back(std::make_unique<VarDeclCallback>(&hits));
    // Anchor: every VarDecl of frontend `qint` carrying an initializer.
    // The initializer-walk decides per-subscript publication.
    finder.addMatcher(
        varDecl(
            hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint"))))),
            hasInitializer(expr())
        ).bind("var"),
        pool.back().get());
}

} // namespace sturm::transpile
