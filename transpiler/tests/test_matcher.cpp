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

// ── Phase F / PF-3 positive case ───────────────────────────────────────────

static void test_pf_when_compound_detects_once() {
    // WHEN(b | c) must fire the PF-3 rewrite path exactly once. The
    // detection counter bumps (same PF-2 signal), and the QUnit gains the
    // three scheduled edits the plan specifies:
    //   (1) one QReplacement covering the `b | c` spelling range with the
    //       fresh `__stu_t0` identifier,
    //   (2) one raw_insertions entry carrying the flat decl block
    //       `qbool __stu_t0 = b | c;\n`,
    //   (3) one QOperation{kind=OR, result=__stu_t0, operands=[b,c]} on
    //       the enclosing CompoundStmt's QScope, tagged with a valid
    //       `insert_before_override` (the post-WHEN-body close-brace loc).
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(b | c) { (void)a; }\n"
        "}\n");
    CHECK(r.detections == 1);

    // (1) Exactly one replacement, with the lifted top-temp name.
    CHECK(r.unit.replacements.size() == 1);
    if (r.unit.replacements.size() == 1) {
        CHECK_EQ_STR(r.unit.replacements[0].replacement,
                     std::string("__stu_t0"));
        CHECK(r.unit.replacements[0].range.isValid());
    }

    // (2) Exactly one raw insertion, whose code is the flat decl block.
    CHECK(r.unit.raw_insertions.size() == 1);
    if (r.unit.raw_insertions.size() == 1) {
        CHECK_EQ_STR(r.unit.raw_insertions[0].code,
                     std::string("qbool __stu_t0 = b | c;\n"));
        CHECK(r.unit.raw_insertions[0].insert_before.isValid());
    }

    // (3) Exactly one scope with exactly one op — QOpKind::OR with the
    // fresh temp as result, `b` / `c` as operands, and a valid
    // insert_before_override so the M8 pass anchors the uncompute at
    // the WHEN body's close brace rather than the enclosing scope's.
    CHECK(r.unit.scopes.size() == 1);
    if (r.unit.scopes.size() == 1) {
        const auto& s = r.unit.scopes.front();
        CHECK(s.ops.size() == 1);
        if (s.ops.size() == 1) {
            const auto& op = s.ops.front();
            CHECK(op.kind == QOpKind::OR);
            CHECK_EQ_STR(op.result.name, std::string("__stu_t0"));
            CHECK(op.operands.size() == 2);
            if (op.operands.size() == 2) {
                CHECK_EQ_STR(op.operands[0].name, std::string("b"));
                CHECK_EQ_STR(op.operands[1].name, std::string("c"));
            }
            CHECK(op.insert_before_override.isValid());
        }
    }
}

// ── Phase F / PF-3 negative cases ──────────────────────────────────────────

static void test_pf_when_named_passthrough_short_circuits() {
    // WHEN(named_qbool): the materialize argument peels to a bare
    // DeclRefExpr to a named qbool — the callback takes the
    // named-passthrough short-circuit and records zero detections. The
    // QUnit must be entirely untouched so the emitter's output is
    // byte-identical to the input (snapshot_when_named_passthrough
    // enforces the same invariant at the file level).
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    WHEN(a) { (void)b; }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.scopes.empty());
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

// ── Phase F / PF-3 NOT-case positive ───────────────────────────────────────

