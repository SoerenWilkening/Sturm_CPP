// matcher_lossy_op.cpp — LO-2a (sturm-9254): lossy compound-assign
// AST matcher. See matcher_lossy_op.hpp for the contract.
//
// Five structurally-identical matchers for `a <op>= b;` on a qint_t<W>
// LHS with a qint_t<W> RHS (bare DeclRefExpr). Node class is
// `CXXOperatorCallExpr` (qint_t's operator overloads are member
// functions). The LHS / RHS guards
// `hasCanonicalType+hasDeclaration → cxxRecordDecl(hasName("qint_t"))`
// peel through typedef + TemplateSpecializationType sugar; user types
// not named `qint_t` are rejected; builtin `int *= int` is rejected
// structurally (it lowers to `CompoundAssignOperator`).
//
// The matcher is READ-ONLY — no QUnit mutation. It records a LossyOpHit
// into a caller-owned vector. LO-2b / LO-2c consume the vector to emit
// the forward-pair and the scope-exit cleanup.
//
// Phase C coexistence: `register_mul_assign_qint_matcher`, div, mod on
// qint_t<W> operands share the AST shape this matcher binds. Both fire
// on the same call — Phase C records a `QOpKind::*_ASSIGN_QINT` op in
// the QUnit; this matcher records a `LossyOpHit` in the hits vector.
// Write sinks are disjoint so both coexist without stepping on each
// other. `&=` and `|=` have no pre-existing matcher.

#include "matcher_lossy_op.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Walk up the parent chain to the nearest enclosing CompoundStmt. LO-2c
// needs the raw lexical block (not the Phase H PH-1 braceless-body
// variant) to anchor the LIFO cleanup at the close brace.
const CompoundStmt* nearest_compound_stmt(const Stmt& start,
                                          ASTContext& ctx) {
    DynTypedNode current = DynTypedNode::create(start);
    for (int hops = 0; hops < 128; ++hops) {
        const auto parents = ctx.getParents(current);
        if (parents.empty()) return nullptr;
        current = parents[0];
        if (const auto* cs = current.get<CompoundStmt>()) {
            return cs;
        }
    }
    return nullptr;
}

// One callback type per opcode kind (NTTP) so the pool stays a single
// template family. Each callback writes into the shared hits vector.
template <LossyOpKind Kind>
class LossyOpCallback : public MatchFinder::MatchCallback {
public:
    explicit LossyOpCallback(std::vector<LossyOpHit>* hits) : hits_(hits) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs  = r.Nodes.getNodeAs<DeclRefExpr>("rhs");
        if (!call || !lhs || !rhs || !r.Context) return;

        // A parsed function body always has a CompoundStmt ancestor;
        // null is defensive. We skip rather than push a half-populated
        // hit so downstream consumers never null-check `enclosing_block`.
        const CompoundStmt* block =
            nearest_compound_stmt(*call, *r.Context);
        if (!block) return;

        LossyOpHit hit;
        hit.opcode = Kind;
        if (const NamedDecl* nd = lhs->getDecl()) {
            hit.lhs_name = nd->getNameAsString();
        }
        if (const NamedDecl* nd = rhs->getDecl()) {
            hit.rhs_name = nd->getNameAsString();
        }
        hit.call = call;
        hit.lhs_ref = lhs;
        hit.rhs_ref = rhs;
        hit.enclosing_block = block;
        hits_->push_back(std::move(hit));
    }

private:
    std::vector<LossyOpHit>* hits_;
};

using MulAssignCallback = LossyOpCallback<LossyOpKind::MulAssign>;
using DivAssignCallback = LossyOpCallback<LossyOpKind::DivAssign>;
using ModAssignCallback = LossyOpCallback<LossyOpKind::ModAssign>;
using AndAssignCallback = LossyOpCallback<LossyOpKind::AndAssign>;
using OrAssignCallback  = LossyOpCallback<LossyOpKind::OrAssign>;

// Per-type unique_ptr pools so callbacks outlive MatchFinder runs.
template <typename Cb>
std::vector<std::unique_ptr<Cb>>& callback_pool() {
    static std::vector<std::unique_ptr<Cb>> pool;
    return pool;
}

// Pattern builder — identical shape for every opcode. `hasName("qint_t")`
// rejects non-qint user types; builtin int shape (CompoundAssignOperator)
// is rejected structurally by the top-level cxxOperatorCallExpr anchor.
template <typename OperatorName>
auto make_lossy_pattern(OperatorName op_name) {
    return cxxOperatorCallExpr(
        hasOverloadedOperatorName(op_name),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(
            declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))
                .bind("lhs"))),
        hasArgument(1, ignoringImplicit(
            declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))
                .bind("rhs")))
    ).bind("call");
}

template <typename Cb, typename OperatorName>
void register_one(MatchFinder& finder,
                  std::vector<LossyOpHit>& hits,
                  OperatorName op_name) {
    auto& pool = callback_pool<Cb>();
    pool.push_back(std::make_unique<Cb>(&hits));
    finder.addMatcher(make_lossy_pattern(op_name), pool.back().get());
}

} // namespace

void register_lossy_op_matcher(MatchFinder& finder,
                               std::vector<LossyOpHit>& hits) {
    register_one<MulAssignCallback>(finder, hits, "*=");
    register_one<DivAssignCallback>(finder, hits, "/=");
    register_one<ModAssignCallback>(finder, hits, "%=");
    register_one<AndAssignCallback>(finder, hits, "&=");
    register_one<OrAssignCallback>(finder, hits, "|=");
}

} // namespace sturm::transpile
