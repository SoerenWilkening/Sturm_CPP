// test_lossy_nested_rewrite.cpp — LO-2e (sturm-rry6) unit tests.
//
// Asserts the matcher fires on `a *= (b & c)` and `emit_nested_lossy`
// produces the depth-first forward + LIFO cleanup pair described by
// PRD §11. Also asserts the matcher does NOT fire on the bare LO-2a
// shape (`a *= b`) so the two matcher families remain disjoint.

#include "lossy_nested_rewrite.hpp"
#include "fresh_names.hpp"
#include "matcher_lossy_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_lossy_nested_rewrite_ns {

using namespace sturm::transpile;

namespace {

constexpr std::string_view kStub = R"CPP(
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
template <int W>
inline qint_t<W> operator&(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator|(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
} // namespace sturm
using qint_t = sturm::qint_t<2>;
)CPP";

std::vector<NestedLossyHit> run_matcher(std::string_view src) {
    std::string code;
    code.append(kStub).append(src);
    std::vector<NestedLossyHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_nested_lossy_matcher(finder, hits);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) std::fprintf(stderr, "FAIL  tool run returned false\n");
    return hits;
}

// Also run the LO-2a matcher to verify it is structurally disjoint —
// a nested call must NOT also fire LO-2a.
std::vector<LossyOpHit> run_bare(std::string_view src) {
    std::string code;
    code.append(kStub).append(src);
    std::vector<LossyOpHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_lossy_op_matcher(finder, hits);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    (void)clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    return hits;
}

int t_run = 0, t_pass = 0;
#define CHECK(c) do { ++t_run; if (c) ++t_pass; else \
    std::fprintf(stderr,"FAIL  %s:%d  %s\n",__FILE__,__LINE__,#c); } while (0)
#define CHECK_EQ_STR(g, w) do { ++t_run; if ((g) == (w)) ++t_pass; else \
    std::fprintf(stderr,                                                 \
        "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",       \
        __FILE__, __LINE__, std::string(g).c_str(),                      \
        std::string(w).c_str()); } while (0)

// Matcher fires on `a *= (b & c)` exactly once.
void test_matches_mul_and() {
    auto hits = run_matcher("void demo(qint_t a, qint_t b, qint_t c) {\n"
                            "    a *= (b & c);\n}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].outer_kind == LossyOpKind::MulAssign);
    CHECK(hits[0].inner_kind == LossyOpKind::AndAssign);
    CHECK_EQ_STR(hits[0].lhs_name, "a");
    CHECK_EQ_STR(hits[0].inner_lhs_name, "b");
    CHECK_EQ_STR(hits[0].inner_rhs_name, "c");
    CHECK(hits[0].enclosing_block != nullptr);
    CHECK(hits[0].outer_call != nullptr);
}

// Matcher does NOT fire on the bare LO-2a shape (no nesting).
void test_does_not_match_bare() {
    auto hits = run_matcher("void demo(qint_t a, qint_t b) {\n"
                            "    a *= b;\n}\n");
    CHECK(hits.empty());
    // And LO-2a still fires on the bare shape — disjointness check.
    auto bare = run_bare("void demo(qint_t a, qint_t b) {\n"
                         "    a *= b;\n}\n");
    CHECK(bare.size() == 1);
}

// LO-2a does NOT fire on the nested shape — disjointness from the
// other side (this is the precondition for LO-2e taking over).
void test_lo2a_does_not_match_nested() {
    auto bare = run_bare("void demo(qint_t a, qint_t b, qint_t c) {\n"
                         "    a *= (b & c);\n}\n");
    CHECK(bare.empty());
}

// Counter monotonicity: inner gets `_0`, outer gets `_1`.
void test_emit_counter_monotonic() {
    auto hits = run_matcher("void demo(qint_t a, qint_t b, qint_t c) {\n"
                            "    a *= (b & c);\n}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    auto em = emit_nested_lossy(hits[0], alloc);
    CHECK_EQ_STR(em.inner_tmp_name, "__sturm_tmp_and_0");
    CHECK_EQ_STR(em.outer_tmp_name, "__sturm_tmp_mul_1");
    // Inner appears lexically BEFORE outer in the forward text.
    const auto p_in  = em.forward_text.find("__sturm_tmp_and_0;");
    const auto p_out = em.forward_text.find("__sturm_tmp_mul_1;");
    CHECK(p_in != std::string::npos && p_out != std::string::npos);
    CHECK(p_in < p_out);
}

// Forward shape: inner allocate (no swap) + outer full triplet.
void test_forward_text_shape() {
    auto hits = run_matcher("void demo(qint_t a, qint_t b, qint_t c) {\n"
                            "    a *= (b & c);\n}\n");
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    auto em = emit_nested_lossy(hits[0], alloc);
    const std::string want =
        "qint __sturm_tmp_and_0;\n"
        "and_oop(b, c, __sturm_tmp_and_0);\n"
        "qint __sturm_tmp_mul_1;\n"
        "mul_oop(a, __sturm_tmp_and_0, __sturm_tmp_mul_1);\n"
        "swap(a, __sturm_tmp_mul_1);\n";
    CHECK_EQ_STR(em.forward_text, want);
}

// Cleanup shape: outer FULL (swap + invert), then inner adjoint-only.
void test_cleanup_text_shape() {
    auto hits = run_matcher("void demo(qint_t a, qint_t b, qint_t c) {\n"
                            "    a *= (b & c);\n}\n");
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    auto em = emit_nested_lossy(hits[0], alloc);
    const std::string want =
        "swap(a, __sturm_tmp_mul_1);\n"
        "sturm::invert<&::sturm::lib_mul_dsl>()"
        "(a, __sturm_tmp_and_0, __sturm_tmp_mul_1);\n"
        "sturm::invert<&::sturm::lib_c_AND_dsl>()"
        "(b, c, __sturm_tmp_and_0);\n";
    CHECK_EQ_STR(em.cleanup_text, want);
}

// Defensive: empty operand names ⇒ empty result.
void test_emit_defensive_empty() {
    NestedLossyHit hit;
    hit.outer_kind = LossyOpKind::MulAssign;
    hit.inner_kind = LossyOpKind::AndAssign;
    // lhs_name intentionally left blank.
    FreshNameAllocator alloc;
    auto em = emit_nested_lossy(hit, alloc);
    CHECK(em.forward_text.empty() && em.cleanup_text.empty());
}

} // namespace

}  // namespace sturm_test_lossy_nested_rewrite_ns

int run_test_lossy_nested_rewrite(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_lossy_nested_rewrite_ns;
    test_matches_mul_and();
    test_does_not_match_bare();
    test_lo2a_does_not_match_nested();
    test_emit_counter_monotonic();
    test_forward_text_shape();
    test_cleanup_text_shape();
    test_emit_defensive_empty();
    std::printf("PASS: %d/%d\n", t_pass, t_run);
    return t_pass == t_run ? 0 : 1;
}
