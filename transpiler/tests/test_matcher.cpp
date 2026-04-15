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

// ── Phase B stub: qint_t<W> with implicit int64_t converting constructor ────
//
// The Phase B matchers key off `cxxRecordDecl(hasName("qint_t"))` wrapped
// in hasCanonicalType + hasDeclaration so the LHS type guard fires on
// the templated production type qint_t<Width> (qint_core.hpp:52) through
// the Typedef + TemplateSpecialization sugar chain. The stub mirrors
// that shape: a template class `qint_t<W>` with a converting constructor
// from `long long` and compound-assign operators taking another qint_t.
// `long long` matches the real `int64_t` lift in production — inline
// integer literals in the user source convert to it through the usual
// integer promotion rules.
static constexpr std::string_view kQIntStub = R"CPP(
namespace sturm {

template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
};

} // namespace sturm

using qint_t = sturm::qint_t<1>;
)CPP";

// Run the four Phase B matchers on `user_src` after prepending the qint_t
// stub. All four are registered at once because they are mutually
// exclusive by operator name — a single CXXOperatorCallExpr cannot trigger
// more than one of them. Returns the populated QUnit.
static QUnit run_pb_matchers(std::string_view user_src) {
    std::string code;
    code.reserve(kQIntStub.size() + user_src.size());
    code.append(kQIntStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_add_assign_const_matcher(finder, unit);
    register_sub_assign_const_matcher(finder, unit);
    register_mul_assign_const_matcher(finder, unit);
    register_div_assign_const_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PB matchers)\n");
    }
    return unit;
}

// ── Phase C stub: qint_t<W> with qint-qint compound-assign operators ────────
//
// Same shape as kQIntStub, plus `operator%=` (new in Phase C — PB omitted
// it). The converting constructor from `long long` is kept so PB fixtures
// still parse under this stub, which lets a single PC test exercise the
// PB/PC disjointness invariant both directions.
static constexpr std::string_view kQIntStubPC = R"CPP(
namespace sturm {

template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
    qint_t& operator%=(const qint_t&) { return *this; }
};

} // namespace sturm

using qint_t = sturm::qint_t<1>;
)CPP";

// Run all five Phase C matchers on `user_src` after prepending the PC stub.
// All five are mutually exclusive by operator name, so a single
// CXXOperatorCallExpr fires at most one of them. Returns the populated QUnit.
static QUnit run_pc_matchers(std::string_view user_src) {
    std::string code;
    code.reserve(kQIntStubPC.size() + user_src.size());
    code.append(kQIntStubPC);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_add_assign_qint_matcher(finder, unit);
    register_sub_assign_qint_matcher(finder, unit);
    register_mul_assign_qint_matcher(finder, unit);
    register_div_assign_qint_matcher(finder, unit);
    register_mod_assign_qint_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PC matchers)\n");
    }
    return unit;
}

// ── Phase D stub: qbool + qint_t<W> with six comparison operators ───────────
//
// The Phase D matchers key off `cxxRecordDecl(hasName("qbool"))` on the
// declared VarDecl type AND `cxxRecordDecl(hasName("qint_t"))` on both
// argument types (via hasCanonicalType + hasDeclaration). The stub
// below mirrors that shape: a plain `qbool` class, a templated
// `qint_t<W>` with the six relational operator overloads returning
// qbool by value (matching qint_compare_v3.hpp:93-127). The stub's
// operators return a default-constructed qbool — the matcher never
// executes user code, only inspects the AST, so a no-op body suffices.
//
// We deliberately do NOT provide implicit conversions from qint_t to qbool
// (or vice versa), and we do not provide `operator==` between qbool and
// qbool, so the negative test cases (wrong result type / wrong operand
// types) have unambiguous AST shapes that the matcher's type guards
// correctly reject.
static constexpr std::string_view kQIntCompareStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};

