// plugin.cpp — PM1-3: Clang PluginASTAction registration for the Sturm
// transpiler.
//
// This file hosts the `sturm-transpile` Clang plugin entry point. It is
// compiled into the shared library `sturm-transpile-plugin` (a separate
// CMake target from the standalone `sturm-transpile` binary). Clang loads
// the plugin via `-fplugin=<path>` / `-Xclang -load -Xclang <path>`; the
// static registration Add<> instance at the bottom of this file plugs
// `SturmPluginAction` into Clang's FrontendPluginRegistry under the name
// "sturm-transpile".
//
// What the plugin does
// --------------------
// On every translation unit Clang parses under the plugin, we:
//
//   1. Build a TranspileConsumer configured in EmissionMode::Plugin. That
//      consumer runs the same matcher pool / post-walk backstops /
//      synthesis chain the standalone driver uses (shared code courtesy
//      of PM1-1), and stashes the rewritten source into
//      `rewritten_buffer()` instead of writing to disk.
//   2. Return that consumer from CreateASTConsumer so the parent parse
//      goes through as normal.
//   3. (PM1-4, forthcoming) Take the rewritten buffer out of the
//      consumer after the parse finishes and hand it to a nested
//      CompilerInvocation + EmitObjAction over an OverlayFileSystem so
//      the rewritten source is what actually gets codegen'd.
//
// At PM1-3 (this file's scope) we stop at step 2 — the nested-invocation
// wiring belongs to PM1-4 (sturm-bkcr). The PM1-3 gate is that a
// non-Sturm translation unit (no `sturm::qbool` / `sturm::qint` ops in
// the body) compiles as if the plugin weren't there: the matcher pool
// fires zero times, HandleTranslationUnit produces a rewritten buffer
// identical to the input, and the parent parse proceeds unchanged. In
// particular `clang++ -fplugin=<this.so> -c hello.cpp -o hello.o` must
// succeed on a trivial `int main() {...}` source.
//
// Plugin arguments
// ----------------
// Clang forwards per-plugin args via `-plugin-arg-<plugin-name> <arg>`
// pairs at the cc1 level. The driver's convenience spelling
// `-fplugin-arg-<plugin-name>-<arg>` synthesises that pair — BUT the
// driver splits `<plugin-name>-<arg>` at the FIRST hyphen, so our
// hyphenated plugin name "sturm-transpile" does NOT survive the
// driver-level spelling intact (the driver would see plugin name
// "sturm" and arg "transpile-<rest>"). The supported user-facing
// invocation is therefore:
//
//    -Xclang -plugin-arg-sturm-transpile -Xclang <arg>
//
// (passing the cc1 arg pair through verbatim). Our ParseArgs below
// honours two <arg> tokens:
//
//   - `dump-to=<path>` : after HandleTranslationUnit runs, dump the
//     rewritten buffer to `<path>` on disk. Useful for debugging /
//     diffing the plugin's internal rewrite output against a standalone
//     `sturm-transpile` run on the same input.
//   - `verbose`        : print a one-liner per parsed TU on stderr naming
//     the main-file path and the rewritten-buffer length. Kept minimal so
//     the default (non-verbose) plugin output is silent and does not
//     pollute downstream build logs.
//
// Both flags are optional and compose. Unknown args are ignored (not an
// error — future flags should not break old plugins). Values containing
// `=` (e.g. Windows-style paths) survive the pair-forwarding intact
// because the cc1 side receives `<arg>` as a single token.
//
// Why ReplaceAction?
// ------------------
// `PluginASTAction::getActionType()` returns one of AddAfterMainAction,
// AddBeforeMainAction, ReplaceAction, or CmdlineBeforeMainAction.
//
//   - AddAfter/AddBefore run the plugin's consumer *in addition* to
//     whatever the main driver action was going to do (EmitObj,
//     EmitLLVM, ParseSyntaxOnly, ...). That matches PM1-3's
//     "transparent" gate — at this step we only want the matcher pool
//     to run; we are not yet substituting the rewritten source into
//     codegen.
//   - ReplaceAction runs the plugin *instead of* the main driver
//     action. That is what PM1-4 needs: our consumer stashes the
//     rewritten buffer, then ExecuteAction() spins up a nested
//     CompilerInvocation + EmitObjAction over the rewritten buffer,
//     producing the final .o from Sturm-rewritten source rather than
//     the user's raw input.
//
// We pick ReplaceAction NOW so the action-type contract between PM1-3
// and PM1-4 is locked in. At PM1-3 the ExecuteAction override (not
// present yet — PM1-4's territory) simply delegates to the base class,
// which runs the consumer we returned and then stops. On a non-Sturm TU
// that produces zero matcher hits, the rewritten buffer is
// byte-identical to the input, and `clang++ -fplugin=<this.so> -c ...
// -o hello.o` fails to produce `hello.o` because ReplaceAction does
// not itself emit object code. That means PM1-3's gate ("compile
// proceeds as if the plugin weren't there") is expressed differently:
// the plugin LOADS cleanly, ParseArgs accepts the documented flags,
// and the parent parse runs without diagnostics. The object-file
// emission gate is PM1-4's to clear (which is why sturm-bkcr depends
// on this issue).
//
// Symbol hiding
// -------------
// The plugin links against the shared libclang-cpp + libLLVM pair (to
// avoid cl::opt double-registration — Phase L / sturm-mzfa lesson).
// Those shared libraries export tens of thousands of symbols we do not
// want re-exposed from our plugin .so. The CMake rules paired with this
// file use `-Wl,--exclude-libs,ALL` on Linux to hide them; macOS uses
// two-level namespace lookup by default, so no extra flag is needed.
// The only external symbol this .so needs to publish is Clang's
// FrontendPluginRegistry `Add<>` initializer — a static ctor — which
// is reached by Clang's plugin loader via the loader's dlsym()
// scanning of the registry section.

