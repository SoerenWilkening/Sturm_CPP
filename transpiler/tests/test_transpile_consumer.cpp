// test_transpile_consumer.cpp — PM1-1 unit boundary tests for the
// shared TranspileConsumer class.
//
// This binary does NOT exercise the byte-identical snapshot gate (the
// existing transpiler_example_* CTests cover that end-to-end). It
// instead pins the new contract PM1-1 introduces:
//
//   1. TranspileConsumer builds cleanly given a clang::CompilerInstance
//      reference and an EmissionMode enum.
//   2. EmissionMode::Plugin leaves the rewritten buffer accessible via
//      rewritten_buffer() after HandleTranslationUnit runs; it does NOT
//      write anything to disk.
//   3. EmissionMode::StandaloneFile writes a file under the supplied
//      output_dir (via the M9 emit() path) with the M5 idempotency
//      header prepended, and leaves rewritten_buffer() empty.
//
// The matcher pool + post-walk backstops are shared across both modes,
// so a single trivial OR-case fixture exercises every registration edge
// the issue description calls out; snapshot fixtures in the main
// CTest run lock byte-identity downstream.

#include "transpile_consumer.hpp"

#include "sturm/transpile/io.hpp"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static int tests_run = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

// Minimal qbool stub + demo routine — mirrors the pattern the M7 test
// harness uses. The OR matcher only needs the class name and a two-arg
// `operator|` overload on qbool.
static const char* kOrFixture = R"CPP(
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

qbool demo(qbool a, qbool b) {
    qbool tmp = a | b;
    return tmp;
}
)CPP";

namespace {

// Snapshot of the consumer's observable state, captured before the
// consumer is destroyed. Phase T T-1 (sturm-xrob.2): the original
// `sink_ = raw pointer to consumer` shape was relying on dangling-pointer
// reads — ClangTool destroys the consumer alongside its owning
// ASTFrontendAction when the tool invocation returns, so accessing
// `consumer->mode()` / `consumer->rewritten_buffer()` after
// `runToolOnCodeWithArgs` returns dereferences freed memory. The
// pre-T-1 layout happened to leave `mode_` and the string's SSO bytes
// readable by coincidence; the T-1 wiring introduces additional
// allocations during teardown that zero `mode_`'s slot, exposing the
// latent bug. Capturing the state into an externally-owned snapshot
// inside `EndSourceFileAction` — which runs BEFORE the consumer is
// destroyed — keeps the test robust regardless of allocator behavior.
struct ConsumerSnapshot {
    bool populated = false;
    sturm::transpile::EmissionMode mode =
        sturm::transpile::EmissionMode::StandaloneFile;
    std::string rewritten_buffer;
};

// A FrontendAction that constructs our shared TranspileConsumer under
// the caller-supplied EmissionMode. After HandleTranslationUnit runs,
// `EndSourceFileAction` snapshots the consumer's observable state into
// a caller-owned `ConsumerSnapshot` so the test can inspect it without
// reaching into the destroyed consumer.
class TestAction : public clang::ASTFrontendAction {
public:
    TestAction(sturm::transpile::EmissionMode mode,
               std::string source_path,
               std::string output_dir,
               ConsumerSnapshot* sink)
        : mode_(mode),
          source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)),
          sink_(sink) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& ci, llvm::StringRef) override {
        auto consumer =
            std::make_unique<sturm::transpile::TranspileConsumer>(
                ci, mode_, source_path_, output_dir_);
        consumer_ = consumer.get();
        return consumer;
    }

    void EndSourceFileAction() override {
        if (sink_ != nullptr && consumer_ != nullptr) {
            sink_->populated = true;
            sink_->mode = consumer_->mode();
            sink_->rewritten_buffer = consumer_->rewritten_buffer();
        }
        clang::ASTFrontendAction::EndSourceFileAction();
    }

private:
    sturm::transpile::EmissionMode mode_;
    std::string source_path_;
    std::string output_dir_;
    ConsumerSnapshot* sink_;
    sturm::transpile::TranspileConsumer* consumer_ = nullptr;
};

