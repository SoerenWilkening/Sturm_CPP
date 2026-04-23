// matcher_reversible_drive.hpp — Phase R R-C (sturm-88d7.4): top-level
// driver matcher orchestrating R-A (`adjoint_emitter`) + R-B
// (`auto_register_emitter`) for each `[[sturm::reversible]]` forward
// that passes P-C validation and Q-B constness enforcement.
//
// Purpose
// -------
// Phase R of the automatic-adjoint-synthesis roadmap (see
// `docs/implementation_plan_automatic_adjoint_synthesis.md` §2.3 R-C)
// wires the per-module emitters together behind a single entry point
// the `TranspileConsumer` calls once per translation unit. For each
// reversible forward that survives validation, the driver:
//
//   1. consults the `SynthesisRegistry` to find the entry populated
//      by Q-1 (`return_to_out_param`) and R-1 (`adjoint_emitter`),
//   2. calls R-A (`emit_adjoint_for_decl`) on the forward's decl +
//      per-statement `QOperation` records to produce the sibling
//      `__<fn>_adj` body text,
//   3. calls R-B (`emit_auto_registration_for_decl`) to produce the
//      companion `STURM_REGISTER_ADJOINT(<fn>, __<fn>_adj);` line so
//      PI-1's matcher picks up the pair on the second pass,
//   4. writes both emitted strings back into the registry entry via
//      `set_adjoint_source` / `set_adjoint_name` and transitions the
//      entry's status to `Emitted`.
//
// Swappable validation hooks
// --------------------------
// P-C (`matcher_reversible_validate`, sturm-z2e8.5) and Q-B
// (`constness_enforcement`, sturm-5kgu.3) have NOT YET LANDED as of
// sturm-88d7.4. The driver is designed so the two validation
// dependencies are swappable: each hook is a `std::function<bool(const
// FunctionDecl*)>` injected through the `DriveOptions` struct. When
// either hook returns false the driver short-circuits — it marks the
// entry `Failed`, skips R-A, skips R-B, and leaves `adjoint_source` /
// `adjoint_name` empty.
//
// By default the hooks are `nullptr`, which the driver treats as "no
// additional constraint" — i.e. the forward is assumed valid. This
// lets today's tests exercise the happy path without the real
// validators in place, AND mirrors the runtime posture once the
// validators ship: P-C / Q-B populate the hooks, and the driver's
// logic is unchanged.
//
// TODO(backend): when sturm-z2e8.5 (P-C `matcher_reversible_validate`)
// and sturm-5kgu.3 (Q-B `constness_enforcement`) land, wire their
// query surfaces into `DriveOptions.body_validator` and
// `DriveOptions.signature_validator` at the call site in
// `transpile_consumer.cpp`. The driver's implementation here does not
// change.
//
// Integration with SynthesisRegistry
// -----------------------------------
// The driver's input is a single `const FunctionDecl*` (one
// reversible forward) and a reference to the `SynthesisRegistry`. It
// reads the entry the earlier Phase-P / Phase-Q stages populated and
// writes the emitted payloads back to the same entry. The driver
// does NOT insert a new entry — absence of an entry is treated as "no
// prior normalization", which skips emission per the P-B contract
// (the synthesis pipeline is strictly layered: the registry must
// carry an entry for a forward before R-A / R-B run).
//
// Per PRD §9 Q2 (hand-registration wins), the driver also consults
// `SynthesisRegistry::conflicts_with_routine_registry(fwd,
// routine_reg)`. When the user has already hand-registered the
// forward via `STURM_REGISTER_ADJOINT`, the driver declines emission
// and leaves the entry's status unchanged — the user's binding wins
// silently.
//
// Single entry point
// ------------------
// The driver exposes `drive_reversible` as its sole public entry
// point. This mirrors the convention used by R-A / R-B: the matcher
// is a pure function of its inputs, stateful only through the
// references it receives. The driver does NOT register an
// AST-matcher callback against a `MatchFinder` — the forward-walk
// shape the driver needs is simpler than the per-op matcher pool,
// and layering a `MatchFinder` on top would duplicate work the
// `TranspileConsumer`'s primary AST walk already does. The caller
// iterates the forwards itself (via `SynthesisRegistry::forwards()`
// or by walking the reversible FDs it has already surfaced) and
// invokes `drive_reversible` once per forward. This matches the
// usage intent spelled out in plan §2.3 R-C: "Single entry point
// callable from `transpile_consumer.cpp`."
//
// Reject-without-diagnostic contract
// ----------------------------------
// Mirrors R-A's and R-B's posture. When the input does not match the
// expected shape (null forward decl, forward not marked
// `[[sturm::reversible]]`, missing `SynthesisRegistry` entry, failing
// validator, hand-registration conflict, R-A reject, R-B reject),
// the function returns a `DriveResult` whose `emitted=false` field
// and `reason` tag explain why. Diagnostics are NOT fired from
// inside the driver — the real validators (P-C / Q-B, when they
// land) own their own diagnostic surfaces and are consulted before
// the driver runs. The driver itself stays diagnostic-free.
//
// LOC budget
// ----------
// CLAUDE.md caps headers at 300 LOC. Plan §2.3 R-C budgets 180 LOC
// for the implementation file; the header is smaller.