static void test_pf_when_not_lifts_once() {
    // WHEN(~a) must lift via flatten_arg's unary-NOT branch:
    //   - One QReplacement mapping the `~a` spelling range to the top
    //     temp name.
    //   - One raw_insertions entry with code `qbool __stu_t0 = ~a;\n`.
    //   - One QOperation{kind=NOT, result=__stu_t0, operands=[a]} on the
    //     enclosing scope, with a valid `insert_before_override`.
    PFTwoRun r = run_pf_when_matcher(
        "namespace sturm { inline qbool operator~(const qbool&)"
        "    { return qbool{}; } }\n"
        "void demo(qbool a, qbool b) {\n"
        "    WHEN(~a) { (void)b; }\n"
        "}\n");
    CHECK(r.detections == 1);

    CHECK(r.unit.replacements.size() == 1);
    if (r.unit.replacements.size() == 1) {
        CHECK_EQ_STR(r.unit.replacements[0].replacement,
                     std::string("__stu_t0"));
    }

    CHECK(r.unit.raw_insertions.size() == 1);
    if (r.unit.raw_insertions.size() == 1) {
        CHECK_EQ_STR(r.unit.raw_insertions[0].code,
                     std::string("qbool __stu_t0 = ~a;\n"));
    }

    CHECK(r.unit.scopes.size() == 1);
    if (r.unit.scopes.size() == 1) {
        const auto& s = r.unit.scopes.front();
        CHECK(s.ops.size() == 1);
        if (s.ops.size() == 1) {
            const auto& op = s.ops.front();
            CHECK(op.kind == QOpKind::NOT);
            CHECK_EQ_STR(op.result.name, std::string("__stu_t0"));
            CHECK(op.operands.size() == 1);
            if (op.operands.size() == 1) {
                CHECK_EQ_STR(op.operands[0].name, std::string("a"));
            }
            CHECK(op.insert_before_override.isValid());
        }
    }
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

// ── Phase G / PG-1 + PG-2 + PG-3 nested-WHEN matcher ───────────────────────
//
// The Phase G matcher detects adjacent pairs of `WHEN(outer) { WHEN(inner)
// { ... } }` where BOTH `outer` and `inner` peel to bare `DeclRefExpr`s
// naming non-synthetic qbools. Tests drive it through the same stub as the
// Phase F tests above (kQBoolWhenStub) so the macro-body / materialize_when
// shape is identical to what the user writes in real code.
//
// PG-1 was detection-only; PG-2 added two source edits per matched pair —
// a `qbool __stu_ctrl<M> = <outer> & <inner>;\n` decl injection (staged on
// `unit.raw_insertions`) and a replacement of the inner `materialize_when`
// argument with the ctrl name (staged on `unit.replacements`). PG-3
// completes the lowering by pushing a synthetic `QOperation{kind=AND,
// insert_before_override=<post_inner_brace>}` onto the enclosing scope —
// the existing uncompute pass renders it as `uncompute_and(<ctrl>,
// <outer>, <inner>);` at that override anchor, immediately past the
// inner WHEN body's closing brace. Each matched pair therefore bumps
// `unit.scopes[.].ops` by one AND op in addition to its replacement +
// raw insertion.
struct PGOneRun {
    QUnit unit;
    int detections = 0;
};

static PGOneRun run_pg_when_nested_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolWhenStub.size() + user_src.size());
    code.append(kQBoolWhenStub);
    code.append(user_src);

    PGOneRun out;
    reset_when_nested_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    register_when_nested_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PG-1 nested WHEN "
                     "matcher)\n");
    }
    out.detections = when_nested_detection_count_for_test();
    return out;
}

