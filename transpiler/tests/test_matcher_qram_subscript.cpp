// test_matcher_qram_subscript.cpp — sturm-u9ge.12 (Beat C1) unit tests.
// Plan §6 / Beat C1; PRD §7. See `matcher_qram_subscript.hpp` for the
// matcher contract. Test surface: three positives (one per container
// shape, populating `kind` + `W`), negatives for classical-size_t
// index, existing-target `b = a[i];`, expression-position read, and
// write `a[i] = b;`. LoC budget: <= 300.

#include "matcher_qram_subscript.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
// alias with the load-bearing implicit `operator size_t`) and backend
// `sturm::qint_t<W>`. A lightweight `array<T, N>` stand-in covers the
// `std::array::operator[]` shape without dragging in libc++.
constexpr std::string_view kQramSubscriptStub = R"CPP(
namespace sturm {
namespace frontend {
class qint;
} // namespace frontend
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

std::vector<QramSubscriptHit> run_on_code(std::string_view code,
                                          std::string_view filename) {
    std::vector<QramSubscriptHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_qram_subscript_matcher(finder, hits);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), std::string(code), args, std::string(filename));
    if (!ok) std::fprintf(stderr, "FAIL  tool run on %.*s\n",
                          (int)filename.size(), filename.data());
    return hits;
}

std::vector<QramSubscriptHit> run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQramSubscriptStub.size() + user_src.size());
    code.append(kQramSubscriptStub);
    code.append(user_src);
    return run_on_code(code, "qram_subscript_input.cpp");
}

#ifdef STURM_QRAM_SUBSCRIPT_FIXTURES_DIR
std::vector<QramSubscriptHit> run_matcher_on_fixture(std::string_view name) {
    std::filesystem::path root(STURM_QRAM_SUBSCRIPT_FIXTURES_DIR);
    std::ifstream in(root / std::string(name), std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "FAIL  fixture %.*s missing\n",
                     (int)name.size(), name.data());
        return {};
    }
    std::ostringstream oss; oss << in.rdbuf();
    return run_on_code(oss.str(), name);
}
#endif

} // anonymous namespace

// ── Positive: std::array<qint_t<W>, N> ─────────────────────────────────────
static void test_std_array_positive() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    array<sturm::qint_t<8>, 4> a;\n"
        "    qint b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::StdArray);
    CHECK(hits[0].target_var != nullptr);
    CHECK(hits[0].container_expr != nullptr);
    CHECK(hits[0].index_expr != nullptr);
    CHECK_EQ_INT(hits[0].W, 8u);
}

// ── Positive: C-array `qint_t<W>[N]` ────────────────────────────────────────
static void test_c_array_positive() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<16> a[4];\n"
        "    qint b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::CArray);
    CHECK(hits[0].target_var != nullptr);
    CHECK(hits[0].container_expr != nullptr);
    CHECK(hits[0].index_expr != nullptr);
    CHECK_EQ_INT(hits[0].W, 16u);
}

// ── Positive: pointer `qint_t<W>*` ─────────────────────────────────────────
static void test_pointer_positive() {
    const auto hits = run_matcher(
        "void demo(sturm::qint_t<32>* a, qint i) {\n"
        "    qint b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::Pointer);
    CHECK(hits[0].target_var != nullptr);
    CHECK(hits[0].container_expr != nullptr);
    CHECK(hits[0].index_expr != nullptr);
    CHECK_EQ_INT(hits[0].W, 32u);
}

// ── Negative: classical size_t index → no UDC, zero hits ───────────────────
static void test_classical_index_no_hits() {
    const auto hits = run_matcher(
        "void demo(unsigned long i) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Negative: existing-target `b = a[i];` → zero hits (PRD §9 row 1) ───────
static void test_existing_target_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint b;\n"
        "    b = a[i];\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Negative: subscript inside larger expression (PRD §9 row 4) ────────────
static void test_expression_position_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i, qint d) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    qint c = a[i] + d;\n"
        "    (void)c;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Negative: write `a[i] = b;` → zero hits (E1's job) ─────────────────────
static void test_write_no_hits() {
    const auto hits = run_matcher(
        "void demo(qint i, qint b) {\n"
        "    sturm::qint_t<8> a[4];\n"
        "    a[i] = b;\n"
        "    (void)b;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Coexistence: two in-scope reads in the same TU → two hits ──────────────
static void test_two_reads_two_hits() {
    const auto hits = run_matcher(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8>  a8[4];\n"
        "    sturm::qint_t<16> a16[4];\n"
        "    qint b1 = a8[i];\n"
        "    qint b2 = a16[i];\n"
        "    (void)b1; (void)b2;\n"
        "}\n");
    CHECK_EQ_INT(hits.size(), 2);
    if (hits.size() != 2) return;
    bool saw8  = false;
    bool saw16 = false;
    for (const auto& h : hits) {
        if (h.W == 8u)  saw8  = true;
        if (h.W == 16u) saw16 = true;
        // Both are C-array shapes here.
        CHECK(h.kind == QramContainerKind::CArray);
    }
    CHECK(saw8);
    CHECK(saw16);
}

// ── Fixture-driven positives (one per container shape) ─────────────────────
#ifdef STURM_QRAM_SUBSCRIPT_FIXTURES_DIR

static void test_fixture_std_array() {
    const auto hits = run_matcher_on_fixture("qram_read_std_array.cpp");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::StdArray);
    CHECK_EQ_INT(hits[0].W, 8u);
}

static void test_fixture_c_array() {
    const auto hits = run_matcher_on_fixture("qram_read_c_array.cpp");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::CArray);
    CHECK_EQ_INT(hits[0].W, 16u);
}

static void test_fixture_pointer() {
    const auto hits = run_matcher_on_fixture("qram_read_pointer.cpp");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == QramContainerKind::Pointer);
    CHECK_EQ_INT(hits[0].W, 32u);
}

#endif // STURM_QRAM_SUBSCRIPT_FIXTURES_DIR

int run_test_matcher_qram_subscript(int /*argc*/, char** /*argv*/) {
    test_std_array_positive();
    test_c_array_positive();
    test_pointer_positive();
    test_classical_index_no_hits();
    test_existing_target_no_hits();
    test_expression_position_no_hits();
    test_write_no_hits();
    test_two_reads_two_hits();
#ifdef STURM_QRAM_SUBSCRIPT_FIXTURES_DIR
    test_fixture_std_array();
    test_fixture_c_array();
    test_fixture_pointer();
#endif
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
