// test_qint_implicit_warn.cpp — sturm-u9ge.14 (Beat F1) unit tests.
// Plan §9 / Beat F1; PRD §10.1 / M7. The matcher fires Warning per
// non-subscript-context UDC for `frontend::qint -> integer`; positives
// `int x = q;`, `std::vector<int> v(q);`, `for (... ; i < q; ...)`;
// negative `arr[q]` (subscript context). Default-suppressed gate.
// LoC budget: <= 200.

#include "matcher_qint_implicit_warn.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_qint_implicit_warn_ns {

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
// unsigned long()` (the load-bearing UDC, host-`<cstddef>`-independent).
constexpr std::string_view kQintWarnStub = R"CPP(
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
} } // namespace sturm::frontend
using qint = sturm::frontend::qint;
namespace std {
template <typename T>
struct vector {
    vector() noexcept = default;
    explicit vector(unsigned long) noexcept {}
};
} // namespace std
)CPP";

class CountingDiagConsumer final : public clang::DiagnosticConsumer {
public:
    unsigned warnings = 0;
    unsigned errors   = 0;
    std::string last_warning_text;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level lvl,
                          const clang::Diagnostic& info) override {
        if (lvl >= clang::DiagnosticsEngine::Error) {
            ++errors;
        } else if (lvl == clang::DiagnosticsEngine::Warning) {
            ++warnings;
            llvm::SmallString<256> buf;
            info.FormatDiagnostic(buf);
            last_warning_text = std::string(buf.str());
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
    std::string last_warning_text;
    bool parsed = false;
};

RunResult run_matcher(std::string_view user_src, bool enable_warning) {
    std::string code;
    code.reserve(kQintWarnStub.size() + user_src.size());
    code.append(kQintWarnStub);
    code.append(user_src);
    Harness h;
    clang::ast_matchers::MatchFinder finder;
    set_qint_implicit_measure_warning_enabled(enable_warning);
    register_qint_implicit_warn_matcher(finder, h.engine);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "qint_warn_input.cpp");
    set_qint_implicit_measure_warning_enabled(false);
    return {h.counter->errors, h.counter->warnings,
            h.counter->last_warning_text, ok};
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

} // anonymous namespace

// ── Positives (warning ENABLED) ────────────────────────────────────────────
static void test_positive_int_var_init() {
    const auto r = run_matcher(
        "void demo(qint q) { int x = q; (void)x; }\n", true);
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
    CHECK_EQ_INT(r.warnings, 1);
    CHECK(contains(r.last_warning_text, kQintImplicitMeasureId));
}

static void test_positive_vector_ctor_arg() {
    const auto r = run_matcher(
        "void demo(qint q) { std::vector<int> v(q); (void)v; }\n", true);
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
    CHECK_EQ_INT(r.warnings, 1);
    CHECK(contains(r.last_warning_text, kQintImplicitMeasureId));
}

static void test_positive_for_loop_bound() {
    const auto r = run_matcher(
        "void demo(qint q) {\n"
        "    for (unsigned long i = 0; i < q; ++i) { (void)i; }\n"
        "}\n", true);
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
    CHECK(r.warnings >= 1);
    CHECK(contains(r.last_warning_text, kQintImplicitMeasureId));
}

// ── Negative: subscript context (warning ENABLED, must NOT fire) ──────────
static void test_negative_subscript_c_array() {
    const auto r = run_matcher(
        "void demo(int* arr, qint q) { int x = arr[q]; (void)x; }\n", true);
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
    CHECK_EQ_INT(r.warnings, 0);
}

// ── Default-suppression: warning DISABLED (default) — nothing fires ────────
static void test_suppressed_by_default() {
    const auto r = run_matcher(
        "void demo(qint q) { int x = q; (void)x; }\n", false);
    CHECK(r.parsed);
    CHECK_EQ_INT(r.errors, 0);
    CHECK_EQ_INT(r.warnings, 0);
}

}  // namespace sturm_test_qint_implicit_warn_ns

int run_test_qint_implicit_warn(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_qint_implicit_warn_ns;
    using sturm_test_qint_implicit_warn_ns::tests_run;
    using sturm_test_qint_implicit_warn_ns::tests_pass;
    test_positive_int_var_init();
    test_positive_vector_ctor_arg();
    test_positive_for_loop_bound();
    test_negative_subscript_c_array();
    test_suppressed_by_default();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
