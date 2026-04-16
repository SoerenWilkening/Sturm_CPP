// routine_registry.cpp — Phase I PI-1 matcher populating RoutineRegistry.
//
// See routine_registry.hpp for the data structure and the matcher's
// semantic contract. This .cpp contains exactly one MatchFinder
// callback: on every `ClassTemplateSpecializationDecl` of the
// qualified name `::sturm::_detail::adjoint_of`, the callback extracts
// the forward FunctionDecl* and the adjoint's source-level name from
// the specialization's template argument and its `value` member
// initializer, and records the pair in the caller-owned registry.
//
// Extraction algorithm (matches the STURM_REGISTER_ADJOINT expansion):
//
//   spec : ClassTemplateSpecializationDecl
//     | qualified name == ::sturm::_detail::adjoint_of
//     | has written template argument TAL[0] of kind Type
//     |   whose TypeLoc chain reaches DecltypeTypeLoc
//     |   whose underlying expr (after peeling ParenExpr / ImplicitCast)
//     |   is a UnaryOperator(AddrOf) wrapping a DeclRefExpr to FunctionDecl F_fwd
//     | has static data member `value`
//     |   whose initializer (after peeling) is a UnaryOperator(AddrOf)
//     |   wrapping a DeclRefExpr to FunctionDecl F_adj
//   →  registry.insert_pair(F_fwd, F_adj.getNameAsString())
//
// Every structural expectation is guarded with an `if (!x) return;` so
// a malformed specialization (e.g. user wrote the primary by mistake,
// or a competing adjoint_of lives in a different namespace) quietly
// declines to match rather than crashing the transpiler.
//
// The matcher is ORDERING-INSENSITIVE with respect to every other
// matcher in the transpiler — it reads no QUnit state and writes only
// to the registry. It is registered in main.cpp before the Phase I
// PI-2 routine-call matcher so the map is populated by the time PI-2
// starts consulting it.

#include "routine_registry.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Peel ParenExpr / ImplicitCastExpr / CXXNoexceptExpr wrappers from an
// Expr. The `value` initializer and the `decltype(&fn)` underlying
// expression both arrive wrapped in implicit glue (e.g. lvalue-to-
// function-pointer conversion). We only care about the payload
// UnaryOperator.
const Expr* peel_impl(const Expr* e) {
    if (!e) return nullptr;
    return e->IgnoreParenImpCasts();
}

// If `e` is `&<decl-ref>`, return the referenced FunctionDecl (after
// canonicalisation). Otherwise return nullptr. The canonicalisation is
// what lets callers compare FunctionDecl* identity across redeclaration
// chains — the STURM_REGISTER_ADJOINT expansion names functions by
// their pointer type, and the forward routine may have a prior
// declaration in the same TU whose canonical decl is shared.
const FunctionDecl* addr_of_function(const Expr* e) {
    const Expr* inner = peel_impl(e);
    const auto* unop = llvm::dyn_cast_or_null<UnaryOperator>(inner);
    if (!unop) return nullptr;
    if (unop->getOpcode() != UO_AddrOf) return nullptr;
    const Expr* sub = peel_impl(unop->getSubExpr());
    const auto* dre = llvm::dyn_cast_or_null<DeclRefExpr>(sub);
    if (!dre) return nullptr;
    const auto* fd = llvm::dyn_cast_or_null<FunctionDecl>(dre->getDecl());
    if (!fd) return nullptr;
    return fd->getCanonicalDecl();
}

// Same as above but return the source-level name even if the referenced
// decl is null/unresolved (defensive fallback — not expected in
// well-formed source, but avoids silently dropping entries on the
// edge). Returns empty string on any structural mismatch.
std::string addr_of_function_name(const Expr* e) {
    const Expr* inner = peel_impl(e);
    const auto* unop = llvm::dyn_cast_or_null<UnaryOperator>(inner);
    if (!unop) return {};
    if (unop->getOpcode() != UO_AddrOf) return {};
    const Expr* sub = peel_impl(unop->getSubExpr());
    const auto* dre = llvm::dyn_cast_or_null<DeclRefExpr>(sub);
    if (!dre) return {};
    if (const auto* nd = dre->getDecl()) {
        return nd->getNameAsString();
    }
    // Fallback: spelling of the name as written. `getNameInfo` returns a
    // DeclarationNameInfo whose getAsString() is the source form.
    return dre->getNameInfo().getAsString();
}

// Extract the forward FunctionDecl from a written template argument
// whose TypeSourceInfo is `decltype(&::fn)`. Walk the TypeLoc chain to
// find a DecltypeTypeLoc, then use its underlying expression.
//
// Falls back to looking at the unsugared type if a DecltypeTypeLoc is
// not present — this is the unlikely-but-possible case where the user
// wrote the specialization out by hand without decltype, e.g.
// `adjoint_of<void(*)(int)>` — in which case we can't resolve which
// function was meant and we return nullptr, declining to register.
const FunctionDecl* extract_forward_from_targ(
    const TemplateArgumentLoc& tal) {
    if (tal.getArgument().getKind() != TemplateArgument::Type) return nullptr;
    TypeSourceInfo* tsi = tal.getTypeSourceInfo();
    if (!tsi) return nullptr;

    // Walk the TypeLoc chain down through any number of sugared wrappers
    // (Paren, Qualified, Attributed, Typedef, ElaboratedType, ...) to
    // find a DecltypeTypeLoc whose underlying expr can be introspected.
    TypeLoc tl = tsi->getTypeLoc();
    for (int hops = 0; hops < 32 && !tl.isNull(); ++hops) {
        if (auto dtl = tl.getAs<DecltypeTypeLoc>()) {
            const Expr* under = dtl.getUnderlyingExpr();
            return addr_of_function(under);
        }
        // Descend through any sugared TypeLoc — getNextTypeLoc() is the
        // standard "walk one wrapper layer" helper. For wrappers that
        // don't have one (LeafType), the result is a null TypeLoc and
        // the loop terminates.
        TypeLoc next = tl.getNextTypeLoc();
        if (next.isNull() || next.getOpaqueData() == tl.getOpaqueData()) {
            break;
        }
        tl = next;
    }
    return nullptr;
}

