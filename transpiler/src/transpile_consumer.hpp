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
// PM4-3: the consumer OWNS a per-TU plugin Registry — not a process-wide
// singleton — so PM1-4's nested `CompilerInvocation` gets a fresh Registry
// when it constructs a second consumer. Threading the Registry object
// through synthesize() keeps each consumer's Registry scoped to its own
// lifetime.
#include "sturm/transpile/plugin_api.hpp"
// sturm-v0ur (LO-2 wiring): the consumer owns an `ExternalCleanup`
// vector populated by the LO drain block in `HandleTranslationUnit`.
// Pull in the full struct definition so member declarations resolve at
// header-parse time.
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

#include "diag_context.hpp"
#include "routine_registry.hpp"
#include "synthesis_registry.hpp"

// sturm-v0ur (LO-2 wiring): the consumer registers the lossy-op
// matcher (LO-2a) and feeds its hits through the LO-2b/2c emitters
// after `matchAST`. Member declarations need the full LossyOpHit
// struct shape, so the matcher header lands here at the consumer
// boundary alongside the existing routine / synthesis registries.
#include "matcher_lossy_op.hpp"
// sturm-rry6 (LO-2e): nested-lossy AST matcher emits NestedLossyHits for
// the outer-lossy / inner-bare-or-compound shape that LO-2a does not
// see. The consumer owns the hits vector alongside `lossy_hits_`.
#include "lossy_nested_rewrite.hpp"
// sturm-qzab.1 (P5.1 beat 5.1): modular-arithmetic AST matcher emits
// ModularOpHits for `(qint OP qint) % qint` shapes. The consumer owns
// the hits vector alongside `lossy_hits_` / `nested_hits_`. Drained
// after the post-walk backstops, BEFORE `synthesize()` so the
// statement-level VarDecl rewrite lands as a `QReplacement` in
// `unit_.replacements`.
#include "matcher_modular_op.hpp"
// sturm-ddgo: QRAM-via-array-subscript matchers. Three flavours:
//   - C1 (matcher_qram_subscript) for `qint b = a[i];` (bare init)
//   - H1 (matcher_qram_subscript_assign) for `b = a[i];` (existing target)
//   - H4 (matcher_qram_subscript_expr) for `qint c = a[i] + d;` (expr position)
// The consumer owns the per-flavour hits vectors and drains them via
// the corresponding `emit_qram_*_replacements` helpers after `matchAST`,
// folding each rewrite into `unit_.replacements` / `unit_.raw_insertions`
// alongside the modular / lossy pipelines so the standard `emit()` /
// `emit_to_string()` path applies them.
#include "matcher_qram_subscript.hpp"
#include "matcher_qram_subscript_assign.hpp"
#include "matcher_qram_subscript_expr.hpp"
// sturm-65rs.10 (Beat C3): qint alias substitution matcher. Drained
// AFTER the QRAM emitter so the QRAM-claimed VarDecls override the
// alias-subst VarDecl arm (PRD R2 / single-VarDecl-single-rewrite).
#include "matcher_qint_alias_subst.hpp"
// sturm-e3ru (Frontend simpl. P7 / Phase 7): main-lifecycle auto-injection.
// The matcher fires on the unique `int main(...)` FunctionDecl when the
// umbrella sentinel is defined and the escape hatch is not. Drained
// alongside the QRAM / modular drains; produces one `QReplacement`
// over main's body CompoundStmt range.
#include "matcher_main_lifecycle.hpp"

