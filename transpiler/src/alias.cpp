// alias.cpp — PM5-2 / PM5-3 alias-analysis implementations.
//
// Implements the three free functions declared in
// `sturm/transpile/alias.hpp` (PM5-1, extended in PM5-3):
//
//   - `footprint(const QValueRef&, const clang::ASTContext&)`
//     peels the underlying VarDecl's type and returns a
//     `QubitFootprint` describing the bit range of the qubit
//     container the operand refers to. This overload covers the
//     PM5-2 subset of the ladder (bare `qbool` DRE + bare `qint_t<W>`
//     DRE with compile-time `W`); it CANNOT peel a `q[k]` BitProxy
//     shape because the `QValueRef` struct carries only name +
//     decl_loc, and the subscript index lives in the raw Clang
//     `Expr*` tree. Callers with a BitProxy operand use the Expr
//     overload below.
//
//   - `footprint(const clang::Expr&, const clang::ASTContext&)`
//     PM5-3 addition. Peels the operand expression — a
//     `CXXOperatorCallExpr` with `OO_Subscript` for the BitProxy
//     case, or a `DeclRefExpr` for the bare-variable case — via
//     `Expr::EvaluateAsInt` to distinguish a compile-time index
//     from a runtime one. Constant index `k` in `[0, W)` emits
//     `{name, decl_loc, {k, k+1}}`; non-constant or out-of-range
//     indices conservatively fall back to the parent's full-width
//     `{0, W}` footprint. Bare DRE shapes delegate to the
//     `QValueRef` overload to avoid duplicating the qbool / qint_t<W>
//     type-resolution logic.
//
//   - `may_overlap(const QubitFootprint&, const QubitFootprint&)`
//     three-step short-circuit: universal sentinel → true,
//     different decl → false, same decl → compare bit ranges.
//
// Extraction algorithm (PM5-2 + PM5-3 coverage — see
// `docs/implementation_plan_transpiler_phase_m_pm5.md` §4):
//
//   `QValueRef::decl_loc` identifies a VarDecl via the canonical
//   `NamedDecl::getLocation()` the matcher framework used to build
//   the QValueRef in the first place (see `make_ref()` in
//   `matcher_common.hpp`). We walk the TranslationUnitDecl looking
//   for a VarDecl whose `getLocation()` matches. The walk is a
//   RecursiveASTVisitor subtree scan (not a parent-chain walk)
//   because VarDecls live inside FunctionDecls / CompoundStmts /
//   nested records / lambdas, and only a full subtree walk
//   surfaces them regardless of structural nesting.
//
//   Once the VarDecl is located, we inspect its type:
//     - `QualType::getAsCXXRecordDecl()` returns the CXXRecordDecl
//       for `qbool` or the `ClassTemplateSpecializationDecl` for
//       `qint_t<W>`. The latter is-a CXXRecordDecl (via
//       multiple-inheritance in Clang's Decl hierarchy), so a
//       `llvm::dyn_cast<ClassTemplateSpecializationDecl>` on the
//       returned record tells qint_t<W> from the plain qbool case.
//     - For the plain `sturm::qbool` record, emit width = 1.
//     - For the `sturm::qint_t` specialization, read the first
//       template argument (an `TemplateArgument::Integral` APSInt).
//       Non-positive or non-integral → universal sentinel.
//
//   Any failure (no VarDecl, dependent type, unrecognised record,
//   BitProxy call, runtime subscript) → universal sentinel. The
//   universal sentinel is a default-constructed `QubitFootprint{}`;
//   `may_overlap` returns true against it unconditionally, so the
//   PM5 peephole matcher conservatively refuses to commute through
//   any operand the extractor cannot resolve.
//
// PM5-3 / BitProxy subscript peel: covered by the new Expr-taking
// `footprint()` overload. A `CXXOperatorCallExpr` with operator
// `OO_Subscript` whose `getArg(0)` is a `DeclRefExpr` to a
// `qint_t<W>` VarDecl is the canonical BitProxy call shape; the
// constructor
//     `BitProxy(qint_t<W>& parent, size_t i)`
// lives in `include/sturm/qtypes/bit_proxy.hpp` and is invoked by
// the qint's `operator[](size_t)` to manufacture a BitProxy
// reference. The peel evaluates `getArg(1)` via
// `Expr::EvaluateAsInt`; success → `{k, k+1}` bit range, failure →
// full-width `{0, W}` fallback (the "conservative optimism
// rejection" case pinned by §12 Sharp edge 1).

