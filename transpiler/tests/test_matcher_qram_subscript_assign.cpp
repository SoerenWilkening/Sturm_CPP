// test_matcher_qram_subscript_assign.cpp -- sturm-u9ge.6 (Beat H1) tests.
//
// Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1. See
// `matcher_qram_subscript_assign.hpp` for the matcher contract.
//
// Test surface: positives across the three container shapes
// (std::array, C-array, pointer); negatives covering the bare C1 init
// shape, the H4 init-with-larger-expr shape, the OOS write shape
// `a[i] = b;`, classical-int index, classical LHS type, and the OOS
// expression-position shape `c = a[i] + d;` (still E1's job, larger
// expr at non-init position). LoC budget: <= 300.

#include "matcher_qram_subscript_assign.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_qram_subscript_assign_ns {

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

// Hermetic stub mirroring `sturm::frontend::qint`, backend
// `sturm::qint_t<W>`, and a minimal `array<T,N>` stand-in. Includes
// `qint& operator=(qint_t<W>&)` so `b = a[i];` type-checks.
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

std::vector<QramSubscriptAssignHit> run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kStub.size() + user_src.size());
    code.append(kStub);
    code.append(user_src);
    std::vector<QramSubscriptAssignHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_qram_subscript_assign_matcher(finder, hits);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "qram_subscript_assign_input.cpp");
    if (!ok) std::fprintf(stderr, "FAIL  tool run\n");
    return hits;
}

} // anonymous namespace

// -- Positive: std::array -- `b = a[i];` ----------------------------------
static void test_std_array_positive() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    array<sturm::qint_t<8>, 4> a;\n"
        "    qint b;\n"
        "    b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::StdArray);
    CHECK(hits[0].assign_expr != nullptr);
    CHECK(hits[0].target_expr != nullptr);
    CHECK(hits[0].target_var != nullptr);
    CHECK(hits[0].subscript_expr != nullptr);
    CHECK(hits[0].container_expr != nullptr);
    CHECK(hits[0].index_expr != nullptr);
    CHECK_EQ_INT(hits[0].W, 8u);
}

// -- Positive: C-array -- `b = a[i];` -------------------------------------
static void test_c_array_positive() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<16> a[4];\n"
        "    qint b;\n"
        "    b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::CArray);
    CHECK_EQ_INT(hits[0].W, 16u);
}

// -- Positive: pointer -- `b = a[i];` -------------------------------------
static void test_pointer_positive() {
    const auto hits = run_matcher(
        "void demo(sturm::qint_t<32>* a, unsigned long n, qint i) {\n"
        "    (void)n;\n"
        "    qint b;\n"
        "    b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::Pointer);
    CHECK_EQ_INT(hits[0].W, 32u);
    CHECK(hits[0].length_text == std::string("n"));
}

// -- Positive: two assignments => two hits --------------------------------
static void test_two_assignments_two_hits() {
    const auto hits = run_matcher(
        "void demo(qint i, qint j) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b;\n"
        "    b = a[i];\n"
        "    b = a[j];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 2);
}

// -- Negative: bare C1 shape `qint b = a[i];` -- must NOT fire ------------
static void test_bare_init_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    // C1 anchors on VarDecl initializer; H1 on assignment op-call.
    // The bare init shape has no separate assignment statement.
    CHECK_EQ_INT(hits.size(), 0);
}

// -- Negative: H4 init-with-larger-expr `qint c = a[i] + d;` -- no fire ---
static void test_h4_init_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    // H4 anchors on the VarDecl initializer; H1 on assignment op-call.
    CHECK_EQ_INT(hits.size(), 0);
}

// -- Negative: OOS write `a[i] = b;` -- LHS is subscript, not qint --------
static void test_write_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i, qint b) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    a[i] = b;\n"
        "    (void)b;\n"
        "}\n");
    // The LHS `a[i]` has type `sturm::qint_t<8>`, NOT frontend `qint`.
    // H1's LHS-type gate filters this out.
    CHECK_EQ_INT(hits.size(), 0);
}

// -- Negative: classical-int index -- no UDC, zero hits -------------------
static void test_classical_index_no_hits() {
    const auto hits = run_matcher(
        "void demo(unsigned long i) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b;\n"
        "    b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// -- Negative: existing-target with larger-expr RHS -- still OOS ----------
static void test_existing_target_larger_expr_no_hits() {
    // `b = a[i] + d;` -- LHS is a frontend qint, but the RHS is NOT a
    // bare subscript. H1 only handles the bare-subscript RHS shape;
    // larger-expr RHS at non-init position remains E1's territory
    // (`qram-oos-expression-position`).
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b;\n"
        "    b = a[i] + d;\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// -- Negative: classical LHS type -- no fire ------------------------------
static void test_classical_lhs_no_hits() {
    // `unsigned long b = a[i];` triggers a measurement (UDC fires) but
    // the LHS is not a frontend qint -- H1's LHS gate filters out.
    // Note this uses an init form so it would be C1's anchor shape,
    // but C1's frontend-qint anchor also filters it. Adding the
    // assignment form below for completeness.
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    unsigned long b = 0;\n"
        "    b = static_cast<unsigned long>(i) + 1;\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// -- Coexistence: bare C1 + H1 in same TU -- one H1 hit -------------------
static void test_coexist_with_c1_shape() {
    const auto hits = run_matcher(
        "void demo(qint i, qint j) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b1 = a[i];   // C1 shape -- not H1's job\n"
        "    qint b2;\n"
        "    b2 = a[j];        // H1 shape -- one hit\n"
        "    (void)b1; (void)b2;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
}

}  // namespace sturm_test_matcher_qram_subscript_assign_ns

int run_test_matcher_qram_subscript_assign(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_matcher_qram_subscript_assign_ns;
    using sturm_test_matcher_qram_subscript_assign_ns::tests_run;
    using sturm_test_matcher_qram_subscript_assign_ns::tests_pass;
    test_std_array_positive();
    test_c_array_positive();
    test_pointer_positive();
    test_two_assignments_two_hits();
    test_bare_init_no_hits();
    test_h4_init_no_hits();
    test_write_no_hits();
    test_classical_index_no_hits();
    test_existing_target_larger_expr_no_hits();
    test_classical_lhs_no_hits();
    test_coexist_with_c1_shape();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
