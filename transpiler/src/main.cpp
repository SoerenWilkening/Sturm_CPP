// main.cpp — sturm-transpile driver.
//
// At M9 this module composes the full MVP pipeline:
//
//   skip detection  →  LibTooling parse  →  MatchFinder (M7)  →  synthesize
//   (M8)            →  emit (M9)         →  file on disk
//
// Skip detection (the M5 idempotency / opt-out contract) is implemented
// via should_skip() from the skip module. Two cases:
//   1. "// sturm-transpile: skip" magic comment → pass through unchanged.
//   2. Our own AUTO-GENERATED header already present → pass through
//      unchanged (enforces PRD AC #5: re-running on emitted output yields
//      a byte-identical file).
//
// For non-skipped inputs we run Clang's ClangTool with a custom
// FrontendAction whose ASTConsumer drives the M7 matcher, invokes M8, and
// then calls the M9 emitter to land the rewritten source on disk.

#include "sturm/transpile/emitter.hpp"
#include "sturm/transpile/io.hpp"
#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/skip.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace cl = llvm::cl;

// ── Version string ────────────────────────────────────────────────────────────
// Bump when the transpiler's contract changes. Kept in one place so the
// tests can regex it and CMake can pass it as a compile definition later.
static const char kSturmTranspileVersion[] =
    "sturm-transpile 0.2.0 (MVP pipeline; M9: C++ emitter)";

// ── Command-line options ──────────────────────────────────────────────────────
static cl::OptionCategory kToolCategory("sturm-transpile options");

static cl::opt<std::string> kOutputDir(
    "output-dir",
    cl::desc("Destination directory for transpiled output files"),
    cl::value_desc("dir"),
    cl::Required,
    cl::cat(kToolCategory));

// Note: the input source file is supplied as a positional argument handled
// by CommonOptionsParser. No additional cl::opt is needed for it.

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool handle_version_flag(int argc, const char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 ||
            std::strcmp(argv[i], "-v") == 0) {
            std::puts(kSturmTranspileVersion);
            return true;
        }
    }
    return false;
}

// ── FrontendAction composing the MVP pipeline ────────────────────────────────
//
// The action holds no state of its own; the ASTConsumer below does the
// real work. We thread the source path + output dir through the action
// constructor so the consumer can hand them to emit().

namespace {

class TranspileConsumer : public clang::ASTConsumer {
public:
    TranspileConsumer(std::string source_path, std::string output_dir)
        : source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)) {
        sturm::transpile::register_or_matcher(finder_, unit_);
        sturm::transpile::register_not_matcher(finder_, unit_);
        sturm::transpile::register_xor_matcher(finder_, unit_);
        sturm::transpile::register_xor_assign_matcher(finder_, unit_);
        sturm::transpile::register_xor_assign_classical_matcher(
            finder_, unit_);
        sturm::transpile::register_add_assign_const_matcher(finder_, unit_);
        sturm::transpile::register_sub_assign_const_matcher(finder_, unit_);
        sturm::transpile::register_mul_assign_const_matcher(finder_, unit_);
        sturm::transpile::register_div_assign_const_matcher(finder_, unit_);
        sturm::transpile::register_add_assign_qint_matcher(finder_, unit_);
        sturm::transpile::register_sub_assign_qint_matcher(finder_, unit_);
        sturm::transpile::register_mul_assign_qint_matcher(finder_, unit_);
        sturm::transpile::register_div_assign_qint_matcher(finder_, unit_);
        sturm::transpile::register_mod_assign_qint_matcher(finder_, unit_);
        sturm::transpile::register_eq_compare_qint_matcher(finder_, unit_);
        sturm::transpile::register_ne_compare_qint_matcher(finder_, unit_);
        sturm::transpile::register_lt_compare_qint_matcher(finder_, unit_);
        sturm::transpile::register_le_compare_qint_matcher(finder_, unit_);
        sturm::transpile::register_gt_compare_qint_matcher(finder_, unit_);
        sturm::transpile::register_ge_compare_qint_matcher(finder_, unit_);
    }

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        // M7: populate the QUnit via the match finder.
        finder_.matchAST(ctx);

        // M8: synthesize uncompute insertions from the QUnit.
        auto insertions = sturm::transpile::synthesize(unit_);

        // M9: build a Rewriter over the same SourceManager/LangOptions and
        // let emit() apply insertions, prepend the header, and write the
        // output file.
        clang::Rewriter rw(ctx.getSourceManager(), ctx.getLangOpts());
        (void)sturm::transpile::emit(ctx.getSourceManager(), rw,
                                     insertions, source_path_, output_dir_);
    }

private:
    sturm::transpile::QUnit unit_;
    clang::ast_matchers::MatchFinder finder_;
    std::string source_path_;
    std::string output_dir_;
};

class TranspileAction : public clang::ASTFrontendAction {
public:
    TranspileAction(std::string source_path, std::string output_dir)
        : source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<TranspileConsumer>(source_path_, output_dir_);
    }
private:
    std::string source_path_;
    std::string output_dir_;
};

class TranspileFactory : public clang::tooling::FrontendActionFactory {
public:
    TranspileFactory(std::string src, std::string out)
        : src_(std::move(src)), out_(std::move(out)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<TranspileAction>(src_, out_);
    }
private:
    std::string src_;
    std::string out_;
};

} // namespace