#include "sturm/transpile/alias.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace sturm::transpile::detail {

namespace {

// Locate the VarDecl whose source location matches `target_loc` by
// walking the TU's subtree. A RecursiveASTVisitor subtree scan is the
// minimal tool: VarDecls live inside FunctionDecls / CompoundStmts /
// nested records, so a parent-chain walk from some anchor would not
// suffice. The walk short-circuits on the first hit (VisitVarDecl
// returns false to stop further traversal).
class VarDeclByLocFinder
    : public clang::RecursiveASTVisitor<VarDeclByLocFinder> {
public:
    explicit VarDeclByLocFinder(clang::SourceLocation target_loc)
        : target_loc_(target_loc) {}

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd) return true;
        // Primary discriminator: `getLocation()` — the identifier token
        // the matcher framework wrote into the QValueRef via
        // `make_ref()` in `matcher_common.hpp`. Two locals with the
        // same spelled name but different decls (shadowed outer vs
        // inner) have different locations, so this check alone
        // disambiguates.
        if (vd->getLocation() != target_loc_) return true;
        found_ = vd;
        return false; // stop traversal — first hit wins
    }

    const clang::VarDecl* found() const { return found_; }

private:
    clang::SourceLocation target_loc_;
    const clang::VarDecl* found_ = nullptr;
};

// Return the VarDecl at `loc` in `ctx`, or nullptr if none. Callers
// that pass an invalid location receive nullptr without a walk.
const clang::VarDecl* find_vardecl_at(clang::SourceLocation loc,
                                      const clang::ASTContext& ctx) {
    if (loc.isInvalid()) return nullptr;
    // ASTContext's TranslationUnitDecl is a non-const Decl*; we need
    // a non-const Decl* to hand to TraverseDecl(). The walker never
    // mutates the tree, so the const_cast is safe (same pattern
    // `count_readers_in_scope` in `matcher_common.hpp` uses when it
    // hands the scope-anchor Stmt to TraverseStmt).
    clang::TranslationUnitDecl* tu =
        const_cast<clang::ASTContext&>(ctx).getTranslationUnitDecl();
    if (!tu) return nullptr;
    VarDeclByLocFinder finder(loc);
    finder.TraverseDecl(tu);
    return finder.found();
}

// Return true iff `rd` names the `sturm::qbool` record. The check is
// by fully qualified name rather than by CXXRecordDecl identity so
// that redeclaration chains (fwd decl + defn) and any future
// namespace-inline changes do not silently break the match.
bool is_qbool_record(const clang::CXXRecordDecl& rd) {
    return rd.getQualifiedNameAsString() == "sturm::qbool";
}

// Extract the template `Width` argument from a qint_t<W>
// specialization. Returns:
//   - `std::nullopt` when the specialization is not `sturm::qint_t`,
//     the first template argument is not an integral constant, the
//     value is non-positive, or the primary ClassTemplateDecl is
//     unresolvable.
//   - `W` otherwise. Callers convert to `int` for the bit_range;
//     this helper returns `int` directly (truncating from APSInt)
//     because the runtime asserts Width ∈ [1, 64] at the header
//     level (qint_core.hpp:55-56), so the cast is lossless in well-
//     formed source.
std::optional<int> extract_qint_width(
    const clang::ClassTemplateSpecializationDecl& spec) {
    // First gate: the specialization's primary template must be
    // `sturm::qint_t`. A different template (e.g. `std::array`) is
    // definitely not a qubit container.
    const clang::ClassTemplateDecl* primary = spec.getSpecializedTemplate();
    if (!primary) return std::nullopt;
    if (primary->getQualifiedNameAsString() != "sturm::qint_t") {
        return std::nullopt;
    }

    // Second gate: the first template argument must be an integral
    // constant. `TemplateArgumentList::size()` of 0 would mean the
    // specialization is dependent / unresolved — bail to the
    // universal sentinel.
    const clang::TemplateArgumentList& args = spec.getTemplateArgs();
    if (args.size() == 0) return std::nullopt;
    const clang::TemplateArgument& arg0 = args.get(0);
    if (arg0.getKind() != clang::TemplateArgument::Integral) {
        // Dependent / type / pack / expression — any of those mean
        // the Width is not yet a compile-time integer. Conservative
        // bail.
        return std::nullopt;
    }

    // Third gate: extract the APSInt and sanity-check it. Width
    // must be positive; the runtime also caps it at 64 but we do
    // not re-enforce the ceiling here because a hand-crafted
    // specialization past 64 is already ill-formed at the header
    // level, and the footprint's `int hi` field has plenty of
    // headroom for any legitimate value.
    const llvm::APSInt& width_apsint = arg0.getAsIntegral();
    // APSInt::getExtValue() sign-extends to int64_t; on a well-
    // formed specialization `W` fits in a signed 64-bit int, and
    // we narrow to `int` for the bit_range field.
    const int64_t w64 = width_apsint.getExtValue();
    if (w64 <= 0) return std::nullopt;
    return static_cast<int>(w64);
}

