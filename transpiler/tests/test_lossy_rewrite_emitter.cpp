// test_lossy_rewrite_emitter.cpp — LO-2b (sturm-yxxa) unit tests.
//
// Forward-only (no cleanup; that's LO-2c). Two harnesses: pure-string
// (`emit_lossy_forward_text`) for byte-exact per-opcode goldens, and
// AST-driven (`emit_lossy_forward`) consuming LO-2a's `LossyOpHit`.
// Counter contract: one FreshNameAllocator across N hits yields
// `_0`, `_1`, ... regardless of opcode mix — LO-2c pairs by name.

#include "lossy_rewrite_emitter.hpp"

#include "fresh_names.hpp"
#include "matcher_lossy_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

constexpr std::string_view kLossyStub = R"CPP(
namespace sturm {
template <int W> class qint_t {
public:
    qint_t() {} qint_t(const qint_t&) {}
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
    qint_t& operator%=(const qint_t&) { return *this; }
    qint_t& operator&=(const qint_t&) { return *this; }
    qint_t& operator|=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint_t = sturm::qint_t<2>;
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
    if (!ok) std::fprintf(stderr, "FAIL  tool run returned false\n");
    return hits;
}

int tests_run = 0, tests_pass = 0;
#define CHECK(cond) do { ++tests_run;                                 \
    if (cond) { ++tests_pass; }                                       \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                  \
                        __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ_STR(got, want) do { ++tests_run;                     \
    if ((got) == (want)) { ++tests_pass; }                            \
    else { std::fprintf(stderr,                                       \
        "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",    \
        __FILE__, __LINE__,                                           \
        std::string(got).c_str(), std::string(want).c_str()); }       \
} while (0)

// Per-opcode forward emission — byte-exact text + tmp-name asserts.
void check_one(LossyOpKind kind, const std::string& want_text,
               const std::string& want_swap, const std::string& want_aux) {
    FreshNameAllocator alloc;
    LossyEmission em = emit_lossy_forward_text(kind, "a", "b", alloc);
    CHECK_EQ_STR(em.text, want_text);
    CHECK_EQ_STR(em.swap_target_name, want_swap);
    CHECK_EQ_STR(em.aux_tmp_name, want_aux);
    CHECK(em.opcode == kind);
}

void test_mul() {
    check_one(LossyOpKind::MulAssign,
        "qint __sturm_tmp_mul_0;\n"
        "mul_oop(a, b, __sturm_tmp_mul_0);\n"
        "swap(a, __sturm_tmp_mul_0);\n",
        "__sturm_tmp_mul_0", "");
}
void test_and() {
    check_one(LossyOpKind::AndAssign,
        "qint __sturm_tmp_and_0;\n"
        "and_oop(a, b, __sturm_tmp_and_0);\n"
        "swap(a, __sturm_tmp_and_0);\n",
        "__sturm_tmp_and_0", "");
}
void test_or() {
    check_one(LossyOpKind::OrAssign,
        "qint __sturm_tmp_or_0;\n"
        "or_oop(a, b, __sturm_tmp_or_0);\n"
        "swap(a, __sturm_tmp_or_0);\n",
        "__sturm_tmp_or_0", "");
}
void test_div() {
    // /= → divide_oop with q + r; swap target is the QUOTIENT.
    check_one(LossyOpKind::DivAssign,
        "qint __sturm_tmp_div_0_q, __sturm_tmp_div_0_r;\n"
        "divide_oop(a, b, __sturm_tmp_div_0_q, __sturm_tmp_div_0_r);\n"
        "swap(a, __sturm_tmp_div_0_q);\n",
        "__sturm_tmp_div_0_q", "__sturm_tmp_div_0_r");
}
void test_mod() {
    // %= shares divide_oop; swap target is the REMAINDER.
    check_one(LossyOpKind::ModAssign,
        "qint __sturm_tmp_mod_0_q, __sturm_tmp_mod_0_r;\n"
        "divide_oop(a, b, __sturm_tmp_mod_0_q, __sturm_tmp_mod_0_r);\n"
        "swap(a, __sturm_tmp_mod_0_r);\n",
        "__sturm_tmp_mod_0_r", "__sturm_tmp_mod_0_q");
}

// Counter scheme — monotonic across opcodes (LO-0.3 fixture pattern).
void test_counter_monotonic_across_opcodes() {
    FreshNameAllocator alloc;
    LossyEmission a = emit_lossy_forward_text(
        LossyOpKind::AndAssign, "b", "c", alloc);
    LossyEmission m = emit_lossy_forward_text(
        LossyOpKind::MulAssign, "a", "b", alloc);
    CHECK_EQ_STR(a.swap_target_name, std::string("__sturm_tmp_and_0"));
    CHECK_EQ_STR(m.swap_target_name, std::string("__sturm_tmp_mul_1"));
}

void test_counter_div_consumes_one_slot() {
    FreshNameAllocator alloc;
    LossyEmission d = emit_lossy_forward_text(
        LossyOpKind::DivAssign, "a", "b", alloc);
    LossyEmission a = emit_lossy_forward_text(
        LossyOpKind::AndAssign, "x", "y", alloc);
    CHECK_EQ_STR(d.swap_target_name, std::string("__sturm_tmp_div_0_q"));
    CHECK_EQ_STR(a.swap_target_name, std::string("__sturm_tmp_and_1"));
}

void test_operand_names_threaded() {
    FreshNameAllocator alloc;
    LossyEmission em = emit_lossy_forward_text(
        LossyOpKind::AndAssign, "lhs_var", "rhs_var", alloc);
    CHECK(em.text.find("and_oop(lhs_var, rhs_var, __sturm_tmp_and_0);")
          != std::string::npos);
    CHECK(em.text.find("swap(lhs_var, __sturm_tmp_and_0);")
          != std::string::npos);
}

// AST-driven — emitter consumes LO-2a's LossyOpHit cleanly.
void test_ast_driven_and() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) { a &= b; }\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    LossyEmission em = emit_lossy_forward(hits[0], alloc);
    CHECK_EQ_STR(em.text,
        std::string("qint __sturm_tmp_and_0;\n"
                    "and_oop(a, b, __sturm_tmp_and_0);\n"
                    "swap(a, __sturm_tmp_and_0);\n"));
    CHECK_EQ_STR(em.swap_target_name, std::string("__sturm_tmp_and_0"));
}

