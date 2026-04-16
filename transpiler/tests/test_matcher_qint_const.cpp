// test_matcher_qint_const.cpp — Phase B: qint_t += / -= / *= / /= const.
//
// Positive coverage for the four PB matchers (one per arithmetic operator)
// plus a disjointness check against the Phase C qint-qint form. All four
// PB matchers are mutually exclusive by operator name, so a single
// CXXOperatorCallExpr fires at most one of them.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Stub: qint_t<W> with an implicit long-long converting constructor and
// four compound-assign operators. Mirrors the shape the PB matchers key
// off (qint_core.hpp:52). `long long` matches the real `int64_t` lift in
// production — inline integer literals convert through integer promotion.
constexpr std::string_view kQIntStub = R"CPP(
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

QUnit run_pb_matchers(std::string_view user_src) {
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

void test_pb_add_assign_const_binds_literal() {
    // `a += 3;` on qint_t must produce ADD_ASSIGN_CONST with operand
    // name "3" — verbatim IntegerLiteral text after peeling the
    // CXXConstructExpr wrapper.
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

void test_pb_sub_assign_const_binds_literal() {
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

void test_pb_mul_assign_const_binds_literal() {
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

void test_pb_div_assign_const_binds_literal() {
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

void test_pb_negative_qint_qint_form_no_match() {
    // Phase C disjointness invariant: the qint-qint form `a += b;` has
    // no CXXConstructExpr wrapper on the RHS because no converting
    // constructor fires. The PB pattern requires the peel and therefore
    // must not match this form. Pinning the separation here means
    // neither PB nor PC can shadow the other as matchers evolve.
    QUnit unit = run_pb_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a += b;\n"
        "    a -= b;\n"
        "    a *= b;\n"
        "    a /= b;\n"
        "}\n");

    CHECK(unit.scopes.empty());
}

} // namespace

void run_qint_const_tests() {
    test_pb_add_assign_const_binds_literal();
    test_pb_sub_assign_const_binds_literal();
    test_pb_mul_assign_const_binds_literal();
    test_pb_div_assign_const_binds_literal();
    test_pb_negative_qint_qint_form_no_match();
}