static void test_pg_when_nested_named_depth2_detects_once() {
    // WHEN(a) { WHEN(b) { ... } } — the named+named base case. The inner
    // WHEN finds outer `a` as its nearest enclosing WHEN; both args peel
    // to bare DREs; detection counter reaches 1. PG-2 stages exactly ONE
    // replacement (the inner arg rewrite) and ONE raw insertion (the
    // decl block). PG-3 additionally pushes ONE `QOperation{kind=AND}`
    // into the enclosing CompoundStmt's QScope with a valid
    // `insert_before_override` anchored past the inner WHEN body's `}`.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(a) { WHEN(b) { (void)c; } }\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.scopes.size() == 1);
    CHECK(r.unit.replacements.size() == 1);
    CHECK(r.unit.raw_insertions.size() == 1);
    if (r.unit.replacements.size() == 1) {
        CHECK_EQ_STR(r.unit.replacements[0].replacement,
                     std::string("__stu_ctrl0"));
    }
    if (r.unit.raw_insertions.size() == 1) {
        CHECK_EQ_STR(r.unit.raw_insertions[0].code,
                     std::string("qbool __stu_ctrl0 = a & b;\n"));
    }
    if (r.unit.scopes.size() == 1) {
        const auto& ops = r.unit.scopes[0].ops;
        CHECK(ops.size() == 1);
        if (ops.size() == 1) {
            CHECK(ops[0].kind == QOpKind::AND);
            CHECK_EQ_STR(ops[0].result.name, std::string("__stu_ctrl0"));
            CHECK(ops[0].operands.size() == 2);
            if (ops[0].operands.size() == 2) {
                CHECK_EQ_STR(ops[0].operands[0].name, std::string("a"));
                CHECK_EQ_STR(ops[0].operands[1].name, std::string("b"));
            }
            CHECK(ops[0].insert_before_override.isValid());
        }
    }
}

static void test_pg_when_nested_named_depth3_detects_twice() {
    // WHEN(a) { WHEN(b) { WHEN(c) { ... } } } — the pairwise cascade:
    //   - inner `c` finds nearest outer `b` → pair 1
    //   - middle `b` finds nearest outer `a` → pair 2
    // Crucially, `c` does NOT transitively pair with `a` because the
    // ParentMap walk stops at the nearest enclosing WHEN ancestor.
    //
    // PG-2 stages ONE replacement + ONE raw insertion per pair, giving
    // two of each. PG-3 adds ONE `QOperation{kind=AND}` per pair too; the
    // two ops land in DIFFERENT QScopes because each pair's enclosing
    // CompoundStmt is the body of a distinct WHEN — (a,b) lives in the
    // outer function body's scope (via the WHEN(b) if-stmt's enclosing
    // compound), (b,c) lives in the WHEN(a) body's scope. The ctrl
    // names are allocated from a per-callback persistent
    // `FreshNameAllocator`, so cascaded levels pick up `__stu_ctrl0`
    // and `__stu_ctrl1` without colliding.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN(a) { WHEN(b) { WHEN(c) { (void)d; } } }\n"
        "}\n");
    CHECK(r.detections == 2);
    CHECK(r.unit.replacements.size() == 2);
    CHECK(r.unit.raw_insertions.size() == 2);
    // Two AND ops across the scopes total. MatchFinder callback order is
    // NOT guaranteed, so we assert the cumulative op count rather than
    // per-scope placement.
    std::size_t total_ops = 0;
    for (const auto& s : r.unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 2);
}