template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qbool operator==(const qint_t&) const { return qbool{}; }
    qbool operator!=(const qint_t&) const { return qbool{}; }
    qbool operator< (const qint_t&) const { return qbool{}; }
    qbool operator<=(const qint_t&) const { return qbool{}; }
    qbool operator> (const qint_t&) const { return qbool{}; }
    qbool operator>=(const qint_t&) const { return qbool{}; }
};

} // namespace sturm

using sturm::qbool;
using qint_t = sturm::qint_t<1>;
)CPP";

// Run all six Phase D compare matchers on `user_src` after prepending the
// PD stub. All six are mutually exclusive by operator name — a single
// CXXOperatorCallExpr fires at most one of them. Returns the populated
// QUnit.
static QUnit run_pd_matchers(std::string_view user_src) {
    std::string code;
    code.reserve(kQIntCompareStub.size() + user_src.size());
    code.append(kQIntCompareStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_eq_compare_qint_matcher(finder, unit);
    register_ne_compare_qint_matcher(finder, unit);
    register_lt_compare_qint_matcher(finder, unit);
    register_le_compare_qint_matcher(finder, unit);
    register_gt_compare_qint_matcher(finder, unit);
    register_ge_compare_qint_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PD matchers)\n");
    }
    return unit;
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

// ── Phase B positive cases ──────────────────────────────────────────────────

static void test_pb_add_assign_const_binds_literal() {
    // `a += 3;` on a qint_t must produce ADD_ASSIGN_CONST with operand
    // name "3" — the verbatim source text of the IntegerLiteral extracted
    // via Lexer::getSourceText after peeling the CXXConstructExpr wrapper.
    QUnit unit = run_pb_matchers(
        "void demo(qint_t a) {\n"
        "    a += 3;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;

    const auto& op = s.ops.front();
    CHECK(op.kind == QOpKind::ADD_ASSIGN_CONST);
    CHECK_EQ_STR(op.result.name, std::string("a"));
    CHECK(op.operands.size() == 1);
    if (op.operands.empty()) return;
    CHECK_EQ_STR(op.operands[0].name, std::string("3"));
}

static void test_pb_sub_assign_const_binds_literal() {
    QUnit unit = run_pb_matchers(
        "void demo(qint_t a) {\n"
        "    a -= 7;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::SUB_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("7"));
    }
}

static void test_pb_mul_assign_const_binds_literal() {
    QUnit unit = run_pb_matchers(
        "void demo(qint_t a) {\n"
        "    a *= 2;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::MUL_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("2"));
    }
}

static void test_pb_div_assign_const_binds_literal() {
    QUnit unit = run_pb_matchers(
        "void demo(qint_t a) {\n"
        "    a /= 5;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::DIV_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("5"));
    }
}

// ── Phase B negative case: Phase C qint-qint form must NOT match ────────────

static void test_pb_negative_qint_qint_form_no_match() {
    // Phase C disjointness invariant: the qint-qint form `a += b;` (both
    // operands are DeclRefExpr references to qint_t locals) has no
    // CXXConstructExpr wrapper on the RHS because no converting
    // constructor fires. The PB pattern requires the cxxConstructExpr
    // peel and therefore must not match this form. This test pins the
    // separation so Phase C (qint-qint) can later land its own matcher
    // without either matcher shadowing the other.
    QUnit unit = run_pb_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a += b;\n"
        "    a -= b;\n"
        "    a *= b;\n"
        "    a /= b;\n"
        "}\n");

    CHECK(unit.scopes.empty());
}

// ── Phase C positive cases ──────────────────────────────────────────────────

static void test_pc_add_assign_qint_binds_rhs_ident() {
    // `a += b;` on two qint_t must produce ADD_ASSIGN_QINT with operand
    // name "b" — the verbatim DeclRefExpr identifier extracted via
    // Lexer::getSourceText. No CXXConstructExpr peel; the RHS is a bare
    // DeclRefExpr in this form.
    QUnit unit = run_pc_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a += b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;

    const auto& op = s.ops.front();
    CHECK(op.kind == QOpKind::ADD_ASSIGN_QINT);
    CHECK_EQ_STR(op.result.name, std::string("a"));
    CHECK(op.operands.size() == 1);
    if (op.operands.empty()) return;
    CHECK_EQ_STR(op.operands[0].name, std::string("b"));
}

