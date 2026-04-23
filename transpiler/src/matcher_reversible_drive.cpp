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
#include "diag_context.hpp"
#include "matcher_reversible_signature.hpp"
#include "matcher_reversible_validate.hpp"
#include "return_to_out_param.hpp"
#include "reversible_attribute.hpp"
#include "routine_registry.hpp"
#include "synthesis_registry.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// ── Phase T T-1 (sturm-xrob.2): consumer-side integration ──────────────────

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// The matcher callback only records [[sturm::reversible]] FDs into the
// synthesis registry. The actual drive work happens in
// drive_reversible_forwards at end-of-TU — matchAST has to complete
// first so every per-op matcher has populated unit_.scopes with the
// forward bodies' QOperations.
class ReversibleDriveCollectorCallback
    : public MatchFinder::MatchCallback {
public:
    explicit ReversibleDriveCollectorCallback(SynthesisRegistry* synth_reg)
        : synth_reg_(synth_reg) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* fd = r.Nodes.getNodeAs<FunctionDecl>("reversible_fd");
        if (fd == nullptr) return;
        // Canonicalise to the decl's definition (where the body lives).
        // Forward declarations without bodies are not synthesis
        // candidates — R-A would reject with NoBody anyway.
        if (!fd->hasBody()) return;
        const FunctionDecl* def = fd->getDefinition();
        if (def == nullptr) def = fd;
        if (!is_reversible(def)) return;
        synth_reg_->insert_forward(def);
    }

private:
    SynthesisRegistry* synth_reg_;
};

// Static pool so repeated registrations across tests (each TU instantiates
// a fresh consumer) do not leak callback lifetimes. Matches every other
// matcher module in the directory.
std::vector<std::unique_ptr<ReversibleDriveCollectorCallback>>&
reversible_drive_callback_pool() {
    static std::vector<std::unique_ptr<ReversibleDriveCollectorCallback>> pool;
    return pool;
}

} // namespace

void register_reversible_drive_matcher(
    MatchFinder& finder,
    SynthesisRegistry& synth_reg,
    const RoutineRegistry& /*routine_reg*/,
    DiagContext& /*diag*/,
    const SourceManager& /*sm*/,
    const LangOptions& /*lang*/) {
    // The AST pattern anchors on any FunctionDecl carrying the
    // `[[clang::annotate("sturm::reversible")]]` attribute. We match
    // broadly on `functionDecl(hasAttr(attr::Annotate))` and refine
    // inside the callback via `is_reversible` so any future expansion of
    // the annotation set (for example distinguishing `sturm::reversible`
    // from a sibling `sturm::*` annotation) is a one-line change in
    // `is_reversible`, not in the matcher pattern.
    auto pattern = functionDecl(hasAttr(attr::Annotate)).bind("reversible_fd");

    auto& pool = reversible_drive_callback_pool();
    pool.push_back(
        std::make_unique<ReversibleDriveCollectorCallback>(&synth_reg));
    finder.addMatcher(pattern, pool.back().get());
}