#include <string>
#include <vector>

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
    // Phase M PM4-3: per-consumer plugin Registry. Owned by the consumer
    // (NOT a global singleton) so the PM1-4 nested `CompilerInvocation`
    // sees a fresh Registry when its own consumer is constructed. The
    // ctor drains the two process-wide Meyer vectors —
    // `runtime_registrars()` (appended by `plugin.cpp`'s `load=` branch)
    // and `registrars()` (appended by `StaticRegistrar` at static init)
    // — into this per-consumer Registry. The drain order follows plan §6:
    // in-tree matcher registration (directly in the consumer ctor body
    // below) → runtime-dlopen plugins → link-time plugins. Matchers
    // registered by plugins are invoked against the same `finder_` /
    // `unit_` the in-tree matchers use via `plugin_registry_.invoke_all`.
    // During `HandleTranslationUnit`, a pointer to this member is passed
    // to `synthesize()` so the M8 uncompute pass's
    // `case QOpKind::PLUGIN:` arm can consult `find_render_fn(...)`
    // against the same Registry the plugin's matcher registered against.
    sturm::transpile::plugin::Registry plugin_registry_;
    // Phase T T-1 (sturm-xrob.2): context-wide synthesis registry
    // populated by the reversible-drive collector matcher. Lives
    // alongside `unit_` and `registry_` so the three AST-bound
    // containers are destroyed together with the ASTContext their
    // matchers ran under. `drive_reversible_forwards` reads this
    // registry at end-of-TU to stitch auto-synthesised `__fn_adj` +
    // `STURM_REGISTER_ADJOINT` text into `unit_.raw_insertions`,
    // ahead of the second PM3 transpile pass.
    SynthesisRegistry synth_registry_;
    // sturm-v0ur (LO-2 wiring): hits vector populated by the LO-2a
    // matcher (`register_lossy_op_matcher`). Lives alongside `unit_`
    // because the AST-bound `LossyOpHit::call` / `enclosing_block`
    // pointers must outlive `matchAST` but die before `ASTContext`
    // tear-down. Drained by `HandleTranslationUnit` after the
    // post-walk backstops, BEFORE `synthesize()` so the LO-2b
    // forward triplets feed `unit_.replacements` and the LO-2c
    // cleanups feed the `synthesize()` external-cleanup overload.
    std::vector<LossyOpHit> lossy_hits_;
    // sturm-rry6 (LO-2e): hits vector populated by the nested-lossy
    // matcher (`register_nested_lossy_matcher`). Drained BEFORE
    // `lossy_hits_` in `HandleTranslationUnit` so the inner+outer
    // forward triplet is planted in `unit_.replacements` ahead of any
    // LO-2a-triggered Phase C suppression — the outer call's begin loc
    // is the same key that suppresses the Phase C *_ASSIGN_QINT op.
    std::vector<NestedLossyHit> nested_hits_;
    // sturm-qzab.1 (P5.1 beat 5.1): hits vector populated by the
    // modular-arithmetic matcher (`register_modular_op_matcher`).
    // Drained alongside the LO-2a/LO-2e drains in
    // `HandleTranslationUnit`. The AST shape it matches
    // (`VarDecl(qint_t<W>, init=(qint OP qint) % qint)`) is structurally
    // disjoint from every per-op compound-assign matcher in the pool,
    // so no per-hit Phase C suppression is needed (unlike LO-2a / LO-2e).
    std::vector<ModularOpHit> modular_hits_;
    // sturm-ddgo: QRAM matcher hit vectors. Drained after `matchAST`
    // alongside the modular / lossy / nested-lossy drains. The three
    // matchers anchor on disjoint AST shapes (C1: VarDecl with init
    // == subscript; H1: assignment op-call; H4: VarDecl with init
    // containing a subscript at non-immediate position), so no
    // cross-flavour suppression is needed.
    std::vector<QramSubscriptHit>       qram_subscript_hits_;
    std::vector<QramSubscriptAssignHit> qram_assign_hits_;
    std::vector<QramSubscriptExprHit>   qram_expr_hits_;
    // sturm-65rs.10 (Beat C3): alias-subst matches collected during
    // `matchAST`, drained AFTER `emit_qram_replacements` so the C3
    // overlap guard (a `QintAliasSubstClaimedDecls` set populated from
    // the QRAM hits) gates the alias-subst VarDecl arm.
    std::vector<QintAliasSubstMatch>    qint_alias_subst_matches_;
    // sturm-e3ru (Frontend simpl. P7): main-lifecycle hits collected
    // during `matchAST`. Drained alongside the QRAM / alias-subst
    // drains; the emitter produces one `QReplacement` per hit over
    // the matched main's body CompoundStmt source range. Limited to
    // at most one hit per TU by the matcher's own de-dup pass.
    std::vector<MainLifecycleHit>       main_lifecycle_hits_;
    // sturm-v0ur (LO-2 wiring): cleanup records assembled from
    // `lossy_hits_` after `matchAST`. Each `ExternalCleanup` carries
    // a close-brace `SourceLocation` and the pre-formatted cleanup
    // body (already `#line`-prefixed per line). `synthesize(unit,
    // external, sm, registry)` reads this vector and converts each
    // entry into one `UncomputeInsertion` anchored at the close
    // brace.
    std::vector<sturm::transpile::ExternalCleanup> external_cleanups_;
    clang::ast_matchers::MatchFinder finder_;

    // Plugin-mode stash — populated by HandleTranslationUnit when
    // `mode_ == EmissionMode::Plugin`.
    std::string rewritten_buffer_;
};

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_CONSUMER_HPP
