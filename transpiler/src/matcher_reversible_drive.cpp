// matcher_reversible_drive.cpp — Phase R R-C (sturm-88d7.4)
// implementation.
//
// See matcher_reversible_drive.hpp for the contract. This module is a
// thin orchestrator: every real work item lives in a sibling module
// (R-A `adjoint_emitter`, R-B `auto_register_emitter`). The driver's
// job is to sequence them in the right order against the shared
// `SynthesisRegistry` surface, consult the PI-1 `RoutineRegistry` for
// the hand-registration conflict gate, and surface the validation
// hooks that P-C and Q-B will populate when they land.
//
// Pipeline
// --------
//
//   1. Null / attribute guard. Matches R-A's and R-B's reject-gates.
//
//   2. Registry-entry lookup. No entry → `NoRegistryEntry`. The
//      synthesis pipeline is strictly layered; R-C is the LAST stage
//      and depends on P-B's `insert_forward` having already recorded
//      the forward on an earlier pass.
//
//   3. Hand-registration conflict. PRD §9 Q2 locks the precedence:
//      the user's hand-registered adjoint wins silently. R-C declines
//      emission and leaves the entry untouched.
//
//   4. Validator hooks. When non-null, either hook returning false
//      transitions the entry to `Failed` and short-circuits R-A / R-B.
//      Null hooks are "no additional constraint" — this is the
//      designed-in stand-in shape for sturm-88d7.4 landing before
//      P-C (sturm-z2e8.5) and Q-B (sturm-5kgu.3). TODO(backend):
//      wire the real validators into `DriveOptions` at the
//      `transpile_consumer.cpp` call site once those issues close.
//
//   5. R-A emission. Reject on any failure (see
//      `AdjointRejectReason`); the driver does not re-expose the
//      underlying reason — callers who need it invoke R-A directly.
//
//   6. R-B emission. Reject on any failure.
//
//   7. Registry writeback + status transition to `Emitted`.
//
// LOC budget
// ----------
// CLAUDE.md caps source files at 400 LOC. Plan §2.3 R-C budget is
// 180 LOC for this implementation; we stay under.

#include "matcher_reversible_drive.hpp"

#include "adjoint_emitter.hpp"
#include "auto_register_emitter.hpp"
#include "reversible_attribute.hpp"
#include "routine_registry.hpp"
#include "synthesis_registry.hpp"

#include "clang/AST/Decl.h"

#include <string>
#include <string_view>
#include <utility>

