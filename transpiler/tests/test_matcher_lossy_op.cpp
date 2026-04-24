// test_matcher_lossy_op.cpp — LO-2a (sturm-9254) unit tests.
//
// Positive: each of the 5 lossy operators (`*=`, `/=`, `%=`, `&=`, `|=`)
// on qint_t LHS + qint_t RHS produces one LossyOpHit with the correct
// opcode and operand names; all five fire together in one scope.
//
// Negative: reversible compound-assigns (`+=`, `-=`, `^=`) and plain
// copy-assign (`=`) do NOT match. Lossy operators on a non-qint LHS
// (user class `NotQInt` or builtin `int`) also do NOT match — the
// `hasName("qint_t")` guard and the `CXXOperatorCallExpr` top-level
// anchor (vs `CompoundAssignOperator` for builtins) cover both arms.
//
// Enclosing-block: a lossy op inside a nested `{ ... }` reports the
// inner CompoundStmt (not the function body), which is what LO-2c
// needs to anchor the scope-exit cleanup.

#include "matcher_lossy_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Stub qint_t<W> with the 5 lossy operators plus reversible siblings
// (+=, -=, ^=, =) for negative coverage. `NotQInt` is a non-qint user
// class with operator*= / operator&= to exercise the type-name guard.
constexpr std::string_view kLossyStub = R"CPP(
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator=(const qint_t&)  { return *this; }
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }
    qint_t& operator^=(const qint_t&) { return *this; }
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
    qint_t& operator%=(const qint_t&) { return *this; }
    qint_t& operator&=(const qint_t&) { return *this; }
    qint_t& operator|=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint_t = sturm::qint_t<2>;
class NotQInt {
public:
    NotQInt() {}
    NotQInt(const NotQInt&) {}
    NotQInt& operator*=(const NotQInt&) { return *this; }
    NotQInt& operator&=(const NotQInt&) { return *this; }
};
)CPP";

std::vector<LossyOpHit> run_lossy_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kLossyStub.size() + user_src.size());
    code.append(kLossyStub);
    code.append(user_src);

    std::vector<LossyOpHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_lossy_op_matcher(finder, hits);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false\n");
    }
    return hits;
}

int tests_run  = 0;
int tests_pass = 0;

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
        std::fprintf(stderr, "FAIL  %s:%d  strings differ got=%s want=%s\n", \
                     __FILE__, __LINE__,                              \
                     std::string(got).c_str(),                        \
                     std::string(want).c_str());                      \
    }                                                                 \
} while (0)

// One helper per opcode: parametrized by expected kind + source op.
void test_one_lossy(LossyOpKind kind, const char* op) {
    std::string src =
        std::string("void demo(qint_t a, qint_t b) {\n    a ") +
        op + std::string(" b;\n}\n");
    auto hits = run_lossy_matcher(src);
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].opcode == kind);
    CHECK_EQ_STR(hits[0].lhs_name, std::string("a"));
    CHECK_EQ_STR(hits[0].rhs_name, std::string("b"));
    CHECK(hits[0].call != nullptr);
    CHECK(hits[0].lhs_ref != nullptr);
    CHECK(hits[0].rhs_ref != nullptr);
    CHECK(hits[0].enclosing_block != nullptr);
}

void test_all_five_in_one_scope() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a *= b;\n"
        "    a /= b;\n"
        "    a %= b;\n"
        "    a &= b;\n"
        "    a |= b;\n"
        "}\n");
    CHECK(hits.size() == 5);
    if (hits.size() != 5) return;
    bool saw[5] = {false, false, false, false, false};
    for (const auto& h : hits) {
        if (h.opcode == LossyOpKind::MulAssign) saw[0] = true;
        if (h.opcode == LossyOpKind::DivAssign) saw[1] = true;
        if (h.opcode == LossyOpKind::ModAssign) saw[2] = true;
        if (h.opcode == LossyOpKind::AndAssign) saw[3] = true;
        if (h.opcode == LossyOpKind::OrAssign)  saw[4] = true;
    }
    for (bool b : saw) CHECK(b);
}

void test_add_sub_xor_assign_do_not_match() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a += b;\n"
        "    a -= b;\n"
        "    a ^= b;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_plain_copy_assign_does_not_match() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a = b;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_lossy_on_notqint_does_not_match() {
    auto hits = run_lossy_matcher(
        "void demo(NotQInt a, NotQInt b) {\n"
        "    a *= b;\n"
        "    a &= b;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_lossy_on_builtin_int_does_not_match() {
    // Builtin int compound-assign lowers to CompoundAssignOperator, not
    // CXXOperatorCallExpr. The top-level anchor rejects it structurally.
    auto hits = run_lossy_matcher(
        "void demo(int a, int b) {\n"
        "    a *= b;\n"
        "    a /= b;\n"
        "    a %= b;\n"
        "    a &= b;\n"
        "    a |= b;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_enclosing_block_tracks_inner_scope() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    {\n"
        "        a *= b;\n"
        "    }\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].enclosing_block != nullptr);
}

} // namespace

int main() {
    test_one_lossy(LossyOpKind::MulAssign, "*=");
    test_one_lossy(LossyOpKind::DivAssign, "/=");
    test_one_lossy(LossyOpKind::ModAssign, "%=");
    test_one_lossy(LossyOpKind::AndAssign, "&=");
    test_one_lossy(LossyOpKind::OrAssign,  "|=");
    test_all_five_in_one_scope();
    test_add_sub_xor_assign_do_not_match();
    test_plain_copy_assign_does_not_match();
    test_lossy_on_notqint_does_not_match();
    test_lossy_on_builtin_int_does_not_match();
    test_enclosing_block_tracks_inner_scope();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