static void test_pg_when_nested_compound_inner_rejected() {
    // WHEN(a) { WHEN(b | c) { ... } } — compound inner. The inner WHEN's
    // materialize arg peels to a CXXOperatorCallExpr (not a bare DRE) so
    // the named-only guard rejects the pair. Phase F handles this inner
    // WHEN's compound lift separately; Phase G stays out.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN(a) { WHEN(b | c) { (void)d; } }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

static void test_pg_when_nested_compound_outer_rejected() {
    // WHEN((b | c)) { WHEN(d) { ... } } — compound outer. From the inner
    // WHEN we walk up to the outer WHEN IfStmt; the outer's materialize
    // arg peels to a CXXOperatorCallExpr; named-only guard rejects the
    // pair. Inner `d` by itself remains on the runtime path.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool b, qbool c, qbool d, qbool e) {\n"
        "    WHEN((b | c)) { WHEN(d) { (void)e; } }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

static void test_pg_when_nested_siblings_tolerated() {
    // WHEN(a) { foo(); WHEN(b) { body } bar(); } — siblings around the
    // inner WHEN. The decision in the Phase G plan is "always lower
    // regardless of siblings"; detection must fire regardless of sibling
    // statements before/after the inner WHEN in the outer's body block.
    // We use `(void)foo_stmt;` placeholders instead of calling a
    // separately-declared function to keep the inline stub self-contained.
    // PG-3 adds one AND op on top of PG-2's one replacement + one raw
    // insertion.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN(a) {\n"
        "        (void)c;\n"
        "        WHEN(b) { (void)d; }\n"
        "        (void)c;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.replacements.size() == 1);
    CHECK(r.unit.raw_insertions.size() == 1);
    std::size_t total_ops = 0;
    for (const auto& s : r.unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 1);
}

// ── Phase H / PH-1: scope-finder covers braced + braceless body shapes ─────
//
// The PH-1 refactor replaces the old `enclosing_compound_stmt` walk with
// `enclosing_scope` returning `{kind, anchor}` where `kind` is
// `CompoundStmt` (legacy braced shape) or `BracelessBody` (new: single-
// statement body of a for/while/if/else). Every Phase A-G matcher calls
// the new API via `find_or_create_scope`, so braceless body contexts that
// the old walk silently rejected now produce QScopes.
//
// We drive the OR matcher (the simplest, one op per match) through the
// four body shapes the acceptance criteria call out:
//   - braced `for` body      → CompoundStmt kind (legacy)
//   - braceless `for` body   → BracelessBody kind (new)
//   - braced `if`  then      → CompoundStmt kind (legacy)
//   - braceless `if`  then   → BracelessBody kind (new)
// Plus a braced-WHEN regression check: the macro-generated inner `if`s
// whose then-arms are macro-synthesised Stmts MUST NOT be classified
// as BracelessBody (the user-visible scope is the function body's
// CompoundStmt; the macro tower is invisible to the user).
//
// The uniform assertion across all four positive cases: exactly one
// QScope with exactly one QOperation{kind=OR}. The close_brace anchor's
// raw encoding differs between the kinds (file loc for BracelessBody
// points past the body's `;`; for CompoundStmt it points at the `}`),
// but both are valid (non-invalid) SourceLocations — we assert that
// property to confirm the PH-1 helper wired the anchor through
// `Lexer::getLocForEndOfToken` correctly.

static void test_ph1_braced_for_body() {
    // Braced for body — legacy shape. One QScope keyed on the inner
    // CompoundStmt. Behaviour byte-identical to pre-PH-1.
    QUnit unit = run_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 1; ++i) {\n"
        "        qbool tmp = a | b;\n"
        "        (void)tmp;\n"
        "    }\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("tmp"));
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

static void test_ph1_braceless_for_body() {
    // Braceless for body — previously silently rejected by the old
    // `enclosing_compound_stmt` walk. After PH-1 the op surfaces in a
    // synthetic QScope whose open_brace is the body Stmt's begin loc
    // and close_brace is just past the terminating `;`.
    QUnit unit = run_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 1; ++i) qbool tmp = a | b;\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("tmp"));
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

static void test_ph1_braced_if_then() {
    // Braced if-then — legacy shape. One QScope keyed on the CompoundStmt.
    QUnit unit = run_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) {\n"
        "        qbool tmp = a | b;\n"
        "        (void)tmp;\n"
        "    }\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

static void test_ph1_braceless_if_then() {
    // Braceless if-then — previously silently rejected. PH-1 surfaces
    // the op in a synthetic QScope keyed on the body Stmt.
    QUnit unit = run_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) qbool tmp = a | b;\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

static void test_ph1_braced_when_not_misidentified() {
    // Regression: WHEN(cond) { qop; } expands to three nested `if`s whose
    // then-arms walk would naively look like "braceless bodies". PH-1's
    // `is_user_braceless_body` guard rejects macro-spelled body stmts, so
    // the walk continues up past the WHEN tower and lands on the
    // user-written function body's CompoundStmt. The emitted QScope's
    // open_brace raw encoding MUST therefore match the function body's
    // `{`, not any intermediate macro `if`.
    //
    // We drive this through the MVP OR matcher + WHEN macro stub. The
    // OR op lives inside the WHEN body's CompoundStmt (the braces the
    // user wrote around the body), so exactly one QScope is produced
    // and it corresponds to the user-written scope.
    std::string code;
    code.reserve(kQBoolWhenStub.size() + 128);
    code.append(kQBoolWhenStub);
    code.append(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(a) {\n"
        "        qbool tmp = b | c;\n"
        "        (void)tmp;\n"
        "    }\n"
        "}\n");

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_or_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PH-1 braced WHEN "
                     "regression)\n");
    }
    // Exactly one QScope: the one the user wrote around the WHEN body.
    // A misidentification would either (a) produce zero scopes (the walk
    // halted too early) or (b) produce a BracelessBody scope anchored on
    // a macro-expanded IfStmt's then-arm.
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK(s.open_brace.isValid());
    // The open_brace must NOT be a macro-ID loc — the user-written body
    // opens at a file spelling, while the macro tower's synthetic braces
    // (if any were picked up) would spell inside the macro expansion.
    CHECK(!s.open_brace.isMacroID());
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

// ── Phase H / PH-3: outer-variable-mutation guard ──────────────────────────
//
// The PH-3 matcher flags compound-assign mutations (`a ^= b`, `a += C`,
// `a += b`, ...) whose target qbool/qint is declared in an outer scope
// relative to the mutation site AND where the mutation lives inside a
// `for`, `while`, `if` (then/else), or `WHEN` body. For every such hit
// the matcher:
//
//   (a) flags the matching QOperation with `skip_uncompute=true`, so the
//       M8 synthesis pass emits no inverse, and
//   (b) prints a stderr diagnostic pointing the user at the line+col of
//       the offending mutation.
//
// These tests exercise the full pipeline via the production xor_assign
// (Phase A PA-3/PA-4) and compound-assign matchers (Phase B/C), layered
// with the PH-3 guard registered LAST — matching the ordering invariant
// described in `matcher.hpp` and enforced in `main.cpp`.

// Stub: qbool with `operator^=` for Phase A PA-3 matching. Deliberately
// minimal — the matcher keys off the operator overload name and the
// LHS declRefExpr; no type guard on the LHS is required for PH-3.
static constexpr std::string_view kQBoolXorAssignStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};

} // namespace sturm
using sturm::qbool;

#define WHEN(cond) if (bool _when_val_ = (bool)(cond); _when_val_)
)CPP";

