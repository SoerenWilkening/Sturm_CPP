// test_matcher_qint_compare.cpp — Phase D: qint_t <, <=, ==, !=, >, >=.
//
// Each PD matcher binds a `qbool c = a <cmp> b;` VarDecl and records TWO
// operands — the comparator inverse depends on both inputs to flip the
// result bit back to |0⟩. Coverage:
//   - Six positive cases (one per comparator).
//   - Two negatives (wrong result type, non-qint operands).
//   - A six-ops-same-scope mixed case.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_qint_compare_ns {

using namespace sturm::transpile;

namespace {

// Stub with qbool + qint_t<W> + six comparison overloads returning qbool
// by value (matching qint_compare_v3.hpp:93-127). No implicit conversions
// from qint_t to qbool, no qbool::operator==(qbool, qbool) — the negative
// tests rely on these exclusions so the type guards have unambiguous
// shapes to reject.
constexpr std::string_view kQIntCompareStub = R"CPP(
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

QUnit run_pd_matchers(std::string_view user_src) {
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

void test_pd_eq_compare_qint_binds_both_operands() {
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

void test_pd_ne_compare_qint_binds_both_operands() {
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

void test_pd_lt_compare_qint_binds_both_operands() {
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

void test_pd_le_compare_qint_binds_both_operands() {
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

void test_pd_gt_compare_qint_binds_both_operands() {
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

void test_pd_ge_compare_qint_binds_both_operands() {
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

void test_pd_negative_wrong_result_type() {
    // The declared variable is `int`, not `qbool`. Even though we invoke
    // `operator==` on two qint_t operands, the VarDecl type guard
    // rejects because a classical result type has no uncompute story.
    // We spell an unused `(void)(a == b);` alongside `int c = 0;` so
    // parsing succeeds; the point is that no qbool-typed VarDecl exists.
    QUnit unit = run_pd_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    (void)(a == b);\n"
        "    int c = 0;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

void test_pd_negative_non_qint_operands() {
    // Classical-int operands: `int == int` has no qint_t record decl
    // at either argument position, so the type guard rejects. A bare
    // `bool c = i == j;` is also a different VarDecl type.
    QUnit unit = run_pd_matchers(
        "void demo(int i, int j) {\n"
        "    bool c = i == j;\n"
        "}\n");
    CHECK(unit.scopes.empty());
}

void test_pd_six_ops_same_scope_hit_all_kinds() {
    // A single block with one of each comparison. All six ops should
    // land in the same QScope (same enclosing CompoundStmt), in source
    // order, with the correct per-op QOpKind.
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

} // namespace

}  // namespace sturm_test_matcher_qint_compare_ns

void run_qint_compare_tests() {
    using namespace sturm_test_matcher_qint_compare_ns;
    test_pd_eq_compare_qint_binds_both_operands();
    test_pd_ne_compare_qint_binds_both_operands();
    test_pd_lt_compare_qint_binds_both_operands();
    test_pd_le_compare_qint_binds_both_operands();
    test_pd_gt_compare_qint_binds_both_operands();
    test_pd_ge_compare_qint_binds_both_operands();
    test_pd_negative_wrong_result_type();
    test_pd_negative_non_qint_operands();
    test_pd_six_ops_same_scope_hit_all_kinds();
}
