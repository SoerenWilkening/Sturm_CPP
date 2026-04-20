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

#include "sturm/transpile/io.hpp"
#include "sturm/transpile/skip.hpp"

// PM1-1: the ASTConsumer body (matcher pool, post-walk backstops, emit
// step) lives in transpile_consumer.{hpp,cpp} so the forthcoming plugin
// (PM1-3) can reuse it. This standalone driver only wraps it in a
// FrontendAction for the ClangTool-driven invocation below.
#include "transpile_consumer.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
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

// PM1-6: --output-dir is NO LONGER unconditionally required. When
// --dump-transpiled (see below) is supplied, the rewritten buffer lands
// at the user-chosen path directly and the output-dir pathway is
// irrelevant. At least one of the two must still be present — the
// positional-input-only invocation would have nowhere to write. We
// enforce that at runtime after option parsing rather than via
// cl::Required on either flag, because cl::Required would reject the
// otherwise-valid `--dump-transpiled` invocation.
static cl::opt<std::string> kOutputDir(
    "output-dir",
    cl::desc("Destination directory for transpiled output files "
             "(optional when --dump-transpiled is set)"),
    cl::value_desc("dir"),
    cl::Optional,
    cl::cat(kToolCategory));

// PM1-6: --dump-transpiled / -d <path>. When set, the standalone binary
// writes the rewritten buffer (header + body) to <path> instead of to
// resolve_output_path(input, output_dir). This is the quick debugging
// hook for "show me what the plugin would emit for one file without
// touching CMake" — the same contract the plugin honours via
// -fplugin-arg-sturm-transpile-dump-to=<path> (see transpiler/src/plugin.cpp).
// The gate the PM1-6 issue commits us to is byte-for-byte equivalence:
// `sturm-transpile foo.cpp --output-dir gen` must land the same bytes
// at `gen/foo.cpp` as `sturm-transpile foo.cpp --dump-transpiled
// /tmp/out.cpp` lands at `/tmp/out.cpp` (sans the path component of
// the destination). The implementation reuses emit_to_string +
// idempotency_header from the M9 emitter so that invariant is
// trivially maintained.
static cl::opt<std::string> kDumpTranspiled(
    "dump-transpiled",
    cl::desc("Write the rewritten buffer (header + body) to this exact "
             "path instead of `<output-dir>/<relpath-of-input>`. "
             "Makes --output-dir optional."),
    cl::value_desc("path"),
    cl::Optional,
    cl::cat(kToolCategory));

// Short alias `-d <path>` mirrors the issue's cl::opt name contract.
// Declared as a separate cl::opt so LLVM's parser accepts both spellings
// and a late-argument `-d=<path>` equivalently.
static cl::alias kDumpTranspiledShort(
    "d",
    cl::desc("Alias for --dump-transpiled=<path>"),
    cl::aliasopt(kDumpTranspiled),
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
// The action holds no state of its own; the ASTConsumer
// (sturm::transpile::TranspileConsumer, shared with the PM1-3 plugin)
// does the real work. We thread the source path + output dir through
// the action constructor so the consumer can hand them to emit().

namespace {

class TranspileAction : public clang::ASTFrontendAction {
public:
    TranspileAction(std::string source_path, std::string output_dir,
                    std::string dump_transpiled_path)
        : source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)),
          dump_transpiled_path_(std::move(dump_transpiled_path)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& ci, llvm::StringRef) override {
        // Standalone driver: hand the shared consumer the file-emission
        // mode so its HandleTranslationUnit lands the rewritten source
        // on disk via the M5 idempotency header + M9 emit() call path.
        // Plugin callers (PM1-3) construct the same consumer with
        // EmissionMode::Plugin so the buffer is stashed in-memory for a
        // nested CompilerInvocation.
        //
        // PM1-6: when `dump_transpiled_path_` is non-empty the consumer
        // writes to that exact path instead of resolving against
        // `output_dir_`.
        return std::make_unique<sturm::transpile::TranspileConsumer>(
            ci,
            sturm::transpile::EmissionMode::StandaloneFile,
            source_path_,
            output_dir_,
            dump_transpiled_path_);
    }
private:
    std::string source_path_;
    std::string output_dir_;
    std::string dump_transpiled_path_;
};