// Run Phase A xor_assign + PH-3 guard on `user_src`. Returns the
// populated QUnit and the PH-3 detection count after the tool run.
struct PH3Run {
    QUnit unit;
    int detections = 0;
};

static PH3Run run_ph3_xor_assign_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolXorAssignStub.size() + user_src.size());
    code.append(kQBoolXorAssignStub);
    code.append(user_src);

    PH3Run out;
    reset_outer_var_guard_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // Phase A matchers come first so they push QOperations onto
    // `unit.scopes` before the PH-3 callback walks to classify each
    // mutation. Registration order is the load-bearing invariant:
    // MatchFinder invokes callbacks in registration order for a given
    // matched node.
    register_xor_assign_matcher(finder, out.unit);
    register_xor_assign_classical_matcher(finder, out.unit);
    register_outer_var_guard_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PH-3 xor_assign)\n");
    }
    out.detections = outer_var_guard_detection_count_for_test();
    return out;
}

static void test_ph3_outer_xor_inside_for_is_flagged() {
    // `qbool a` declared in the function body; mutated inside the for
    // body. PH-3 classifies this as OuterMutation, flags the op, and
    // emits the diagnostic. Exactly one op with `skip_uncompute == true`.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    // One op should exist and have skip_uncompute set.
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 1);
}