// Locate the `value` static data member of the specialization's record
// declaration and return its initializer expression. Returns nullptr if
// the record is incomplete, has no `value` field, or the field has no
// initializer.
const Expr* find_value_initializer(const ClassTemplateSpecializationDecl* spec) {
    if (!spec) return nullptr;
    // The class body is a CXXRecordDecl. Iterate its VarDecls looking
    // for a static data member named `value`.
    const auto* rec = llvm::dyn_cast<CXXRecordDecl>(spec);
    if (!rec) return nullptr;
    for (const Decl* d : rec->decls()) {
        const auto* vd = llvm::dyn_cast<VarDecl>(d);
        if (!vd) continue;
        if (vd->getNameAsString() != "value") continue;
        // `static constexpr auto value = &::adj;` — the initializer
        // hangs directly off the VarDecl.
        return vd->getInit();
    }
    return nullptr;
}

// True iff `spec` is a specialization of `::sturm::_detail::adjoint_of`.
// We check by qualified name rather than by matching the primary
// ClassTemplateDecl identity, so the matcher remains robust to
// redeclaration chains / template-parameter tweaks the runtime header
// might evolve to.
bool is_adjoint_of_specialization(const ClassTemplateSpecializationDecl* spec) {
    if (!spec) return false;
    const ClassTemplateDecl* tmpl = spec->getSpecializedTemplate();
    if (!tmpl) return false;
    // `getQualifiedNameAsString` returns the fully qualified name, e.g.
    // "sturm::_detail::adjoint_of". We tolerate both "sturm::_detail::
    // adjoint_of" (no leading ::) and any equivalent form by asserting
    // the suffix.
    const std::string qn = tmpl->getQualifiedNameAsString();
    return qn == "sturm::_detail::adjoint_of";
}

class RoutineRegistryCallback : public MatchFinder::MatchCallback {
public:
    explicit RoutineRegistryCallback(RoutineRegistry* reg) : reg_(reg) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* spec = r.Nodes.getNodeAs<ClassTemplateSpecializationDecl>(
            "spec");
        if (!spec || !reg_) return;
        if (!is_adjoint_of_specialization(spec)) return;

        // ── Forward FunctionDecl from template argument 0 ───────────
        // ClassTemplateSpecializationDecl exposes the written
        // template-argument list via `getTypeAsWritten()` — a
        // TypeSourceInfo whose TypeLoc is a
        // TemplateSpecializationTypeLoc. We walk its first argument
        // loc, whose TypeSourceInfo is the source form of
        // `decltype(&::fn)` (the STURM_REGISTER_ADJOINT shape). Once
        // we have the decltype's underlying expression we extract the
        // forward FunctionDecl via addr_of_function().
        const FunctionDecl* fwd = nullptr;
        if (TypeSourceInfo* tsi = spec->getTypeAsWritten()) {
            TypeLoc tl = tsi->getTypeLoc();
            // The ElaboratedTypeLoc wrapper is present when the
            // specialization is written with a qualified name; peel it
            // off with the standard idiom so we reach the
            // TemplateSpecializationTypeLoc underneath.
            if (auto etl = tl.getAs<ElaboratedTypeLoc>()) {
                tl = etl.getNamedTypeLoc();
            }
            if (auto tstl = tl.getAs<TemplateSpecializationTypeLoc>()) {
                if (tstl.getNumArgs() >= 1) {
                    fwd = extract_forward_from_targ(tstl.getArgLoc(0));
                }
            }
        }
        if (!fwd) return;

        // ── Adjoint name from `value` initializer ───────────────────
        const Expr* init = find_value_initializer(spec);
        if (!init) return;
        std::string adj_name = addr_of_function_name(init);
        if (adj_name.empty()) return;

        reg_->insert_pair(fwd, std::move(adj_name));
    }

private:
    RoutineRegistry* reg_;
};

// Static pool so repeated registrations across tests do not leak
// callback lifetimes (mirrors the discipline used by every other
// matcher module in this directory).
std::vector<std::unique_ptr<RoutineRegistryCallback>>&
routine_registry_callback_pool() {
    static std::vector<std::unique_ptr<RoutineRegistryCallback>> pool;
    return pool;
}

} // namespace

void register_routine_registry_matcher(
    clang::ast_matchers::MatchFinder& finder, RoutineRegistry& registry) {
    // Match any ClassTemplateSpecializationDecl and filter by qualified
    // name inside the callback. Attempting to match by qualified name
    // directly via an AST matcher requires `hasName(...)` on the
    // specialized template, which is the form below — cheap and keeps
    // the match set narrow even before the callback runs.
    auto pattern = classTemplateSpecializationDecl(
        hasSpecializedTemplate(
            classTemplateDecl(hasName("::sturm::_detail::adjoint_of"))))
        .bind("spec");

    auto& pool = routine_registry_callback_pool();
    pool.push_back(std::make_unique<RoutineRegistryCallback>(&registry));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