class TranspileFactory : public clang::tooling::FrontendActionFactory {
public:
    TranspileFactory(std::string src, std::string out, std::string dump)
        : src_(std::move(src)),
          out_(std::move(out)),
          dump_(std::move(dump)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<TranspileAction>(src_, out_, dump_);
    }
private:
    std::string src_;
    std::string out_;
    std::string dump_;
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

// PM1-6: copy `path` verbatim to the user-supplied --dump-transpiled
// destination. Skip files (magic comment + already-generated) are passed
// through BYTE-IDENTICAL just like the output-dir variant — the dump
// destination is the sole mutation. Returns true on success.
static bool verbatim_copy_to(const fs::path& path, const fs::path& dst) {
    std::string bytes;
    if (!sturm::transpile::read_file(path, bytes)) return false;
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
            "  sturm-transpile <input.cpp> --dump-transpiled <path>\n"
            "  sturm-transpile --version\n\n"
            "For each input source file, sturm-transpile parses it with\n"
            "Clang and rewrites quantum intermediates with explicit\n"
            "uncompute_* calls. Output is written to\n"
            "<output-dir>/<relpath-of-input> with an AUTO-GENERATED header,\n"
            "or — when --dump-transpiled=<path> (-d <path>) is supplied —\n"
            "to <path> verbatim.\n"
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

    // PM1-6: validate --output-dir / --dump-transpiled mutual
    // non-emptiness. At least one destination flag must be present —
    // otherwise the driver has nowhere to write the rewritten buffer.
    // We allow BOTH to be set only because the legacy CI driving path
    // already supplies --output-dir; when --dump-transpiled is also
    // present it takes precedence (both the skip path and the full
    // pipeline path below write to the dump destination in that case).
    const bool have_dump = !kDumpTranspiled.getValue().empty();
    const bool have_outdir = !kOutputDir.getValue().empty();
    if (!have_dump && !have_outdir) {
        std::fprintf(stderr,
                     "sturm-transpile: error: one of --output-dir=<dir> "
                     "or --dump-transpiled=<path> is required\n");
        return 2;
    }

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
        bool ok = false;
        if (have_dump) {
            // PM1-6: the dump-transpiled path wins. A skipped file in
            // dump mode lands BYTE-IDENTICAL at the user-supplied path
            // — same invariant as the output-dir verbatim copy, just a
            // different destination.
            ok = verbatim_copy_to(fs::path(input_path),
                                  fs::path(kDumpTranspiled.getValue()));
        } else {
            ok = verbatim_copy(fs::path(input_path),
                               fs::path(kOutputDir.getValue()));
        }
        if (!ok) {
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

    // Inject -resource-dir so libTooling can find its builtin headers
    // (stdarg.h, stddef.h, etc.). Without this, the Clang driver fails to
    // resolve macOS libc++ typedefs like __uint32_t / __darwin_wint_t and
    // the parser abandons main-file translation before the user's body is
    // built into the AST — every Phase A-I matcher then sees no ops from
    // the user's code and the transpile produces a byte-identical pass-
    // through.
    //
    // Lookup order:
    //   1. `<bindir>/../lib/clang/<LLVM_VERSION_MAJOR>` next to the running
    //      executable. This is the only path that survives a relocatable
    //      tarball install (the build job ships the Clang builtin-headers
    //      dir alongside `bin/sturm-transpile`), so it must come first.
    //   2. `STURM_CLANG_RESOURCE_DIR` baked in at CMake-configure time from
    //      the LLVM package the transpiler was linked against. This keeps
    //      local in-tree builds green even when the install rule does not
    //      run (e.g. `cmake --build build` + running the binary directly).
    //
    // The adjuster prepends the arg at BEGIN, so a user-supplied
    // `--extra-arg=-resource-dir=...` that appears later in argv wins.
    {
        std::string rd;
        const std::string exe_path = llvm::sys::fs::getMainExecutable(
            argv[0], reinterpret_cast<void*>(&main));
        if (!exe_path.empty()) {
            llvm::SmallString<256> candidate(exe_path);
            llvm::sys::path::remove_filename(candidate);                 // strip exe
            llvm::sys::path::remove_filename(candidate);                 // strip bin/
            llvm::sys::path::append(candidate, "lib", "clang",
                                    STURM_LLVM_VERSION_MAJOR_STR);
            if (llvm::sys::fs::is_directory(candidate)) {
                rd = std::string(candidate.str());
            }
        }
#ifdef STURM_CLANG_RESOURCE_DIR
        if (rd.empty()) rd = STURM_CLANG_RESOURCE_DIR;
#endif
        if (!rd.empty()) {
            const std::string rd_arg = std::string("-resource-dir=") + rd;
            tool.appendArgumentsAdjuster(
                clang::tooling::getInsertArgumentAdjuster(
                    rd_arg.c_str(),
                    clang::tooling::ArgumentInsertPosition::BEGIN));
        }
    }

    TranspileFactory factory(input_path, kOutputDir.getValue(),
                             kDumpTranspiled.getValue());
    int tool_rc = tool.run(&factory);
    // `tool.run` returns non-zero on hard parse failures. Treat them as
    // soft: if the emitter managed to write an output (because the AST
    // was recoverable), we still prefer returning 0 so downstream CMake
    // builds see the generated file. If NO output was produced we return
    // the tool's error code so the caller notices.
    //
    // PM1-6: when --dump-transpiled is set the expected output lives at
    // the user-supplied path, not `resolve_output_path(...)`. Check that
    // instead.
    fs::path expected_out = have_dump
        ? fs::path(kDumpTranspiled.getValue())
        : sturm::transpile::resolve_output_path(
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