static void test_ph3_outer_xor_inside_while_is_flagged() {
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    while (true) {\n"
        "        a ^= b;\n"
        "        break;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

static void test_ph3_outer_xor_inside_if_is_flagged() {
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

static void test_ph3_outer_xor_inside_if_else_is_flagged() {
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) { (void)a; } else { a ^= b; }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

static void test_ph3_outer_xor_inside_when_is_flagged() {
    // WHEN barriers count the same as user-written if barriers. The
    // test-only stub above defines WHEN as a thin `if` macro whose
    // expansion loc spells the name WHEN — the PH-3 classifier treats
    // macro-expanded IfStmts exactly like user ifs for this analysis.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    WHEN(cond) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(skip_count == 1);
}

static void test_ph3_local_xor_without_control_flow_is_not_flagged() {
    // Negative: a bare `a ^= b;` statement at the function body level
    // is LocalMutation — no for/while/if/WHEN barrier between the
    // declaration and the mutation. PH-3 must NOT flag it.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    a ^= b;\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 0);
}

static void test_ph3_local_xor_in_for_of_locally_declared_var_is_not_flagged() {
    // Negative: `qbool r = a | b;` declared inside the for-body block,
    // then `r ^= b;` right after — the mutation target lives in the
    // SAME scope as the mutation (both inside the for-body CompoundStmt).
    // PH-3 walks up from the `^=` and hits the CompoundStmt (the
    // declaring scope) BEFORE any control-flow barrier, so it is
    // LocalMutation — not flagged.
    //
    // We use a bare variable (no qbool initializer here — PA-3 only
    // covers the `^=` statement, so declaring another qbool with `qbool
    // r;` is enough to give the mutation a local target.)
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool r;\n"
        "        r ^= a;\n"
        "        (void)b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 0);
}

static void test_ph3_mixed_inner_intermediate_and_outer_mutation() {
    // Mixed scope — the key PH-3 acceptance criterion: "per-op flag
    // granularity". One scope contains:
    //
    //   - `r ^= a;` where r is declared inside the for body
    //     (intermediate, uncompute normally) — skip_uncompute=false
    //   - `a ^= b;` where a is declared outside the for
    //     (outer mutation, skipped) — skip_uncompute=true
    //
    // After the run exactly ONE op carries the skip flag and the
    // detection counter bumps once.
    PH3Run r = run_ph3_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool r;\n"
        "        r ^= a;\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t skip_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
        }
    }
    CHECK(total == 2);
    CHECK(skip_count == 1);
}

// ── Phase H / PH-2: auto-brace-wrap matcher ─────────────────────────────────
//
// The PH-2 matcher schedules `{` + `}` raw insertions around every
// braceless `for` / `while` / `if` / `else` body that transitively
// contains a recognised quantum op. Tests exercise:
//
//   - Positive: braceless for / while / if (then) / if (else) bodies
//     each produce exactly one pair of raw insertions at the correct
//     anchor locations.
//   - Negative: already-braced bodies produce NO raw insertions
//     (PH-2 leaves CompoundStmt bodies alone so existing snapshots
//     stay byte-identical).
//   - Negative: a purely-classical braceless body (no qbool / qint
//     operand anywhere) produces NO raw insertions — PH-2 must not
//     gratuitously wrap user code that has no quantum content.
//   - Regression: a `WHEN(cond) { ... }` invocation does NOT trigger
//     the matcher on its macro-expanded inner `if`s — the
//     !body_loc.isMacroID() guard collapses the WHEN tower.
//
// Drives the OR matcher + brace-wrap matcher on a shared stub. The OR
// matcher is incidental (we never check its output here); it is
// included so the inline `qbool tmp = a | b;` body the fixtures use
// parses cleanly — PH-2's quantum-op heuristic recognises it whether
// or not the OR matcher itself fires.

struct PH2Run {
    QUnit unit;
    int detections = 0;
};

// Shared stub + runner for the PH-2 tests. We embed a small qbool with
// `operator|` so the quantum-op probe's CXXOperatorCallExpr + argument-
// type check fires on the positive fixtures. The `bool should_run()`
// method + WHEN macro let one of the tests check the WHEN-body
// short-circuit. We deliberately do NOT prepend kQBoolWhenStub because
// a simpler stub keeps the positive tests independent of the Phase F
// materialize_when fixture shape.
static constexpr std::string_view kQBoolPH2Stub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }

} // namespace sturm
using sturm::qbool;