#ifndef STURM_TRANSPILE_MATCHER_REVERSIBLE_DRIVE_HPP
#define STURM_TRANSPILE_MATCHER_REVERSIBLE_DRIVE_HPP

#include "sturm/transpile/qir.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace clang {
class ASTContext;
class FunctionDecl;
class LangOptions;
class SourceManager;
namespace ast_matchers { class MatchFinder; }
} // namespace clang

namespace sturm::transpile {

class DiagContext;
class RoutineRegistry;
class SynthesisRegistry;

namespace plugin {
class Registry;
} // namespace plugin

/// Why a `drive_reversible` invocation did NOT produce an adjoint +
/// registration pair. Exposed so callers (tests, driver glue) can
/// distinguish the expected reject shapes from a bug. The spellings
/// are part of the contract — tests may compare against the exact
/// strings below.
enum class DriveRejectReason {
    /// Adjoint + registration emitted successfully. The registry
    /// entry now carries `adjoint_source`, `adjoint_name`, and
    /// status `Emitted`.
    None,
    /// `fd == nullptr`.
    NullDecl,
    /// The decl is not carrying `[[clang::annotate("sturm::reversible")]]`.
    /// R-C is opt-in by the same P9 contract R-A / R-B use.
    NotReversible,
    /// No `SynthesisRegistry` entry exists for this forward. The
    /// synthesis pipeline is strictly layered: P-B's `insert_forward`
    /// must fire before R-C runs. Absence means the earlier stages
    /// did not surface the forward.
    NoRegistryEntry,
    /// An injected validator (body_validator or signature_validator)
    /// returned false. The entry transitions to `Failed`; no adjoint
    /// is emitted. This is the catch-all for the P-C and Q-B passes
    /// once they land.
    ValidationFailed,
    /// The forward is already bound to a hand-written adjoint via
    /// `STURM_REGISTER_ADJOINT`. Per PRD §9 Q2 the user's binding
    /// wins silently; R-C declines emission and leaves the entry
    /// unchanged.
    HandRegistrationWins,
    /// R-A (`emit_adjoint_for_decl`) returned a reject. The
    /// underlying `AdjointRejectReason` is not re-exposed here —
    /// callers that need it should invoke R-A directly; the driver
    /// simply short-circuits on the first sign of trouble.
    AdjointEmissionFailed,
    /// R-B (`emit_auto_registration_for_decl`) returned a reject.
    /// Same rationale as above.
    AutoRegistrationFailed,
};

/// Human-readable spelling of a `DriveRejectReason`. Stable across
/// runs — tests compare against these exact strings.
std::string_view to_string(DriveRejectReason reason);

/// Result of a single `drive_reversible` invocation.
struct DriveResult {
    /// `true` iff the driver transitioned the entry to `Emitted`
    /// and wrote both the adjoint body and the registration line
    /// back into the registry.
    bool emitted = false;