// The "universal sentinel" helper — a default-constructed footprint
// with empty name, invalid decl_loc, and `{0, 0}` bit range. See
// `alias.hpp` for the sentinel semantics.
QubitFootprint universal_sentinel() {
    return QubitFootprint{};
}

// PM5-3 — BitProxy constant-index peel helper. Given a raw operand
// expression that may be a `CXXOperatorCallExpr` with the
// `OO_Subscript` operator (the canonical `q[k]` BitProxy-
// construction shape from `include/sturm/qtypes/bit_proxy.hpp`),
// attempt to extract a compile-time integer index. Returns:
//   - `std::nullopt` when the expression is NOT a subscript call at
//     all. Callers fall through to the bare-DRE path.
//   - `std::nullopt` when the expression IS a subscript call but the
//     index argument is non-constant (loop variable, function
//     parameter, dependent expression). Callers fall back to the
//     parent's full-width `{0, W}` footprint — see §12 Sharp edge 1.
//   - `std::optional<int>(k)` when the index argument evaluates via
//     `Expr::EvaluateAsInt` to a non-negative integer. Callers still
//     must range-check `k < W` against the parent's width; the
//     helper does NOT reject out-of-range values, it only distinguishes
//     "constant" from "non-constant" (out-of-range indices are an
//     extraction failure too, but the width check happens at the
//     caller where W is in scope).
//
// The `dre_out` out-parameter receives the parent's `DeclRefExpr`
// peeled from `getArg(0)` so the caller can resolve the parent
// qint_t<W>'s VarDecl and width without walking the subscript call
// a second time. `dre_out` is only written on "this IS a subscript
// call shape" — callers that receive `std::nullopt` AND a non-null
// `*dre_out` know the call-shape matched but the index was runtime.
std::optional<int> peel_bitproxy_constant(
    const clang::Expr& expr,
    const clang::ASTContext& ctx,
    const clang::DeclRefExpr** dre_out) {
    if (dre_out) *dre_out = nullptr;
    // `IgnoreParenImpCasts` strips the implicit-cast / paren wrappers
    // Clang sometimes inserts around a BitProxy operand (e.g. the
    // BitProxy(qint_t<W>&, size_t) constructor's l-value-to-r-value
    // coercion). The helper sees through these without dragging in
    // `matcher_common.hpp`'s `peel_to_payload` (which handles a wider
    // set of wrappers, including CXXConstructExpr and
    // MaterializeTemporaryExpr — excessive for this caller).
    const clang::Expr* cur = expr.IgnoreParenImpCasts();
    if (!cur) return std::nullopt;
    const auto* call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(cur);
    if (!call) return std::nullopt;
    if (call->getOperator() != clang::OO_Subscript) return std::nullopt;
    // A `q[k]` subscript call is a two-arg CXXOperatorCallExpr where
    // getArg(0) is the array-like object (the qint_t<W>) and getArg(1)
    // is the index. A non-two-arg shape is ill-formed for OO_Subscript
    // so the guard is defensive rather than semantic.
    if (call->getNumArgs() != 2) return std::nullopt;

    // Peel getArg(0) to its DeclRefExpr so the caller can resolve the
    // parent VarDecl. We strip paren/implicit-cast wrappers only; the
    // BitProxy constructor takes the qint_t<W>& by reference, so any
    // CXXConstructExpr peel is unnecessary at this layer.
    const clang::Expr* arg0 = call->getArg(0);
    if (!arg0) return std::nullopt;
    const clang::Expr* arg0_peeled = arg0->IgnoreParenImpCasts();
    const auto* dre =
        llvm::dyn_cast_or_null<clang::DeclRefExpr>(arg0_peeled);
    if (!dre) return std::nullopt;
    if (dre_out) *dre_out = dre;

    // Attempt to evaluate the index argument as a compile-time integer.
    // `Expr::EvaluateAsInt` is the canonical constant-folder in the
    // Clang AST: it handles integer literals, constexpr variables,
    // template parameters after instantiation, and integer-valued
    // constant expressions. Failure (returns false) means the index is
    // runtime-valued (e.g. a for-loop induction variable) — the caller
    // interprets this as "conservative fallback to full-width."
    const clang::Expr* arg1 = call->getArg(1);
    if (!arg1) return std::nullopt;
    // Dependent indices inside a template body before instantiation
    // are not constant-foldable. Bail before calling EvaluateAsInt so
    // we do not assert inside Clang's evaluator on a dependent Expr.
    if (arg1->isValueDependent() || arg1->isTypeDependent()) {
        return std::nullopt;
    }
    clang::Expr::EvalResult eval_result;
    const bool ok = arg1->EvaluateAsInt(eval_result, ctx);
    if (!ok) return std::nullopt;
    if (!eval_result.Val.isInt()) return std::nullopt;
    const llvm::APSInt& idx_apsint = eval_result.Val.getInt();
    // Reject negative indices up-front — the BitProxy constructor
    // takes `size_t` so a negative index would have been coerced by
    // the frontend, but the APSInt carries a sign bit and we want a
    // clean non-negative int in the range check at the caller.
    if (idx_apsint.isSigned() && idx_apsint.isNegative()) {
        return std::nullopt;
    }
    // Fits-in-int check. A `size_t` subscript would in principle allow
    // values past INT_MAX, but the qint_t<W> width is capped at 64 by
    // qint_core.hpp, so any legitimate index is well under INT_MAX.
    // Values beyond that are extraction failures (fall back to full
    // width).
    const uint64_t k_u64 = idx_apsint.getZExtValue();
    if (k_u64 > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(k_u64);
}

// Resolve the parent qint_t<W>'s VarDecl + width from the
// BitProxy-subscript's `getArg(0)` DRE. Returns:
//   - `std::nullopt` when the DRE does not refer to a VarDecl, or the
//     VarDecl's type is not a qint_t<W> specialization, or the
//     template width argument is not a positive integer.
//   - A pair `(vd, W)` otherwise. The VarDecl pointer lives in the
//     same ASTContext as the DRE; the `int W` is the first template
//     argument of the `ClassTemplateSpecializationDecl`.
// The helper shares the extraction shape with the `footprint(
// QValueRef, ctx)` path (find_vardecl_at + extract_qint_width) but
// walks the DRE directly instead of going through the location-based
// TU scan — saving a traversal and simplifying tests that build
// tiny in-memory ASTs.
struct BitProxyParent {
    const clang::VarDecl* vd;
    int width;
};
std::optional<BitProxyParent> resolve_bitproxy_parent(
    const clang::DeclRefExpr& dre) {
    const clang::ValueDecl* nd = dre.getDecl();
    if (!nd) return std::nullopt;
    const auto* vd = llvm::dyn_cast<clang::VarDecl>(nd);
    if (!vd) return std::nullopt;
    clang::QualType qt = vd->getType();
    if (qt.isNull() || qt->isDependentType()) return std::nullopt;
    clang::QualType stripped =
        qt.getNonReferenceType().getUnqualifiedType();
    const clang::CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return std::nullopt;
    // Only qint_t<W> has a BitProxy-returning operator[]. A qbool is
    // width-1 and does not expose a subscript in the DSL; we bail
    // rather than silently accept a qbool[0] pattern that should not
    // exist in valid user code.
    const auto* spec =
        llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rd);
    if (!spec) return std::nullopt;
    auto w = extract_qint_width(*spec);
    if (!w) return std::nullopt;
    return BitProxyParent{vd, *w};
}

} // namespace

