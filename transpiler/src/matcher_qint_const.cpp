// matcher_qint_const.cpp — Phase B (PB-1..PB-4) qint-classical compound-assign.
//
// Four near-identical matchers for `a <op>= <classical>;` on a `qint_t<W>`:
//
//   - PB-1 : `a += <expr>;` → QOpKind::ADD_ASSIGN_CONST
//   - PB-2 : `a -= <expr>;` → QOpKind::SUB_ASSIGN_CONST
//   - PB-3 : `a *= <expr>;` → QOpKind::MUL_ASSIGN_CONST
//   - PB-4 : `a /= <expr>;` → QOpKind::DIV_ASSIGN_CONST
//
// The RHS reaches the qint_t operator through the implicit qint_t(int64_t)
// converting constructor (qint_core.hpp:89). In the AST this shows up as a
// CXXConstructExpr wrapping the user-written source expression. We peel one
// extra layer beyond PA-4 so the bound `rhs_expr` points at the literal
// token, and Lexer::getSourceText returns the verbatim text ("3" instead
// of "qint_t(3)").
//
// The LHS type guard peels through the typedef + TemplateSpecializationType
// sugar the real qint_t<Width> wears (qint_core.hpp:52):
//     hasCanonicalType(hasDeclaration(cxxRecordDecl(hasName("qint_t"))))
// A bare `hasType(cxxRecordDecl(hasName("qint_t")))` does NOT fire on
// `qint_t<1>` because the DeclRefExpr's type is a sugared
// TemplateSpecializationType, not a RecordType directly.

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

// Shared body for all four Phase B callbacks: walk scope, extract RHS
// source text, push one QOperation of the supplied kind. Factoring this
// lets each concrete callback collapse to a one-liner and keeps the
// module size tight.
template <QOpKind Kind>
class QIntAssignConstCallback : public MatchFinder::MatchCallback {
public:
    explicit QIntAssignConstCallback(QUnit* unit) : unit_(unit) {}
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

using AddAssignConstCallback =
    QIntAssignConstCallback<QOpKind::ADD_ASSIGN_CONST>;
using SubAssignConstCallback =
    QIntAssignConstCallback<QOpKind::SUB_ASSIGN_CONST>;
using MulAssignConstCallback =
    QIntAssignConstCallback<QOpKind::MUL_ASSIGN_CONST>;
using DivAssignConstCallback =
    QIntAssignConstCallback<QOpKind::DIV_ASSIGN_CONST>;

template <typename Cb>
std::vector<std::unique_ptr<Cb>>& const_callback_pool() {
    static std::vector<std::unique_ptr<Cb>> pool;
    return pool;
}

// Build the matcher pattern for a compound-assign of operator name `op`
// against a qint_t LHS and a CXXConstructExpr-wrapped classical RHS. The
// pattern shape is identical across PB-1..PB-4, only the operator name
// changes, so we build it through a helper rather than re-typing four
// near-identical 14-line blocks.
template <typename OperatorName>
auto make_qint_const_pattern(OperatorName op_name) {
    return cxxOperatorCallExpr(
        hasOverloadedOperatorName(op_name),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(
            declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))
                .bind("lhs"))),
        hasArgument(1, ignoringImplicit(cxxConstructExpr(
            argumentCountIs(1),
            hasArgument(0, ignoringImplicit(
                expr().bind("rhs_expr"))))))
    ).bind("call");
}

template <typename Cb, typename OperatorName>
void register_qint_const(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit, OperatorName op_name) {
    auto& pool = const_callback_pool<Cb>();
    pool.push_back(std::make_unique<Cb>(&unit));
    finder.addMatcher(make_qint_const_pattern(op_name), pool.back().get());
}

} // namespace

// PB-1. See matcher.hpp for the rationale behind the LHS canonical-type
// peel and the CXXConstructExpr peel on the RHS; both are identical across
// PB-1..PB-4 so the detailed comment lives there (and on the helpers above).
// Each registrar below is a one-liner that differs only in operator name
// and QOpKind.
void register_add_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<AddAssignConstCallback>(finder, unit, "+=");
}

void register_sub_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<SubAssignConstCallback>(finder, unit, "-=");
}

void register_mul_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<MulAssignConstCallback>(finder, unit, "*=");
}

void register_div_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<DivAssignConstCallback>(finder, unit, "/=");
}

} // namespace sturm::transpile