    /// Machine-readable reason code. `None` on success; every
    /// reject path leaves the registry entry in its previous state
    /// (or transitions it to `Failed` only on `ValidationFailed`).
    DriveRejectReason reason = DriveRejectReason::NullDecl;

    /// The emitted adjoint's source-level identifier, of the form
    /// `__<forward>_adj`. Populated whenever `reason == None`;
    /// empty on every reject path. Mirrors R-A's return.
    std::string adjoint_name;
};

/// Optional extension points the driver consults before firing R-A /
/// R-B. Each hook defaults to `nullptr`, which the driver interprets
/// as "no additional constraint" (the forward is assumed to pass).
///
/// When P-C and Q-B land, their query surfaces plug into
/// `body_validator` and `signature_validator` respectively — the
/// driver's logic does not change.
///
/// TODO(backend): wire `body_validator` to P-C's
/// `matcher_reversible_validate` (sturm-z2e8.5) and
/// `signature_validator` to Q-B's `constness_enforcement`
/// (sturm-5kgu.3) at the call site in `transpile_consumer.cpp` once
/// those issues close.
struct DriveOptions {
    /// P-C validator hook (sturm-z2e8.5). Returns `true` when the
    /// forward's body passes the P9d reject checks (no
    /// measurement, no classical I/O, no unregistered callees, no
    /// while-loop, no quantum-dependent conditions). A null hook
    /// is treated as "valid".
    std::function<bool(const clang::FunctionDecl*)> body_validator;

    /// Q-B validator hook (sturm-5kgu.3). Returns `true` when the
    /// forward's parameter list passes the constness + ref-vs-
    /// pointer checks. A null hook is treated as "valid".
    std::function<bool(const clang::FunctionDecl*)> signature_validator;