QubitFootprint footprint(const QValueRef& op, const clang::ASTContext& ctx) {
    // Step 0 — reject obviously-unusable QValueRefs up front. A
    // QValueRef with an invalid decl_loc is the conventional shape
    // for "text only" operands (e.g. the RHS of a classical qint
    // compound-assign carries the RHS source text in `name` and
    // leaves `decl_loc` invalid; see `matcher_qint_const.cpp`). Those
    // are not qubit containers; they cannot be reasoned about by
    // footprint.
    if (op.decl_loc.isInvalid()) return universal_sentinel();

    // Step 1 — locate the VarDecl the QValueRef points at. If we
    // cannot find one (e.g. the QValueRef was synthesised from a
    // location outside the current TU), the operand is opaque.
    const clang::VarDecl* vd = find_vardecl_at(op.decl_loc, ctx);
    if (!vd) return universal_sentinel();

    // Step 2 — reject dependent types. A VarDecl in a template
    // function body before instantiation has a dependent type; the
    // Width is not yet known and we cannot answer the footprint
    // question.
    clang::QualType qt = vd->getType();
    if (qt.isNull()) return universal_sentinel();
    if (qt->isDependentType()) return universal_sentinel();

    // Step 3 — resolve the CXXRecordDecl. The type may be wrapped
    // in const / reference / typedef sugar; the canonical un-
    // qualified stripped form is what `getAsCXXRecordDecl()`
    // expects. For `qbool` this yields the qbool CXXRecordDecl;
    // for `qint_t<W>` this yields the specialization (which is-a
    // CXXRecordDecl via the Clang Decl hierarchy).
    clang::QualType stripped =
        qt.getNonReferenceType().getUnqualifiedType();
    const clang::CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return universal_sentinel();

    // Step 4a — bare qbool. The qualified-name check matches both
    // `class sturm::qbool` and any namespace-inline re-exports; the
    // spelled name alone ("qbool") would be too lax because a
    // user-defined `qbool` in a different namespace would match.
    if (is_qbool_record(*rd)) {
        QubitFootprint fp;
        fp.name = op.name;
        fp.decl_loc = op.decl_loc;
        fp.bit_range = {0, 1};
        return fp;
    }

    // Step 4b — bare qint_t<W>. The specialization is-a
    // ClassTemplateSpecializationDecl; `llvm::dyn_cast` tells us
    // whether the record was produced by template instantiation.
    // A non-template CXXRecordDecl falls through to the universal
    // sentinel (step 5).
    if (const auto* spec = llvm::dyn_cast<
            clang::ClassTemplateSpecializationDecl>(rd)) {
        if (auto width = extract_qint_width(*spec)) {
            QubitFootprint fp;
            fp.name = op.name;
            fp.decl_loc = op.decl_loc;
            fp.bit_range = {0, *width};
            return fp;
        }
        // Fall through — some other specialization (or qint_t with
        // a non-integral template arg). Conservative bail.
    }

    // Step 5 — fallback. Any operand shape the ladder did not
    // recognise collapses to the universal sentinel. The BitProxy
    // `q[k]` subscript shape is handled by the Expr-taking overload
    // below (PM5-3), because a `QValueRef` carrying only name +
    // decl_loc cannot encode the subscript index — that lives in
    // the raw Clang `Expr*` tree.
    return universal_sentinel();
}

