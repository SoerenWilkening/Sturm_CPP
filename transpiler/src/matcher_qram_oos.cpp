// matcher_qram_oos.cpp — sturm-u9ge.16 (Beat E1) implementation.
//
// Plan §8 / Beat E1; PRD §9 / M5. See `matcher_qram_oos.hpp` for the
// four shapes, the diagnostic ids, and the rationale.
//
// Implementation strategy
// -----------------------
// All four shapes share a discriminator: a subscript expression whose
// index contains an `ImplicitCastExpr` with `CK_UserDefinedConversion`
// targeting `sturm::frontend::qint::operator <integral>`.
//
// Per-shape AST anchor (all CXXOperatorCallExpr):
//   (1) existing-target  `b = a[i];`         arg(1) is UDC subscript
//   (2) write            `a[i] = b;`         arg(0) is UDC subscript
//   (3) RMW              `a[i] += b;` etc.   compound-assign with
//                                            arg(0) UDC subscript
//   (4) expression-pos   `c = a[i] + d;`     non-assign op with any
//                                            arg a UDC subscript
//
// The four shapes are structurally disjoint: each callback gates on a
// distinct combination of operator-kind and arg-position so a single
// matched op-call hits at most one callback. The C1 shape `qint b =
// a[i];` anchors on a VarDecl init (NOT on a CXXOperatorCallExpr at
// all), so coexistence is deterministic — zero double-fires.

#include "matcher_qram_oos.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Walk an expression subtree looking for an ImplicitCastExpr whose
// CastKind is `CK_UserDefinedConversion` and whose conversion function
// is declared on a class named `qint` (or `qint_t`, defensively). The
// implicit conversion is implemented as a CXXMemberCallExpr on the
// ICE's sub-expr; the callee resolves to a CXXConversionDecl whose
// parent CXXRecordDecl carries the class name we type-test on.
class UdcFinder : public RecursiveASTVisitor<UdcFinder> {
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
        const std::string name = parent->getNameAsString();
        if (name != "qint" && name != "qint_t") return true;
        found_ = ice;
        return false; // stop early
    }
    const ImplicitCastExpr* find() const { return found_; }
private:
    const ImplicitCastExpr* found_ = nullptr;
};

// Convenience: scan an expression subtree for the qint UDC site.
bool subexpr_has_qint_udc(const Expr* e) {
    if (!e) return false;
    UdcFinder f;
    f.TraverseStmt(const_cast<Expr*>(e));
    return f.find() != nullptr;
}

// Discriminate "is this expression a subscript-on-qint whose index
// uses the qint UDC?". Both `ArraySubscriptExpr` (built-in subscript
// for C-array / pointer) and `CXXOperatorCallExpr` for `operator[]`
// (e.g. `std::array::operator[]`) count.
bool expr_is_qint_udc_subscript(const Expr* e) {
    if (!e) return false;
    const Expr* peeled = e->IgnoreParenImpCasts();
    if (const auto* ase = llvm::dyn_cast<ArraySubscriptExpr>(peeled)) {
        return subexpr_has_qint_udc(ase->getIdx());
    }
    if (const auto* coe = llvm::dyn_cast<CXXOperatorCallExpr>(peeled)) {
        if (coe->getOperator() == OO_Subscript &&
            coe->getNumArgs() >= 2) {
            return subexpr_has_qint_udc(coe->getArg(1));
        }
    }
    return false;
}

// Lazy-cached diag-ID resolver. The cache lives on the callback so
// each format string registers exactly once per TU.
unsigned get_diag_id(DiagnosticsEngine& diag, unsigned& cache,
                     const char* fmt) {
    if (cache != 0) return cache;
    cache = diag.getDiagnosticIDs()->getCustomDiagID(
        clang::DiagnosticIDs::Error, llvm::StringRef(fmt));
    return cache;
}

// Common assign-shape predicate for shape (4)'s exclusion gate and
// shape (3)'s inclusion gate.
bool is_compound_assign_op(OverloadedOperatorKind k) {
    return k == OO_PlusEqual    || k == OO_MinusEqual ||
           k == OO_StarEqual    || k == OO_SlashEqual ||
           k == OO_PercentEqual ||
           k == OO_AmpEqual     || k == OO_PipeEqual  ||
           k == OO_CaretEqual   ||
           k == OO_LessLessEqual || k == OO_GreaterGreaterEqual;
}

// ── Shape (1): existing-target  `b = a[i];` ─────────────────────────────────
class ExistingTargetCallback : public MatchFinder::MatchCallback {
public:
    explicit ExistingTargetCallback(DiagnosticsEngine* diag)
        : diag_(diag) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* op = r.Nodes.getNodeAs<CXXOperatorCallExpr>("op");
        if (!op || !diag_) return;
        if (op->getOperator() != OO_Equal) return;
        if (op->getNumArgs() != 2) return;
        // RHS is the UDC subscript; LHS is NOT (otherwise this is the
        // shape-(2) write case, handled by WriteCallback).
        if (!expr_is_qint_udc_subscript(op->getArg(1))) return;
        if (expr_is_qint_udc_subscript(op->getArg(0))) return;
        const unsigned id = get_diag_id(
            *diag_, cached_id_,
            "STURM: out-of-scope QRAM-subscript shape "
            "[qram-oos-existing-target]: assignment to a pre-existing "
            "qint target needs uncompute of the old value before the "
            "QRAM read writes (PRD docs/prd_qram_subscript.md section 9, "
            "row 1).");
        diag_->Report(op->getBeginLoc(), id);
    }