void test_ast_driven_all_five_distinct() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a *= b;\n    a /= b;\n    a %= b;\n"
        "    a &= b;\n    a |= b;\n"
        "}\n");
    CHECK(hits.size() == 5);
    if (hits.size() != 5) return;
    FreshNameAllocator alloc;
    std::vector<std::string> seen;
    for (const auto& h : hits) {
        LossyEmission em = emit_lossy_forward(h, alloc);
        CHECK(!em.swap_target_name.empty());
        for (const auto& prev : seen)
            CHECK(prev != em.swap_target_name);
        seen.push_back(em.swap_target_name);
    }
    CHECK(seen.size() == 5);
}

// Defensive — empty operand names yield empty text.
void test_empty_lhs() {
    FreshNameAllocator alloc;
    LossyEmission em = emit_lossy_forward_text(
        LossyOpKind::AndAssign, "", "b", alloc);
    CHECK(em.text.empty());
    CHECK(em.swap_target_name.empty());
}

void test_empty_rhs() {
    FreshNameAllocator alloc;
    LossyEmission em = emit_lossy_forward_text(
        LossyOpKind::MulAssign, "a", "", alloc);
    CHECK(em.text.empty());
    CHECK(em.swap_target_name.empty());
}

} // namespace

int main() {
    test_mul();
    test_and();
    test_or();
    test_div();
    test_mod();
    test_counter_monotonic_across_opcodes();
    test_counter_div_consumes_one_slot();
    test_operand_names_threaded();
    test_ast_driven_and();
    test_ast_driven_all_five_distinct();
    test_empty_lhs();
    test_empty_rhs();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
