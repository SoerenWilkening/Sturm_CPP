// test_lossy_scope_exit_emitter.cpp — LO-2c (sturm-hbwr) unit tests.
// Pure-string per-opcode goldens + AST-driven LIFO/scope-grouping
// covering all five lossy operators. LO-2d (sturm-wva7) extends with
// main-outer-scope suppression: `group_cleanups_by_block` consults the
// `is_main_outer_block` predicate when an ASTContext is supplied and
// emits an empty cleanup string for blocks that ARE main's outermost
// body. Lambdas inside main, nested CompoundStmts, and free functions
// remain unaffected — only the literal `int main()` body is suppressed.

#include "lossy_scope_exit_emitter.hpp"

#include "fresh_names.hpp"
#include "lossy_rewrite_emitter.hpp"
#include "matcher_lossy_op.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <memory>
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

// AST-driven cleanup goldens. sturm-czfi swapped the cleanup line from the
// pre-czfi `sturm::invert<&::sturm::lib_X_dsl>()` form to a direct call into
// the registered `*_oop_adj<W>` wrapper (rationale: invert<&fn<W>>'s NTTP
// type depends on W, which makes the trait specialization ill-formed; the
// runtime helper module promotes the adjoint into `sturm::` via using-decls
// so unqualified ADL on `qint_t<W>&` finds it). The AST-driven hits resolve
// W=2 off the stub `using qint_t = sturm::qint_t<2>;`, so the goldens here
// pin the post-czfi width-aware shape.
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
        "and_oop_adj(a, b, __sturm_tmp_and_0);\n");
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
        "and_oop_adj(a, b, __sturm_tmp_and_1);\n"
        "swap(a, __sturm_tmp_mul_0);\n"
        "mul_oop_adj(a, b, __sturm_tmp_mul_0);\n";
    CHECK_EQ_STR(blocks[0].text, want);
    CHECK(blocks[0].text.find("and_oop_adj") <
          blocks[0].text.find("mul_oop_adj"));
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
    auto p_or  = t.find("or_oop_adj");
    auto p_and = t.find("and_oop_adj");
    auto p_mul = t.find("mul_oop_adj");
    auto p_div0 = t.find("divide_oop_adj");
    auto p_div1 = t.find("divide_oop_adj", p_div0 + 1);
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
        if (blocks[i].text.find("mul_oop_adj") != std::string::npos) outer = i;
        if (blocks[i].text.find("and_oop_adj") != std::string::npos) inner = i;
    }
    CHECK(outer != -1 && inner != -1 && outer != inner);
    if (outer == -1 || inner == -1) return;
    CHECK(blocks[outer].text.find("and_oop_adj") == std::string::npos);
    CHECK(blocks[inner].text.find("mul_oop_adj") == std::string::npos);
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
        "or_oop_adj(a, b, __sturm_tmp_or_0);\n");
    CHECK(cu.opcode == LossyOpKind::OrAssign);
    CHECK(cu.enclosing_block == hits[0].enclosing_block);
}

// Mismatched vector sizes → empty result.
void test_grouper_size_mismatch_is_empty() {
    std::vector<LossyOpHit> hits(1);
    std::vector<LossyEmission> fwd_empty;
    CHECK(group_cleanups_by_block(hits, fwd_empty).empty());
}

// ── LO-2d (sturm-wva7) main-outer-scope suppression tests ────────────
//
// The 3-arg `group_cleanups_by_block(hits, fwd, ctx)` overload runs
// `is_main_outer_block` on each block and emits an empty cleanup string
// for the matches. The four cases below pin the predicate's edges:
//   1. main body                → suppressed
//   2. non-main free function   → emitted (regression)
//   3. lambda body inside main  → emitted (lambda body is NOT main's)
//   4. if-block inside main     → emitted (nested block is NOT main's)
//
// The harness uses a custom ASTConsumer that drives matchAST then
// invokes `group_cleanups_by_block` with the live ASTContext while the
// AST is still alive (group output is captured by reference).

struct GroupRunResult {
    std::vector<LossyOpHit> hits;
    std::vector<BlockCleanup> blocks;
};

