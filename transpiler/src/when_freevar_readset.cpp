// when_freevar_readset.cpp — E7.M1 implementation.
//
// See `when_freevar_readset.hpp` for the public contract. The walk has
// two cooperating pieces:
//
//   - `ReadCollector` (RecursiveASTVisitor) — traverses an expression
//     or statement subtree, recording every `DeclRefExpr` to a
//     `VarDecl` and dispatching on every `CallExpr` to schedule
//     transitive descents.
//   - `compute_when_freevar_readset` — the public entry point that
//     constructs a single shared `WhenFreeVarReadSet` accumulator and a
//     `visited` guard, then runs the collector on the input expression
//     and any callee bodies the collector queues.
//
// The visited-functions guard is keyed on `FunctionDecl::getCanonicalDecl()`
// so multiple redeclarations of the same function (e.g. forward decl +
// definition) collapse to one identity. The guard is shared across the
// entire walk, which is what gives the recursion termination.

#include "when_freevar_readset.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <vector>

namespace sturm::transpile {

namespace {

// Visited-functions guard. We key on `getCanonicalDecl()` so a forward
// declaration and its later definition are treated as the same function;
// the guard is also what breaks cycles in self- or mutually-recursive
// chains.
using VisitedSet =
    llvm::SmallPtrSet<const clang::FunctionDecl*, 8>;

// Forward declaration so `ReadCollector::VisitCallExpr` can enqueue
// callee bodies for the driver to walk.
class ReadCollector;

// Walk a Stmt subtree (typically the WHEN expression or a callee body)
// and fold its reads + transitive reads into `out`. Implemented as a
// free function so both the public entry point and the per-call
// transitive descent share one code path.
void walk_subtree(const clang::Stmt* root,
                  WhenFreeVarReadSet& out,
                  VisitedSet& visited);

class ReadCollector
    : public clang::RecursiveASTVisitor<ReadCollector> {
public:
    ReadCollector(WhenFreeVarReadSet* out, VisitedSet* visited)
        : out_(out), visited_(visited) {}

    // Every variable reference inside the walked subtree contributes
    // its referenced VarDecl to the read-set. Reads through `const`
    // references and by-value parameters fall under the same shape —
    // a DeclRefExpr is a DeclRefExpr regardless of how the value is
    // consumed downstream.
    bool VisitDeclRefExpr(clang::DeclRefExpr* dre) {
        if (!dre || !out_) return true;
        const clang::ValueDecl* vd = dre->getDecl();
        if (!vd) return true;
        // Only `VarDecl`s contribute; references to FunctionDecls,
        // EnumConstantDecls, etc. are not part of any write-set the
        // E7.M2 checker compares against.
        if (const auto* var = clang::dyn_cast<clang::VarDecl>(vd)) {
            out_->vars.insert(var);
        }
        return true;
    }

    // Every CallExpr potentially leaks reads through the callee body.
    // Resolve the direct callee; if its definition is in this TU,
    // queue the body for traversal. Otherwise flag conservative.
    //
    // Note: we still let the RecursiveASTVisitor descend into the
    // call's argument expressions (the default Visit-then-traverse
    // shape) so any DeclRefExprs spelled at the call site (e.g.
    // `f(a + 1)` reading `a`) are captured by the DeclRefExpr visitor
    // above. The transitive body walk is purely additive on top.
    bool VisitCallExpr(clang::CallExpr* ce) {
        if (!ce || !out_ || !visited_) return true;
        const clang::FunctionDecl* fd = ce->getDirectCallee();
        if (!fd) {
            // Indirect call (function pointer, member-pointer, virtual
            // dispatch we cannot resolve, etc.). The body is by
            // definition unknown — flag conservative and stop.
            out_->conservative = true;
            return true;
        }
        // Canonicalize so re-declarations collapse to one identity.
        const clang::FunctionDecl* canon = fd->getCanonicalDecl();
        if (!canon) canon = fd;
        // Cycle / re-visit guard: if we've already entered this
        // callee on the current walk, do nothing (the previous entry
        // will have collected its reads / set conservative as
        // appropriate). This is what makes recursive callees
        // terminate.
        if (!visited_->insert(canon).second) {
            return true;
        }
        // Prefer the definition's body if any redeclaration in this
        // TU has one. `getDefinition()` walks the redecl chain and
        // returns the FunctionDecl that owns the body, or nullptr.
        const clang::FunctionDecl* def = fd->getDefinition();
        const clang::Stmt* body = def ? def->getBody() : nullptr;
        if (!body) {
            // Declared but not defined here. We cannot see the reads
            // the callee performs, so the read-set is potentially
            // incomplete. Flag and move on.
            out_->conservative = true;
            return true;
        }
        // Recurse into the callee body. The visited guard keeps us
        // safe against self- and mutual recursion; the shared
        // `out_` accumulates reads across every transitive call.
        walk_subtree(body, *out_, *visited_);
        return true;
    }

private:
    WhenFreeVarReadSet* out_;
    VisitedSet*         visited_;
};

void walk_subtree(const clang::Stmt* root,
                  WhenFreeVarReadSet& out,
                  VisitedSet& visited) {
    if (!root) return;
    ReadCollector collector(&out, &visited);
    // `TraverseStmt` accepts a non-const Stmt*; the visitor does not
    // mutate the tree. Same const_cast pattern used by the matcher
    // helpers in `matcher_common.hpp` (see `count_readers_in_scope`).
    collector.TraverseStmt(const_cast<clang::Stmt*>(root));
}

} // namespace

WhenFreeVarReadSet
compute_when_freevar_readset(const clang::Expr* expr) {
    WhenFreeVarReadSet out;
    if (!expr) return out;
    VisitedSet visited;
    walk_subtree(expr, out, visited);
    return out;
}

} // namespace sturm::transpile