// Read up to 1 KiB from the head of `path` — enough to cover any credible
// leading-blank + sentinel line count — so we can detect skip / already-
// generated files without loading the whole file.
static std::string read_head(const fs::path& path, std::size_t n = 1024) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string buf;
    buf.resize(n);
    in.read(buf.data(), static_cast<std::streamsize>(n));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

// Copy `path` verbatim to `<output_dir>/<resolved relpath>`.
// Returns true on success.
static bool verbatim_copy(const fs::path& path, const fs::path& output_dir) {
    std::string bytes;
    if (!sturm::transpile::read_file(path, bytes)) return false;
    fs::path dst = sturm::transpile::resolve_output_path(path, output_dir);
    return sturm::transpile::write_file(dst, bytes);
}

// ── Driver ────────────────────────────────────────────────────────────────────

int main(int argc, const char** argv) {
    if (handle_version_flag(argc, argv)) return 0;

    auto expected_parser =
        clang::tooling::CommonOptionsParser::create(
            argc, argv, kToolCategory,
            /*OccurrencesFlag=*/cl::OneOrMore,
            /*Overview=*/
            "sturm-transpile: STURM's Clang LibTooling-based uncomputation\n"
            "transpiler.\n\n"
            "USAGE:\n"
            "  sturm-transpile <input.cpp> --output-dir <dir>\n"
            "  sturm-transpile --version\n\n"
            "For each input source file, sturm-transpile parses it with\n"
            "Clang and rewrites quantum intermediates with explicit\n"
            "uncompute_* calls. Output is written to\n"
            "<output-dir>/<relpath-of-input> with an AUTO-GENERATED header.\n"
            "Files whose first non-blank line is either the magic comment\n"
            "`// sturm-transpile: skip` or the AUTO-GENERATED sentinel\n"
            "already emitted by a prior run are copied through verbatim.\n");
    if (!expected_parser) {
        llvm::errs() << toString(expected_parser.takeError());
        return 2;
    }
    auto& parser = *expected_parser;

    const auto& inputs = parser.getSourcePathList();
    if (inputs.empty()) {
        std::fprintf(stderr,
                     "sturm-transpile: error: no input file provided\n");
        return 2;
    }
    if (inputs.size() > 1) {
        std::fprintf(stderr,
                     "sturm-transpile: error: multiple inputs not yet "
                     "supported\n");
        return 2;
    }
    const std::string& input_path = inputs.front();

    // Fail-fast on missing input.
    std::error_code ec;
    if (!fs::exists(fs::path(input_path), ec) || ec) {
        std::fprintf(stderr,
                     "sturm-transpile: error: input file not found: %s\n",
                     input_path.c_str());
        return 1;
    }

    // Skip detection: read the first 1 KiB and check for either sentinel.
    // On a hit we short-circuit to a verbatim byte-copy, enforcing PRD AC
    // #5 (idempotency) and AC #6 (skip marker).
    std::string head = read_head(fs::path(input_path));
    if (sturm::transpile::should_skip(head)) {
        if (!verbatim_copy(fs::path(input_path),
                           fs::path(kOutputDir.getValue()))) {
            std::fprintf(stderr,
                         "sturm-transpile: error: verbatim copy failed for "
                         "%s\n", input_path.c_str());
            return 1;
        }
        return 0;
    }

    // Full pipeline. Run the tool with the compilation database resolved
    // by CommonOptionsParser and our custom factory; the factory's consumer
    // drives M7 → M8 → M9.
    //
    // We deliberately use parser.getCompilations() rather than constructing
    // our own empty FixedCompilationDatabase: doing so lets sturm-transpile
    // honor `--extra-arg=-I...`, `--extra-arg=-D...`, `--extra-arg=-std=...`,
    // and any compile_commands.json that lives alongside the input. This is
    // what lets the build-system glue (cmake/SturmTranspile.cmake) propagate
    // the include paths and feature defines a real example like
    // examples/or_circuit.cpp needs in order for `sturm::qbool` to resolve
    // — without those, the matcher's `cxxRecordDecl(hasName("qbool"))`
    // would never fire on the real header chain (LP4 risk R1).
    std::vector<std::string> source_paths{input_path};
    clang::tooling::ClangTool tool(parser.getCompilations(), source_paths);
    // Suppress diagnostics: the MVP transpiler does not need to surface
    // parse errors (the user will re-see them in the downstream compile).
    tool.setDiagnosticConsumer(new clang::IgnoringDiagConsumer());

    TranspileFactory factory(input_path, kOutputDir.getValue());
    int tool_rc = tool.run(&factory);
    // `tool.run` returns non-zero on hard parse failures. Treat them as
    // soft: if the emitter managed to write an output (because the AST
    // was recoverable), we still prefer returning 0 so downstream CMake
    // builds see the generated file. If NO output was produced we return
    // the tool's error code so the caller notices.
    fs::path expected_out = sturm::transpile::resolve_output_path(
        fs::path(input_path), fs::path(kOutputDir.getValue()));
    if (!fs::exists(expected_out)) {
        // Nothing landed on disk — treat that as a hard failure.
        if (tool_rc == 0) tool_rc = 1;
        std::fprintf(stderr,
                     "sturm-transpile: error: no output produced for %s\n",
                     input_path.c_str());
        return tool_rc;
    }
    return 0;
}
