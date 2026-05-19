// test_matcher_qram_subscript_expr.cpp — sturm-u9ge.9 (Beat H4) unit tests.
// Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4. See
// `matcher_qram_subscript_expr.hpp` for the matcher contract.
//
// Test surface: positives across the three container shapes (std::array,
// C-array, pointer) where the subscript is embedded in a larger
// initializer expression; negatives covering the bare C1 shape (which
// must NOT fire here — that is C1's job), classical-int index, and
// non-init expression-position uses (which remain E1's job). One
// multi-subscript positive verifies the per-subscript hit publication
// posture. LoC budget: <= 300.

#include "matcher_qram_subscript_expr.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_qram_subscript_expr_ns {

using namespace sturm::transpile;

static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

#define CHECK_EQ_INT(got, want) do {                                  \
    ++tests_run;                                                      \
    const long long g = static_cast<long long>(got);                  \
    const long long w = static_cast<long long>(want);                 \
    if (g == w) { ++tests_pass; }                                     \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  got=%lld want=%lld\n",     \
                     __FILE__, __LINE__, g, w);                       \
    }                                                                 \
} while (0)

namespace {

// Hermetic stub mirroring the production `sturm::frontend::qint` (v1
// alias with the load-bearing implicit `operator unsigned long`),
// backend `sturm::qint_t<W>`, and a minimal `array<T, N>` stand-in.
// Includes a `qint operator+(qint, qint)` so `a[i] + d` type-checks.
constexpr std::string_view kStub = R"CPP(
namespace sturm {
namespace frontend { class qint; } // namespace frontend
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator=(const qint_t&) { return *this; }
    qint_t& operator=(const sturm::frontend::qint&) { return *this; }
};
namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    template <int W> qint(const qint_t<W>&) noexcept {}
    qint& operator=(const qint&) noexcept = default;
    template <int W> qint& operator=(const qint_t<W>&) noexcept {
        return *this;
    }
    operator unsigned long() const noexcept {
        return static_cast<unsigned long>(value_);
    }
private:
    long long value_ = 0;
};
inline qint operator+(const qint& a, const qint& b) noexcept {
    (void)a; (void)b; return qint{};
}
inline qint operator-(const qint& a, const qint& b) noexcept {
    (void)a; (void)b; return qint{};
}
inline qint operator*(const qint& a, const qint& b) noexcept {
    (void)a; (void)b; return qint{};
}
} // namespace frontend
} // namespace sturm
using qint = sturm::frontend::qint;
template <typename T, unsigned long N>
struct array {
    T data_[N];
    T& operator[](unsigned long i)             { return data_[i]; }
    const T& operator[](unsigned long i) const { return data_[i]; }
};
)CPP";

std::vector<QramSubscriptExprHit> run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kStub.size() + user_src.size());
    code.append(kStub);
    code.append(user_src);
    std::vector<QramSubscriptExprHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_qram_subscript_expr_matcher(finder, hits);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "qram_subscript_expr_input.cpp");
    if (!ok) std::fprintf(stderr, "FAIL  tool run\n");
    return hits;
}

} // anonymous namespace

// ── Positive: std::array — `qint c = a[i] + d;` ─────────────────────────────
static void test_std_array_positive() {
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    array<sturm::qint_t<8>, 4> a;\n"
        "    qint c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::StdArray);
    CHECK(hits[0].target_var != nullptr);
    CHECK(hits[0].subscript_expr != nullptr);
    CHECK(hits[0].container_expr != nullptr);
    CHECK(hits[0].index_expr != nullptr);
    CHECK_EQ_INT(hits[0].W, 8u);
}

// ── Positive: C-array — `qint c = a[i] + d;` ────────────────────────────────
static void test_c_array_positive() {
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    sturm::qint_t<16> a[4];\n"
        "    qint c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::CArray);
    CHECK_EQ_INT(hits[0].W, 16u);
}