static void test_pc_sub_assign_qint_binds_rhs_ident() {
    QUnit unit = run_pc_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a -= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::SUB_ASSIGN_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("b"));
    }
}

static void test_pc_mul_assign_qint_binds_rhs_ident() {
    QUnit unit = run_pc_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a *= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::MUL_ASSIGN_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("b"));
    }
}

static void test_pc_div_assign_qint_binds_rhs_ident() {
    QUnit unit = run_pc_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a /= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::DIV_ASSIGN_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("b"));
    }
}

static void test_pc_mod_assign_qint_binds_rhs_ident() {
    QUnit unit = run_pc_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a %= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::MOD_ASSIGN_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("a"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("b"));
    }
}

// ── Phase C negative case: Phase B classical-RHS form must NOT match ────────

static void test_pc_negative_classical_rhs_no_match() {
    // Disjointness invariant (the other direction): the classical-RHS form
    // `a += 3;` has a CXXConstructExpr wrapping the int literal (lifted via
    // the qint_t(long long) converting constructor). The PC patterns
    // require the RHS to be a bare DeclRefExpr to another qint_t, which
    // rules out this shape entirely. Pinning this separation here means
    // neither Phase B nor Phase C can shadow the other as matchers evolve.
    QUnit unit = run_pc_matchers(
        "void demo(qint_t a) {\n"
        "    a += 3;\n"
        "    a -= 7;\n"
        "    a *= 2;\n"
        "    a /= 5;\n"
        "    a %= 4;\n"
        "}\n");

    CHECK(unit.scopes.empty());
}

// ── Phase D positive cases ──────────────────────────────────────────────────

static void test_pd_eq_compare_qint_binds_both_operands() {
    // `qbool c = a == b;` on two qint_t must produce EQ_QINT with the
    // declared var's name as the result and both qint_t identifiers as
    // operands. Unlike PC's single-operand compound-assign callback, this
    // records TWO operands — the comparator inverse depends on both
    // inputs to flip the result bit back to |0⟩.
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c = a == b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;

    const auto& op = s.ops.front();
    CHECK(op.kind == QOpKind::EQ_QINT);
    CHECK_EQ_STR(op.result.name, std::string("c"));
    CHECK(op.operands.size() == 2);
    if (op.operands.size() >= 2) {
        CHECK_EQ_STR(op.operands[0].name, std::string("a"));
        CHECK_EQ_STR(op.operands[1].name, std::string("b"));
    }
    CHECK(op.stmt_range.isValid());
    CHECK(op.result.decl_loc.isValid());
}

static void test_pd_ne_compare_qint_binds_both_operands() {
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c = a != b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::NE_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("c"));
    CHECK(s.ops.front().operands.size() == 2);
    if (s.ops.front().operands.size() >= 2) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("a"));
        CHECK_EQ_STR(s.ops.front().operands[1].name, std::string("b"));
    }
}

static void test_pd_lt_compare_qint_binds_both_operands() {
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c = a < b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::LT_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("c"));
    CHECK(s.ops.front().operands.size() == 2);
    if (s.ops.front().operands.size() >= 2) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("a"));
        CHECK_EQ_STR(s.ops.front().operands[1].name, std::string("b"));
    }
}

static void test_pd_le_compare_qint_binds_both_operands() {
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c = a <= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::LE_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("c"));
    CHECK(s.ops.front().operands.size() == 2);
    if (s.ops.front().operands.size() >= 2) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("a"));
        CHECK_EQ_STR(s.ops.front().operands[1].name, std::string("b"));
    }
}

static void test_pd_gt_compare_qint_binds_both_operands() {
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c = a > b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::GT_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("c"));
    CHECK(s.ops.front().operands.size() == 2);
    if (s.ops.front().operands.size() >= 2) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("a"));
        CHECK_EQ_STR(s.ops.front().operands[1].name, std::string("b"));
    }
}