QubitFootprint footprint(const clang::Expr& expr,
                         const clang::ASTContext& ctx) {
    // PM5-3 — Expr-taking overload. The ladder mirrors the
    // `QValueRef` overload with a BitProxy-subscript peel wedged
    // above the bare-DRE delegation:
    //
    //   Ladder step 1 — `q[k]` BitProxy subscript. The canonical
    //   shape is a `CXXOperatorCallExpr` with `OO_Subscript`; the
    //   `peel_bitproxy_constant` helper distinguishes three
    //   sub-cases:
    //     - Not a subscript call at all → fall through to step 2.
    //     - Subscript call with constant index `k` in `[0, W)` →
    //       emit `{name, decl_loc, {k, k+1}}`.
    //     - Subscript call with non-constant or out-of-range index →
    //       emit the parent's full-width footprint `{0, W}` (the
    //       §12 Sharp edge 1 conservative fallback).
    //
    //   Ladder step 2 — bare `DeclRefExpr`. Delegate to the
    //   `QValueRef` overload via a synthesised `QValueRef{name,
    //   decl_loc}` so the qbool / qint_t<W> resolution logic lives
    //   in exactly one place.
    //
    //   Ladder step 3 — anything else (dependent types, plugin-op
    //   operands, unrecognised sugar) → universal sentinel.

    // Step 1 — attempt the BitProxy subscript peel. The helper returns
    // the parent DRE via `dre_out` if the shape matched the
    // CXXOperatorCallExpr<OO_Subscript> pattern, regardless of whether
    // the index is a compile-time constant. We branch on the pair
    // (dre_matched, constant_index) rather than a single return value
    // because the "subscript-shape matched but index is runtime" case
    // still needs the parent's VarDecl + width for the full-width
    // fallback.
    const clang::DeclRefExpr* parent_dre = nullptr;
    const std::optional<int> peeled_k =
        peel_bitproxy_constant(expr, ctx, &parent_dre);

    if (parent_dre != nullptr) {
        // Subscript call shape matched. Resolve the parent qint_t<W>'s
        // VarDecl + width. If the parent is not a qint_t<W> (e.g. the
        // DRE points at an unrelated `std::array`-shaped object), we
        // cannot produce a useful footprint; fall through to the
        // universal sentinel at the bottom.
        if (auto parent = resolve_bitproxy_parent(*parent_dre)) {
            QubitFootprint fp;
            fp.name = parent.value().vd->getNameAsString();
            fp.decl_loc = parent.value().vd->getLocation();
            const int W = parent.value().width;
            if (peeled_k && *peeled_k >= 0 && *peeled_k < W) {
                // Constant index in range — narrow the footprint to a
                // single bit.
                fp.bit_range = {*peeled_k, *peeled_k + 1};
            } else {
                // Non-constant index, or constant but out-of-range.
                // Conservative fallback to the parent's full width.
                // This is the §12 Sharp edge 1 case — the dominant
                // loop-induction-variable shape.
                fp.bit_range = {0, W};
            }
            return fp;
        }
        // Parent DRE did not resolve to a qint_t<W>; fall through to
        // step 3 (universal sentinel). We do NOT delegate to the bare
        // DRE overload here because the Expr is still a subscript
        // call, not a bare DRE — reusing the QValueRef path would
        // misclassify.
        return universal_sentinel();
    }

    // Step 2 — bare DeclRefExpr. Any wrapping implicit-cast / paren
    // nodes have already been stripped by `peel_bitproxy_constant`'s
    // `IgnoreParenImpCasts` inside the subscript check, but we
    // re-peel here because that helper short-circuits on a
    // non-subscript call and does not return the peeled expression.
    const clang::Expr* peeled = expr.IgnoreParenImpCasts();
    if (peeled != nullptr) {
        if (const auto* dre =
                llvm::dyn_cast<clang::DeclRefExpr>(peeled)) {
            // Manufacture a QValueRef and delegate. `getDecl()`
            // returning null is unusual (a DRE always has a Decl
            // target) but we treat it as "unresolvable" → universal
            // sentinel, matching the QValueRef-overload's step 0.
            const clang::ValueDecl* nd = dre->getDecl();
            if (!nd) return universal_sentinel();
            QValueRef synthetic;
            synthetic.name = nd->getNameAsString();
            synthetic.decl_loc = nd->getLocation();
            return footprint(synthetic, ctx);
        }
    }

    // Step 3 — anything else: universal sentinel. This catches
    // dependent-type operands, plugin-op opaque arguments, unary
    // operators, compound expressions the matcher did not flatten,
    // and any AST shape the extractor has not been taught to peel.
    return universal_sentinel();
}

