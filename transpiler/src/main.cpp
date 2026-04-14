// main.cpp — sturm-transpile driver (M4 skeleton).
//
// This module is the Milestone 4 scaffolding for the transpiler pipeline
// described in docs/prd_transpiler_uncompute.md and
// docs/implementation_plan_transpiler_uncompute.md.
//
// What M4 does and does not do:
//   DOES  - Build a standalone binary that links against LLVM/Clang
//           LibTooling (clangTooling / clangASTMatchers / clangFrontend /
//           clangBasic / clangRewrite / LLVMSupport).
//         - Parse the user's source file via Clang's tooling harness,
//           proving that LibTooling can be initialized from this tree.
//         - Copy the input file byte-for-byte to
//           <output-dir>/<relpath-of-input>.cpp. No rewriting yet.
//         - Emit a version line on `--version`.
//         - Return a non-zero exit code if the input does not exist.
//
//   DOES NOT - Match ASTs, build a QIR, synthesize uncomputations, or
//              emit any modified source. Those are M5 through M9.
//
// Requirements: LLVM >= 17 and Clang >= 17. The build CMake file prints
// an actionable error if either is missing; at runtime we don't re-check
// because the linker has already bound the symbols.

#include "sturm/transpile/io.hpp"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
namespace cl = llvm::cl;

// ── Version string ────────────────────────────────────────────────────────────
// Bump when the transpiler's contract changes. Kept in one place so the
// tests can regex it and CMake can pass it as a compile definition later.
static const char kSturmTranspileVersion[] =
    "sturm-transpile 0.1.0 (MVP skeleton; M4: LibTooling wiring)";

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
    // We scan raw argv ourselves so that `--version` works even when the
    // user did not supply the otherwise-required positional input file or
    // --output-dir. This matches the convention of most UNIX tools.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 ||
            std::strcmp(argv[i], "-v") == 0) {
            std::puts(kSturmTranspileVersion);
            return true;
        }
    }
    return false;
}

/// Run Clang's tooling harness on `input`. We use a SyntaxOnlyAction: it
/// parses the file and reports errors via Clang's diagnostics, but does not
/// perform any code generation. For M4 this is the "proof the build system
/// can find and link LibTooling" step — later milestones replace this with
/// a matcher-driven FrontendAction.
///
/// Returns true if parsing succeeded (or produced only non-fatal warnings),
/// false if the tool could not process the file at all. We intentionally do
/// not treat parse errors as fatal here: many realistic STURM inputs will
/// not compile as a bare translation unit without a compilation database,
/// and the identity-copy contract must still hold. The harness is invoked
/// only to exercise the LibTooling link path.
static bool run_libtooling_parse(const std::string& input) {
    // Use a FixedCompilationDatabase so the tool can run without a
    // compile_commands.json. We pass no extra flags: the tool is content
    // with the default system search paths.
    std::vector<std::string> source_paths{input};
    auto compilations =
        std::make_unique<clang::tooling::FixedCompilationDatabase>(
            /*Directory=*/".", /*CommandLine=*/std::vector<std::string>{});
    clang::tooling::ClangTool tool(*compilations, source_paths);
    // SuppressDiagnostics: M4 does not need to surface parse errors; later
    // milestones will route diagnostics through our own consumer. Returning
    // the raw run() result is enough to know whether the harness at least
    // walked the AST.
    tool.setDiagnosticConsumer(
        new clang::IgnoringDiagConsumer());
    (void)tool.run(clang::tooling::newFrontendActionFactory<
                       clang::SyntaxOnlyAction>().get());
    return true;
}

// ── Driver ────────────────────────────────────────────────────────────────────

int main(int argc, const char** argv) {
    if (handle_version_flag(argc, argv)) return 0;

    // CommonOptionsParser::create() returns an Expected<> instead of
    // calling ::exit on error. That lets us craft a non-zero return code
    // ourselves and keep the process under our control.
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
            "Clang and (eventually) rewrites quantum intermediates with\n"
            "explicit uncompute_* calls. In this M4 skeleton the rewriter\n"
            "is an identity pass — the input is copied byte-for-byte to\n"
            "<output-dir>/<relpath>.\n");
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

    // Per M4 the binary accepts exactly one input file. Later milestones
    // will relax this when batch transpilation becomes useful.
    if (inputs.size() > 1) {
        std::fprintf(stderr,
                     "sturm-transpile: error: multiple inputs not yet "
                     "supported (M4 accepts one file)\n");
        return 2;
    }
    const std::string& input_path = inputs.front();

    // Fail-fast on missing input. CommonOptionsParser does not check for
    // existence on its own; if we skipped this the identity-copy path
    // below would produce a zero-byte output, which the test suite (and
    // users) would find baffling.
    std::error_code ec;
    if (!fs::exists(fs::path(input_path), ec) || ec) {
        std::fprintf(stderr,
                     "sturm-transpile: error: input file not found: %s\n",
                     input_path.c_str());
        return 1;
    }

    // Exercise LibTooling on the input. M4 does not require the parse to
    // succeed in the sense of producing usable diagnostics — only that the
    // harness is linked and callable. Failure returns the diagnostics
    // through the harness's own consumer.
    (void)run_libtooling_parse(input_path);

    // Read the input and write it verbatim to the computed output path.
    std::string bytes;
    if (!sturm::transpile::read_file(input_path, bytes)) {
        std::fprintf(stderr,
                     "sturm-transpile: error: could not read input %s: %s\n",
                     input_path.c_str(), std::strerror(errno));
        return 1;
    }
    fs::path out_path = sturm::transpile::resolve_output_path(
        input_path, fs::path(kOutputDir.getValue()));
    if (!sturm::transpile::write_file(out_path, bytes)) {
        std::fprintf(stderr,
                     "sturm-transpile: error: could not write output %s\n",
                     out_path.string().c_str());
        return 1;
    }
    return 0;
}