#define WHEN(cond) if (bool _when_val_ = (bool)(cond); _when_val_)
)CPP";

static PH2Run run_ph2_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolPH2Stub.size() + user_src.size());
    code.append(kQBoolPH2Stub);
    code.append(user_src);

    PH2Run out;
    reset_brace_wrap_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // Register the brace-wrap matcher ALONE — we exercise just PH-2
    // here, without other matchers contributing ops / insertions that
    // would muddy the raw_insertions assertions.
    register_brace_wrap_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PH-2 brace_wrap)\n");
    }
    out.detections = brace_wrap_detection_count_for_test();
    return out;
}

static void test_ph2_braceless_for_body_is_wrapped() {
    // Braceless for-body containing a qbool VarDecl. PH-2 must schedule
    // two raw insertions: `{` at the body's begin loc and `}` at the
    // loc immediately past the body's terminating `;`.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
    if (r.unit.raw_insertions.size() != 2) return;
    // First insertion is the opening `{`, second is the closing `}`.
    // Both must carry valid insertion locations so the emitter can
    // apply them.
    CHECK(r.unit.raw_insertions[0].insert_before.isValid());
    CHECK(r.unit.raw_insertions[1].insert_before.isValid());
    // Text content is whatever the matcher chose, but the two
    // insertions together must contain exactly one `{` and one `}`.
    CHECK(r.unit.raw_insertions[0].code.find('{') != std::string::npos);
    CHECK(r.unit.raw_insertions[1].code.find('}') != std::string::npos);
}

static void test_ph2_braceless_while_body_is_wrapped() {
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    int i = 0;\n"
        "    while (i < 3) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

static void test_ph2_braceless_if_then_body_is_wrapped() {
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

static void test_ph2_braceless_if_else_body_is_wrapped() {
    // Only the else-arm is braceless + quantum. The then-arm is already
    // braced, so PH-2 leaves it alone.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) { (void)a; } else qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

static void test_ph2_braceless_if_both_arms_wrapped() {
    // Both arms are braceless + quantum: the matcher wraps both,
    // producing two pairs (four raw_insertions total).
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) qbool x = a | b; else qbool y = a | b;\n"
        "}\n");
    CHECK(r.detections == 2);
    CHECK(r.unit.raw_insertions.size() == 4);
}

static void test_ph2_braced_for_body_is_not_wrapped() {
    // Regression: an already-braced for-body must produce zero raw
    // insertions so existing Phase A-G snapshot fixtures stay
    // byte-identical to pre-PH-2.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool tmp = a | b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.raw_insertions.empty());
}

static void test_ph2_braced_if_then_body_is_not_wrapped() {
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) { qbool tmp = a | b; }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.raw_insertions.empty());
}