    /// Plugin registry for R-A to consult when rendering
    /// `QOpKind::PLUGIN` ops. Null is safe — R-A degrades to empty
    /// output for plugin ops without a registry, matching the
    /// defensive posture every other kind takes on malformed
    /// input.
    const plugin::Registry* plugin_registry = nullptr;
};

/// Drive the R-A + R-B emitters for a single `[[sturm::reversible]]`
/// forward routine.
///
/// Pipeline:
///
///   1. Null-decl + `is_reversible` guards. A non-reversible
///      forward returns `NotReversible` without touching the
///      registry — R-C is opt-in by the same P9 contract R-A / R-B
///      use.
///
///   2. `SynthesisRegistry::lookup(fd)` guard. An absent entry
///      returns `NoRegistryEntry`; the synthesis pipeline is
///      strictly layered and the earlier stages (P-B
///      `insert_forward`) must have recorded the forward before
///      the driver runs.
///
///   3. `conflicts_with_routine_registry(fd, routine_reg)` guard.
///      Hand-registered forwards win per PRD §9 Q2; the driver
///      declines emission and returns `HandRegistrationWins`.
///
///   4. `body_validator` / `signature_validator` hooks (when
///      non-null). On any false return the entry transitions to
///      `Failed` and the driver returns `ValidationFailed`.
///
///   5. `emit_adjoint_for_decl`. On any reject the driver returns
///      `AdjointEmissionFailed`; the entry's prior fields are
///      left untouched.
///
///   6. `emit_auto_registration_for_decl`. On any reject the
///      driver returns `AutoRegistrationFailed`.
///
///   7. On success: write `adjoint_source` + `adjoint_name` back to
///      the registry entry, transition the status to `Emitted`,
///      return `emitted=true`.
///
/// Contract:
///   - `fd == nullptr` ⇒ `emitted=false`, `reason=NullDecl`.
///   - `fd` without `[[sturm::reversible]]` ⇒ `emitted=false`,
///     `reason=NotReversible`.
///   - Missing registry entry ⇒ `NoRegistryEntry`.
///   - Hand-registration wins ⇒ `HandRegistrationWins`.
///   - Validator rejects ⇒ `ValidationFailed`; entry transitions
///     to `Failed`.
///   - R-A rejects ⇒ `AdjointEmissionFailed`; entry untouched.
///   - R-B rejects ⇒ `AutoRegistrationFailed`; entry untouched.
///   - Otherwise ⇒ `emitted=true`, entry transitions to `Emitted`.
///
/// The function never allocates beyond the returned strings and the
/// registry-entry mutations. Safe to call once per reversible
/// forward during `HandleTranslationUnit`.
DriveResult drive_reversible(
    const clang::FunctionDecl* fd,
    const std::vector<QOperation>& ops,
    SynthesisRegistry& synth_reg,
    const RoutineRegistry& routine_reg,
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const DriveOptions& options = {});

/// Phase T T-1 (sturm-xrob.2): register a Clang AST matcher that records
/// every `[[clang::annotate("sturm::reversible")]]` FunctionDecl into
/// `synth_reg` so `drive_reversible_forwards` can drive the R-A + R-B
/// emission pipeline at end-of-TU.
///
/// The matcher is a *collector*, not an emitter — it only records the
/// forwards it sees. `drive_reversible_forwards` (below) is the
/// companion function the consumer calls from `HandleTranslationUnit`
/// after `matchAST` completes.
///
/// The `routine_registry`, `diag`, `sm`, and `lang` references are
/// captured for the end-of-TU drive phase; the match callback itself
/// only uses `synth_reg`. All references must outlive the MatchFinder's
/// run.
void register_reversible_drive_matcher(
    clang::ast_matchers::MatchFinder& finder,
    SynthesisRegistry& synth_reg,
    const RoutineRegistry& routine_reg,
    DiagContext& diag,
    const clang::SourceManager& sm,
    const clang::LangOptions& lang);

/// Phase T T-1 (sturm-xrob.2): end-of-TU driver for the reversible
/// synthesis pipeline. Called from `TranspileConsumer::HandleTranslationUnit`
/// AFTER `matchAST` + the backstop cleanups, so the per-op matchers have
/// fully populated `unit.scopes` and every `[[sturm::reversible]]` FD
/// has been collected into `synth_reg` by the matcher above.
///
/// For each forward recorded in `synth_reg`, the function:
///
///   1. Locates the forward's body QScope in `unit` by matching the
///      forward's body CompoundStmt LBraceLoc against each scope's
///      `open_brace`. A missing scope means the forward's body
///      contained no quantum ops (stub / empty body) — the drive still
///      runs, producing an empty adjoint body.
///
///   2. Runs `validate_reversible_body` and
///      `validate_reversible_signature` to collect per-forward
///      validator verdicts.
///
///   3. Invokes `drive_reversible` with bool-returning lambdas that
///      short-circuit on the captured verdicts. The real validators
///      only fire once per forward — re-running them inside the hook
///      would double-emit diagnostics.
///
///   4. On successful emission, appends one `UncomputeInsertion`
///      record to `unit.raw_insertions` whose `insert_before` is the
///      SourceLocation immediately after the forward's body close
///      brace. The M8 emitter stitches the adjoint body +
///      `STURM_REGISTER_ADJOINT` line into the rewritten buffer
///      BEFORE the second PM3 transpile pass, so PI-1's matcher picks
///      up the registration on pass two.
///
/// This is strictly a collector-driven end-of-TU flush. No IR mutation
/// beyond `unit.raw_insertions` and `synth_reg` status transitions.
void drive_reversible_forwards(
    QUnit& unit,
    SynthesisRegistry& synth_reg,
    const RoutineRegistry& routine_reg,
    DiagContext& diag,
    clang::ASTContext& ctx);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_REVERSIBLE_DRIVE_HPP
