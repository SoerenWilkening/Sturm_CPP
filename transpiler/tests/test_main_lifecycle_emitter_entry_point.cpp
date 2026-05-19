// test_main_lifecycle_emitter_entry_point.cpp — sturm-0tcv snapshot
// tests for the emitter's per-hit behaviour on entry-point hits.
//
// The matcher publishes `MainLifecycleHit{fn, kind}` entries; the
// emitter `emit_main_lifecycle_replacements` consumes them and
// pushes `QReplacement` records that the consumer's M9 emitter
// applies to the source buffer. These tests pin the per-kind
// rewrite shape:
//
//   (1) EntryPointVoid — library fixture / TEST_F wrapping case.
//       Generated body wraps `void` IIFE without a trailing return.
//   (2) EntryPointReturn — `bool check()` / `int probe()` case.
//       Generated body captures the IIFE result and emits a
//       `return __sturm_rc;`.
//   (3) Main — backward-compat regression: the original P7 rewrite
//       is unchanged. Spelled "int" hardcoded.
//
// Harness posture: run the matcher + emitter in a single ASTAction,
// apply the resulting QReplacements to the source via a Rewriter,
// and snapshot the rewritten buffer text. The test then asserts on
// substrings characteristic of each rewrite shape (full byte-exact
// comparison would be fragile against unrelated rewrite passes).

#include "main_lifecycle_emitter.hpp"
#include "matcher_main_lifecycle.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_main_lifecycle_emitter_entry_point_ns {

using sturm::transpile::MainLifecycleHit;
using sturm::transpile::MainLifecycleHitKind;
using sturm::transpile::QReplacement;
using sturm::transpile::emit_main_lifecycle_replacements;
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

namespace {

bool contains(const std::string& hay, std::string_view needle) {
    return hay.find(needle) != std::string::npos;
}

// Snapshot result returned by `rewrite()` below.
struct RewriteSnapshot {
    bool parsed = false;
    std::string rewritten;
    std::size_t n_hits = 0;
    MainLifecycleHitKind first_kind = MainLifecycleHitKind::Main;
};

// Consumer that wraps a MatchFinder consumer and runs the emitter
// in its HandleTranslationUnit override (after the matcher has
// populated the hit vector via the inner consumer's traversal).
class SnapshotConsumer : public clang::ASTConsumer {
public:
    SnapshotConsumer(std::unique_ptr<clang::ASTConsumer> inner,
                     clang::CompilerInstance* ci,
                     std::vector<MainLifecycleHit>* hits_sink,
                     RewriteSnapshot* sink)
        : inner_(std::move(inner)),
          ci_(ci),
          hits_sink_(hits_sink),
          sink_(sink) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        // Drive the matcher's consumer first — populates `*hits_sink_`.
        if (inner_) inner_->HandleTranslationUnit(ctx);

        sink_->n_hits = hits_sink_->size();
        if (!hits_sink_->empty()) {
            sink_->first_kind = (*hits_sink_)[0].kind;
        }

        // Emit replacements.
        std::vector<QReplacement> reps;
        emit_main_lifecycle_replacements(
            ci_->getSourceManager(),
            ci_->getLangOpts(),
            *hits_sink_, reps);

        // Apply replacements via a Rewriter.
        clang::Rewriter rewriter(ci_->getSourceManager(),
                                 ci_->getLangOpts());
        for (const auto& r : reps) {
            const clang::SourceLocation end_tok_loc =
                clang::Lexer::getLocForEndOfToken(
                    r.range.getEnd(), 0,
                    ci_->getSourceManager(),
                    ci_->getLangOpts());
            rewriter.ReplaceText(
                clang::SourceRange(r.range.getBegin(), end_tok_loc),
                r.replacement);
        }

        // Recover rewritten main-file buffer.
        const clang::FileID main =
            ci_->getSourceManager().getMainFileID();
        if (auto* buf = rewriter.getRewriteBufferFor(main)) {
            std::string out;
            llvm::raw_string_ostream os(out);
            buf->write(os);
            os.flush();
            sink_->rewritten = std::move(out);
        } else {
            const llvm::StringRef txt =
                ci_->getSourceManager().getBufferData(main);
            sink_->rewritten = std::string(txt);
        }
        sink_->parsed = true;
    }

private:
    std::unique_ptr<clang::ASTConsumer> inner_;
    clang::CompilerInstance* ci_;
    std::vector<MainLifecycleHit>* hits_sink_;
    RewriteSnapshot* sink_;
};