class TestFactory : public clang::tooling::FrontendActionFactory {
public:
    TestFactory(sturm::transpile::EmissionMode mode,
                std::string source_path,
                std::string output_dir,
                ConsumerSnapshot* sink)
        : mode_(mode),
          source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)),
          sink_(sink) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<TestAction>(
            mode_, source_path_, output_dir_, sink_);
    }

private:
    sturm::transpile::EmissionMode mode_;
    std::string source_path_;
    std::string output_dir_;
    ConsumerSnapshot* sink_;
};

} // namespace

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// A minimal scratch directory under /tmp. The test drops fixture input
// and output files here so we do not depend on any specific build tree
// layout.
static fs::path make_scratch_dir(const char* tag) {
    fs::path base = fs::temp_directory_path() / "sturm-pm1-consumer-test";
    fs::create_directories(base);
    fs::path dir = base / tag;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

// Gate 1: StandaloneFile mode writes to disk AND the disk bytes begin
// with the M5 idempotency header's AUTO-GENERATED sentinel.
static void test_standalone_file_mode() {
    fs::path dir = make_scratch_dir("standalone");
    fs::path src = dir / "in.cpp";
    {
        std::ofstream out(src, std::ios::binary);
        out << kOrFixture;
    }
    fs::path out_dir = dir / "out";
    fs::create_directories(out_dir);

    ConsumerSnapshot snapshot;
    TestFactory factory(
        sturm::transpile::EmissionMode::StandaloneFile,
        src.string(), out_dir.string(), &snapshot);

    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), kOrFixture, args, src.filename().string());
    CHECK(ok);
    CHECK(snapshot.populated);

    // Plugin-mode stash must be empty in StandaloneFile mode.
    if (snapshot.populated) {
        CHECK(snapshot.rewritten_buffer.empty());
        CHECK(snapshot.mode ==
              sturm::transpile::EmissionMode::StandaloneFile);
    }

    // The emit step resolves the output path against the main-file
    // name the ClangTool saw (src.filename()), not the full source
    // path. See io.cpp::resolve_output_path for the rule.
    fs::path expected = out_dir / src.filename();
    CHECK(fs::exists(expected));

    std::string bytes = slurp(expected);
    // M5 idempotency header carries the AUTO-GENERATED sentinel the
    // standalone mode prepends; Plugin mode must NOT prepend it (gate 2).
    CHECK(bytes.find("AUTO-GENERATED") != std::string::npos);
}

// Gate 2: Plugin mode skips the disk write AND populates
// rewritten_buffer() with the rewritten source, WITHOUT the M5 header.
static void test_plugin_mode() {
    fs::path dir = make_scratch_dir("plugin");
    fs::path src = dir / "in.cpp";
    {
        std::ofstream out(src, std::ios::binary);
        out << kOrFixture;
    }
    fs::path out_dir = dir / "out";
    fs::create_directories(out_dir);

    ConsumerSnapshot snapshot;
    TestFactory factory(
        sturm::transpile::EmissionMode::Plugin,
        src.string(), out_dir.string(), &snapshot);

    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), kOrFixture, args, src.filename().string());
    CHECK(ok);
    CHECK(snapshot.populated);

    if (snapshot.populated) {
        CHECK(snapshot.mode ==
              sturm::transpile::EmissionMode::Plugin);
        const std::string& buf = snapshot.rewritten_buffer;
        CHECK(!buf.empty());
        // The plugin's rewritten buffer is fed to a NESTED
        // CompilerInvocation. A leading AUTO-GENERATED header would
        // parse as a comment there but double-prepend on a subsequent
        // standalone run, so emit_to_string must leave it off.
        CHECK(buf.find("AUTO-GENERATED") == std::string::npos);
        // The rewritten buffer must still contain the original
        // source's `demo` token so we know the Rewriter serialized
        // the main file rather than handing back an empty string.
        CHECK(buf.find("demo") != std::string::npos);
    }

    // Plugin mode MUST NOT have written anything under out_dir.
    fs::path unexpected = out_dir / src.filename();
    CHECK(!fs::exists(unexpected));
}

int main() {
    test_standalone_file_mode();
    test_plugin_mode();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