namespace {

// Locate the QScope whose open-brace matches the FD's body LBraceLoc.
// Returns nullptr when the FD's body is absent or contains no tracked
// ops (for example a routine whose body is all classical / non-quantum
// statements — the matcher pool never populates a scope for it).
const QScope* find_body_scope(const FunctionDecl* fd, const QUnit& unit) {
    if (fd == nullptr) return nullptr;
    const Stmt* body = fd->getBody();
    if (body == nullptr) return nullptr;
    const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
    if (compound == nullptr) return nullptr;
    const SourceLocation lbrace = compound->getLBracLoc();
    if (lbrace.isInvalid()) return nullptr;
    for (const QScope& s : unit.scopes) {
        if (s.open_brace == lbrace) return &s;
    }
    return nullptr;
}

// Compute the insertion point for the emitted `__fn_adj` + registration
// line: the location immediately AFTER the forward's body closing brace.
// We route through `Lexer::getLocForEndOfToken` so `getBeginLoc()` +1
// actually lands past the `}`, matching the posture every other raw
// insertion anchor uses.
SourceLocation after_body_loc(const FunctionDecl* fd,
                              const SourceManager& sm,
                              const LangOptions& lang) {
    if (fd == nullptr) return {};
    const Stmt* body = fd->getBody();
    if (body == nullptr) return {};
    const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
    if (compound == nullptr) return {};
    const SourceLocation rbrace = compound->getRBracLoc();
    if (rbrace.isInvalid()) return {};
    // getLocForEndOfToken returns the location one past the last byte
    // of the given token. For `}`, that is the position where
    // InsertTextBefore will land the new text directly after the close
    // brace, on the same line, which is what we want — the Rewriter's
    // subsequent newline handling produces a clean separation.
    return Lexer::getLocForEndOfToken(rbrace, 0, sm, lang);
}

// Phase T T-5 (sturm-xrob.6): split the combined R-A body + R-B
// registration blob into two pieces so the registration line can be
// emitted at global scope.
//
// The combined blob assembled by `drive_reversible` looks like:
//
//     void __<fn>_adj(<signature>) {
//     <body>
//     }
//     STURM_REGISTER_ADJOINT(<fn>, __<fn>_adj);
//
// R-B's line always starts with the literal token
// `STURM_REGISTER_ADJOINT(`. We find that anchor and split; everything
// before is the adjoint-body payload and everything from that token on
// is the registration line. Returns {body, registration}. On any
// malformed input (no anchor found) the full blob is returned as the
// body and the registration comes back empty — preserving the legacy
// single-insertion behaviour for callers that do not route through
// the split.
std::pair<std::string, std::string>
split_adjoint_source(std::string_view blob) {
    constexpr std::string_view anchor = "STURM_REGISTER_ADJOINT(";
    const auto pos = blob.find(anchor);
    if (pos == std::string_view::npos) {
        return {std::string(blob), {}};
    }
    return {std::string(blob.substr(0, pos)),
            std::string(blob.substr(pos))};
}

// Produce a re-qualified STURM_REGISTER_ADJOINT line for `fd`.
//
// The macro `STURM_REGISTER_ADJOINT(fn, adj)` expands to a full
// specialization of `sturm::_detail::adjoint_of<decltype(&::fn)>` whose
// body opens and closes `namespace sturm { namespace _detail { ... } }`.
// When the forward lives inside a user namespace (e.g.
// `namespace m12_reversible_synth_transpiled { ... }`), planting the
// macro invocation INSIDE that namespace does two things wrong at once:
//
//   1. `::fn` and `::adj` fail to resolve because the leading `::`
//      refers to the global namespace, where `fn` does not live;
//      the compiler reports "no member named '<fn>' in the global
//      namespace" and the TU fails to parse.
//   2. Even if (1) were patched, the macro would expand into
//      `user_namespace::sturm::_detail::adjoint_of`, which is a
//      different template than the `::sturm::_detail::adjoint_of` the
//      runtime `invert(fn)` helper keys on — the specialization
//      lands in the wrong namespace and `invert(fn)` sees the
//      primary undefined template.
//
// Both failure modes disappear when the macro is invoked at global
// scope (outside every user namespace) with qualified identifiers.
// This helper produces exactly that shape:
//
//     STURM_REGISTER_ADJOINT(<qualified fn>, <qualified adj>);\n
//
// For an FD at global namespace scope, `getQualifiedNameAsString`
// returns the bare identifier, so the emitted line degenerates to
// the short-name form the legacy emitter produced. For a namespaced
// FD, the qualification is preserved so `decltype(&::ns::fn)` /
// `&::ns::__fn_adj` resolve correctly.
std::string emit_registration_at_global_scope(
    const FunctionDecl* fd, std::string_view short_adjoint_name) {
    if (fd == nullptr) return {};
    if (short_adjoint_name.empty()) return {};

    const std::string qualified_fn = fd->getQualifiedNameAsString();
    if (qualified_fn.empty()) return {};

    // Build the adjoint's qualified name by replacing the forward's
    // short tail with the adjoint spelling. `getQualifiedNameAsString`
    // produces `ns0::ns1::fn`; we strip the final `::fn` (or leave
    // the global-scope bare name untouched) and append `::<adj>`.
    std::string qualified_adj;
    const auto last_sep = qualified_fn.rfind("::");
    if (last_sep == std::string::npos) {
        qualified_adj.assign(short_adjoint_name.data(),
                             short_adjoint_name.size());
    } else {
        qualified_adj.assign(qualified_fn, 0, last_sep + 2);
        qualified_adj.append(short_adjoint_name.data(),
                             short_adjoint_name.size());
    }

    return emit_auto_registration(qualified_fn, qualified_adj);
}

// Compute the end-of-file insertion point for the emitted
// STURM_REGISTER_ADJOINT line. Used to plant the registration at
// global scope — outside every user namespace — where the macro's
// `::fn` / `::adj` spelling and its `namespace sturm { namespace
// _detail { ... } }` expansion resolve correctly.
//
// `fd` must carry a valid location; we route through the FD's
// declaration location rather than its body close brace so the
// helper still works for forward declarations (no body) — though in
// practice `drive_reversible_forwards` only calls this for FDs whose
// body it has already located.
SourceLocation end_of_file_loc(const FunctionDecl* fd,
                               const SourceManager& sm) {
    if (fd == nullptr) return {};
    const clang::SourceLocation loc = fd->getLocation();
    if (loc.isInvalid()) return {};
    const clang::FileID fid = sm.getFileID(loc);
    if (fid.isInvalid()) return {};
    return sm.getLocForEndOfFile(fid);
}

} // namespace

