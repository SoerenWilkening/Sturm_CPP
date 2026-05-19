// matcher_qram_subscript.cpp — sturm-u9ge.12 (Beat C1) implementation.
//
// Plan §6 / Beat C1; PRD §7 / M3. See `matcher_qram_subscript.hpp` for
// the three container shapes, the discriminator, and the rationale.
//
// Implementation strategy
// -----------------------
// Two AST anchors, one per Clang subscript-node class:
//
//   (A) `CXXOperatorCallExpr` on `operator[]` — covers the
//       `std::array<qint_t<W>, N>::operator[]` shape.  Surfaces
//       `kind = StdArray`.
//
//   (B) `ArraySubscriptExpr` — covers both the C-array (`qint_t<W>[N]`)
//       and pointer (`qint_t<W>*`) shapes.  The base-expression's
//       *un-decayed* type discriminates: `isArrayType()` → CArray,
//       `isPointerType()` → Pointer.  Walking through the
//       `ArrayToPointerDecay` ICE on the C-array case is essential —
//       otherwise both would peel to `qint_t<W>*`.
//
// Both anchors gate on:
//
//   * the enclosing `VarDecl` being a frontend `qint` (the alias
//     class lives at `sturm::frontend::qint`; A1 placed it there);
//   * the index sub-expression containing an `ImplicitCastExpr` of
//     `CK_UserDefinedConversion` whose conversion function is declared
//     on a class named `qint` (mirrors the discriminator in
//     `matcher_qram_oos.cpp:UdcFinder`);
//   * the subscript being the *immediate* initializer of the VarDecl
//     (so `qint c = a[i] + d;` does NOT match — the binary-op `+` is
//     the immediate init, not the subscript).
//
// Width is populated via Beat B1's `infer_width()` (PRD §11.3 / D0c.3
// mandates a single source of truth — C1 must not derive `W` itself).
// The diag sink is `nullptr` because C1 only wants the rule-3 fallback
// for non-witnessed widths; the ambiguity diag fires from the same
// surface only inside B1's tests.

#include "matcher_qram_subscript.hpp"
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

namespace sturm_matcher_qram_subscript_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// ── UDC discriminator (mirrors matcher_qram_oos.cpp::UdcFinder) ─────────────
//
// Walk an expression subtree looking for an `ImplicitCastExpr` whose
// CastKind is `CK_UserDefinedConversion` and whose conversion function
// is declared on a class named `qint`. The discriminator must accept
// both `sturm::frontend::qint` (the production placement, A1) and any
// other-namespace `qint` (defensively — the test stub places it under
// `sturm::frontend::qint` exactly to mirror production). The matcher
// rejects `qint_t` here because that is the BACKEND type and its
// `operator int64_t()` is `explicit`, so it cannot produce a UDC node
// anyway — but the predicate keeps the production class name explicit
// for readability.
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
        // The frontend class is the only surface type that owns an
        // implicit `qint -> integer` UDC. See PRD §4.1 + §5.
        if (parent->getNameAsString() != "qint") return true;
        found_ = ice;
        return false; // stop early
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

// ── Container-kind discrimination ────────────────────────────────────────────
//
// For an `ArraySubscriptExpr`, return CArray if the un-decayed base
// type is a true array (`T[N]`), Pointer otherwise. The base is read
// via `IgnoreParenImpCasts()` to strip LValueToRValue /
// ArrayToPointerDecay layers Clang inserts above the raw DeclRefExpr;
// in the C-array case the wrapped DRE has type `T[N]` (`isArrayType()`
// true), in the pointer case `T*`.
QramContainerKind discriminate_array_subscript(
    const ArraySubscriptExpr& e) {
    const Expr* base = e.getBase();
    if (!base) return QramContainerKind::Pointer;
    QualType bt = base->IgnoreParenImpCasts()->getType();
    if (bt.isNull()) return QramContainerKind::Pointer;
    if (bt->isArrayType()) return QramContainerKind::CArray;
    return QramContainerKind::Pointer;
}

// ── Pointer-arm length recovery (PRD §11.1.6 — D2 extension) ───────────────
//
// For a pointer subscript `a[i]` where `a` is a function parameter, walk
// up to the enclosing `FunctionDecl` and look for the parameter sibling
// that immediately follows `a` in the parameter list. If that sibling is
// integral-typed (size_t, unsigned long, std::size_t — anything that
// satisfies `Type::isIntegerType()`), record its name as the rendered
// length text. Empty otherwise — the emitter then surfaces a
// `qram-pointer-length-missing` placeholder per §11.1.6.
//
// This is the v1 heuristic: minimal but covers the canonical pattern
// `void demo(qint_t<W>* a, std::size_t n, qint i)`. A future beat may
// extend to fields-on-objects (`obj.tbl_len` next to `obj.tbl`) or
// alloca-shaped allocations.
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
    // Find the parameter immediately after the container in the
    // function's parameter list.
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