class SnapshotAction : public clang::ASTFrontendAction {
public:
    explicit SnapshotAction(RewriteSnapshot* sink) : sink_(sink) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& ci, llvm::StringRef) override {
        finder_ = std::make_unique<clang::ast_matchers::MatchFinder>();
        register_main_lifecycle_matcher(
            *finder_, ci.getPreprocessor(), hits_);
        return std::make_unique<SnapshotConsumer>(
            finder_->newASTConsumer(), &ci, &hits_, sink_);
    }

private:
    std::unique_ptr<clang::ast_matchers::MatchFinder> finder_;
    std::vector<MainLifecycleHit> hits_;
    RewriteSnapshot* sink_ = nullptr;
};

class SnapshotFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit SnapshotFactory(RewriteSnapshot* sink) : sink_(sink) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<SnapshotAction>(sink_);
    }
private:
    RewriteSnapshot* sink_;
};

RewriteSnapshot rewrite(std::string_view src,
                        std::string_view fn = "snap_input.cpp") {
    RewriteSnapshot snap;
    SnapshotFactory factory(&snap);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(src), args,
        std::string(fn));
    return snap;
}

#ifdef STURM_ENTRY_POINT_FIXTURES_DIR
namespace fs = std::filesystem;

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// Whitespace normaliser — collapses runs of whitespace into a single
// space so the snapshot tests stay robust against the emitter's
// rewriter inserting extra blank lines. This is the same posture used
// by `test_qint_alias_subst_emitter.cpp`'s `normalise()`.
std::string normalise(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool prev_ws = true;  // collapse leading whitespace.
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!prev_ws) {
                out.push_back(' ');
                prev_ws = true;
            }
        } else {
            out.push_back(c);
            prev_ws = false;
        }
    }
    while (!out.empty() &&
           (out.back() == ' ' || out.back() == '\n')) {
        out.pop_back();
    }
    return out;
}
#endif // STURM_ENTRY_POINT_FIXTURES_DIR

} // namespace

// ── (1) EntryPointVoid library-fixture snapshot ────────────────────────────
//
// Per the emitter's EntryPointVoid arm, the rewrite must:
//   * Open the body with `sturm_backend_create(STURM_MODE_DEFAULT)`.
//   * Open an IIFE with `-> void` trailing return.
//   * Close with the IIFE invocation, the teardown, and NO `return
//     __sturm_rc;` (the outer function is void).
void test_void_entry_point_emits_no_return() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void library_fixture() {
    int x = 0;
    (void)x;
}
)CPP";
    auto snap = rewrite(src);
    CHECK(snap.parsed);
    CHECK(snap.n_hits == 1);
    if (!snap.parsed) return;

    // Prologue + IIFE opener.
    CHECK(contains(snap.rewritten,
                   "sturm_backend_context_t* __sturm_ctx ="));
    CHECK(contains(snap.rewritten,
                   "sturm_backend_create(STURM_MODE_DEFAULT)"));
    CHECK(contains(snap.rewritten,
                   "sturm_set_thread_context(__sturm_ctx)"));
    CHECK(contains(snap.rewritten, "([&]() -> void {"));

    // Teardown.
    CHECK(contains(snap.rewritten, "})();"));
    CHECK(contains(snap.rewritten,
                   "sturm_set_thread_context(nullptr)"));
    CHECK(contains(snap.rewritten,
                   "sturm_backend_destroy(__sturm_ctx)"));

    // No return-statement after teardown — outer void return.
    CHECK(!contains(snap.rewritten, "return __sturm_rc;"));

    // No `int __sturm_rc =` either.
    CHECK(!contains(snap.rewritten, "int __sturm_rc ="));
}

