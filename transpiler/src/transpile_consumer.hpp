// transpile_consumer.hpp — PM1-1: shared TranspileConsumer.
//
// The consumer drives the MVP pipeline inside a clang::ASTConsumer's
// HandleTranslationUnit hook:
//
//   matcher pool (Phase A..I + PJ-1/PJ-3/PJ-4) → synthesize → post-walk
//   backstops → emit
//
// Two clients consume the same class:
//
//   EmissionMode::StandaloneFile
//     The sturm-transpile binary (main.cpp). HandleTranslationUnit lands
//     the rewritten source on disk at `<output_dir>/<relpath-of-source>`,
//     prepending the M5 idempotency header. This is the pre-PM1
//     byte-identical behavior.
//
//   EmissionMode::Plugin  (PM1-3+, forthcoming)
//     A clang PluginASTAction. HandleTranslationUnit still runs the whole
//     matcher + synthesis chain, but instead of writing to disk it hands
//     the rewritten buffer to a nested CompilerInvocation. The header
//     prepend is skipped (a sentinel comment inside the TU body is fine,
//     but the nested compile would choke on the full header lines). The
//     backend wiring lands in sturm-gnmo / sturm-bkcr; the enum knob
//     here is the extraction point PM1-1 owes the plugin work.
//
// The consumer takes the parent CompilerInstance by reference so future
// Plugin-mode code (PM1-4) can use it to construct an OverlayFileSystem
// + nested invocation without the action having to thread extra state
// back to the consumer. StandaloneFile mode does not read from the CI —
// it works off the ASTContext the base ASTConsumer already exposes.

#ifndef STURM_TRANSPILE_CONSUMER_HPP
#define STURM_TRANSPILE_CONSUMER_HPP

#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

#include "diag_context.hpp"
#include "routine_registry.hpp"

#include <string>

namespace clang { class CompilerInstance; class ASTContext; }

namespace sturm::transpile {

/// PM1-1 — selects the post-walk output contract.
enum class EmissionMode {
    /// Standalone driver: write the rewritten buffer to
    /// `<output_dir>/<relpath-of-source>` with the M5 idempotency header.
    StandaloneFile,
    /// In-process plugin: stash the rewritten buffer on the consumer
    /// instead of writing to disk, so the wrapping PluginASTAction can
    /// hand it to a nested CompilerInvocation. Skip the idempotency
    /// header — the nested parse would treat the `// AUTO-GENERATED`
    /// sentinel as ordinary comment noise, but duplicating it through
    /// a subsequent standalone run would double-prepend.
    Plugin,
};

/// PM1-1 — the shared ASTConsumer driving the matcher pool and emission.
///
/// Construct per translation unit. The consumer registers the full
/// Phase A..I + PJ-1/PJ-3/PJ-4 matcher pool in its constructor (matching
/// the pre-PM1 registration order in main.cpp — downstream blocks rely
/// on that ordering) and runs it during HandleTranslationUnit, followed
/// by the post-walk fused / eliminated backstops, synthesis, and the
/// mode-specific emit step.
class TranspileConsumer : public clang::ASTConsumer {
public:
    /// - `ci`           : parent CompilerInstance. Referenced for Plugin
    ///                    mode's nested invocation; unused in StandaloneFile.
    /// - `mode`         : selects the post-walk output contract.
    /// - `source_path`  : user-facing input path. Embedded in the M5
    ///                    header (StandaloneFile) and passed through to
    ///                    resolve_output_path.
    /// - `output_dir`   : destination directory. Ignored in Plugin mode.
    ///                    Ignored in StandaloneFile mode when
    ///                    `dump_transpiled_path` is non-empty.
    /// - `dump_transpiled_path` : PM1-6. StandaloneFile-mode override. When
    ///                    non-empty, the rewritten buffer + idempotency
    ///                    header are written to this exact path instead
    ///                    of `resolve_output_path(source_path, output_dir)`.
    ///                    Ignored in Plugin mode (the plugin has its own
    ///                    `dump-to=<path>` arg that is handled in the
    ///                    wrapping PluginASTAction, not here).
    TranspileConsumer(clang::CompilerInstance& ci,
                      EmissionMode mode,
                      std::string source_path,
                      std::string output_dir,
                      std::string dump_transpiled_path = {});

    void HandleTranslationUnit(clang::ASTContext& ctx) override;

    /// Plugin-mode accessor. Returns the rewritten main-file buffer
    /// emitted by `emit_to_string` after HandleTranslationUnit. Empty
    /// when the consumer was constructed with EmissionMode::StandaloneFile
    /// or when HandleTranslationUnit has not yet run.
    const std::string& rewritten_buffer() const { return rewritten_buffer_; }

    EmissionMode mode() const { return mode_; }

private:
    clang::CompilerInstance& ci_;
    EmissionMode mode_;
    std::string source_path_;
    std::string output_dir_;
    // PM1-6: StandaloneFile-mode override. When non-empty, emit lands
    // at this exact path (header + rewritten buffer) instead of
    // resolve_output_path(source_path_, output_dir_).
    std::string dump_transpiled_path_;

    // PM3-0: shared DiagContext threaded into PM3 matcher registrations.
    // Constructed from `ci_.getDiagnostics()` in the ctor init list so
    // the engine reference binds to the same DiagnosticsEngine the
    // PM3-1 `TextDiagnosticPrinter` is attached to (standalone driver)
    // or the plugin's parent CompilerInstance (plugin mode). PM3-0 only
    // constructs this member — it is not yet threaded into any matcher
    // layer. PM3-2 .. PM3-6 extend the matcher signatures to accept
    // `DiagContext&` and replace raw `fprintf` / `Report` call sites
    // with `diag_.report_*(...)` invocations.
    DiagContext diag_;

    QUnit unit_;
    // Phase I PI-1: context-wide forward/adjoint map populated by the
    // routine-registry matcher. Lives here — alongside `unit_` — so both
    // are destroyed together with the ASTContext the matcher ran under
    // (the registry stores raw FunctionDecl pointers with
    // ASTContext-bound lifetime). Pre-Phase-I consumers leave this
    // empty; downstream PI-2..PI-7 matchers consult it.
    RoutineRegistry registry_;
    clang::ast_matchers::MatchFinder finder_;

    // Plugin-mode stash — populated by HandleTranslationUnit when
    // `mode_ == EmissionMode::Plugin`.
    std::string rewritten_buffer_;
};

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_CONSUMER_HPP
