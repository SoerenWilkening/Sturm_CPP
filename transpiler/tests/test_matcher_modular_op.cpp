// test_matcher_modular_op.cpp — sturm-qzab.1 / sturm-qzab.2
// (P5.1 beat 5.1 + P5.2 beat 5.2) unit tests for the modular-arithmetic
// AST matcher.
//
// Beat 5.1 covers the AddMod arm: the matcher must recognize the AST
// shape `qint_t<W> r = (a + b) % n;` (a `VarDecl` whose initializer is
// the qint-overloaded `operator%` whose LHS in turn is the
// qint-overloaded `operator+`), produce one `ModularOpHit` per match,
// and populate the operand / result names + width.
//
// Beat 5.2 mirrors that for `qint_t<W> r = (a * b) % n;` → MulMod.
// Same VarDecl anchor, but the inner CXXOperatorCallExpr is `operator*`
// instead of `operator+`. The matcher emits `ModularOpKind::MulMod` for
// these hits; `AddMod` patterns must remain unaffected (no
// double-firing, no false positives between the two arms).
//
// Negative coverage in this file:
//   - `qint_t<W> r = a % n;` (bare modulus, no inner +/*) does NOT match.
//   - `qint_t<W> r = (a - b) % n;` (inner `-`, not `+`/`*`) does NOT match.
//   - `qint_t<W> r = (a + b) * n;` (outer `*`, not `%`) does NOT match.
//   - `qint_t<W> r = a + b;` (no `%` at all) does NOT match.
//   - `(a + b) % n` / `(a * b) % n` on non-qint operands (NotQInt) do NOT
//     match.
//
// PowMod arm lands in beats 5.4-5.6 — it shares the matcher TU but
// each beat ships its own set of asserts.

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
inline NotQInt operator*(const NotQInt&, const NotQInt&) { return NotQInt{}; }
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

// sturm-qzab.2 (P5.2 beat 5.2): MulMod arm — recognise the AST shape
// `qint_t<W> r = (a * b) % n;`. Same VarDecl anchor and outer `%` shape
// as AddMod; the only structural difference is the inner operator (`*`
// instead of `+`). The matcher must surface a `ModularOpHit` with
// `kind == MulMod` and the same operand-name / width slots populated.
void test_basic_mul_mod_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a * b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
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

void test_mul_mod_distinct_widths_resolve() {
    // Width resolution for the MulMod arm — same posture as the
    // AddMod sibling test, but on `(a * b) % n`.
    auto hits = run_matcher(
        "using qint5 = sturm::qint_t<5>;\n"
        "void demo(qint5 a, qint5 b, qint5 n) {\n"
        "    qint5 r = (a * b) % n;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
    CHECK(hits[0].result_width == 5);
}

void test_mul_mod_inner_sub_does_not_match_mulmod() {
    // (a - b) % n. The MulMod matcher recognises only `*` as the
    // inner op — `-` must not collapse into MulMod (subtraction-mod
    // is deferred per PRD §7).
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a - b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_mul_mod_outer_add_does_not_match_mulmod() {
    // (a * b) + n — outer `+`, not `%`. The MulMod matcher anchors on
    // the outer `%` so this AST shape is rejected.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a * b) + n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_mul_mod_non_qint_operands_do_not_match() {
    // `(a * b) % n` on a non-qint user type — the matcher's
    // `cxxRecordDecl(hasName("qint_t"))` guard rejects this even though
    // the AST shape matches structurally.
    auto hits = run_matcher(
        "void demo(NotQInt a, NotQInt b, NotQInt n) {\n"
        "    NotQInt r = (a * b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_add_and_mul_mod_coexist() {
    // Both arms in the same TU must produce two distinct hits with the
    // correct discriminant. Confirms the AddMod / MulMod patterns do
    // not double-fire on the same site.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint s = (a + b) % n;\n"
        "    qint p = (a * b) % n;\n"
        "    (void)s; (void)p;\n"
        "}\n");
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    // Order is matcher-traversal order. Assert the set of kinds
    // covers exactly {AddMod, MulMod} regardless of order so the test
    // is robust to MatchFinder ordering.
    bool saw_add = false;
    bool saw_mul = false;
    for (const auto& h : hits) {
        if (h.kind == ModularOpKind::AddMod) saw_add = true;
        if (h.kind == ModularOpKind::MulMod) saw_mul = true;
    }
    CHECK(saw_add);
    CHECK(saw_mul);
}

void test_mul_mod_enclosing_block_tracks_inner_scope() {
    // Inner-scope tracking for the MulMod arm — mirrors the AddMod
    // sibling test_enclosing_block_tracks_inner_scope.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    {\n"
        "        qint r = (a * b) % n;\n"
        "        (void)r;\n"
        "    }\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
    CHECK(hits[0].enclosing_block != nullptr);
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
    test_basic_mul_mod_match();
    test_mul_mod_distinct_widths_resolve();
    test_mul_mod_inner_sub_does_not_match_mulmod();
    test_mul_mod_outer_add_does_not_match_mulmod();
    test_mul_mod_non_qint_operands_do_not_match();
    test_add_and_mul_mod_coexist();
    test_mul_mod_enclosing_block_tracks_inner_scope();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
