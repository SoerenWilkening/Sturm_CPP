// synthesis_registry.hpp — Phase P P-B (sturm-z2e8.3): the
// context-wide book of forward routines the transpiler plans to
// synthesise adjoints for.
//
// Purpose
// -------
// Phase P of the automatic-adjoint-synthesis roadmap (see
// `docs/implementation_plan_automatic_adjoint_synthesis.md`) walks
// every user routine carrying the `[[sturm::reversible]]` opt-in
// marker and produces:
//
//   - an out-param twin FunctionDecl (when the forward is return-style
//     — see P-A / Q-A),
//   - a named sibling adjoint `__fn_adj`,
//   - a `STURM_REGISTER_ADJOINT` expansion that PI-1's
//     `RoutineRegistry` will pick up on the second transpile pass.
//
// The synthesis pipeline is multi-stage: validation (P-C) gates
// emission, the signature-normalisation pass (Q-A) materialises the
// out-param twin, the adjoint emitter (R-A) produces the body, and
// `auto_register_emitter` (R-B) threads the resulting pair back
// through `RoutineRegistry`. Each of those stages needs a shared
// book-keeping surface to answer questions like "has this forward
// been validated yet?", "what is its twin's decl?", "what status
// code did Q-A leave it in?", and so on.
//
// This header owns that book-keeping surface: a `SynthesisRegistry`
// keyed on the canonical forward `clang::FunctionDecl*`, with one
// entry per reversible forward, tracking:
//
//   - `forward`    — the forward routine's canonical FunctionDecl
//                    pointer (the map key; redundant in the value
//                    but lets `entries()` return self-describing
//                    records).
//   - `twin`       — the out-param twin FunctionDecl produced by
//                    Q-A, or nullptr when the forward is already in
//                    canonical out-param shape.
//   - `adjoint_name` — the emitted adjoint's source-level identifier
//                    (conventionally `"__<fwd>_adj"`). Empty string
//                    until R-A runs.
//   - `status`     — one of `{Pending, Normalized, Emitted, Failed}`.
//                    Transitions are monotonic within a successful
//                    pipeline (Pending → Normalized → Emitted) but a
//                    diagnostic at any stage can drop the entry into
//                    Failed, short-circuiting every later stage.
//
// Bridge to `RoutineRegistry` (PRD §9 Q2)
// ---------------------------------------
// PI-1's `RoutineRegistry` is the source of truth for user-supplied
// forward/adjoint pairs declared via `STURM_REGISTER_ADJOINT`. PRD §9
// Q2 locks the precedence rule: if the user has hand-registered a
// forward, that binding wins — synthesis MUST NOT overwrite it, and
// no diagnostic fires for the conflict.
//
// `SynthesisRegistry` honours this by exposing
// `conflicts_with_routine_registry(fwd, routine_reg)`: a read-only
// query callers (P-C, R-C) consult before marking an entry
// `Emitted`. When the query returns true, the caller is expected to
// decline synthesis for that forward; the `SynthesisRegistry` entry
// stays in its current state (typically `Normalized` — Q-A already
// ran, but R-A won't). No side effects fire inside
// `SynthesisRegistry` itself; the two registries remain independent
// containers and the bridge is one-way (read-only from
// `RoutineRegistry`'s side).
//
// `insert_forward` does NOT perform the conflict check itself: some
// callers (tests, pre-validation dry runs) legitimately want to
// record a forward whose adjoint they do not plan to emit — e.g. a
// validation failure might still want the entry in `Failed` status so
// later diagnostics can reference it. The check is a deliberate
// separate step, callers gate on it before emission.
//
// Null-FD guard
// -------------
// Every public method that takes a `const clang::FunctionDecl*`
// silently rejects nullptr. The downstream matchers occasionally
// receive a null decl when a CallExpr's callee fails to resolve, and
// the registry must not store a phantom entry in that case. The
// guard mirrors `RoutineRegistry::insert_pair`'s early-return
// contract so the bridge semantics stay symmetric.
//
// Deterministic iteration
// -----------------------
// Like `RoutineRegistry`, the registry maintains an insertion-order
// vector so `entries()` / iteration is deterministic across runs.
// Tests depend on this for snapshot-style checks, and diagnostics
// benefit from stable ordering regardless.
//
// Lifetime + thread-safety
// ------------------------
// The registry stores raw `clang::FunctionDecl*` keys and values
// owned by the `clang::ASTContext` the matcher ran under. Callers
// MUST NOT use the registry after that ASTContext is destroyed.
// The transpiler's main pipeline keeps both on the
// `TranspileConsumer` alongside the `QUnit`; all three go away
// together.
//
// The registry is single-threaded: `clang::MatchFinder` runs all
// callbacks on one thread within `matchAST`.
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers / 400 LOC for
// implementation files. The plan §2.1 P-B budget is 150 LOC for the
// header; we stay well under.

