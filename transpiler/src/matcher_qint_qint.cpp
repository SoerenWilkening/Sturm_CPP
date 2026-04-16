// matcher_qint_qint.cpp — Phase C (PC-1..PC-5) qint-qint compound-assign.
//
// Five near-identical matchers for `a <op>= b;` on a `qint_t<W>` LHS with a
// `qint_t<W>` RHS (bare DeclRefExpr — no converting constructor):
//
//   - PC-1 : `a += b;` → QOpKind::ADD_ASSIGN_QINT
//   - PC-2 : `a -= b;` → QOpKind::SUB_ASSIGN_QINT
//   - PC-3 : `a *= b;` → QOpKind::MUL_ASSIGN_QINT
//   - PC-4 : `a /= b;` → QOpKind::DIV_ASSIGN_QINT
//   - PC-5 : `a %= b;` → QOpKind::MOD_ASSIGN_QINT
//
// Structural difference from Phase B: the RHS is a bare DeclRefExpr to a
// qint_t — no CXXConstructExpr wrapper because no converting constructor
// fires. The LHS guard is the same hasCanonicalType+hasDeclaration chain
// Phase B uses to peel through typedef + TemplateSpecializationType sugar
// to the qint_t RecordDecl. The RHS guard mirrors it so both sides require
// a qint_t type — this is what makes PB and PC mutually disjoint: PB's AST
// has a CXXConstructExpr wrapping an int literal, PC's AST has a bare
// DeclRefExpr to another qint_t. No CXXOperatorCallExpr can trigger both.
//
// The M8 pass emits `uncompute_{add,sub,mul,div,mod}_qint(lhs, rhs);` as
// the inverse line.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::make_ref;

// Shared body: same walk-and-record shape as Phase B, but the RHS is a
// bare DeclRefExpr (no CXXConstructExpr to peel) so the bound `rhs_expr`
// points directly at the identifier token. Lexer::getSourceText still
// returns the bare name; using the Expr overload via `.bind("rhs_expr")`
// keeps the field type identical to PB so the body can be literal-shared.
template <QOpKind Kind>
class QIntAssignQIntCallback : public MatchFinder::MatchCallback {
public:
    explicit QIntAssignQIntCallback(QUnit* unit) : unit_(unit) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs  = r.Nodes.getNodeAs<Expr>("rhs_expr");
        if (!call || !lhs || !rhs || !r.Context) return;

        // Phase H PH-1: `enclosing_scope` transparently supports braced
        // CompoundStmt and braceless for/while/if/else body positions.
        const auto es = enclosing_scope(*call, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_es = r.Context->getSourceManager();
        const LangOptions& lang_es = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_es, lang_es);

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lo = r.Context->getLangOpts();
        auto text = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(rhs->getSourceRange()),
            sm, lo);
        if (text.empty()) return;

        QValueRef rhs_ref;
        rhs_ref.name = text.str();

        QOperation op;
        op.kind   = Kind;
        op.result = make_ref(*lhs);
        op.operands.push_back(std::move(rhs_ref));
        op.stmt_range = call->getSourceRange();
        scope.ops.push_back(std::move(op));
    }
private:
    QUnit* unit_;
};

using AddAssignQIntCallback =
    QIntAssignQIntCallback<QOpKind::ADD_ASSIGN_QINT>;
using SubAssignQIntCallback =
    QIntAssignQIntCallback<QOpKind::SUB_ASSIGN_QINT>;
using MulAssignQIntCallback =
    QIntAssignQIntCallback<QOpKind::MUL_ASSIGN_QINT>;
using DivAssignQIntCallback =
    QIntAssignQIntCallback<QOpKind::DIV_ASSIGN_QINT>;
using ModAssignQIntCallback =
    QIntAssignQIntCallback<QOpKind::MOD_ASSIGN_QINT>;

template <typename Cb>
std::vector<std::unique_ptr<Cb>>& qint_callback_pool() {
    static std::vector<std::unique_ptr<Cb>> pool;
    return pool;
}

// Pattern builder — differs from Phase B's only in the RHS: a qint-typed
// DeclRefExpr rather than a CXXConstructExpr-wrapped classical literal.
template <typename OperatorName>
auto make_qint_qint_pattern(OperatorName op_name) {
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
                .bind("rhs_expr")))
    ).bind("call");
}

template <typename Cb, typename OperatorName>
void register_qint_qint(clang::ast_matchers::MatchFinder& finder,
                        QUnit& unit, OperatorName op_name) {
    auto& pool = qint_callback_pool<Cb>();
    pool.push_back(std::make_unique<Cb>(&unit));
    finder.addMatcher(make_qint_qint_pattern(op_name), pool.back().get());
}

} // namespace

void register_add_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_qint<AddAssignQIntCallback>(finder, unit, "+=");
}

void register_sub_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_qint<SubAssignQIntCallback>(finder, unit, "-=");
}

void register_mul_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_qint<MulAssignQIntCallback>(finder, unit, "*=");
}

void register_div_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_qint<DivAssignQIntCallback>(finder, unit, "/=");
}

void register_mod_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_qint<ModAssignQIntCallback>(finder, unit, "%=");
}

} // namespace sturm::transpile
