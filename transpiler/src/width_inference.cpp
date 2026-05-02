// width_inference.cpp — sturm-u9ge.11 (Beat B1) implementation.
//
// Plan §5 / Beat B1; PRD §6 + §11.3 (D0c). See `width_inference.hpp`
// for the rule order and the rationale.
//
// Implementation strategy
// -----------------------
// Three rules, evaluated in strict order. Each helper below
// implements one rule's preconditions + return; the public
// `infer_width` is a 3-line dispatcher that pipelines them.
//
//   Rule 1 — Annotation: walk the VarDecl's QualType for a
//   ClassTemplateSpecializationDecl whose primary template is named
//   `qint` (any namespace — both `sturm::frontend::qint` and
//   `sturm::qint` count, the unifying signal is the bare `qint`
//   identifier on the primary template). When found and
//   `kAllowAnnotation == false`, fire
//   `qram-width-annotation-reserved` and fall through to rule 3 by
//   returning `std::nullopt`. When found and `kAllowAnnotation ==
//   true`, return the integral first template argument.
//
//   Rule 2 — RHS-driven: walk the VarDecl's initializer subtree
//   collecting every subscript expression (CXXOperatorCallExpr with
//   OO_Subscript, ArraySubscriptExpr) and, for each, peel the
//   container's element type to a `sturm::qint_t<W_e>` element width.
//   Collapse the candidate set: zero hits → fall through;
//   one unique W_e → return W_e; two or more distinct → fire
//   `qram-width-mismatch` + one Note per candidate, fall through.
//
//   Rule 3 — Default: return `ctx.default_width` (PRD §11.3 / D0c.1
//   row 3, defaults to `kDefaultWidth = 32`).

#include "width_inference.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"

#include <optional>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;

// ── Width extraction helpers ────────────────────────────────────────────────

// Extract the integral first template argument of a
// ClassTemplateSpecializationDecl. Returns std::nullopt for
// dependent / non-integral / non-positive widths. Mirrors
// `matcher_modular_op.cpp::extract_qint_width_from_vd` but takes the
// spec directly so we can reuse for both qint_t<W> and qint<W>.
std::optional<unsigned> integral_first_arg(
    const ClassTemplateSpecializationDecl& spec) {
    const TemplateArgumentList& args = spec.getTemplateArgs();
    if (args.size() == 0) return std::nullopt;
    const TemplateArgument& arg0 = args.get(0);
    if (arg0.getKind() != TemplateArgument::Integral) return std::nullopt;
    const llvm::APSInt& apsint = arg0.getAsIntegral();
    const int64_t v = apsint.getExtValue();
    if (v <= 0) return std::nullopt;
    return static_cast<unsigned>(v);
}

// Peel a QualType to the `W` of `sturm::qint_t<W>` if it is one.
// Returns std::nullopt otherwise (including dependent, non-record,
// non-qint_t, or zero/negative width).
std::optional<unsigned> peel_qint_t_width(QualType qt) {
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
    return integral_first_arg(*spec);
}

// Peel a QualType to the `W` of a `qint<W>` annotation form. The
// rule-1 detection accepts any `ClassTemplateSpecializationDecl`
// whose primary template's bare identifier is `qint` (so both
// `sturm::frontend::qint<W>` and any other-namespace `qint<W>`
// match — the v1 frontend type is the only one users can produce,
// but the test stub uses `sturm::frontend::qint<W>` to avoid a
// name clash with the non-templated `qint` it sits next to).
std::optional<unsigned> peel_qint_annotation_width(QualType qt) {
    qt = qt.getCanonicalType();
    const auto* record = qt->getAsCXXRecordDecl();
    if (!record) return std::nullopt;
    const auto* spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(record);
    if (!spec) return std::nullopt;
    const ClassTemplateDecl* primary = spec->getSpecializedTemplate();
    if (!primary) return std::nullopt;
    if (primary->getNameAsString() != "qint") return std::nullopt;
    return integral_first_arg(*spec);
}

// ── Rule 2 helper: collect element widths for every subscript ───────────────

// Element type of the container expression on the LHS of a
// subscript. Three container shapes per PRD §7:
//   - `std::array<qint_t<W>, N>::operator[]` — CXXOperatorCallExpr,
//     element type is the `T` of `array<T, N>`. We peel through
//     QualType::getAsCXXRecordDecl + ClassTemplateSpecializationDecl
//     and read template arg #0.
//   - `qint_t<W>[N]` — ArraySubscriptExpr; LHS type is `qint_t<W>[N]`,
//     element type is `qint_t<W>`. `Type::getAsArrayTypeUnsafe` peels.
//   - `qint_t<W>*` — ArraySubscriptExpr; LHS type is `qint_t<W>*`,
//     element type is `qint_t<W>`. `Type::getPointeeType` peels.
//
// In all three shapes we end up with a QualType for the element and
// then run `peel_qint_t_width` on it. Returns std::nullopt if any
// intermediate step fails or the element type is not a
// `sturm::qint_t<W>` instantiation.
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
        // Container class — peel the first template argument off the
        // ClassTemplateSpecializationDecl. Works for the std::array
        // stand-in (and the real `std::array<qint_t<W>, N>`).
        const auto* record =
            ct.getCanonicalType()->getAsCXXRecordDecl();
        if (!record) return std::nullopt;
        const auto* spec =
            llvm::dyn_cast<ClassTemplateSpecializationDecl>(record);
        if (!spec) return std::nullopt;
        const TemplateArgumentList& args = spec->getTemplateArgs();
        if (args.size() == 0) return std::nullopt;
        const TemplateArgument& arg0 = args.get(0);
        if (arg0.getKind() != TemplateArgument::Type) {
            return std::nullopt;
        }
        elem = arg0.getAsType();
    }
    return peel_qint_t_width(elem);
}

