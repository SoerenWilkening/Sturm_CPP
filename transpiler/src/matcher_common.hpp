// matcher_common.hpp — internal helpers shared by every matcher module.
//
// This header is NOT part of the public transpiler API. It lives under
// transpiler/src/ (not include/) because it exposes Clang AST types that
// downstream callers should not transitively pick up through the public
// matcher.hpp. Only the matcher_*.cpp implementation files include this.
//
// Contents:
//   - `enclosing_compound_stmt` : walk ParentMapContext to the nearest
//     CompoundStmt ancestor (two overloads: Decl, Stmt).
//   - `find_or_create_scope`    : look up / allocate a QScope by the
//     enclosing brace's raw SourceLocation encoding.
//   - `make_ref`                : lift a DeclRefExpr into a QValueRef.
//
// Callback-pool lifetime pattern
// ------------------------------
// Every matcher module keeps a file-scope `std::vector<unique_ptr<Cb>>` with
// program-scoped storage duration (function-local `static`). The MatchFinder
// stores raw callback pointers with no ownership semantics, so the pool's
// job is to keep them alive for the MatchFinder's lifetime. Callbacks are
// never freed — a one-shot transpiler tool exits shortly after matching
// completes, so the bounded leak is the right trade-off. Each call to a
// register_*_matcher helper appends one new callback to its module's pool.
//
// Style note: the helpers live in an anonymous namespace via `static`
// linkage (one internal-linkage inline definition per TU is cheaper than a
// one-definition-rule header with `inline`, and LibClang has no
// cross-TU optimization to benefit from the alternative).

#ifndef STURM_TRANSPILE_MATCHER_COMMON_HPP
#define STURM_TRANSPILE_MATCHER_COMMON_HPP

#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceLocation.h"

#include <utility>

namespace sturm::transpile::detail {

// Walk up the parent chain of `node` until we find the immediately enclosing
// CompoundStmt. Returns nullptr if none exists (e.g. a declaration at
// namespace scope, which cannot be the MVP pattern anyway). Uses the dynamic
// ParentMapContext because Decl / Stmt do not carry parent pointers.
//
// Two overloads are provided: initializer-based matchers (PA-1/PA-2) enter
// via a VarDecl, while statement-based matchers (PA-3 onward) enter via an
// Expr/Stmt. Both funnel through the same DynTypedNode walk.
template <typename T>
inline const clang::CompoundStmt*
enclosing_compound_stmt_impl(const T& n, clang::ASTContext& ctx) {
    clang::DynTypedNode node = clang::DynTypedNode::create(n);
    while (true) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return nullptr;
        // We deliberately follow only the first parent. Clang's
        // ParentMapContext occasionally yields multiple parents for template
        // instantiations, but the MVP matcher does not run inside templated
        // contexts (the class-name matcher does not bind dependent types).
        node = parents[0];
        if (const auto* cs = node.get<clang::CompoundStmt>()) return cs;
    }
}

inline const clang::CompoundStmt*
enclosing_compound_stmt(const clang::Decl& decl, clang::ASTContext& ctx) {
    return enclosing_compound_stmt_impl(decl, ctx);
}

inline const clang::CompoundStmt*
enclosing_compound_stmt(const clang::Stmt& stmt, clang::ASTContext& ctx) {
    return enclosing_compound_stmt_impl(stmt, ctx);
}

// Locate the QScope in `unit` whose open_brace matches `cs`, creating one at
// the end of `unit.scopes` if none exists. The raw encoding of the opening
// brace is a stable scope identity within a single translation unit.
inline QScope& find_or_create_scope(QUnit& unit, const clang::CompoundStmt& cs) {
    const auto key = cs.getLBracLoc().getRawEncoding();
    for (auto& scope : unit.scopes) {
        if (scope.open_brace.getRawEncoding() == key) return scope;
    }
    QScope fresh;
    fresh.open_brace  = cs.getLBracLoc();
    fresh.close_brace = cs.getRBracLoc();
    unit.scopes.push_back(std::move(fresh));
    return unit.scopes.back();
}

// Extract a QValueRef from a DeclRefExpr. The decl_loc is the referenced
// declaration's location (NOT the call-site DeclRefExpr's location), which
// is what QValueRef equality uses to discriminate shadowed locals.
inline QValueRef make_ref(const clang::DeclRefExpr& dre) {
    QValueRef ref;
    const clang::NamedDecl* nd = dre.getDecl();
    if (nd) {
        ref.name     = nd->getNameAsString();
        ref.decl_loc = nd->getLocation();
    }
    return ref;
}

} // namespace sturm::transpile::detail

#endif // STURM_TRANSPILE_MATCHER_COMMON_HPP