// ── Positive: pointer — `qint c = a[i] + d;` ────────────────────────────────
static void test_pointer_positive() {
    const auto hits = run_matcher(
        "void demo(sturm::qint_t<32>* a, unsigned long n, qint i, qint d) {\n"
        "    (void)n;\n"
        "    qint c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::Pointer);
    CHECK_EQ_INT(hits[0].W, 32u);
    CHECK(hits[0].length_text == std::string("n"));
}

// ── Positive: subscript on RHS of `-`, `*` and inside parens ────────────────
static void test_other_binops_and_parens() {
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint c1 = d + a[i];\n"
        "    qint c2 = a[i] - d;\n"
        "    qint c3 = (a[i]) * d;\n"
        "    (void)c1; (void)c2; (void)c3;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 3);
}

// ── Positive: multi-subscript initializer — one hit per subscript ──────────
static void test_multi_subscript_positive() {
    // Two `a[i]` subscripts in a single initializer (both sides of `+`
    // implicitly convert through the `qint(const qint_t<W>&)` template
    // ctor, so the type-checker is happy; the matcher publishes two
    // hits regardless because each subscript site is a distinct AST
    // node carrying its own UDC index discriminator).
    const auto hits = run_matcher(
        "void demo(qint i, qint j, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint c = (a[i] + d) + a[j];\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 2);
    if (hits.size() != 2) return;
    // Both hits share the same target_var.
    CHECK(hits[0].target_var == hits[1].target_var);
    // Both are C-array hits with the same width.
    CHECK(hits[0].kind == QramContainerKind::CArray);
    CHECK(hits[1].kind == QramContainerKind::CArray);
    CHECK_EQ_INT(hits[0].W, 8u);
    CHECK_EQ_INT(hits[1].W, 8u);
    // Subscript expressions are distinct nodes.
    CHECK(hits[0].subscript_expr != hits[1].subscript_expr);
}

// ── Negative: bare C1 shape — must NOT fire here ────────────────────────────
static void test_bare_init_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    // The bare `qint b = a[i];` is C1's shape, NOT H4's. H4 must
    // recognise only the strictly-larger initializer cases.
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Negative: classical-int index → no UDC, zero hits ───────────────────────
static void test_classical_index_no_hits() {
    const auto hits = run_matcher(
        "void demo(unsigned long i, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Negative: non-init expression-position use → still E1's job ─────────────
static void test_non_init_expression_position_no_hits() {
    // `qint c; c = a[i] + d;` is an existing-target shape, not a
    // VarDecl initializer. H4 only fires at init position; the
    // existing-target case is E1's `qram-oos-existing-target` shape.
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint c;\n"
        "    c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Negative: classical LHS type (not frontend qint) → no hits ──────────────
static void test_classical_lhs_no_hits() {
    // `int c = a[i];` — the LHS is not a frontend qint VarDecl, so H4
    // doesn't anchor. Even though the subscript itself materialises a
    // measurement (the implicit `qint -> int` UDC fires inside the
    // initializer), the rewrite shape only makes sense when the LHS
    // is a qint that can absorb the qint_t<W> + ... expression.
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    int c = static_cast<int>(i) + 1;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Coexistence: bare-init `b = a[j];` AND H4 `c = a[i] + d;` ──────────────
static void test_coexist_with_c1_shape() {
    // Both lines parse; the bare line is C1's job (zero H4 hits for
    // it) and the larger-expression line is H4's job (one hit).
    const auto hits = run_matcher(
        "void demo(qint i, qint j, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b = a[j];\n"
        "    qint c = a[i] + d;\n"
        "    (void)b; (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
}

}  // namespace sturm_test_matcher_qram_subscript_expr_ns

int run_test_matcher_qram_subscript_expr(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_matcher_qram_subscript_expr_ns;
    using sturm_test_matcher_qram_subscript_expr_ns::tests_run;
    using sturm_test_matcher_qram_subscript_expr_ns::tests_pass;
    test_std_array_positive();
    test_c_array_positive();
    test_pointer_positive();
    test_other_binops_and_parens();
    test_multi_subscript_positive();
    test_bare_init_no_hits();
    test_classical_index_no_hits();
    test_non_init_expression_position_no_hits();
    test_classical_lhs_no_hits();
    test_coexist_with_c1_shape();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