GroupRunResult run_grouper_with_ctx(std::string_view user_src) {
    std::string code;
    code.append(kLossyStub).append(user_src);
    GroupRunResult result;
    clang::ast_matchers::MatchFinder finder;
    register_lossy_op_matcher(finder, result.hits);

    class Consumer : public clang::ASTConsumer {
    public:
        Consumer(clang::ast_matchers::MatchFinder* f, GroupRunResult* r)
            : finder_(f), result_(r) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            finder_->matchAST(ctx);
            FreshNameAllocator alloc;
            std::vector<LossyEmission> fwd;
            fwd.reserve(result_->hits.size());
            for (const auto& h : result_->hits) {
                fwd.push_back(emit_lossy_forward(h, alloc));
            }
            result_->blocks = group_cleanups_by_block(
                result_->hits, fwd, &ctx);
        }
    private:
        clang::ast_matchers::MatchFinder* finder_;
        GroupRunResult* result_;
    };
    class Action : public clang::ASTFrontendAction {
    public:
        Action(clang::ast_matchers::MatchFinder* f, GroupRunResult* r)
            : finder_(f), result_(r) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            return std::make_unique<Consumer>(finder_, result_);
        }
    private:
        clang::ast_matchers::MatchFinder* finder_;
        GroupRunResult* result_;
    };

    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        std::make_unique<Action>(&finder, &result),
        code, args, "test_input.cpp");
    if (!ok) std::fprintf(stderr, "FAIL  tool run returned false\n");
    return result;
}

// Case 1: top-level `&=` directly inside main's body → cleanup
// suppressed. The block IS still recorded so callers can see "we
// matched a hit here", but its `text` is empty.
void test_main_outer_block_suppresses_cleanup() {
    auto r = run_grouper_with_ctx(
        "int main() { qint_t a, b; a &= b; return 0; }\n");
    CHECK(r.hits.size() == 1);
    if (r.hits.empty()) return;
    CHECK(r.blocks.size() == 1);
    if (r.blocks.empty()) return;
    CHECK_EQ_STR(r.blocks[0].text, std::string());
    CHECK(r.blocks[0].enclosing_block == r.hits[0].enclosing_block);
}

// Case 2: hit inside a non-main free function → cleanup emitted.
// Regression guard against the predicate over-firing on any free
// function whose body is its outer CompoundStmt.
//
// sturm-czfi: post-czfi the cleanup line uses `and_oop_adj` directly (the
// pre-czfi `lib_c_AND_dsl` token is now gated to the legacy width-0 path).
void test_non_main_free_function_emits_cleanup() {
    auto r = run_grouper_with_ctx(
        "void demo(qint_t a, qint_t b) { a &= b; }\n");
    CHECK(r.hits.size() == 1);
    if (r.hits.empty()) return;
    CHECK(r.blocks.size() == 1);
    if (r.blocks.empty()) return;
    CHECK(!r.blocks[0].text.empty());
    CHECK(r.blocks[0].text.find("and_oop_adj") != std::string::npos);
}

// Case 3: lambda whose body sits inside main has its OWN CompoundStmt
// — a child of the closure's `operator()` CXXMethodDecl, NOT the main
// FunctionDecl. The predicate must NOT fire on the lambda body.
// This is the plan §9 risk-register edge case ("`main`-exception
// detection mis-classifies lambdas whose body is `main`'s outer
// `CompoundStmt`").
void test_lambda_inside_main_emits_cleanup() {
    auto r = run_grouper_with_ctx(
        "int main() {\n"
        "    auto inner = [](qint_t x, qint_t y) { x &= y; };\n"
        "    (void)inner;\n"
        "    return 0;\n"
        "}\n");
    CHECK(r.hits.size() == 1);
    if (r.hits.empty()) return;
    CHECK(r.blocks.size() == 1);
    if (r.blocks.empty()) return;
    CHECK(!r.blocks[0].text.empty());
    CHECK(r.blocks[0].text.find("and_oop_adj") != std::string::npos);
}

// Case 4: hit in a nested `if (cond) { … }` block inside main. The
// if-body's parent is an IfStmt, not the main FunctionDecl, so the
// predicate must NOT fire — cleanup at that nested block's brace.
void test_nested_block_inside_main_emits_cleanup() {
    auto r = run_grouper_with_ctx(
        "int main() {\n"
        "    qint_t a, b;\n"
        "    if (true) { a &= b; }\n"
        "    return 0;\n"
        "}\n");
    CHECK(r.hits.size() == 1);
    if (r.hits.empty()) return;
    CHECK(r.blocks.size() == 1);
    if (r.blocks.empty()) return;
    CHECK(!r.blocks[0].text.empty());
    CHECK(r.blocks[0].text.find("and_oop_adj") != std::string::npos);
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
    test_main_outer_block_suppresses_cleanup();
    test_non_main_free_function_emits_cleanup();
    test_lambda_inside_main_emits_cleanup();
    test_nested_block_inside_main_emits_cleanup();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
