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
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/FrontendActions.h"

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

    // After HandleTranslationUnit finishes we honour the -verbose /
    // -dump-to flags. The rewritten buffer lives on the consumer; we
    // read it via the observer pointer stashed in CreateASTConsumer.
    //
    // TODO(backend): PM1-4 (sturm-bkcr) will override ExecuteAction
    // instead and hand the rewritten buffer to a nested
    // CompilerInvocation + EmitObjAction here. At PM1-3 we only need
    // the plumbing to reach this point without crashing on a non-Sturm
    // translation unit.
    void EndSourceFileAction() override {
        if (consumer_observer_ == nullptr) {
            clang::PluginASTAction::EndSourceFileAction();
            return;
        }
        const std::string& buf = consumer_observer_->rewritten_buffer();

        if (verbose_) {
            llvm::errs() << "sturm-transpile plugin: TU '"
                         << getCurrentFile()
                         << "' rewritten_buffer() size="
                         << buf.size() << "\n";
        }

        if (!dump_to_.empty()) {
            std::ofstream os(dump_to_, std::ios::binary);
            if (!os) {
                llvm::errs() << "sturm-transpile plugin: cannot open "
                             << dump_to_ << " for dump-to write\n";
            } else {
                os.write(buf.data(),
                         static_cast<std::streamsize>(buf.size()));
            }
        }

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
    // buffer to this path after HandleTranslationUnit runs. Empty
    // when the flag was not passed.
    std::string dump_to_;
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
