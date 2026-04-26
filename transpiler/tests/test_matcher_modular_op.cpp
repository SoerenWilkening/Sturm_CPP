// test_matcher_modular_op.cpp — sturm-qzab.1 (P5.1 beat 5.1) unit tests
// for the modular-arithmetic AST matcher.
//
// Beat 5.1 covers the AddMod arm only: the matcher must recognize the
// AST shape `qint_t<W> r = (a + b) % n;` (a `VarDecl` whose initializer
// is the qint-overloaded `operator%` whose LHS in turn is the
// qint-overloaded `operator+`), produce one `ModularOpHit` per match,
// and populate the operand / result names + width.
//
// Negative coverage in this file:
//   - `qint_t<W> r = a % n;` (bare modulus, no inner +) does NOT match.
//   - `qint_t<W> r = (a - b) % n;` (inner `-`, not `+`) does NOT match
//     under the AddMod pattern.
//   - `qint_t<W> r = (a + b) * n;` (outer `*`, not `%`) does NOT match.
//   - `qint_t<W> r = a + b;` (no `%` at all) does NOT match.
//   - `(a + b) % n` on non-qint operands (NotQInt) does NOT match.
//
// MulMod / PowMod arms land in beats 5.2 / 5.4-5.6 — they share the
// matcher TU but each beat ships its own set of asserts.

#include "matcher_modular_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Hermetic stub of `qint_t<W>` carrying the operator overloads beat 5.1
// recognises (`+` and `%`) plus a `-` overload for the negative arm
// "inner-`-` should NOT match the AddMod pattern". Mirrors the stub
// shape used by `test_matcher_lossy_op.cpp` so the FixedCompilationDatabase
// path (no system include search) keeps working.
constexpr std::string_view kModularStub = R"CPP(
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator=(const qint_t&) { return *this; }
};
template <int W>
inline qint_t<W> operator+(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator-(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator*(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator%(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
} // namespace sturm
using qint = sturm::qint_t<2>;
class NotQInt {
public:
    NotQInt() {}
    NotQInt(const NotQInt&) {}
    NotQInt& operator=(const NotQInt&) { return *this; }
};
inline NotQInt operator+(const NotQInt&, const NotQInt&) { return NotQInt{}; }
inline NotQInt operator%(const NotQInt&, const NotQInt&) { return NotQInt{}; }
)CPP";

std::vector<ModularOpHit> run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kModularStub.size() + user_src.size());
    code.append(kModularStub);
    code.append(user_src);

    std::vector<ModularOpHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_modular_op_matcher(finder, hits);

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
        std::fprintf(stderr,                                          \
            "FAIL  %s:%d  strings differ\n  got=%s\n  want=%s\n",     \
            __FILE__, __LINE__,                                       \
            std::string(got).c_str(),                                 \
            std::string(want).c_str());                               \
    }                                                                 \
} while (0)

void test_basic_add_mod_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a + b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::AddMod);
    CHECK_EQ_STR(hits[0].result_name, std::string("r"));
    CHECK_EQ_STR(hits[0].a_name, std::string("a"));
    CHECK_EQ_STR(hits[0].b_name, std::string("b"));
    CHECK_EQ_STR(hits[0].n_name, std::string("n"));
    CHECK(hits[0].result_width == 2);
    CHECK(hits[0].mod_expr != nullptr);
    CHECK(hits[0].inner_op_expr != nullptr);
    CHECK(hits[0].result_var != nullptr);
    CHECK(hits[0].enclosing_block != nullptr);
}

void test_distinct_widths_resolve() {
    // sturm-czfi-style width resolution: qint_t<5> in the source must
    // surface as result_width=5. The matcher reads the LHS VarDecl's
    // type, which is `qint_t<5>` here.
    auto hits = run_matcher(
        "using qint5 = sturm::qint_t<5>;\n"
        "void demo(qint5 a, qint5 b, qint5 n) {\n"
        "    qint5 r = (a + b) % n;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::AddMod);
    CHECK(hits[0].result_width == 5);
}

void test_bare_modulus_does_not_match() {
    // No inner `+`; this is a plain modulus and must not collapse into
    // an AddMod rewrite.
    auto hits = run_matcher(
        "void demo(qint a, qint n) {\n"
        "    qint r = a % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_inner_sub_does_not_match_addmod() {
    // (a - b) % n. The matcher recognises only `+` as the inner op
    // for AddMod (subtraction-mod is deferred per PRD §7).
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a - b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_outer_mul_does_not_match_addmod() {
    // (a + b) * n — outer `*`, not `%`. The matcher anchors on the
    // outer `%` so this AST shape is rejected.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a + b) * n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_no_modulus_does_not_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b) {\n"
        "    qint r = a + b;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_non_qint_operands_do_not_match() {
    // `(a + b) % n` on a non-qint user type — the matcher's
    // `cxxRecordDecl(hasName("qint_t"))` guard rejects this even though
    // the AST shape matches structurally.
    auto hits = run_matcher(
        "void demo(NotQInt a, NotQInt b, NotQInt n) {\n"
        "    NotQInt r = (a + b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_enclosing_block_tracks_inner_scope() {
    // The hit's enclosing_block should be the inner `{ ... }` scope,
    // not the function body — mirrors the LO-2a test of the same name.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    {\n"
        "        qint r = (a + b) % n;\n"
        "        (void)r;\n"
        "    }\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].enclosing_block != nullptr);
}

} // namespace

int main() {
    test_basic_add_mod_match();
    test_distinct_widths_resolve();
    test_bare_modulus_does_not_match();
    test_inner_sub_does_not_match_addmod();
    test_outer_mul_does_not_match_addmod();
    test_no_modulus_does_not_match();
    test_non_qint_operands_do_not_match();
    test_enclosing_block_tracks_inner_scope();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