namespace sturm::transpile {

// ── Public surface ──────────────────────────────────────────────────────────

std::string_view to_string(DriveRejectReason reason) {
    switch (reason) {
        case DriveRejectReason::None:
            return "none";
        case DriveRejectReason::NullDecl:
            return "null_decl";
        case DriveRejectReason::NotReversible:
            return "not_reversible";
        case DriveRejectReason::NoRegistryEntry:
            return "no_registry_entry";
        case DriveRejectReason::ValidationFailed:
            return "validation_failed";
        case DriveRejectReason::HandRegistrationWins:
            return "hand_registration_wins";
        case DriveRejectReason::AdjointEmissionFailed:
            return "adjoint_emission_failed";
        case DriveRejectReason::AutoRegistrationFailed:
            return "auto_registration_failed";
    }
    // Unreachable for a well-formed enum. Empty view is the safe
    // default — we never construct this branch in production code.
    return {};
}

DriveResult drive_reversible(
    const clang::FunctionDecl* fd,
    const std::vector<QOperation>& ops,
    SynthesisRegistry& synth_reg,
    const RoutineRegistry& routine_reg,
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const DriveOptions& options) {
    DriveResult result;

    // (1) Null / attribute reject-gate. Mirrors R-A and R-B's
    // opt-in posture — we never synthesise for a forward the user
    // did not mark, and a null decl is always a bug in the caller.
    if (fd == nullptr) {
        result.reason = DriveRejectReason::NullDecl;
        return result;
    }
    if (!is_reversible(fd)) {
        result.reason = DriveRejectReason::NotReversible;
        return result;
    }

    // (2) Registry-entry lookup. The synthesis pipeline is layered:
    // P-B's `insert_forward` must have recorded the forward on an
    // earlier stage before the driver runs. Absence is not a bug
    // — it simply means the earlier stages did not surface this
    // forward (e.g. validation already rejected it with status
    // Failed, or the forward was never reversible-tagged in the
    // first pass).
    if (!synth_reg.contains(fd)) {
        result.reason = DriveRejectReason::NoRegistryEntry;
        return result;
    }

    // (3) Hand-registration precedence. PRD §9 Q2 locks the rule:
    // when the user has hand-registered an adjoint via
    // `STURM_REGISTER_ADJOINT`, that binding wins silently. R-C
    // declines emission and leaves the synthesis entry in its
    // current state (typically `Normalized` — Q-A may have run,
    // but R-A / R-B do not).
    if (synth_reg.conflicts_with_routine_registry(fd, routine_reg)) {
        result.reason = DriveRejectReason::HandRegistrationWins;
        return result;
    }

    // (4) Validator hooks. Each hook is a `std::function<bool(const
    // FunctionDecl*)>` — null means "no additional constraint".
    // This is the swappable extension point P-C / Q-B plug into
    // when they land; today they default to null and the driver
    // treats the forward as valid.
    //
    // TODO(backend): once sturm-z2e8.5 (P-C) and sturm-5kgu.3 (Q-B)
    // land, the caller in `transpile_consumer.cpp` populates these
    // hooks with the real validators. No change to the driver's
    // logic is required — the hook contract is already "return
    // true iff valid", matching P-C and Q-B's designed surface.
    if (options.body_validator && !options.body_validator(fd)) {
        synth_reg.set_status(fd, SynthesisStatus::Failed);
        result.reason = DriveRejectReason::ValidationFailed;
        return result;
    }
    if (options.signature_validator &&
        !options.signature_validator(fd)) {
        synth_reg.set_status(fd, SynthesisStatus::Failed);
        result.reason = DriveRejectReason::ValidationFailed;
        return result;
    }

    // (5) R-A adjoint emission. The per-op reverse walk + signature
    // recovery happens inside `emit_adjoint_for_decl`; the driver
    // simply forwards the inputs and short-circuits on any reject.
    // We do not re-expose the underlying `AdjointRejectReason` via
    // `DriveResult` — callers needing that granularity can invoke
    // R-A directly and inspect its result. The driver's job is the
    // single "emitted / not emitted" signal.
    AdjointEmissionResult adj =
        emit_adjoint_for_decl(fd, ops, sm, lang,
                              options.plugin_registry);
    if (!adj.synthesized) {
        result.reason = DriveRejectReason::AdjointEmissionFailed;
        return result;
    }

    // (6) R-B registration-line emission. Consumes R-A's adjoint
    // identifier verbatim — same name the registry entry will pick
    // up in step (7). Reject on any failure (template
    // instantiations, empty adjoint_name, non-reversible drift —
    // the latter two cannot fire after step (5) succeeded but the
    // defensive check matches R-B's opt-in contract).
    AutoRegisterResult reg =
        emit_auto_registration_for_decl(fd, adj.adjoint_name);
    if (!reg.registered) {
        result.reason = DriveRejectReason::AutoRegistrationFailed;
        return result;
    }

    // (7) Registry writeback. The emitted adjoint body, the
    // registration line, and the adjoint's source-level identifier
    // are each separate fields on the synthesis entry — Q-A's
    // `twin_source` slot is orthogonal and stays untouched here.
    // The driver concatenates the R-A body and R-B registration
    // into a single `adjoint_source` payload so downstream
    // consumers (the rewriter, fixture snapshots) see a single
    // emitted blob per reversible forward. This keeps the
    // per-forward rewrite point consistent with the "one
    // adjoint = one emitted payload" mental model; separating the
    // two strings on the registry would force every consumer to
    // re-concatenate them on read.
    std::string combined;
    combined.reserve(adj.source.size() + reg.source.size());
    combined.append(adj.source);
    combined.append(reg.source);

    synth_reg.set_adjoint_source(fd, std::move(combined));
    synth_reg.set_adjoint_name(fd, adj.adjoint_name);
    synth_reg.set_status(fd, SynthesisStatus::Emitted);

    result.emitted      = true;
    result.reason       = DriveRejectReason::None;
    result.adjoint_name = std::move(adj.adjoint_name);
    return result;
}

} // namespace sturm::transpile
