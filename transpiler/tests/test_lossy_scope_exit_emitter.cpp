// test_lossy_scope_exit_emitter.cpp — LO-2c (sturm-hbwr) unit tests.
// Pure-string per-opcode goldens + AST-driven LIFO/scope-grouping
// covering all five lossy operators. Main-outer-scope suppression is
// LO-2d's concern.

#include "lossy_scope_exit_emitter.hpp"

#include "fresh_names.hpp"
#include "lossy_rewrite_emitter.hpp"
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
namespace sturm { template <int W> class qint_t {
public: qint_t() {} qint_t(const qint_t&) {}
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
    qint_t& operator%=(const qint_t&) { return *this; }
    qint_t& operator&=(const qint_t&) { return *this; }
    qint_t& operator|=(const qint_t&) { return *this; }
};}
using qint_t = sturm::qint_t<2>;
)CPP";

std::vector<LossyOpHit> run_lossy_matcher(std::string_view user_src) {
    std::string code;
    code.append(kLossyStub).append(user_src);
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
#define CHECK(c) do { ++tests_run; if (c) ++tests_pass; else \
    std::fprintf(stderr,"FAIL  %s:%d  %s\n",__FILE__,__LINE__,#c);} while(0)
#define CHECK_EQ_STR(g, w) do { ++tests_run; if ((g)==(w)) ++tests_pass; \
    else std::fprintf(stderr,                                            \
        "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",       \
        __FILE__, __LINE__,                                              \
        std::string(g).c_str(), std::string(w).c_str()); } while (0)

// Per-opcode pure-string cleanup goldens.
void check_single(LossyOpKind k, const char* tmp_root, const char* dsl) {
    const std::string tmp = std::string("__sturm_tmp_") + tmp_root;
    auto em = emit_lossy_cleanup_text(k, "a", "b", tmp, "");
    CHECK_EQ_STR(em.text,
        "swap(a, " + tmp + ");\n"
        "sturm::invert<&::sturm::" + dsl + ">()"
        "(a, b, " + tmp + ");\n");
    CHECK(em.opcode == k);
}
void test_cleanup_mul() { check_single(LossyOpKind::MulAssign,
                                       "mul_0", "lib_mul_dsl"); }
void test_cleanup_and() { check_single(LossyOpKind::AndAssign,
                                       "and_0", "lib_c_AND_dsl"); }
void test_cleanup_or()  { check_single(LossyOpKind::OrAssign,
                                       "or_0",  "lib_or_dsl"); }
// /= : swap target is q, invert args (lhs, rhs, q, r).
void test_cleanup_div() {
    auto em = emit_lossy_cleanup_text(LossyOpKind::DivAssign, "a", "b",
        "__sturm_tmp_div_0_q", "__sturm_tmp_div_0_r");
    CHECK_EQ_STR(em.text,
        "swap(a, __sturm_tmp_div_0_q);\n"
        "sturm::invert<&::sturm::lib_div_dsl>()"
        "(a, b, __sturm_tmp_div_0_q, __sturm_tmp_div_0_r);\n");
}
// %= : swap target is r, invert args still (lhs, rhs, q, r).
void test_cleanup_mod() {
    auto em = emit_lossy_cleanup_text(LossyOpKind::ModAssign, "a", "b",
        "__sturm_tmp_mod_0_r", "__sturm_tmp_mod_0_q");
    CHECK_EQ_STR(em.text,
        "swap(a, __sturm_tmp_mod_0_r);\n"
        "sturm::invert<&::sturm::lib_div_dsl>()"
        "(a, b, __sturm_tmp_mod_0_q, __sturm_tmp_mod_0_r);\n");
}

// Defensive: empty operand or empty swap target → empty text.
void test_cleanup_defensive() {
    CHECK(emit_lossy_cleanup_text(LossyOpKind::AndAssign,
        "", "b", "__sturm_tmp_and_0", "").text.empty());
    CHECK(emit_lossy_cleanup_text(LossyOpKind::MulAssign,
        "a", "", "__sturm_tmp_mul_0", "").text.empty());
    CHECK(emit_lossy_cleanup_text(LossyOpKind::OrAssign,
        "a", "b", "", "").text.empty());
    // Divide kernel needs aux_tmp too — empty aux ⇒ empty text.
    CHECK(emit_lossy_cleanup_text(LossyOpKind::DivAssign,
        "a", "b", "__sturm_tmp_div_0_q", "").text.empty());
}

std::vector<LossyEmission>
forward_for(const std::vector<LossyOpHit>& hits, FreshNameAllocator& alloc) {
    std::vector<LossyEmission> out; out.reserve(hits.size());
    for (const auto& h : hits) out.push_back(emit_lossy_forward(h, alloc));
    return out;
}

void test_grouper_single_hit() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) { a &= b; }\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    auto fwd = forward_for(hits, alloc);
    auto blocks = group_cleanups_by_block(hits, fwd);
    CHECK(blocks.size() == 1);
    if (blocks.empty()) return;
    CHECK_EQ_STR(blocks[0].text,
        "swap(a, __sturm_tmp_and_0);\n"
        "sturm::invert<&::sturm::lib_c_AND_dsl>()"
        "(a, b, __sturm_tmp_and_0);\n");
    CHECK(blocks[0].enclosing_block == hits[0].enclosing_block);
}