#ifndef STURM_TRANSPILE_SYNTHESIS_REGISTRY_HPP
#define STURM_TRANSPILE_SYNTHESIS_REGISTRY_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clang { class FunctionDecl; }

namespace sturm::transpile {

class RoutineRegistry;

/// Pipeline status of a `SynthesisRegistry` entry. The enum is
/// deliberately linear: `Pending` is the state on insertion, every
/// subsequent step upgrades monotonically, and any diagnostic drops
/// the entry to `Failed`.
///
///   - `Pending`    — entry exists (insertion happened) but no
///                    pipeline stage has processed it yet. The
///                    forward decl is recorded; twin / adjoint name
///                    are empty.
///   - `Normalized` — Q-A ran: the forward is either already in
///                    out-param shape (twin == nullptr) or a twin
///                    FunctionDecl has been recorded. Validation
///                    (P-C) may have passed too; this status does
///                    not distinguish.
///   - `Emitted`    — R-A produced the adjoint body and R-B wrote
///                    the `STURM_REGISTER_ADJOINT` expansion. The
///                    adjoint name field is populated with the
///                    emitted identifier.
///   - `Failed`     — some pipeline stage (validation, Q-A, R-A)
///                    emitted a diagnostic for this forward. The
///                    entry stays in the registry so downstream
///                    consumers can avoid re-processing it; no
///                    further state transitions are allowed.
enum class SynthesisStatus {
    Pending,
    Normalized,
    Emitted,
    Failed,
};

/// Human-readable spelling of a `SynthesisStatus`, exposed for
/// diagnostic surfaces and test output. Stable across runs — tests
/// may compare against the exact strings below.
std::string_view to_string(SynthesisStatus status);

/// One tracked reversible-forward record. Returned by value from
/// `entries()`; callers must not retain raw pointers past the next
/// mutating call or registry destruction.
struct SynthesisEntry {
    const clang::FunctionDecl* forward = nullptr;
    const clang::FunctionDecl* twin = nullptr;
    std::string adjoint_name;
    SynthesisStatus status = SynthesisStatus::Pending;
};

/// Context-wide book-keeping of reversible forwards the transpiler
/// intends to synthesise. Layered on top of `RoutineRegistry` (PI-1)
/// — the two maps are peers, with the bridge helper
/// `conflicts_with_routine_registry` answering whether a given
/// forward is already bound to a hand-written adjoint.
///
/// Public API summary:
///   - `insert_forward(fwd)`   — create a `Pending` entry (or return
///                                the existing one). Returns the
///                                entry pointer (stable until the
///                                next mutating call).
///   - `lookup(fwd)`           — read-only pointer into the map, or
///                                nullptr if absent / null input.
///   - `contains(fwd)`         — true iff an entry exists.
///   - `size`, `empty`         — the typical read-side surface.
///   - `set_twin`,
///     `set_adjoint_name`,
///     `set_status`            — mutating setters. Each is a no-op on
///                                null FD or on a missing entry.
///   - `entries()`             — snapshot of all entries in insertion
///                                order.
///   - `forwards()`            — snapshot of canonical forward decl
///                                pointers in insertion order. Useful
///                                for callers that want to iterate
///                                without materialising the whole
///                                entry copy.
///   - `conflicts_with_routine_registry(fwd, reg)`
///                             — read-only bridge to PI-1.
///   - `clear()`               — reset state; primarily for tests.
class SynthesisRegistry {
public:
    SynthesisRegistry() = default;
    // Non-copyable for the same reason `RoutineRegistry` is:
    // accidental copies would silently duplicate the raw
    // ASTContext-tied pointers and make lifetime bugs near-
    // invisible. Moves are fine.
    SynthesisRegistry(const SynthesisRegistry&)            = delete;
    SynthesisRegistry& operator=(const SynthesisRegistry&) = delete;
    SynthesisRegistry(SynthesisRegistry&&)                 = default;
    SynthesisRegistry& operator=(SynthesisRegistry&&)      = default;