// ── Hit-population helpers ──────────────────────────────────────────────────
//
// Both AST shapes share the same VarDecl + UDC + B1-width plumbing;
// the only structural difference is which AST node is the immediate
// initializer (CXXOperatorCallExpr vs ArraySubscriptExpr) and which
// child slot holds the index. Factor the common path so the per-arm
// callbacks stay tiny.
void publish_hit(QramContainerKind kind,
                 const VarDecl& vd,
                 const Expr* container,
                 const Expr* index,
                 std::vector<QramSubscriptHit>* hits) {
    if (!hits || !container || !index) return;
    QramSubscriptHit hit;
    hit.kind = kind;
    hit.target_var = &vd;
    hit.container_expr = container;
    hit.index_expr = index;
    // PRD §11.3 / D0c.3 mandates a single source of truth — C1 calls
    // B1's `infer_width()` rather than re-deriving the width. Pass
    // `nullptr` for the diag sink: ambiguity surfaces from B1's own
    // tests, not from a matcher fire (where the diag would risk
    // double-counting against the same VarDecl when the consumer
    // re-runs B1 elsewhere).
    InferContext ctx{};
    hit.W = infer_width(vd, ctx);
    // PRD §11.1.6: pointer arm needs an explicit `n` length argument.
    // For StdArray / CArray the length is encoded in the type and the
    // emitter does not consume `length_text`.
    if (kind == QramContainerKind::Pointer) {
        hit.length_text = recover_pointer_length_text(container);
    }
    hits->push_back(hit);
}

// ── std::array arm: CXXOperatorCallExpr on operator[] ───────────────────────
class StdArrayCallback : public MatchFinder::MatchCallback {
public:
    explicit StdArrayCallback(std::vector<QramSubscriptHit>* hits)
        : hits_(hits) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* op  =
            r.Nodes.getNodeAs<CXXOperatorCallExpr>("subscript");
        if (!var || !op) return;
        if (op->getOperator() != OO_Subscript) return;
        if (op->getNumArgs() != 2) return;
        // arg(0) is the container; arg(1) is the index.
        const Expr* container = op->getArg(0);
        const Expr* index     = op->getArg(1);
        if (!container || !index) return;
        // Discriminator: index must carry the qint UDC (PRD §7).
        if (!index_has_qint_udc(index)) return;
        publish_hit(QramContainerKind::StdArray, *var, container, index,
                    hits_);
    }
private:
    std::vector<QramSubscriptHit>* hits_;
};

// ── C-array / pointer arm: ArraySubscriptExpr ──────────────────────────────
class ArraySubscriptCallback : public MatchFinder::MatchCallback {
public:
    explicit ArraySubscriptCallback(std::vector<QramSubscriptHit>* hits)
        : hits_(hits) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        const auto* ase =
            r.Nodes.getNodeAs<ArraySubscriptExpr>("subscript");
        if (!var || !ase) return;
        const Expr* container = ase->getBase();
        const Expr* index     = ase->getIdx();
        if (!container || !index) return;
        if (!index_has_qint_udc(index)) return;
        const QramContainerKind kind = discriminate_array_subscript(*ase);
        publish_hit(kind, *var, container, index, hits_);
    }
private:
    std::vector<QramSubscriptHit>* hits_;
};

// Per-callback pools owned by function-local statics — outlive the
// MatchFinder run. Mirrors `matcher_modular_op.cpp::add_mod_pool` and
// `matcher_qram_oos.cpp::qram_subscript_callback_pool<T>`.
template <class T>
std::vector<std::unique_ptr<T>>& qram_subscript_callback_pool() {
    static std::vector<std::unique_ptr<T>> pool;
    return pool;
}

// ── Match patterns ──────────────────────────────────────────────────────────
//
// Both shapes share the VarDecl anchor + frontend-qint type guard +
// "subscript is the immediate initializer" requirement. `ignoringImplicit`
// peels the `<ConstructorConversion>` ICE plus the `CXXConstructExpr`
// Clang inserts when copy-initialising a frontend `qint` from a
// `qint_t<W>` (see the AST dumps in plan §6). `hasInitializer(...)` is
// the gate that excludes `qint c = a[i] + d;` (PRD §9 row 4) — its
// immediate initializer is the `+` op-call, not the subscript.
template <class SubscriptMatcher>
auto build_pattern(SubscriptMatcher subscript) {
    return varDecl(
        hasType(hasCanonicalType(hasDeclaration(
            cxxRecordDecl(hasName("qint"))))),
        hasInitializer(ignoringImplicit(
            cxxConstructExpr(hasArgument(
                0, ignoringParenImpCasts(subscript)))))
    ).bind("var");
}

} // namespace sturm_matcher_qram_subscript_anon_ns
using namespace sturm_matcher_qram_subscript_anon_ns;

void register_qram_subscript_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QramSubscriptHit>& hits) {
    {
        auto& pool = qram_subscript_callback_pool<StdArrayCallback>();
        pool.push_back(std::make_unique<StdArrayCallback>(&hits));
        finder.addMatcher(build_pattern(cxxOperatorCallExpr(
            hasOverloadedOperatorName("[]")).bind("subscript")),
            pool.back().get());
    }
    {
        auto& pool = qram_subscript_callback_pool<ArraySubscriptCallback>();
        pool.push_back(std::make_unique<ArraySubscriptCallback>(&hits));
        finder.addMatcher(
            build_pattern(arraySubscriptExpr().bind("subscript")),
            pool.back().get());
    }
}

} // namespace sturm::transpile