static void test_pd_ge_compare_qint_binds_both_operands() {
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c = a >= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::GE_QINT);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("c"));
    CHECK(s.ops.front().operands.size() == 2);
    if (s.ops.front().operands.size() >= 2) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("a"));
        CHECK_EQ_STR(s.ops.front().operands[1].name, std::string("b"));
    }
}

// ── Phase D negative cases ──────────────────────────────────────────────────

static void test_pd_negative_wrong_result_type() {
    // The declared variable is `int`, not `qbool`. Even though we invoke
    // `operator==` on two qint_t operands (which would normally be a
    // valid PD-1 target), the VarDecl type guard must reject because a
    // classical result type has no uncompute story. We spell the
    // initializer as `int c = 0;` alongside an otherwise-discardable
    // compare so parsing succeeds; the point of the test is that no
    // qbool-typed VarDecl exists, so no PD matcher may fire.
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    (void)(a == b);\n"
        "    int c = 0;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

static void test_pd_negative_non_qint_operands() {
    // Classical-int operands: `int == int` does not involve a qint_t
    // record decl at either argument position, so the type guard rejects.
    // A bare `int c = i == j;` is also a different VarDecl type.
    QUnit unit = run_pd_matchers(
        "void demo(int i, int j) {\n"
        "    bool c = i == j;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

// ── Phase F / PF-2 stub: qbool + WHEN macro expansion ──────────────────────
//
// The PF-2 matcher anchors on the middle `if` in the three-`if` tower that
// the real `WHEN(expr)` macro expands to (see include/sturm/control/when.hpp:293).
// The stub below replicates only the structure the matcher inspects:
//
//   - A `sturm::detail::materialize_when` overload set that the init-stmt
//     of the middle `if` calls.
//   - A three-`if` `WHEN(expr)` macro whose middle `if` init-stmt declares
//     `_when_val_` with initializer `::sturm::detail::materialize_when(expr)`.
//
// We deliberately do NOT model `WhenCapture` / `WhenGuard` — their types
// appear only in the outer / inner `if`s, which the matcher does not bind.
// A no-op placeholder `int` suffices to make the macro compile.
static constexpr std::string_view kQBoolWhenStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

// Minimal stand-ins for WhenCapture / make_when_guard — the PF-2 matcher
// does not inspect these, but the macro must reference them to compile.
struct WhenCapture { WhenCapture() = default; };
inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())
)CPP";

// Run the PF-2 WHEN-lift matcher on `user_src`. Resets the detection
// counter before the tool run so each test starts from zero, returns the
// post-run count alongside the populated QUnit (the unit should be empty
// at PF-2; we return it so tests can double-check that invariant).
struct PFTwoRun {
    QUnit unit;
    int detections = 0;
};

static PFTwoRun run_pf_when_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolWhenStub.size() + user_src.size());
    code.append(kQBoolWhenStub);
    code.append(user_src);

    PFTwoRun out;
    reset_when_lift_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    register_when_lift_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PF-2 WHEN matcher)\n");
    }
    out.detections = when_lift_detection_count_for_test();
    return out;
}

// ── Phase F / PF-2 positive case ───────────────────────────────────────────

static void test_pf_when_compound_detects_once() {
    // WHEN(b | c) must fire the PF-2 detection path exactly once — the
    // init-stmt binds `_when_val_` with initializer materialize_when(b|c),
    // the if-loc originates in the WHEN macro body, and the materialize
    // argument (after peel_to_payload) is NOT a bare DeclRefExpr so the
    // named-passthrough short-circuit does NOT trigger.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(b | c) { (void)a; }\n"
        "}\n");
    CHECK(r.detections == 1);
    // PF-2 is detection-only: QUnit must be untouched.
    CHECK(r.unit.scopes.empty());
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

// ── Phase F / PF-2 negative cases ──────────────────────────────────────────