static void test_ph2_classical_braceless_for_is_not_wrapped() {
    // A braceless for-body that contains ONLY classical statements
    // (no qbool / qint operand anywhere) must NOT be wrapped — PH-2
    // would otherwise churn the output on arbitrary user code.
    PH2Run r = run_ph2_matcher(
        "void demo() {\n"
        "    int counter = 0;\n"
        "    for (int i = 0; i < 3; ++i) counter += 1;\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.raw_insertions.empty());
}

static void test_ph2_when_macro_inner_ifs_not_wrapped() {
    // Regression guard: WHEN(cond) { body } expands (via the
    // kQBoolPH2Stub minimal macro) to a nested `if` whose then-arm
    // IS a CompoundStmt — so the `hasThen(non_compound)` matcher
    // would not fire anyway. But if the user wrote WHEN(cond) body;
    // without braces, the then-arm is braceless AND macro-expanded
    // (its begin loc is inside the macro body). PH-2 must skip it
    // because the loc is a macro ID. Tested via a WHEN invocation
    // whose body is a single statement (no user-written braces
    // around the body). The resulting AST has the WHEN macro's `if`
    // own the body as its non-compound then-arm; begin loc is
    // macro-spelled, so `!begin.isMacroID()` rejects it.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    WHEN(cond) qbool tmp = a | b;\n"
        "}\n");
    // The user-spelled statement after `WHEN(cond)` has a non-macro
    // begin loc, so PH-2 wraps it as a braceless ifStmt then-body
    // exactly once. The macro-expanded inner `if` is skipped by
    // `begin.isMacroID()`. Net: exactly one pair of raw insertions.
    //
    // Rationale: the user DID write a braceless body — `qbool tmp =
    // a | b;` — and PH-2 should wrap it. What we are guarding against
    // is the matcher firing ADDITIONALLY on the macro-expanded inner
    // `if`'s synthetic then-arm, which would produce two pairs of
    // brace insertions at overlapping locations and break the output.
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

static void test_ph2_nested_braceless_fors_both_wrapped() {
    // Regression: a nested pair of braceless fors, the outer body is
    // the inner for-stmt, whose body is a quantum op. The OUTER for's
    // body is the inner ForStmt itself — which IS a Stmt but NOT a
    // CompoundStmt — so the `unless(compoundStmt())` filter fires on
    // it. The quantum-op probe walks into the inner for and finds
    // the qbool VarDecl, so the outer body qualifies as "contains a
    // quantum op". The inner body is `qbool tmp = a | b;` — also
    // braceless + quantum. Both bodies get wrapped independently.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 2; ++i)\n"
        "        for (int j = 0; j < 2; ++j) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 2);
    CHECK(r.unit.raw_insertions.size() == 4);
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
    test_pf_when_not_lifts_once();
    test_pf_when_bare_if_no_match();
    test_pf_when_direct_materialize_call_no_match();

    test_pg_when_nested_named_depth2_detects_once();
    test_pg_when_nested_named_depth3_detects_twice();
    test_pg_when_nested_compound_inner_rejected();
    test_pg_when_nested_compound_outer_rejected();
    test_pg_when_nested_siblings_tolerated();

    test_ph1_braced_for_body();
    test_ph1_braceless_for_body();
    test_ph1_braced_if_then();
    test_ph1_braceless_if_then();
    test_ph1_braced_when_not_misidentified();

    test_ph2_braceless_for_body_is_wrapped();
    test_ph2_braceless_while_body_is_wrapped();
    test_ph2_braceless_if_then_body_is_wrapped();
    test_ph2_braceless_if_else_body_is_wrapped();
    test_ph2_braceless_if_both_arms_wrapped();
    test_ph2_braced_for_body_is_not_wrapped();
    test_ph2_braced_if_then_body_is_not_wrapped();
    test_ph2_classical_braceless_for_is_not_wrapped();
    test_ph2_when_macro_inner_ifs_not_wrapped();
    test_ph2_nested_braceless_fors_both_wrapped();

    test_ph3_outer_xor_inside_for_is_flagged();
    test_ph3_outer_xor_inside_while_is_flagged();
    test_ph3_outer_xor_inside_if_is_flagged();
    test_ph3_outer_xor_inside_if_else_is_flagged();
    test_ph3_outer_xor_inside_when_is_flagged();
    test_ph3_local_xor_without_control_flow_is_not_flagged();
    test_ph3_local_xor_in_for_of_locally_declared_var_is_not_flagged();
    test_ph3_mixed_inner_intermediate_and_outer_mutation();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
