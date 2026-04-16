// test_matcher_qint_qint.cpp — Phase C: qint_t += / -= / *= / /= / %= qint.
//
// Positive coverage for the five PC matchers plus the other half of the
// PB/PC disjointness invariant (classical-RHS form must NOT match).

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

// Same shape as the PB stub, plus `operator%=` (PC only). The converting
// constructor from `long long` is retained so one PC test can exercise
// the PB/PC disjointness invariant from the PC side.
constexpr std::string_view kQIntStubPC = R"CPP(
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

QUnit run_pc_matchers(std::string_view user_src) {
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

void test_pc_add_assign_qint_binds_rhs_ident() {
    // `a += b;` on two qint_t must produce ADD_ASSIGN_QINT with operand
    // name "b" — verbatim DeclRefExpr identifier. No CXXConstructExpr
    // peel; the RHS is a bare DeclRefExpr in this form.
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

void test_pc_sub_assign_qint_binds_rhs_ident() {
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

void test_pc_mul_assign_qint_binds_rhs_ident() {
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

void test_pc_div_assign_qint_binds_rhs_ident() {
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

void test_pc_mod_assign_qint_binds_rhs_ident() {
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

void test_pc_negative_classical_rhs_no_match() {
    // Disjointness invariant (PC side): the classical-RHS form `a += 3;`
    // has a CXXConstructExpr wrapping the int literal. The PC patterns
    // require a bare DeclRefExpr RHS, so this shape is rejected. Neither
    // PB nor PC can shadow the other.
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

} // namespace

void run_qint_qint_tests() {
    test_pc_add_assign_qint_binds_rhs_ident();
    test_pc_sub_assign_qint_binds_rhs_ident();
    test_pc_mul_assign_qint_binds_rhs_ident();
    test_pc_div_assign_qint_binds_rhs_ident();
    test_pc_mod_assign_qint_binds_rhs_ident();
    test_pc_negative_classical_rhs_no_match();
}