// Two hits in one scope → cleanup REVERSED (LIFO). a *= b; a &= b;
// → cleanup AND first, then MUL.
void test_grouper_lifo_two_hits_same_scope() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a *= b;\n    a &= b;\n}\n");
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    FreshNameAllocator alloc;
    auto fwd = forward_for(hits, alloc);
    auto blocks = group_cleanups_by_block(hits, fwd);
    CHECK(blocks.size() == 1);
    if (blocks.empty()) return;
    const std::string want =
        "swap(a, __sturm_tmp_and_1);\n"
        "sturm::invert<&::sturm::lib_c_AND_dsl>()"
        "(a, b, __sturm_tmp_and_1);\n"
        "swap(a, __sturm_tmp_mul_0);\n"
        "sturm::invert<&::sturm::lib_mul_dsl>()"
        "(a, b, __sturm_tmp_mul_0);\n";
    CHECK_EQ_STR(blocks[0].text, want);
    CHECK(blocks[0].text.find("lib_c_AND_dsl") <
          blocks[0].text.find("lib_mul_dsl"));
}

// All five ops in one scope → LIFO. |= < &= < %= < /= < *= in output.
void test_grouper_lifo_all_five_same_scope() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a *= b;\n    a /= b;\n    a %= b;\n"
        "    a &= b;\n    a |= b;\n}\n");
    CHECK(hits.size() == 5);
    if (hits.size() != 5) return;
    FreshNameAllocator alloc;
    auto fwd = forward_for(hits, alloc);
    auto blocks = group_cleanups_by_block(hits, fwd);
    CHECK(blocks.size() == 1);
    if (blocks.empty()) return;
    const std::string& t = blocks[0].text;
    auto p_or  = t.find("lib_or_dsl");
    auto p_and = t.find("lib_c_AND_dsl");
    auto p_mul = t.find("lib_mul_dsl");
    auto p_div0 = t.find("lib_div_dsl");
    auto p_div1 = t.find("lib_div_dsl", p_div0 + 1);
    CHECK(p_or != std::string::npos && p_and != std::string::npos);
    CHECK(p_mul != std::string::npos);
    CHECK(p_div0 != std::string::npos && p_div1 != std::string::npos);
    CHECK(p_or < p_and);
    CHECK(p_and < p_div0);
    CHECK(p_div0 < p_div1);
    CHECK(p_div1 < p_mul);
}

// Nested scopes → cleanup at each scope's brace, no cross-bleed.
void test_grouper_nested_scopes() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) {\n"
        "    a *= b;\n"
        "    {\n        a &= b;\n    }\n}\n");
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    FreshNameAllocator alloc;
    auto fwd = forward_for(hits, alloc);
    auto blocks = group_cleanups_by_block(hits, fwd);
    CHECK(blocks.size() == 2);
    if (blocks.size() != 2) return;
    int outer = -1, inner = -1;
    for (int i = 0; i < 2; ++i) {
        if (blocks[i].text.find("lib_mul_dsl") != std::string::npos) outer = i;
        if (blocks[i].text.find("lib_c_AND_dsl") != std::string::npos) inner = i;
    }
    CHECK(outer != -1 && inner != -1 && outer != inner);
    if (outer == -1 || inner == -1) return;
    CHECK(blocks[outer].text.find("lib_c_AND_dsl") == std::string::npos);
    CHECK(blocks[inner].text.find("lib_mul_dsl") == std::string::npos);
    CHECK(blocks[outer].enclosing_block != blocks[inner].enclosing_block);
}

// AST-driven: emit_lossy_cleanup() carries enclosing_block through.
void test_ast_driven_single() {
    auto hits = run_lossy_matcher(
        "void demo(qint_t a, qint_t b) { a |= b; }\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    auto cu = emit_lossy_cleanup(hits[0], emit_lossy_forward(hits[0], alloc));
    CHECK_EQ_STR(cu.text,
        "swap(a, __sturm_tmp_or_0);\n"
        "sturm::invert<&::sturm::lib_or_dsl>()"
        "(a, b, __sturm_tmp_or_0);\n");
    CHECK(cu.opcode == LossyOpKind::OrAssign);
    CHECK(cu.enclosing_block == hits[0].enclosing_block);
}

// Mismatched vector sizes → empty result.
void test_grouper_size_mismatch_is_empty() {
    std::vector<LossyOpHit> hits(1);
    std::vector<LossyEmission> fwd_empty;
    CHECK(group_cleanups_by_block(hits, fwd_empty).empty());
}

} // namespace

int main() {
    test_cleanup_mul();
    test_cleanup_and();
    test_cleanup_or();
    test_cleanup_div();
    test_cleanup_mod();
    test_cleanup_defensive();
    test_grouper_single_hit();
    test_grouper_lifo_two_hits_same_scope();
    test_grouper_lifo_all_five_same_scope();
    test_grouper_nested_scopes();
    test_ast_driven_single();
    test_grouper_size_mismatch_is_empty();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
