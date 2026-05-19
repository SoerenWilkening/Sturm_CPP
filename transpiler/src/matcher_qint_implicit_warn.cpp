// matcher_qint_implicit_warn.cpp — sturm-u9ge.14 (Beat F1) implementation.
// Plan §9 / F1; PRD §10.1 / M7. See header for rationale + coexistence
// with C1 / E1.
//
// Strategy: anchor on every UDC ImplicitCastExpr; the callback (a)
// gates on the conversion-function being declared on a class named
// `qint` (mirrors C1's `QintUdcFinder` and E1's `UdcFinder`), (b) walks
// parents through ParenExpr / ImplicitCastExpr wrappers to the first
// real enclosing expression, (c) skips when that parent is an
// ArraySubscriptExpr or a CXXOperatorCallExpr on `operator[]` (in-scope
// C1 territory), (d) otherwise fires one Warning citing PRD §10.1.

#include "matcher_qint_implicit_warn.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
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

namespace sturm_matcher_qint_implicit_warn_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// ── Default-off gate ───────────────────────────────────────────────────────
// PRD §10.1: warning is suppressed by default in v1. The toggle is a
// single static bool until the transpiler driver grows a `-W*` flag
// parser; the boolean's spelling mirrors the eventual flag name
// `-Wsturm-qint-implicit-measure`.
bool& warning_gate() {
    static bool enabled = false;
    return enabled;
}

// Predicate: is `cd` a conversion function declared on a class whose
// unqualified name is `qint`? Mirrors the discriminator used by the
// C1 (`QintUdcFinder`) and E1 (`UdcFinder`) matchers — kept local to
// keep this TU dependency-free of the C1 / E1 production headers.
bool is_qint_udc(const CXXConversionDecl* cd) {
    if (!cd) return false;
    const CXXRecordDecl* parent = cd->getParent();
    if (!parent) return false;
    return parent->getNameAsString() == "qint";
}

// Walk one logical step up: skip transparent ICE / Paren wrappers Clang
// inserts between the UDC node and its semantic context. Returns the
// first parent node that is not a ParenExpr / ImplicitCastExpr cast
// chain — that is the "real" enclosing expression we discriminate on.
//
// `current` is updated in place; returns the dyn-typed parent, or a
// default-constructed (empty) node if the walk runs out of parents
// (e.g. at the TU root).
DynTypedNode walk_to_first_real_parent(ASTContext& ctx,
                                       const Expr* start) {
    DynTypedNode current = DynTypedNode::create(*start);
    for (int hops = 0; hops < 32; ++hops) {
        const auto parents = ctx.getParents(current);
        if (parents.empty()) return DynTypedNode();
        const DynTypedNode& p = parents[0];
        // Peel transparent wrappers: ICE (e.g. IntegralCast above the
        // UDC) and ParenExpr both lift the inner expr's effective
        // surrounding context unchanged.
        if (p.get<ImplicitCastExpr>() != nullptr ||
            p.get<ParenExpr>() != nullptr) {
            current = p;
            continue;
        }
        return p;
    }
    return DynTypedNode();
}

// Subscript-context predicate. Returns true iff the immediate enclosing
// expression of `udc` (post ICE/Paren peeling) is an array subscript
// AND `udc` sits in the index slot. Both `ArraySubscriptExpr` (built-in
// subscript, used by C-arrays and raw pointers) and
// `CXXOperatorCallExpr` for `operator[]` (e.g. `std::array::operator[]`)
// are recognised — symmetric with C1's container-arm dispatch.
bool is_subscript_context(ASTContext& ctx, const ImplicitCastExpr* udc) {
    if (!udc) return false;
    const DynTypedNode parent = walk_to_first_real_parent(ctx, udc);
    if (!parent.getMemoizationData()) return false;
    if (parent.get<ArraySubscriptExpr>() != nullptr) {
        // ArraySubscriptExpr's only sub-expression slots are base + idx.
        // The UDC originates on the index path: the base is always
        // pointer-typed and never reached through `operator size_t()`.
        return true;
    }
    if (const auto* coe = parent.get<CXXOperatorCallExpr>()) {
        return coe->getOperator() == OO_Subscript;
    }
    return false;
}

// ── Warning callback ───────────────────────────────────────────────────────
class WarnCallback : public MatchFinder::MatchCallback {
public:
    explicit WarnCallback(DiagnosticsEngine* diag) : diag_(diag) {}
    void run(const MatchFinder::MatchResult& r) override {
        if (!diag_ || !r.Context) return;
        if (!warning_gate()) return; // default-suppressed

        const auto* udc = r.Nodes.getNodeAs<ImplicitCastExpr>("udc");
        if (!udc || udc->getCastKind() != CK_UserDefinedConversion) return;

        // Resolve the conversion function on the inner CXXMemberCallExpr.
        const Expr* sub = udc->getSubExpr();
        if (!sub) return;
        const auto* mce =
            llvm::dyn_cast_or_null<CXXMemberCallExpr>(sub->IgnoreParens());
        if (!mce) return;
        const auto* cd =
            llvm::dyn_cast_or_null<CXXConversionDecl>(mce->getDirectCallee());
        if (!is_qint_udc(cd)) return;

        // Skip the in-scope subscript context — that is C1's domain.
        if (is_subscript_context(*r.Context, udc)) return;

        const unsigned id = get_diag_id();
        diag_->Report(udc->getBeginLoc(), id);
    }
private:
    unsigned get_diag_id() {
        if (cached_id_ != 0) return cached_id_;
        cached_id_ = diag_->getDiagnosticIDs()->getCustomDiagID(
            DiagnosticIDs::Warning,
            llvm::StringRef(
                "STURM: implicit qint-to-integer conversion is a silent "
                "measurement [qint-implicit-measure]; this conversion is "
                "not in array-subscript context, so the C1 QRAM rewrite "
                "will not pick it up (PRD docs/prd_qram_subscript.md "
                "section 10.1)."));
        return cached_id_;
    }
    DiagnosticsEngine* diag_;
    unsigned cached_id_ = 0;
};

// Per-callback unique_ptr pool — outlives MatchFinder runs. Same posture
// as `matcher_qram_oos.cpp::callback_pool` and `matcher_modular_op.cpp::
// add_mod_pool`.
std::vector<std::unique_ptr<WarnCallback>>& qint_implicit_warn_callback_pool() {
    static std::vector<std::unique_ptr<WarnCallback>> pool;
    return pool;
}

} // namespace sturm_matcher_qint_implicit_warn_anon_ns
using namespace sturm_matcher_qint_implicit_warn_anon_ns;

void set_qint_implicit_measure_warning_enabled(bool on) {
    warning_gate() = on;
}

bool qint_implicit_measure_warning_enabled() {
    return warning_gate();
}

void register_qint_implicit_warn_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::DiagnosticsEngine& diag) {
    auto& pool = qint_implicit_warn_callback_pool();
    pool.push_back(std::make_unique<WarnCallback>(&diag));
    // Anchor on every UserDefinedConversion ICE; the callback gates
    // on the conversion-function discriminator and the subscript
    // context predicate.
    finder.addMatcher(
        implicitCastExpr(hasCastKind(CK_UserDefinedConversion))
            .bind("udc"),
        pool.back().get());
}

} // namespace sturm::transpile