private:
    DiagnosticsEngine* diag_;
    unsigned cached_id_ = 0;
};

// ── Shape (2): write  `a[i] = b;` ───────────────────────────────────────────
class WriteCallback : public MatchFinder::MatchCallback {
public:
    explicit WriteCallback(DiagnosticsEngine* diag) : diag_(diag) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* op = r.Nodes.getNodeAs<CXXOperatorCallExpr>("op");
        if (!op || !diag_) return;
        if (op->getOperator() != OO_Equal) return;
        if (op->getNumArgs() != 2) return;
        if (!expr_is_qint_udc_subscript(op->getArg(0))) return;
        const unsigned id = get_diag_id(
            *diag_, cached_id_,
            "STURM: out-of-scope QRAM-subscript shape "
            "[qram-oos-write]: subscript on the LHS of '=' is a "
            "QRAM-write, a different unitary from the v1 QRAM-read "
            "rewrite (PRD docs/prd_qram_subscript.md section 9, row 2).");
        diag_->Report(op->getBeginLoc(), id);
    }
private:
    DiagnosticsEngine* diag_;
    unsigned cached_id_ = 0;
};

// ── Shape (3): RMW  `a[i] += b;` (and other compound-assigns) ───────────────
class RmwCallback : public MatchFinder::MatchCallback {
public:
    explicit RmwCallback(DiagnosticsEngine* diag) : diag_(diag) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* op = r.Nodes.getNodeAs<CXXOperatorCallExpr>("op");
        if (!op || !diag_) return;
        if (!is_compound_assign_op(op->getOperator())) return;
        if (op->getNumArgs() != 2) return;
        if (!expr_is_qint_udc_subscript(op->getArg(0))) return;
        const unsigned id = get_diag_id(
            *diag_, cached_id_,
            "STURM: out-of-scope QRAM-subscript shape "
            "[qram-oos-rmw]: read-modify-write decomposes as a "
            "QRAM-read followed by an adjusted-write composition v1 "
            "does not synthesise (PRD docs/prd_qram_subscript.md "
            "section 9, row 3).");
        diag_->Report(op->getBeginLoc(), id);
    }
private:
    DiagnosticsEngine* diag_;
    unsigned cached_id_ = 0;
};

// ── Shape (4): expression-position  `c = a[i] + d;` ─────────────────────────
class ExpressionPositionCallback : public MatchFinder::MatchCallback {
public:
    explicit ExpressionPositionCallback(DiagnosticsEngine* diag)
        : diag_(diag) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* op = r.Nodes.getNodeAs<CXXOperatorCallExpr>("op");
        if (!op || !diag_) return;
        const OverloadedOperatorKind k = op->getOperator();
        // Reject every assign-shape — those are shapes (1) / (2) / (3).
        if (k == OO_Equal || is_compound_assign_op(k)) return;
        // Walk every argument; on the first hit, fire once.
        for (unsigned i = 0; i < op->getNumArgs(); ++i) {
            if (!expr_is_qint_udc_subscript(op->getArg(i))) continue;
            const unsigned id = get_diag_id(
                *diag_, cached_id_,
                "STURM: out-of-scope QRAM-subscript shape "
                "[qram-oos-expression-position]: subscript inside a "
                "larger expression needs ancilla extraction + "
                "uncompute, which v1 does not synthesise (PRD "
                "docs/prd_qram_subscript.md section 9, row 4).");
            diag_->Report(op->getBeginLoc(), id);
            return;
        }
    }
private:
    DiagnosticsEngine* diag_;
    unsigned cached_id_ = 0;
};

// Per-callback unique_ptr pool — outlives MatchFinder runs. Same
// posture as `matcher_modular_op.cpp::add_mod_pool`.
template <class T>
std::vector<std::unique_ptr<T>>& callback_pool() {
    static std::vector<std::unique_ptr<T>> pool;
    return pool;
}

} // anonymous namespace

void register_qram_oos_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::DiagnosticsEngine& diag) {
    // Single broad anchor: every CXXOperatorCallExpr in the TU. The
    // four callbacks each gate on the operator-kind discriminant
    // before walking subexpressions for the UDC subscript. Bundling
    // four shapes behind one anchor keeps the AST traversal cost
    // predictable (each op-call visited four times — one per
    // callback's `run`, but each `run` early-returns in O(1) on the
    // operator-kind / arg-position guards).
    auto pattern = cxxOperatorCallExpr().bind("op");

    {
        auto& pool = callback_pool<ExistingTargetCallback>();
        pool.push_back(std::make_unique<ExistingTargetCallback>(&diag));
        finder.addMatcher(pattern, pool.back().get());
    }
    {
        auto& pool = callback_pool<WriteCallback>();
        pool.push_back(std::make_unique<WriteCallback>(&diag));
        finder.addMatcher(pattern, pool.back().get());
    }
    {
        auto& pool = callback_pool<RmwCallback>();
        pool.push_back(std::make_unique<RmwCallback>(&diag));
        finder.addMatcher(pattern, pool.back().get());
    }
    {
        auto& pool = callback_pool<ExpressionPositionCallback>();
        pool.push_back(std::make_unique<ExpressionPositionCallback>(&diag));
        finder.addMatcher(pattern, pool.back().get());
    }
}

} // namespace sturm::transpile
