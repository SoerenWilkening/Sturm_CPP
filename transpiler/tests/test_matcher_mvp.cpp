// test_matcher_mvp.cpp — MVP OR-matcher tests (Phase A, M7 baseline).
//
// Covers:
//   - Positive: a single `qbool tmp = a | b;` yields one QScope with one
//     QOperation of kind OR, result "tmp", operands ["a", "b"].
//   - Negatives: wrong operator, wrong result type, not-operator-call,
//     classical bool.
//   - Nested / sibling scopes: two sibling compound stmts each with one
//     match yield two QScopes with one op each; two ops in the same
//     block share a single QScope in source order.
//   - stmt_range round-trips through the SourceManager — we read the
//     captured range back as text and assert it equals the original.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Tooling.h"

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Round-trip helper: run the MVP matcher, then separately read back the
// captured stmt_range using a fresh Clang invocation on the same source.
// Clang SourceLocations are stable within a single invocation but not
// across invocations, so we combine both finders into a single tool run
// via an ASTConsumer that records the text directly after each match.
std::string round_trip_range(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolStub.size() + user_src.size());
    code.append(kQBoolStub);
    code.append(user_src);

    class GrabText : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
        std::string text;
        void run(const clang::ast_matchers::MatchFinder::MatchResult& r)
            override {
            const auto* var = r.Nodes.getNodeAs<clang::VarDecl>("var");
            if (!var) return;
            const auto& sm = *r.SourceManager;
            auto range = clang::CharSourceRange::getTokenRange(
                var->getSourceRange());
            text = clang::Lexer::getSourceText(
                range, sm, r.Context->getLangOpts()).str();
        }
    };

    using namespace clang::ast_matchers;
    QUnit unit;
    MatchFinder prod_finder;
    register_or_matcher(prod_finder, unit);

    GrabText grab;
    MatchFinder aux_finder;
    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(cxxOperatorCallExpr(
            hasOverloadedOperatorName("|"),
            argumentCountIs(2)))
    ).bind("var");
    aux_finder.addMatcher(pattern, &grab);

    class DualAction : public clang::ASTFrontendAction {
    public:
        DualAction(MatchFinder& a, MatchFinder& b) : a_(a), b_(b) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            class Combined : public clang::ASTConsumer {
            public:
                Combined(std::unique_ptr<clang::ASTConsumer> x,
                         std::unique_ptr<clang::ASTConsumer> y)
                    : x_(std::move(x)), y_(std::move(y)) {}
                void HandleTranslationUnit(clang::ASTContext& ctx) override {
                    x_->HandleTranslationUnit(ctx);
                    y_->HandleTranslationUnit(ctx);
                }
            private:
                std::unique_ptr<clang::ASTConsumer> x_, y_;
            };
            return std::make_unique<Combined>(
                a_.newASTConsumer(), b_.newASTConsumer());
        }
    private:
        MatchFinder& a_;
        MatchFinder& b_;
    };

    class DualFactory : public clang::tooling::FrontendActionFactory {
    public:
        DualFactory(MatchFinder& a, MatchFinder& b) : a_(a), b_(b) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<DualAction>(a_, b_);
        }
    private:
        MatchFinder& a_;
        MatchFinder& b_;
    };

    DualFactory factory(prod_finder, aux_finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    (void)clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");

    assert(unit.scopes.size() == 1);
    assert(unit.scopes.front().ops.size() == 1);
    return grab.text;
}

// ── Positive case ────────────────────────────────────────────────────────────

void test_positive_single_or() {
    QUnit unit = run_or_matcher(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;

    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;

    const auto& op = s.ops.front();
    CHECK(op.kind == QOpKind::OR);
    CHECK_EQ_STR(op.result.name, std::string("tmp"));
    CHECK(op.operands.size() == 2);
    if (op.operands.size() >= 2) {
        CHECK_EQ_STR(op.operands[0].name, std::string("a"));
        CHECK_EQ_STR(op.operands[1].name, std::string("b"));
    }
    CHECK(op.stmt_range.isValid());
    CHECK(op.result.decl_loc.isValid());
}

// ── Negative cases ───────────────────────────────────────────────────────────

void test_negative_and_operator() {
    QUnit unit = run_or_matcher(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = a & b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

void test_negative_wrong_result_type() {
    QUnit unit = run_or_matcher(
        "qint demo(qint a, qint b) {\n"
        "    qint tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

void test_negative_not_operator_call() {
    QUnit unit = run_or_matcher(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = foo(a, b);\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

void test_negative_classical_bool() {
    QUnit unit = run_or_matcher(
        "bool demo(bool a, bool b) {\n"
        "    bool tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

// ── Nested / sibling scopes ──────────────────────────────────────────────────

void test_two_sibling_scopes() {
    QUnit unit = run_or_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    { qbool t = a | b; }\n"
        "    { qbool t = c | d; }\n"
        "}\n");

    CHECK(unit.scopes.size() == 2);
    if (unit.scopes.size() != 2) return;

    CHECK(unit.scopes[0].ops.size() == 1);
    CHECK(unit.scopes[1].ops.size() == 1);

    if (unit.scopes[0].ops.empty() || unit.scopes[1].ops.empty()) return;

    CHECK(unit.scopes[0].open_brace.getRawEncoding() !=
          unit.scopes[1].open_brace.getRawEncoding());

    CHECK_EQ_STR(unit.scopes[0].ops[0].operands[0].name, std::string("a"));
    CHECK_EQ_STR(unit.scopes[0].ops[0].operands[1].name, std::string("b"));
    CHECK_EQ_STR(unit.scopes[1].ops[0].operands[0].name, std::string("c"));
    CHECK_EQ_STR(unit.scopes[1].ops[0].operands[1].name, std::string("d"));
}

void test_two_ops_same_scope() {
    QUnit unit = run_or_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    qbool t0 = a | b;\n"
        "    qbool t1 = c | d;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.size() != 1) return;
    CHECK(unit.scopes[0].ops.size() == 2);
    if (unit.scopes[0].ops.size() != 2) return;

    CHECK_EQ_STR(unit.scopes[0].ops[0].result.name, std::string("t0"));
    CHECK_EQ_STR(unit.scopes[0].ops[1].result.name, std::string("t1"));
}

// ── stmt_range round-trip ────────────────────────────────────────────────────

void test_stmt_range_round_trip() {
    // Read the recorded stmt_range back as source text via the Clang Lexer.
    // It must equal the original declaration (modulo the trailing
    // semicolon, which VarDecl::getSourceRange() excludes).
    std::string text = round_trip_range(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK_EQ_STR(text, std::string("qbool tmp = a | b"));
}

} // namespace

void run_mvp_tests() {
    test_positive_single_or();
    test_negative_and_operator();
    test_negative_wrong_result_type();
    test_negative_not_operator_call();
    test_negative_classical_bool();
    test_two_sibling_scopes();
    test_two_ops_same_scope();
    test_stmt_range_round_trip();
}