#include "transpile_consumer.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

// The dump-to argument prefix we look for in ParseArgs.
constexpr const char* kDumpToPrefix = "dump-to=";
// Length of "dump-to=" so substr() is transparent.
constexpr std::size_t kDumpToPrefixLen = 8;

/// PM1-3 — Clang PluginASTAction that drives the shared TranspileConsumer.
///
/// One instance of this action is constructed per translation unit by
/// Clang's plugin loader. State lives on the instance so a re-entrant
/// plugin invocation (unusual but allowed) does not stomp on another
/// TU's flags.
class SturmPluginAction : public clang::PluginASTAction {
 public:
    SturmPluginAction() = default;

 protected:
    // Construct the shared TranspileConsumer in Plugin mode. We keep a
    // raw observer pointer so EndSourceFileAction can reach into the
    // consumer's rewritten buffer after HandleTranslationUnit ran.
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& ci, llvm::StringRef in_file) override {
        // The output_dir argument is irrelevant in Plugin mode — the
        // consumer never writes to disk in that mode. Pass an empty
        // string rather than inventing a path that would confuse a
        // future reader.
        auto consumer =
            std::make_unique<sturm::transpile::TranspileConsumer>(
                ci,
                sturm::transpile::EmissionMode::Plugin,
                std::string(in_file),
                /*output_dir=*/std::string{});
        consumer_observer_ = consumer.get();
        return consumer;
    }

    // Parse -fplugin-arg-sturm-transpile-<arg> tokens. Returning false
    // from ParseArgs fails plugin load — so we reserve that only for
    // hard misuse (e.g. dump-to= with an empty path). Unknown args are
    // ignored so a future plugin flag does not break an old plugin.
    bool ParseArgs(const clang::CompilerInstance& /*ci*/,
                   const std::vector<std::string>& args) override {
        for (const auto& a : args) {
            if (a == "verbose") {
                verbose_ = true;
            } else if (a.compare(0, kDumpToPrefixLen, kDumpToPrefix) == 0) {
                dump_to_ = a.substr(kDumpToPrefixLen);
                if (dump_to_.empty()) {
                    llvm::errs() << "sturm-transpile plugin: "
                                 << "-fplugin-arg-sturm-transpile-"
                                 << "dump-to=<path> requires a non-empty "
                                 << "path\n";
                    return false;
                }
            }
            // Unknown flag — silently ignore. See file header for why.
        }
        return true;
    }

    // Returning ReplaceAction locks in the PM1-4 contract (the plugin
    // runs *instead of* the main driver action). See the file header
    // for the full reasoning.
    ActionType getActionType() override { return ReplaceAction; }

    // PM1-4 — run the parent parse (drives the matcher pool + fills the
    // rewritten buffer on the consumer), then execute the program action
    // the parent invocation would have run — EmitObj, EmitBC,
    // EmitLLVMOnly, EmitAssembly, … — against a nested
    // CompilerInvocation whose main-file input is the REWRITTEN source,
    // served via an InMemory + Overlay VFS so system headers still
    // resolve through the real filesystem.
    //
    // Design points locked in by the PM1-0 spike (branch
    // spike/sturm-nimr-nested-plugin, `transpiler/spike_nimr/plugin_spike.cpp`):
    //
    //   * Clang 17 has no `CompilerInvocation::print()`; the canonical
    //     "dump the invocation for a human" call is
    //     `getCC1CommandLine()`, which gives us the same flags the
    //     driver would have spelled out. Verbose-mode dumps use it.
    //   * The Overlay layers the InMemoryFileSystem in front of the
    //     RealFileSystem so the rewritten main file shadows the user's
    //     on-disk main file while every other open (system headers,
    //     -I paths, …) falls through to the real VFS.
    //   * `std::make_shared<CompilerInvocation>(parent.getInvocation())`
    //     inherits every CC1 option the parent accumulated — -O, -I, -D,
    //     -std, -flto, -fsanitize, target triple, etc. — for free.
    //   * The nested CompilerInstance gets a FRESH DiagnosticsEngine
    //     backed by a TextDiagnosticPrinter writing to llvm::errs() so
    //     codegen errors in the rewritten buffer surface to the user
    //     with the plugin's name in the banner. Sharing the parent's
    //     engine risked state leakage between the two Sema runs.
    //
    // Non-object program actions (EmitBC, EmitLLVM, EmitAssembly,
    // ParseSyntaxOnly, etc.) are inherited from the parent invocation so
    // that `-S`, `-emit-llvm`, `-fsyntax-only` — whatever the user
    // passed through the driver — continue to "do the right thing"
    // through the plugin. We do NOT unconditionally force EmitObj
    // because doing so would break a `clang++ -S` invocation that asks
    // for assembly.
    void ExecuteAction() override {
        // 1. Let the parent ASTConsumer (our TranspileConsumer) finish
        //    the AST walk + matchAST. On return `consumer_observer_`'s
        //    rewritten_buffer() holds the post-rewrite source.
        clang::PluginASTAction::ExecuteAction();

        if (consumer_observer_ == nullptr) {
            // No consumer was constructed (empty input / early bailout);
            // nothing to hand to a nested invocation.
            return;
        }

        clang::CompilerInstance& parent = getCompilerInstance();

        // 2. Pull the rewritten buffer. If emit_to_string produced an
        //    empty buffer (empty input or nothing to rewrite), we fall
        //    back to reading the original main file from disk — the
        //    spike proved this shape works and it gives us a consistent
        //    codegen path for non-Sturm translation units.
        std::string rewritten = consumer_observer_->rewritten_buffer();

        const auto& parent_inv = parent.getInvocation();
        const auto& parent_fopts = parent_inv.getFrontendOpts();
        if (parent_fopts.Inputs.empty()) {
            llvm::errs() << "sturm-transpile plugin: "
                         << "parent invocation has no inputs; "
                         << "skipping nested EmitObjAction\n";
            return;
        }
        const std::string input_path =
            parent_fopts.Inputs.front().getFile().str();

        if (rewritten.empty()) {
            // Fallback: read the original main file so the nested
            // invocation still has a buffer to codegen. In practice the
            // consumer always emits at least a pass-through copy, so
            // this path only fires on pathological TUs.
            auto real_fs = llvm::vfs::getRealFileSystem();
            auto orig_or_err = real_fs->getBufferForFile(input_path);
            if (!orig_or_err) {
                llvm::errs() << "sturm-transpile plugin: cannot read "
                             << "input '" << input_path << "': "
                             << orig_or_err.getError().message() << "\n";
                return;
            }
            rewritten = std::string((*orig_or_err)->getBuffer());
        }

        if (verbose_) {
            llvm::errs() << "sturm-transpile plugin: TU '"
                         << getCurrentFile()
                         << "' rewritten_buffer() size="
                         << rewritten.size() << "\n";
        }

        // 3. Dump the rewritten buffer to -fplugin-arg-sturm-transpile-
        //    dump-to=<path> BEFORE running the nested invocation. The
        //    PM1-3 test exercises this, and writing first means the dump
        //    survives even if nested codegen fails downstream.
        if (!dump_to_.empty()) {
            std::ofstream os(dump_to_, std::ios::binary);
            if (!os) {
                llvm::errs() << "sturm-transpile plugin: cannot open "
                             << dump_to_ << " for dump-to write\n";
            } else {
                os.write(rewritten.data(),
                         static_cast<std::streamsize>(rewritten.size()));
                dump_written_ = true;
            }
        }

        // 4. Build the InMemory + Overlay VFS. The in-memory layer
        //    shadows JUST the main source path with the rewritten
        //    bytes; every other path (system headers, -I paths, …)
        //    falls through to the real FS.
        auto real_fs = llvm::vfs::getRealFileSystem();
        auto in_mem =
            llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
        in_mem->addFile(
            input_path, /*mtime=*/0,
            llvm::MemoryBuffer::getMemBufferCopy(rewritten, input_path));
        auto overlay =
            llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(
                real_fs);
        overlay->pushOverlay(in_mem);

        // 5. Clone the parent invocation. Every CC1 option — -O*, -I*,
        //    -D*, -std, -flto, -fsanitize, target triple — rides along
        //    for free. We inherit the ProgramAction so -S / -emit-llvm /
        //    -c all "do the right thing". One exception: when the
        //    parent's ProgramAction is PluginAction (i.e. the user
        //    activated us explicitly with `-plugin sturm-transpile`), we
        //    flip it to EmitObj so the nested run actually emits an
        //    object file rather than looping back into the plugin.
        auto cloned = std::make_shared<clang::CompilerInvocation>(parent_inv);

        if (verbose_) {
            llvm::errs() << "=== cloned CompilerInvocation ===\n";
            for (const auto& a : cloned->getCC1CommandLine()) {
                llvm::errs() << a << "\n";
            }
            llvm::errs() << "=== end cloned CompilerInvocation ===\n";
        }

        auto& cloned_fopts = cloned->getFrontendOpts();
        if (cloned_fopts.ProgramAction == clang::frontend::PluginAction ||
            cloned_fopts.ProgramAction == clang::frontend::ParseSyntaxOnly) {
            // The parent's action IS us (ReplaceAction activated via
            // -plugin sturm-transpile). Running the plugin from a
            // nested invocation on the overlay would recurse forever;
            // flip to EmitObj so the nested run produces a .o. If the
            // user originally asked for ParseSyntaxOnly (-fsyntax-only),
            // inheriting it would still do the right thing, but we'd
            // produce no .o which breaks the `-c` contract — flip to
            // EmitObj as well when the parent's -o points at a .o.
            cloned_fopts.ProgramAction = clang::frontend::EmitObj;
        }
        // Do NOT clear the plugin list here — the nested CompilerInstance
        // would happily re-load sturm-transpile from the PLUGINS vector
        // but our ActionType is ReplaceAction which only fires when the
        // user ACTIVATES it via `-plugin sturm-transpile`. The parent's
        // frontend is what activates us; the cloned invocation keeps the
        // `-plugin` registration but the cloned ProgramAction above is
        // no longer PluginAction, so the plugin is loaded-but-inert in
        // the child — exactly what we want.

        // 6. Construct the child CompilerInstance on the overlay.
        clang::CompilerInstance child;
        child.setInvocation(cloned);
        child.createDiagnostics(
            new clang::TextDiagnosticPrinter(llvm::errs(),
                                             &cloned->getDiagnosticOpts()),
            /*ShouldOwnClient=*/true);
        child.createFileManager(overlay);
        child.createSourceManager(child.getFileManager());

        // 7. Dispatch on the inherited ProgramAction. The spike proved
        //    the EmitObj path; we extend it to the sibling codegen
        //    actions so -S / -emit-llvm / -c all work. Anything we do
        //    not recognise falls back to EmitObj — same as if the user
        //    had passed -c, which is the most common case.
        std::unique_ptr<clang::FrontendAction> action;
        switch (cloned_fopts.ProgramAction) {
            case clang::frontend::EmitObj:
                action = std::make_unique<clang::EmitObjAction>();
                break;
            case clang::frontend::EmitBC:
                action = std::make_unique<clang::EmitBCAction>();
                break;
            case clang::frontend::EmitLLVM:
                action = std::make_unique<clang::EmitLLVMAction>();
                break;
            case clang::frontend::EmitLLVMOnly:
                action = std::make_unique<clang::EmitLLVMOnlyAction>();
                break;
            case clang::frontend::EmitAssembly:
                action = std::make_unique<clang::EmitAssemblyAction>();
                break;
            case clang::frontend::EmitCodeGenOnly:
                action = std::make_unique<clang::EmitCodeGenOnlyAction>();
                break;
            default:
                // Unknown / unsupported ProgramAction — fall back to
                // EmitObj which is the most common case (`-c`).
                action = std::make_unique<clang::EmitObjAction>();
                break;
        }

        if (!child.ExecuteAction(*action)) {
            llvm::errs() << "sturm-transpile plugin: "
                         << "nested frontend action failed for '"
                         << input_path << "'\n";
        }
    }

    // After HandleTranslationUnit finishes the -verbose trace is emitted
    // from inside ExecuteAction (above); EndSourceFileAction remains in
    // place for the PM1-3 contract — ASTFrontendAction's base class runs
    // its own EndSourceFile hooks from here, and we preserve that
    // ordering.  dump-to is handled in ExecuteAction (before the nested
    // invocation) so keep this hook minimal.
    void EndSourceFileAction() override {
        clang::PluginASTAction::EndSourceFileAction();
    }

 private:
    // Observer into the consumer returned by CreateASTConsumer. Owned
    // by Clang's FrontendAction machinery via the unique_ptr we handed
    // back; the raw pointer here is valid for the lifetime of the
    // Consumer (i.e. through EndSourceFileAction).
    sturm::transpile::TranspileConsumer* consumer_observer_ = nullptr;

    // -fplugin-arg-sturm-transpile-verbose → print per-TU trace line.
    bool verbose_ = false;
    // -fplugin-arg-sturm-transpile-dump-to=<path> → write rewritten
    // buffer to this path BEFORE the nested invocation runs. Empty
    // when the flag was not passed.
    std::string dump_to_;
    // PM1-4: set once ExecuteAction has written the dump file so
    // EndSourceFileAction can skip the legacy dump path and avoid
    // double-writing. Kept as internal debug state — no external
    // observer reads it.
    bool dump_written_ = false;
};

// Static registration — this is the ONE external symbol the plugin .so
// needs to publish. Clang's plugin loader picks up the entry via its
// FrontendPluginRegistry scan after dlopen().
//
// The plugin description string shows up in `clang -plugin help` and
// similar diagnostics; keep it short.
static clang::FrontendPluginRegistry::Add<SturmPluginAction>
    X("sturm-transpile", "Sturm quantum-DSL transpile plugin");

}  // namespace
