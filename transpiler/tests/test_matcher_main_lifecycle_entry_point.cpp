// test_matcher_main_lifecycle_entry_point.cpp — sturm-0tcv matcher
// tests for the `[[sturm::entry_point]]` extension to
// `matcher_main_lifecycle`.
//
// The matcher fires on two anchors per PRD §5.4 (post-sturm-0tcv):
//   (a) the unique `int main(...)` FunctionDecl in a TU that defines
//       STURM_UMBRELLA_INCLUDED (the original P7 path);
//   (b) any FunctionDecl carrying `[[clang::annotate(
//       "sturm::entry_point")]]` (the sturm-0tcv extension).
//
// These tests cover the SECOND anchor — the first is covered by the
// existing P7 fixtures (e.g. examples/qram_demo.cpp going through the
// example_qram_demo target). The tests pin:
//
//   (1) Library-fixture shape: a `void setup()` function carrying the
//       attribute → MainLifecycleHit with kind == EntryPointVoid.
//   (2) GoogleTest TEST_F shape: a method on a fixture class
//       carrying the attribute — the matcher REJECTS this (CXXMethodDecl
//       is out of scope; users wrap a free helper instead). This pins
//       the "free-function only" gate.
//   (3) Integral-return shape: a `bool check()` function carrying the
//       attribute → MainLifecycleHit with kind == EntryPointReturn.
//   (4) `STURM_UMBRELLA_INCLUDED` undefined → no hit (sentinel skip
//       gates entry-point hits too).
//   (5) `STURM_NO_AUTO_LIFECYCLE` defined → no hit (escape hatch
//       gates entry-point hits too).
//   (6) Non-integral return type (e.g. `double`, `std::string`,
//       pointer) → no hit (out-of-scope return type).
//   (7) Idempotency probe: a function whose body already starts with
//       `__sturm_ctx` → no hit.
//   (8) gtest-named source file with `int main` + entry_point on a
//       sibling function: the Main anchor is skipped (gtest probe);
//       the entry-point anchor IS NOT skipped (sturm-0tcv contract).
//   (9) Coexistence with Main: a TU with both an `int main(...)` and
//       a `[[sturm::entry_point]]` function publishes TWO hits, one
//       per FunctionDecl.

#include "matcher_main_lifecycle.hpp"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using sturm::transpile::MainLifecycleHit;
using sturm::transpile::MainLifecycleHitKind;
using sturm::transpile::register_main_lifecycle_matcher;

static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                                 \
    ++tests_run;                                                         \
    if (cond) { ++tests_pass; }                                          \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                        \
                     __FILE__, __LINE__, #cond);                         \
    }                                                                    \
} while (0)

#define CHECK_EQ_INT(got, want) do {                                     \
    ++tests_run;                                                         \
    const long long g = static_cast<long long>(got);                     \
    const long long w = static_cast<long long>(want);                    \
    if (g == w) { ++tests_pass; }                                        \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  got=%lld want=%lld\n",        \
                     __FILE__, __LINE__, g, w);                          \
    }                                                                    \
} while (0)

namespace {

// Custom Action that wires the matcher into a MatchFinder. We need
// access to the CompilerInstance's Preprocessor so the macro gates
// can be consulted at hit time. `clang::tooling::runToolOnCodeWithArgs`
// hands us the CompilerInstance through `CreateASTConsumer`; we
// register the matcher there with the live Preprocessor, then
// return MatchFinder's ASTConsumer so the standard parse + traversal
// pipeline drives the matcher.
class LifecycleAction : public clang::ASTFrontendAction {
public:
    LifecycleAction(std::vector<MainLifecycleHit>* sink)
        : sink_(sink) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& ci, llvm::StringRef) override {
        finder_ = std::make_unique<clang::ast_matchers::MatchFinder>();
        register_main_lifecycle_matcher(
            *finder_, ci.getPreprocessor(), *sink_);
        return finder_->newASTConsumer();
    }

private:
    std::unique_ptr<clang::ast_matchers::MatchFinder> finder_;
    std::vector<MainLifecycleHit>* sink_ = nullptr;
};

class LifecycleFactory : public clang::tooling::FrontendActionFactory {
public:
    LifecycleFactory(std::vector<MainLifecycleHit>* sink) : sink_(sink) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<LifecycleAction>(sink_);
    }