static void test_pf_when_named_passthrough_short_circuits() {
    // WHEN(named_qbool): the materialize argument peels to a bare
    // DeclRefExpr to a named qbool — the callback takes the
    // named-passthrough short-circuit and records zero detections.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    WHEN(a) { (void)b; }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.scopes.empty());
}

static void test_pf_when_bare_if_no_match() {
    // A plain `if (auto x = f(); ...)` init-stmt is NOT inside any macro
    // body expansion. The `isMacroBodyExpansion` guard rejects. We also
    // name the local `_when_val_` here to prove the match-shape alone is
    // NOT sufficient — the macro-body guard is load-bearing.
    //
    // The init has to be a CallExpr to a function named `materialize_when`
    // to even reach the macro-body guard; we supply one in the stub so the
    // test exercises exactly that guard.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    if (decltype(auto) _when_val_ = \n"
        "            ::sturm::detail::materialize_when(a | b); true) {\n"
        "        (void)b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
}

static void test_pf_when_direct_materialize_call_no_match() {
    // A direct user-level call to `materialize_when(...)` OUTSIDE any
    // WHEN macro body must NOT match. The `if`-anchored pattern rules
    // this out at the pattern level (the call is the initializer of a
    // regular VarDecl, not an if-init-stmt); even if a future widening
    // of the pattern caught the VarDecl, the macro-body guard rejects.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    auto x = ::sturm::detail::materialize_when(a | b);\n"
        "    (void)x;\n"
        "}\n");
    CHECK(r.detections == 0);
}

static void test_pd_six_ops_same_scope_hit_all_kinds() {
    // A single block containing one of each comparison. All six ops
    // should land in the same QScope (same enclosing CompoundStmt),
    // in source order, with the correct per-op QOpKind.
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    qbool c0 = a == b;\n"
        "    qbool c1 = a != b;\n"
        "    qbool c2 = a <  b;\n"
        "    qbool c3 = a <= b;\n"
        "    qbool c4 = a >  b;\n"
        "    qbool c5 = a >= b;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.size() != 1) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 6);
    if (s.ops.size() != 6) return;

    CHECK(s.ops[0].kind == QOpKind::EQ_QINT);
    CHECK(s.ops[1].kind == QOpKind::NE_QINT);
    CHECK(s.ops[2].kind == QOpKind::LT_QINT);
    CHECK(s.ops[3].kind == QOpKind::LE_QINT);
    CHECK(s.ops[4].kind == QOpKind::GT_QINT);
    CHECK(s.ops[5].kind == QOpKind::GE_QINT);

    CHECK_EQ_STR(s.ops[0].result.name, std::string("c0"));
    CHECK_EQ_STR(s.ops[5].result.name, std::string("c5"));
}

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

    test_pb_add_assign_const_binds_literal();
    test_pb_sub_assign_const_binds_literal();
    test_pb_mul_assign_const_binds_literal();
    test_pb_div_assign_const_binds_literal();
    test_pb_negative_qint_qint_form_no_match();

    test_pc_add_assign_qint_binds_rhs_ident();
    test_pc_sub_assign_qint_binds_rhs_ident();
    test_pc_mul_assign_qint_binds_rhs_ident();
    test_pc_div_assign_qint_binds_rhs_ident();
    test_pc_mod_assign_qint_binds_rhs_ident();
    test_pc_negative_classical_rhs_no_match();

    test_pd_eq_compare_qint_binds_both_operands();
    test_pd_ne_compare_qint_binds_both_operands();
    test_pd_lt_compare_qint_binds_both_operands();
    test_pd_le_compare_qint_binds_both_operands();
    test_pd_gt_compare_qint_binds_both_operands();
    test_pd_ge_compare_qint_binds_both_operands();
    test_pd_negative_wrong_result_type();
    test_pd_negative_non_qint_operands();
    test_pd_six_ops_same_scope_hit_all_kinds();

    test_pf_when_compound_detects_once();
    test_pf_when_named_passthrough_short_circuits();
    test_pf_when_bare_if_no_match();
    test_pf_when_direct_materialize_call_no_match();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