bool may_overlap(const QubitFootprint& a, const QubitFootprint& b) {
    // Criterion 1 — universal sentinel on either side short-circuits
    // to "may overlap" (true). The sentinel is the "any decl, any
    // bits" wildcard; it is designed to force the peephole matcher
    // to refuse commutation whenever extraction failed.
    if (a.name.empty() || b.name.empty()) return true;

    // Criterion 2 — different decls cannot alias in single-TU mode.
    // `decl_loc` is the primary discriminator (two locals with the
    // same spelled name in shadowed scopes have different
    // locations); `name` is a belt-and-braces check that rejects any
    // AST shape where two distinct decls happen to share a
    // location (unlikely but defensive).
    if (a.name != b.name) return false;
    if (a.decl_loc.getRawEncoding() != b.decl_loc.getRawEncoding()) {
        return false;
    }

    // Criterion 3 — same decl: compare the bit ranges. Disjoint iff
    // one range ends at-or-before the other begins. Identical
    // ranges, partial overlaps, and full containment all return
    // true.
    const int a_lo = a.bit_range.lo;
    const int a_hi = a.bit_range.hi;
    const int b_lo = b.bit_range.lo;
    const int b_hi = b.bit_range.hi;
    if (a_hi <= b_lo) return false; // a strictly before b
    if (b_hi <= a_lo) return false; // b strictly before a
    return true;
}

} // namespace sturm::transpile::detail