private:
    std::vector<MainLifecycleHit>* sink_ = nullptr;
};

// Run the matcher against a snippet. `filename` lets the test
// simulate a `gtest`-named source file for the R3 mitigation gate.
std::vector<MainLifecycleHit> run_on(
    std::string_view code,
    std::string_view filename = "entry_point_input.cpp") {
    std::vector<MainLifecycleHit> hits;
    LifecycleFactory factory(&hits);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(code), args,
        std::string(filename));
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed "
                     "to parse\n");
    }
    return hits;
}

} // namespace

// ── Library-fixture shape ──────────────────────────────────────────────────
//
// PRD §5.4 / sturm-0tcv: a library-level fixture function flagged
// with `[[sturm::entry_point]]` should be auto-wrapped with the
// lifecycle prologue / IIFE / epilogue. The void return type maps
// to MainLifecycleHitKind::EntryPointVoid.
void test_library_fixture_void_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void library_fixture_setup() {
    int x = 0;
    (void)x;
}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].main_fn != nullptr);
    CHECK(hits[0].kind == MainLifecycleHitKind::EntryPointVoid);
    if (hits[0].main_fn) {
        CHECK(hits[0].main_fn->getNameAsString() ==
              "library_fixture_setup");
    }
}

// ── GoogleTest TEST_F shape (method on fixture class) ──────────────────────
//
// PRD §3 non-goal: GoogleTest fixtures call sturm_backend_create
// explicitly. The matcher refuses CXXMethodDecl subjects so a TEST_F
// body — which expands to a method on a generated fixture class —
// is NOT auto-wrapped. Users who want auto-injection on test bodies
// must wrap the body in a free function that the TEST_F calls.
//
// The fixture below mimics what a TEST_F macro produces: a class
// extending a fixture base and an override method that carries the
// `[[sturm::entry_point]]` attribute. The matcher must publish
// ZERO hits for this shape.
void test_googletest_method_rejected() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

class Fixture {
public:
    [[clang::annotate("sturm::entry_point")]]
    void TestBody() {
        int x = 0;
        (void)x;
    }
};
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

// ── GoogleTest TEST_F-WRAPPING idiom (free function approach) ──────────────
//
// The recommended pattern under sturm-0tcv: instead of annotating
// the TEST_F method itself, the user writes the test body in a
// free function and annotates THAT, then the TEST_F method calls
// the free function. The matcher must publish ONE hit on the free
// function and ZERO on the method.
void test_googletest_wrapping_idiom() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void TestFooBody() {
    int x = 0;
    (void)x;
}

class FooTest {
public:
    void TestBody() {
        TestFooBody();
    }
};
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].main_fn != nullptr);
    CHECK(hits[0].kind == MainLifecycleHitKind::EntryPointVoid);
    if (hits[0].main_fn) {
        CHECK(hits[0].main_fn->getNameAsString() == "TestFooBody");
    }
}

// ── Integral-return shape ──────────────────────────────────────────────────
//
// A non-void return type → MainLifecycleHitKind::EntryPointReturn.
// Mirrors the existing Main path but the return type can be any
// integral type (`int`, `bool`, `int64_t`, ...).
void test_integral_return_shape() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
bool check_invariant() {
    return true;
}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].main_fn != nullptr);
    CHECK(hits[0].kind == MainLifecycleHitKind::EntryPointReturn);
}

// ── Umbrella-sentinel gate ─────────────────────────────────────────────────
//
// `STURM_UMBRELLA_INCLUDED` undefined → no hit. The matcher's macro
// gate fires for both the Main and Entry-Point anchors.
void test_umbrella_undefined_no_hit() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::entry_point")]]
void no_umbrella() {}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Escape-hatch gate ──────────────────────────────────────────────────────
//
// `STURM_NO_AUTO_LIFECYCLE` defined → no hit. Mirrors the Main path.
void test_escape_hatch_no_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1
#define STURM_NO_AUTO_LIFECYCLE 1