// Walk an expression subtree collecting every subscript site's
// element-width candidate. Both ArraySubscriptExpr and
// CXXOperatorCallExpr-with-OO_Subscript count; for the op-call the
// container is `getArg(0)`, for the built-in it is `getBase()`.
class SubscriptWidthCollector
    : public RecursiveASTVisitor<SubscriptWidthCollector> {
public:
    bool VisitArraySubscriptExpr(ArraySubscriptExpr* e) {
        if (!e) return true;
        if (auto w = element_qint_t_width(e->getBase())) {
            widths_.push_back(*w);
        }
        return true;
    }
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* e) {
        if (!e) return true;
        if (e->getOperator() != OO_Subscript) return true;
        if (e->getNumArgs() < 1) return true;
        if (auto w = element_qint_t_width(e->getArg(0))) {
            widths_.push_back(*w);
        }
        return true;
    }
    const std::vector<unsigned>& widths() const { return widths_; }
private:
    std::vector<unsigned> widths_;
};

// Deduplicate-and-sort a width-candidate vector in place. Returned
// vector has each unique width exactly once, sorted ascending so
// the diagnostic message is stable across runs.
void dedup_sort(std::vector<unsigned>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

// Format a list of unique widths as a human-readable enumeration:
// {8} -> "8", {8,16} -> "8, 16", {8,16,32} -> "8, 16, 32".
std::string format_widths(const std::vector<unsigned>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::to_string(v[i]);
    }
    return out;
}

// ── Diagnostic emitters ─────────────────────────────────────────────────────

void fire_annotation_reserved(DiagnosticsEngine& diag, const VarDecl& vd,
                              unsigned annotated_w) {
    const unsigned id = diag.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Error,
        "STURM: width-inference rule 1 annotation 'qint<%0>' on '%1' is "
        "a reserved future syntax in v1 [qram-width-annotation-reserved] "
        "(falling back to default; PRD docs/prd_qram_subscript.md "
        "section 11.3 / D0c.1 row 1).");
    diag.Report(vd.getLocation(), id)
        << annotated_w << vd.getNameAsString();
}

void fire_width_mismatch(DiagnosticsEngine& diag, const VarDecl& vd,
                         const std::vector<unsigned>& widths) {
    const unsigned id = diag.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Error,
        "STURM: subscript on '%0' admits multiple element widths (%1); "
        "add an explicit qint<W> annotation to disambiguate "
        "[qram-width-mismatch] (PRD docs/prd_qram_subscript.md "
        "section 11.3 / D0c.2).");
    diag.Report(vd.getLocation(), id)
        << vd.getNameAsString() << format_widths(widths);
    const unsigned note_id = diag.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note,
        "STURM: candidate element width %0.");
    for (unsigned w : widths) {
        diag.Report(vd.getLocation(), note_id) << w;
    }
}

// ── Rule dispatchers ────────────────────────────────────────────────────────

// Rule 1: annotation form `qint<W> b = …;`. Returns the parsed W when
// `ctx.allow_annotation == true`; otherwise fires the reserved
// diagnostic and returns std::nullopt (fall through).
std::optional<unsigned> try_rule1_annotation(
    const VarDecl& vd, const InferContext& ctx) {
    auto w = peel_qint_annotation_width(vd.getType());
    if (!w) return std::nullopt;
    if (ctx.allow_annotation) {
        return w;
    }
    if (ctx.diag) {
        fire_annotation_reserved(*ctx.diag, vd, *w);
    }
    return std::nullopt;
}

// Rule 2: RHS-driven. Walks the initializer for subscript shapes,
// collapses the candidate set. One unique width → return it. Two or
// more → fire mismatch and return std::nullopt (fall through).
std::optional<unsigned> try_rule2_rhs(
    const VarDecl& vd, const InferContext& ctx) {
    const Expr* init = vd.getInit();
    if (!init) return std::nullopt;
    SubscriptWidthCollector c;
    c.TraverseStmt(const_cast<Expr*>(init));
    std::vector<unsigned> widths = c.widths();
    if (widths.empty()) return std::nullopt;
    dedup_sort(widths);
    if (widths.size() == 1) {
        return widths.front();
    }
    if (ctx.diag) {
        fire_width_mismatch(*ctx.diag, vd, widths);
    }
    return std::nullopt;
}

} // anonymous namespace

unsigned infer_width(const clang::VarDecl& vd, const InferContext& ctx) {
    // Rule 1: annotation (gated, fires reserved diag in v1).
    if (auto w = try_rule1_annotation(vd, ctx)) return *w;
    // Rule 2: RHS-driven (fires mismatch diag on ambiguity, falls through).
    if (auto w = try_rule2_rhs(vd, ctx)) return *w;
    // Rule 3: global default. Always defined.
    return ctx.default_width;
}

} // namespace sturm::transpile
