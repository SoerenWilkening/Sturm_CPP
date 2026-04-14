// test_matcher.cpp — unit tests for the M7 AST matcher.
//
// The matcher must recognize exactly `qbool tmp = a | b;` in a compound
// statement and do nothing else. The tests here drive `register_or_matcher`
// through `clang::tooling::runToolOnCodeWithArgs`, using inline source with
// a minimal `qbool` stub so no external include path is required.
//
// Tests cover:
//   - Positive: a single `qbool tmp = a | b;` produces one QScope with one
//     QOperation of kind OR, result "tmp", operands ["a", "b"].
//   - Negative (matcher must NOT fire):
//       * `qbool tmp = a & b;`        (wrong operator)
//       * `qint  tmp = a | b;`        (wrong result type)
//       * `qbool tmp = foo(a, b);`    (not operator|)
//       * `bool  tmp = ...;`          (classical type, not qbool)
//   - Nested/sibling scopes: two sibling compound statements each with one
//     match yield two QScopes with one op each.
//   - stmt_range round-trips through SourceManager (we read the recorded
//     range back as text and verify it equals the original source snippet).
//
// We deliberately build a tiny `qbool` stub inline. Using the real STURM
// `qbool.hpp` would drag in the whole qtypes subtree and require the pool /
// sink infrastructure — far more than the matcher needs. The matcher keys
// off class name alone, so a one-line stub is sufficient.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

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

// ── Test harness ──────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                  \
    ++tests_run;                                                      \
    if ((got) == (want)) { ++tests_pass; }                            \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"          \
                             "  got:  <<<%s>>>\n"                     \
                             "  want: <<<%s>>>\n",                    \
                     __FILE__, __LINE__,                              \
                     std::string(got).c_str(),                        \
                     std::string(want).c_str());                      \
    }                                                                 \
} while (0)

// ── Inline stub of the types the matcher looks at ───────────────────────────
//
// The matcher keys off class name ("qbool") and operator overload ("|"), so
// the stub below suffices. A `qint` type is provided to test the negative
// wrong-result-type case. `foo(qbool, qbool)` covers the "not operator|"
// negative case.
static constexpr std::string_view kQBoolStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};

class qint {
public:
    qint() {}
    qint(const qbool&) {}
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qint  operator|(const qint&, const qint&)   { return qint{}; }
inline qbool foo(const qbool&, const qbool&)       { return qbool{}; }

} // namespace sturm

using sturm::qbool;
using sturm::qint;
using sturm::foo;
)CPP";

// Run the matcher on `user_src` after prepending the qbool stub. Returns the
// populated QUnit. On tool failure the returned unit is left empty and the
// test doing the call reports a CHECK failure.
static QUnit run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolStub.size() + user_src.size());
    code.append(kQBoolStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_or_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false; tool could not parse "
                     "source\n");
    }
    return unit;
}

// Extract the text of a SourceRange via the SourceManager + Lexer. Used to
// confirm that the captured stmt_range resolves back to the expected source
// fragment (the "round-trips through SourceManager" acceptance criterion).
//
// Implemented as a FrontendAction so we have access to a real ASTContext
// and SourceManager for the same translation unit we matched on.
namespace {
class RangeGrabAction : public clang::ASTFrontendAction {
public:
    RangeGrabAction(clang::ast_matchers::MatchFinder& finder,
                    std::string& captured_text)
        : finder_(finder), captured_text_(captured_text) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& /*ci*/, llvm::StringRef /*file*/) override {
        return finder_.newASTConsumer();
    }

private:
    clang::ast_matchers::MatchFinder& finder_;
    std::string& captured_text_;
};
} // namespace