[[clang::annotate("sturm::entry_point")]]
void escaped() {}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Non-integral return type ───────────────────────────────────────────────
//
// Pointer / class / float / templated return types are OOS. The
// matcher classifies the entry-point shape via
// `classify_entry_point_shape` which only accepts `void` and
// integral types.
void test_non_integral_return_no_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
double returns_double() { return 0.0; }

[[clang::annotate("sturm::entry_point")]]
int* returns_pointer() { return nullptr; }
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Idempotency probe ──────────────────────────────────────────────────────
//
// A function whose body already starts with the `__sturm_ctx` decl
// is treated as already rewritten — no hit. Both the Main and
// Entry-Point anchors share this gate.
void test_idempotency_no_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

typedef int sturm_backend_context_t;
sturm_backend_context_t* sturm_backend_create(int);

[[clang::annotate("sturm::entry_point")]]
void already_wrapped() {
    sturm_backend_context_t* __sturm_ctx = sturm_backend_create(0);
    (void)__sturm_ctx;
}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

// ── gtest-named filename: entry-point anchor is NOT skipped ────────────────
//
// PRD §3 non-goal "test-framework auto-injection" was the original
// motivation for the gtest filename probe in the Main path. sturm-0tcv
// extends the matcher with an opt-in attribute that lets a user
// override that posture file-by-file — auto-injection on a flagged
// FunctionDecl works even when the file is named `*gtest*.cpp`.
// This test pins that the gtest filename probe does NOT block the
// entry-point hit (canonical use case: a flagged setup helper inside
// a `test_foo_gtest.cpp` file).
void test_gtest_filename_entry_point_still_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void fixture_in_gtest_file() {}
)CPP";
    const auto hits = run_on(src, "my_test_gtest.cpp");
    CHECK_EQ_INT(hits.size(), 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == MainLifecycleHitKind::EntryPointVoid);
}

// ── gtest-named filename: Main anchor IS still skipped ─────────────────────
//
// The companion to the previous test: the existing Main-anchor R3
// mitigation is unchanged. A `int main(int, char**)` inside a
// `*gtest*.cpp` file is NOT auto-wrapped.
void test_gtest_filename_main_still_skipped() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return 0;
}
)CPP";
    const auto hits = run_on(src, "test_foo_gtest.cpp");
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Coexistence: Main + entry-point in same TU → two hits ──────────────────
//
// A TU may legally contain both an `int main(...)` definition AND
// any number of `[[sturm::entry_point]]`-flagged free functions.
// The matcher must publish one hit per FunctionDecl with the right
// `kind` value.
void test_main_and_entry_point_coexist() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void helper_setup() {}

int main(int, char**) {
    helper_setup();
    return 0;
}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 2);

    bool saw_main = false;
    bool saw_entry = false;
    for (const auto& h : hits) {
        if (h.kind == MainLifecycleHitKind::Main) saw_main = true;
        if (h.kind == MainLifecycleHitKind::EntryPointVoid) {
            saw_entry = true;
        }
    }
    CHECK(saw_main);
    CHECK(saw_entry);
}

// ── Forward declaration: no hit ────────────────────────────────────────────
//
// A function with `[[sturm::entry_point]]` but no body (forward
// declaration) is NOT a rewrite candidate.
void test_forward_declaration_no_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void forward_only();
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

// ── Typo in attribute string: no hit ───────────────────────────────────────
//
// The matcher consults `has_entry_point_attr` which byte-matches the
// annotation; a typo silently fails detection. This protects users
// from silently mis-spelled markers.
void test_typo_no_hit() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_pont")]]
void typoed() {}
)CPP";
    const auto hits = run_on(src);
    CHECK_EQ_INT(hits.size(), 0);
}

int main() {
    test_library_fixture_void_hit();
    test_googletest_method_rejected();
    test_googletest_wrapping_idiom();
    test_integral_return_shape();
    test_umbrella_undefined_no_hit();
    test_escape_hatch_no_hit();
    test_non_integral_return_no_hit();
    test_idempotency_no_hit();
    test_gtest_filename_entry_point_still_hit();
    test_gtest_filename_main_still_skipped();
    test_main_and_entry_point_coexist();
    test_forward_declaration_no_hit();
    test_typo_no_hit();

    std::fprintf(
        stderr,
        "test_matcher_main_lifecycle_entry_point: %d / %d passed\n",
        tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