void drive_reversible_forwards(
    QUnit& unit,
    SynthesisRegistry& synth_reg,
    const RoutineRegistry& routine_reg,
    DiagContext& diag,
    ASTContext& ctx) {
    // Snapshot the forwards list before we iterate. `drive_reversible`
    // mutates the registry (status transitions + payload writeback), so
    // the snapshot keeps the walk decoupled from in-flight mutation.
    const std::vector<const FunctionDecl*> forwards = synth_reg.forwards();
    if (forwards.empty()) return;

    const SourceManager& sm = ctx.getSourceManager();
    const LangOptions& lang = ctx.getLangOpts();

    for (const FunctionDecl* fd : forwards) {
        if (fd == nullptr) continue;
        // Canonicalise on the defining decl — R-A, R-B, and
        // drive_reversible all key on the body-carrying decl, not
        // redeclarations.
        const FunctionDecl* def = fd->getDefinition();
        if (def == nullptr) def = fd;
        if (!is_reversible(def)) continue;

        // (1) Validate eagerly. The validators own their own diagnostic
        // surface; we capture their verdicts so drive_reversible's
        // hooks can short-circuit without re-invoking them (re-entry
        // would double-emit every diagnostic).
        const ReversibleValidationResult body_result =
            validate_reversible_body(def, ctx, diag, routine_reg);
        const ReversibleSignatureResult sig_result =
            validate_reversible_signature(def, ctx, diag);

        // (1b) Phase T T-4 (sturm-xrob.5): surface Q-A's multi-return
        // reject as a user-facing diagnostic. The Q-A classifier
        // (`synthesize_out_param_twin`) rejects return-style forwards
        // whose body is not a single `return <expr>;` statement —
        // without a canonical target the adjoint emitter cannot reverse.
        // We fire one Error-severity diagnostic at the forward's
        // declaration site and short-circuit synthesis. Non-return-style
        // forwards (void return) reject with `NonQuantumReturnType` and
        // no diagnostic fires — the canonical out-param path is silent.
        const TwinSynthesisResult early_twin =
            synthesize_out_param_twin(def, sm, lang);
        if (!early_twin.synthesized &&
            early_twin.reason == TwinRejectReason::MultiStatementBody) {
            const SourceLocation fd_loc =
                sm.getFileLoc(def->getLocation());
            diag.report_reversible_sig_multi_return(
                fd_loc, std::string_view(def->getNameAsString()));
            continue;
        }

        // (2) Locate the FD's body scope in the unit. An absent scope
        // simply means the body contained no tracked quantum ops — we
        // still run drive_reversible with an empty ops vector so R-A
        // emits the adjoint-stub shape (a valid empty body).
        const QScope* scope = find_body_scope(def, unit);
        std::vector<QOperation> ops;
        if (scope != nullptr) ops = scope->ops;

        // (3) Drive. The validator hooks return the cached verdicts so
        // a rejection in either pass short-circuits emission without
        // re-running the validators.
        DriveOptions opts;
        const bool body_ok = body_result.valid;
        const bool sig_ok  = sig_result.valid;
        opts.body_validator =
            [body_ok](const FunctionDecl*) { return body_ok; };
        opts.signature_validator =
            [sig_ok](const FunctionDecl*) { return sig_ok; };

        const DriveResult dr =
            drive_reversible(def, ops, synth_reg, routine_reg, sm, lang,
                             opts);
        if (!dr.emitted) continue;

        // (4a) Phase T T-1: attach the Q-A twin source to the registry
        // entry for downstream consumers. The Q-A classifier ran
        // speculatively above (step 1b); a successful return-style
        // synthesis is reused here. Non-return-style forwards (void
        // return) reject with `NonQuantumReturnType` and `twin.source`
        // is empty — the canonical out-param path is silent.
        const TwinSynthesisResult& twin = early_twin;
        if (twin.synthesized && !twin.source.empty()) {
            synth_reg.set_twin_source(def, twin.source);
        }

        // (4b) Append the emitted adjoint body + registration line as
        // raw insertions. The registry's `adjoint_source` carries R-A's
        // body concatenated with R-B's registration line (done inside
        // `drive_reversible`). Phase T T-5 (sturm-xrob.6) splits the
        // blob into two insertion records:
        //
        //   * twin + `__fn_adj` body — anchored immediately after the
        //     forward's body close brace, so the definition lands
        //     INSIDE whatever namespace the forward is declared in.
        //     The body references the namespace's local types (e.g.
        //     `qbool` via a `using` alias or ADL), so keeping it in
        //     the same namespace is load-bearing.
        //
        //   * `STURM_REGISTER_ADJOINT(<qualified fn>, <qualified adj>);`
        //     — anchored at end-of-file, so the macro expansion lands
        //     at GLOBAL scope. The macro's `::fn` / `::adj` spelling
        //     and its `namespace sturm { namespace _detail { ... } }`
        //     opener both require global-scope invocation to resolve
        //     correctly; planting the line inside a user namespace
        //     would (a) fail to parse (the leading `::` resolves to
        //     the global namespace, where the short name does not
        //     live) and (b) land the specialization in
        //     `user_ns::sturm::_detail::adjoint_of`, a different
        //     template than the `::sturm::_detail::adjoint_of` the
        //     runtime `invert(fn)` helper keys on. See
        //     `include/sturm/routines/invert.hpp` line 84 for the
        //     macro contract.
        //
        // Prepend two newlines to the body payload so the new
        // function definitions start on their own lines, matching the
        // hand-written convention in `reversible_synth_reference.cpp`.
        const SynthesisEntry* entry = synth_reg.lookup(def);
        if (entry == nullptr || entry->adjoint_source.empty()) continue;

        const SourceLocation anchor = after_body_loc(def, sm, lang);
        if (anchor.isInvalid()) continue;

        const auto split = split_adjoint_source(entry->adjoint_source);
        const std::string& body_payload = split.first;

        // Body insertion (inside namespace): twin + adjoint body.
        UncomputeInsertion body_ins;
        body_ins.insert_before = anchor;
        body_ins.code = "\n\n";
        if (twin.synthesized && !twin.source.empty()) {
            body_ins.code.append(twin.source);
            if (!body_ins.code.empty() && body_ins.code.back() != '\n') {
                body_ins.code.push_back('\n');
            }
            body_ins.code.push_back('\n');
        }
        body_ins.code.append(body_payload);
        unit.raw_insertions.push_back(std::move(body_ins));

        // Registration insertion (global scope): re-qualified
        // STURM_REGISTER_ADJOINT line at end-of-file. We regenerate
        // the line with fully-qualified names rather than reusing the
        // split-off R-B text because R-B's FD-based overload keys
        // only on short names (by design, per the top-of-file
        // comment in `auto_register_emitter.cpp`). Empty output
        // means the registration was never generated or cannot be
        // re-qualified — skip silently.
        const std::string global_registration =
            emit_registration_at_global_scope(def, entry->adjoint_name);
        if (global_registration.empty()) continue;

        const SourceLocation eof_anchor = end_of_file_loc(def, sm);
        if (eof_anchor.isInvalid()) continue;

        UncomputeInsertion reg_ins;
        reg_ins.insert_before = eof_anchor;
        // Lead with a newline so the registration line starts on its
        // own line even if the source file does not end with one.
        reg_ins.code = "\n";
        reg_ins.code.append(global_registration);
        unit.raw_insertions.push_back(std::move(reg_ins));
    }
}

} // namespace sturm::transpile