// ── (2) EntryPointReturn integral snapshot ─────────────────────────────────
//
// Per the EntryPointReturn arm, the rewrite must:
//   * Capture the IIFE result into `<rettype> __sturm_rc`.
//   * Spell the trailing return type as the outer function's return
//     type (here `bool`).
//   * Emit `return __sturm_rc;` after teardown.
void test_integral_entry_point_emits_return() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
bool check_invariant() {
    return true;
}
)CPP";
    auto snap = rewrite(src);
    CHECK(snap.parsed);
    CHECK(snap.n_hits == 1);
    if (!snap.parsed) return;

    // Prologue + IIFE opener with `bool` return capture.
    CHECK(contains(snap.rewritten, "bool __sturm_rc"));
    CHECK(contains(snap.rewritten, "([&]() -> bool {"));

    // Teardown with return.
    CHECK(contains(snap.rewritten, "})();"));
    CHECK(contains(snap.rewritten, "return __sturm_rc;"));
}

// ── (3) Main backward-compat snapshot ──────────────────────────────────────
//
// The original P7 rewrite must remain unchanged: `int` is hardcoded,
// `int __sturm_rc` captures the IIFE result, the trailing return
// type spells `int`, and the outer function emits the final
// `return __sturm_rc;`.
void test_main_kind_backward_compat() {
    constexpr std::string_view src = R"CPP(
#define STURM_UMBRELLA_INCLUDED 1

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return 0;
}
)CPP";
    auto snap = rewrite(src);
    CHECK(snap.parsed);
    CHECK(snap.n_hits == 1);
    if (!snap.parsed) return;
    CHECK(snap.first_kind == MainLifecycleHitKind::Main);

    CHECK(contains(snap.rewritten, "int __sturm_rc ="));
    CHECK(contains(snap.rewritten, "([&]() -> int {"));
    CHECK(contains(snap.rewritten, "return __sturm_rc;"));
}

// ── Golden-file snapshot tests (fixture-based) ─────────────────────────────
//
// These exercise the same transform paths via on-disk fixture pairs
// (`tests/fixtures/entry_point_*.{cpp,expected.cpp}`). The .cpp file
// is the matcher input; the .expected.cpp file is the byte-normalised
// expected output. Matching is on the whitespace-normalised text so
// the snapshot stays robust against `clang::Rewriter`'s incidental
// whitespace insertions.

#ifdef STURM_ENTRY_POINT_FIXTURES_DIR

void fixture_case(std::string_view input_name,
                  std::string_view expected_name) {
    fs::path root(STURM_ENTRY_POINT_FIXTURES_DIR);
    const std::string input = slurp(root / std::string(input_name));
    const std::string gold  = slurp(root / std::string(expected_name));
    CHECK(!input.empty());
    CHECK(!gold.empty());
    if (input.empty() || gold.empty()) return;
    auto snap = rewrite(input, input_name);
    CHECK(snap.parsed);
    if (!snap.parsed) return;

    const std::string got = normalise(snap.rewritten);
    const std::string want = normalise(gold);
    if (got != want) {
        ++tests_run;
        std::fprintf(stderr,
                     "FAIL  fixture %.*s\n  got:  <<<%s>>>\n"
                     "  want: <<<%s>>>\n",
                     (int)input_name.size(), input_name.data(),
                     got.c_str(), want.c_str());
    } else {
        ++tests_run;
        ++tests_pass;
    }
}

// Library-fixture snapshot.
void test_fixture_library_fixture() {
    fixture_case("entry_point_library_fixture.cpp",
                 "entry_point_library_fixture.expected.cpp");
}

// GoogleTest TEST_F-wrapping idiom snapshot — the free function is
// rewritten, the CXXMethodDecl on the fixture class is left untouched.
void test_fixture_gtest_wrapping() {
    fixture_case("entry_point_gtest_wrapping.cpp",
                 "entry_point_gtest_wrapping.expected.cpp");
}

#endif // STURM_ENTRY_POINT_FIXTURES_DIR

}  // namespace sturm_test_main_lifecycle_emitter_entry_point_ns

int run_test_main_lifecycle_emitter_entry_point(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_main_lifecycle_emitter_entry_point_ns;
    using sturm_test_main_lifecycle_emitter_entry_point_ns::tests_run;
    using sturm_test_main_lifecycle_emitter_entry_point_ns::tests_pass;
    test_void_entry_point_emits_no_return();
    test_integral_entry_point_emits_return();
    test_main_kind_backward_compat();
#ifdef STURM_ENTRY_POINT_FIXTURES_DIR
    test_fixture_library_fixture();
    test_fixture_gtest_wrapping();
#endif

    std::fprintf(
        stderr,
        "test_main_lifecycle_emitter_entry_point: %d / %d passed\n",
        tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