    /// Ensure an entry exists for `fwd`. Returns a pointer to the
    /// entry (newly created in `Pending` status or pre-existing).
    /// `fwd == nullptr` is a no-op — returns nullptr without
    /// mutating the registry. Pointer stability holds until the
    /// next mutating call (`insert_forward`, any setter, `clear`).
    SynthesisEntry* insert_forward(const clang::FunctionDecl* fwd);

    /// Read-only accessors.
    bool empty() const noexcept       { return map_.empty(); }
    std::size_t size() const noexcept { return map_.size(); }
    bool contains(const clang::FunctionDecl* fwd) const;

    /// Return a pointer to the tracked entry, or nullptr if `fwd`
    /// is not registered / is null. Pointer stability holds until
    /// the next mutating call.
    const SynthesisEntry* lookup(const clang::FunctionDecl* fwd) const;

    /// Attach the out-param twin FunctionDecl produced by Q-A.
    /// `twin == nullptr` is legitimate — it records the "forward is
    /// already in out-param shape, no twin needed" case. Returns
    /// true on success, false if `fwd` is null or absent from the
    /// registry.
    bool set_twin(const clang::FunctionDecl* fwd,
                  const clang::FunctionDecl* twin);

    /// Record the emitted adjoint's source-level identifier.
    /// Returns true on success, false if `fwd` is null or absent.
    bool set_adjoint_name(const clang::FunctionDecl* fwd,
                          std::string name);

    /// Transition the entry to a new status. Returns true on
    /// success, false if `fwd` is null or absent. The method does
    /// NOT enforce the monotonic ordering documented on
    /// `SynthesisStatus` — transitions are the caller's
    /// responsibility so tests can exercise edge cases, and so a
    /// future phase can choose to reset state when re-running a
    /// pipeline stage.
    bool set_status(const clang::FunctionDecl* fwd,
                    SynthesisStatus status);

    /// Deterministic iteration: yields a snapshot of every entry in
    /// insertion order. Callers must not retain the returned
    /// vector past the next mutating call — the entry's raw
    /// `FunctionDecl*` keys stay valid as long as the ASTContext
    /// does, but the vector's layout is materialised on demand and
    /// has no special lifetime guarantee.
    std::vector<SynthesisEntry> entries() const;

    /// Cheap iteration over just the canonical forward decl
    /// pointers, in insertion order. Avoids materialising the whole
    /// entry struct when the caller only needs the keys.
    std::vector<const clang::FunctionDecl*> forwards() const;

    /// Bridge to PI-1 (PRD §9 Q2). Returns true iff `fwd` is
    /// non-null AND `routine_reg` already contains a hand-registered
    /// adjoint for it. The caller is responsible for taking action
    /// on the conflict (declining to emit, leaving the
    /// `SynthesisRegistry` entry in its current state). A null FD
    /// always answers false — per the null-FD guard contract, a
    /// bogus key is a no-op, not a conflict.
    bool conflicts_with_routine_registry(
        const clang::FunctionDecl* fwd,
        const RoutineRegistry& routine_reg) const;

    /// Reset state. Primarily for tests.
    void clear();

private:
    std::unordered_map<const clang::FunctionDecl*, SynthesisEntry> map_;
    std::vector<const clang::FunctionDecl*> order_;
};

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_SYNTHESIS_REGISTRY_HPP
