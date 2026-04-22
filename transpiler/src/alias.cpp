// alias.cpp — PM5-2 / PM5-3 alias-analysis implementations.
//
// Implements the two free functions declared in
// `sturm/transpile/alias.hpp` (PM5-1):
//
//   - `footprint(const QValueRef&, const clang::ASTContext&)`
//     peels the underlying VarDecl's type and returns a
//     `QubitFootprint` describing the bit range of the qubit
//     container the operand refers to. This TU covers the PM5-2
//     subset of the ladder (bare `qbool` DRE + bare `qint_t<W>`
//     DRE with compile-time `W`); the PM5-3 BitProxy constant-index
//     peel is a separate follow-up issue.
//
//   - `may_overlap(const QubitFootprint&, const QubitFootprint&)`
//     three-step short-circuit: universal sentinel → true,
//     different decl → false, same decl → compare bit ranges.
//
// Extraction algorithm (PM5-2 coverage — see
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
// PM5-3 / BitProxy / non-const subscript: deferred. The current
// implementation returns the universal sentinel for any operand
// whose VarDecl is neither a bare qbool nor a bare qint_t<W>. The
// subscript peel will land in a follow-up TU edit that does not
// need to touch this file's public entry points.

#include "sturm/transpile/alias.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

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
    // recognise collapses to the universal sentinel. PM5-3 will
    // add the BitProxy constant-index peel above this fallback.
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