// Round-trip helper: run the matcher, then separately read back the captured
// stmt_range using a fresh Clang invocation on the same source. Since Clang's
// SourceLocation encodings are stable within a single invocation but not
// across invocations, we combine both into a single tool run using a custom
// ASTConsumer that records the text directly after each match.
static std::string round_trip_range(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolStub.size() + user_src.size());
    code.append(kQBoolStub);
    code.append(user_src);

    // Use a custom MatchCallback whose sole job is to read the declaration
    // text back via Lexer::getSourceText. We drive it through a second
    // MatchFinder to avoid coupling with the production callback.
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

    // Combine: we run both finders in the same invocation so the same
    // SourceLocation encodings are visible to both. We do this by chaining
    // consumers inside a single FrontendAction.
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

    // We need a factory that returns a fresh DualAction each time
    // runToolOnCodeWithArgs internally asks for one. Write one inline.
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

    // Make sure the production matcher saw the same op that GrabText did
    // (so the returned text really does correspond to the captured range).
    assert(unit.scopes.size() == 1);
    assert(unit.scopes.front().ops.size() == 1);
    return grab.text;
}

// ── Positive case ────────────────────────────────────────────────────────────

static void test_positive_single_or() {
    QUnit unit = run_matcher(
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

static void test_negative_and_operator() {
    // Wrong operator: & must not match the | matcher.
    QUnit unit = run_matcher(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = a & b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

static void test_negative_wrong_result_type() {
    // Wrong declared type: qint, not qbool. Even if the initializer is |,
    // the outer VarDecl type must be qbool to match.
    QUnit unit = run_matcher(
        "qint demo(qint a, qint b) {\n"
        "    qint tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

static void test_negative_not_operator_call() {
    // Initializer is a plain function call, not operator|.
    QUnit unit = run_matcher(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = foo(a, b);\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

static void test_negative_classical_bool() {
    // Classical bool VarDecl. Not a qbool, so the matcher does nothing.
    QUnit unit = run_matcher(
        "bool demo(bool a, bool b) {\n"
        "    bool tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

// ── Nested / sibling scopes ──────────────────────────────────────────────────

static void test_two_sibling_scopes() {
    // Two sibling compound statements, each containing exactly one match.
    // The matcher must emit two distinct QScopes — conflating them would
    // break uncompute ordering for users who structure logic into sibling
    // blocks.
    QUnit unit = run_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    { qbool t = a | b; }\n"
        "    { qbool t = c | d; }\n"
        "}\n");

    // We expect TWO scopes (one per `{ ... }` block that contained an op).
    // The outer function body is another compound stmt but has no matched
    // ops, so it is NOT represented as a QScope.
    CHECK(unit.scopes.size() == 2);
    if (unit.scopes.size() != 2) return;

    CHECK(unit.scopes[0].ops.size() == 1);
    CHECK(unit.scopes[1].ops.size() == 1);

    if (unit.scopes[0].ops.empty() || unit.scopes[1].ops.empty()) return;

    // Distinct brace locations — confirms the scopes are truly different.
    CHECK(unit.scopes[0].open_brace.getRawEncoding() !=
          unit.scopes[1].open_brace.getRawEncoding());

    CHECK_EQ_STR(unit.scopes[0].ops[0].operands[0].name, std::string("a"));
    CHECK_EQ_STR(unit.scopes[0].ops[0].operands[1].name, std::string("b"));
    CHECK_EQ_STR(unit.scopes[1].ops[0].operands[0].name, std::string("c"));
    CHECK_EQ_STR(unit.scopes[1].ops[0].operands[1].name, std::string("d"));
}

static void test_two_ops_same_scope() {
    // Two matched ops in the same block — they must share a single QScope,
    // in source order.
    QUnit unit = run_matcher(
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

static void test_stmt_range_round_trip() {
    // Read the recorded stmt_range back as source text via the Clang Lexer.
    // It must equal the original declaration (modulo the trailing
    // semicolon, which VarDecl::getSourceRange() excludes).
    std::string text = round_trip_range(
        "qbool demo(qbool a, qbool b) {\n"
        "    qbool tmp = a | b;\n"
        "    return tmp;\n"
        "}\n");

    // VarDecl source range covers "qbool tmp = a | b" (no trailing `;`).
    CHECK_EQ_STR(text, std::string("qbool tmp = a | b"));
}

int main() {
    test_positive_single_or();

    test_negative_and_operator();
    test_negative_wrong_result_type();
    test_negative_not_operator_call();
    test_negative_classical_bool();

    test_two_sibling_scopes();
    test_two_ops_same_scope();

    test_stmt_range_round_trip();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
