// test_matcher_qram_oos.cpp — sturm-u9ge.16 (Beat E1) unit tests.
// Plan §8 / E1; PRD §9 / M5.

#include "matcher_qram_oos.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

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

// Hermetic stub mirroring `sturm::frontend::qint`: implicit `operator
// size_t()` (the load-bearing UDC), arithmetic + compound-assign
// surface needed for the four shapes.
constexpr std::string_view kQRamOosStub = R"CPP(
namespace sturm { namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    qint(qint&&)      noexcept = default;
    qint& operator=(const qint&) noexcept = default;
    qint& operator=(qint&&)      noexcept = default;
    ~qint()                      noexcept = default;
    operator unsigned long() const noexcept {
        return (unsigned long)value_;
    }
private:
    long long value_ = 0;
};
inline qint operator+(const qint& a, const qint& b) noexcept {
    (void)a; (void)b; return qint{};
}
inline qint& operator+=(qint& a, const qint& b) noexcept {
    (void)b; return a;
}
} } // namespace sturm::frontend
using qint = sturm::frontend::qint;
)CPP";

class CountingDiagConsumer final : public clang::DiagnosticConsumer {
public:
    unsigned warnings = 0;
    unsigned errors   = 0;
    std::string last_error_text;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level lvl,
                          const clang::Diagnostic& info) override {
        if (lvl >= clang::DiagnosticsEngine::Error) {
            ++errors;
            llvm::SmallString<256> buf;
            info.FormatDiagnostic(buf);
            last_error_text = std::string(buf.str());
        } else if (lvl == clang::DiagnosticsEngine::Warning) {
            ++warnings;
        }
    }
};

struct Harness {
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>     ids;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts;
    CountingDiagConsumer*                              counter;
    clang::DiagnosticsEngine                           engine;
    Harness()
        : ids(new clang::DiagnosticIDs()),
          opts(new clang::DiagnosticOptions()),
          counter(new CountingDiagConsumer()),
          engine(ids, opts.get(), counter, /*ShouldOwnClient=*/true) {}
};

struct RunResult {
    unsigned errors   = 0;
    unsigned warnings = 0;
    std::string last_error_text;
    bool parsed = false;
};

RunResult run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQRamOosStub.size() + user_src.size());
    code.append(kQRamOosStub);
    code.append(user_src);
    Harness h;
    clang::ast_matchers::MatchFinder finder;
    register_qram_oos_matcher(finder, h.engine);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "qram_oos_input.cpp");
    return {h.counter->errors, h.counter->warnings,
            h.counter->last_error_text, ok};
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

#ifdef STURM_QRAM_OOS_FIXTURES_DIR
std::string slurp_fixture(std::string_view name) {
    std::filesystem::path root(STURM_QRAM_OOS_FIXTURES_DIR);
    std::ifstream in(root / std::string(name), std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}
RunResult run_matcher_on_fixture(std::string_view name) {
    const std::string src = slurp_fixture(name);
    if (src.empty()) {
        std::fprintf(stderr, "FAIL  fixture %.*s missing or empty\n",
                     (int)name.size(), name.data());
        return {};
    }
    Harness h;
    clang::ast_matchers::MatchFinder finder;
    register_qram_oos_matcher(finder, h.engine);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), src, args, std::string(name));
    return {h.counter->errors, h.counter->warnings,
            h.counter->last_error_text, ok};
}
#endif

} // anonymous namespace

// ── Shape positives (inline; fixtures cover them too, see below) ────────────
static void test_existing_target_basic() {
    const auto r = run_matcher(
        "void demo(qint a[4], qint i) { qint b; b = a[i]; }\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosExistingTargetId));
    CHECK(contains(r.last_error_text, "PRD"));
}
static void test_write_basic() {
    const auto r = run_matcher(
        "void demo(qint a[4], qint i, qint b) { a[i] = b; }\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosWriteId));
    CHECK(contains(r.last_error_text, "PRD"));
}
static void test_rmw_basic() {
    const auto r = run_matcher(
        "void demo(qint a[4], qint i, qint b) { a[i] += b; }\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosRmwId));
    CHECK(contains(r.last_error_text, "PRD"));
}
static void test_expression_position_basic() {
    const auto r = run_matcher(
        "void demo(qint a[4], qint i, qint d) { qint c=a[i]+d; (void)c; }\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosExpressionPositionId));
    CHECK(contains(r.last_error_text, "PRD"));
}

// ── Negatives ────────────────────────────────────────────────────────────────
static void test_in_scope_read_does_not_fire() {
    // The in-scope C1 shape MUST NOT fire any OOS diagnostic.
    const auto r = run_matcher(
        "void demo(qint a[4], qint i) { qint b = a[i]; (void)b; }\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
    CHECK_EQ_INT(r.warnings, 0);
}
static void test_classical_index_does_not_fire() {
    // Classical-int subscript — no UDC node at all.
    const auto r = run_matcher(
        "void demo(qint a[4], unsigned long i) { qint b; b = a[i]; }\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
}

// ── Coexistence: in-scope read + OOS shape — one diagnostic, either order ──
static void test_coexistence_in_scope_then_oos() {
    const auto r = run_matcher(
        "void demo(qint a[4], qint i, qint b) {\n"
        "    qint b1 = a[i];   // in-scope; not E1's job\n"
        "    a[i] = b;         // OOS write\n"
        "    (void)b1;\n"
        "}\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosWriteId));
}
static void test_coexistence_oos_then_in_scope() {
    const auto r = run_matcher(
        "void demo(qint a[4], qint i, qint d) {\n"
        "    qint c = a[i] + d;   // OOS expression-position\n"
        "    qint b1 = a[i];      // in-scope read\n"
        "    (void)c; (void)b1;\n"
        "}\n");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosExpressionPositionId));
}

// ── Fixture-driven positive cases ───────────────────────────────────────────
#ifdef STURM_QRAM_OOS_FIXTURES_DIR

static void test_fixture_existing_target() {
    const auto r = run_matcher_on_fixture("qram_oos_existing_target.cpp");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosExistingTargetId));
}

static void test_fixture_write() {
    const auto r = run_matcher_on_fixture("qram_oos_write.cpp");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosWriteId));
}

static void test_fixture_rmw() {
    const auto r = run_matcher_on_fixture("qram_oos_rmw.cpp");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosRmwId));
}

static void test_fixture_expression_position() {
    const auto r = run_matcher_on_fixture(
        "qram_oos_expression_position.cpp");
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 1);
    CHECK(contains(r.last_error_text, kQramOosExpressionPositionId));
}

#endif // STURM_QRAM_OOS_FIXTURES_DIR

int main() {
    test_existing_target_basic();
    test_write_basic();
    test_rmw_basic();
    test_expression_position_basic();
    test_in_scope_read_does_not_fire();
    test_classical_index_does_not_fire();
    test_coexistence_in_scope_then_oos();
    test_coexistence_oos_then_in_scope();
#ifdef STURM_QRAM_OOS_FIXTURES_DIR
    test_fixture_existing_target();
    test_fixture_write();
    test_fixture_rmw();
    test_fixture_expression_position();
#endif
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
