// synthesis_registry.cpp — Phase P P-B (sturm-z2e8.3) implementation.
//
// See synthesis_registry.hpp for the full contract. This .cpp implements
// the narrow map-access surface described there; there is no matcher
// bound to this module (unlike PI-1's `routine_registry.cpp`, which
// carries an AST-matcher callback alongside the container). Synthesis
// stages populate the registry by calling its methods directly from
// their own matcher callbacks — the container stays a pure data
// structure.
//
// Design notes
// ------------
//
// 1. The storage is an `std::unordered_map` keyed on
//    `const clang::FunctionDecl*` plus a parallel insertion-order
//    vector. The same shape `RoutineRegistry` uses. We rely on
//    `unordered_map::emplace` / `find` for O(1) access and on the
//    parallel vector to give `entries()` / `forwards()` deterministic
//    ordering across runs.
//
// 2. Every mutating method (`insert_forward`, `set_twin`,
//    `set_adjoint_name`, `set_status`) goes through the same
//    `find_mutable_entry` helper. The helper returns a pointer to the
//    stored `SynthesisEntry` (or nullptr when the lookup fails or the
//    input is null), so each setter becomes a two-liner that short-
//    circuits on nullptr. This keeps the null-FD guard contract
//    identical across the whole public surface without copy-pasting
//    the early-return four times.
//
// 3. `conflicts_with_routine_registry` is the one member that reaches
//    outside the container. It consults `RoutineRegistry::contains`
//    — a const read-only member — and returns its answer verbatim
//    after gating on a non-null `fwd`. The guard mirrors the rest of
//    the API; a null FD is "no conflict, but also not something you
//    can act on", which is the safest default for a caller that
//    forgot to gate.
//
// 4. `to_string(SynthesisStatus)` returns a `std::string_view` into a
//    static constant table. The spellings are part of the documented
//    contract (tests match on them), so we keep them byte-stable
//    regardless of the enum's underlying integer value.

#include "synthesis_registry.hpp"

#include "routine_registry.hpp"

namespace sturm::transpile {

namespace {

/// Shared mutating-lookup helper. Returns a pointer to the stored
/// entry for `fwd`, or nullptr when `fwd` is null or absent from
/// the map. All mutating setters funnel through this helper so the
/// null-FD guard behaviour stays identical across the public
/// surface.
SynthesisEntry* find_mutable_entry(
    std::unordered_map<const clang::FunctionDecl*, SynthesisEntry>& map,
    const clang::FunctionDecl* fwd) {
    if (fwd == nullptr) return nullptr;
    auto it = map.find(fwd);
    if (it == map.end()) return nullptr;
    return &it->second;
}

} // namespace

// ── Status stringification ──────────────────────────────────────────────────
//
// The spellings below are the single source of truth — diagnostic code
// and tests compare against these exact byte sequences, so any change
// is a breaking one. The default branch returns the empty string view
// rather than aborting: future additions to the enum should slot
// cleanly with a recompile of this TU (the test suite will catch a
// mismatched name before the diagnostic surface notices).
std::string_view to_string(SynthesisStatus status) {
    switch (status) {
        case SynthesisStatus::Pending:    return "pending";
        case SynthesisStatus::Normalized: return "normalized";
        case SynthesisStatus::Emitted:    return "emitted";
        case SynthesisStatus::Failed:     return "failed";
    }
    // Unreachable for a well-formed enum value; returning an empty
    // view is the conservative choice (the caller sees "unknown"
    // without undefined behaviour).
    return {};
}

// ── Insertion ──────────────────────────────────────────────────────────────
//
// `insert_forward` is the sole entry point for growing the registry.
// A brand-new key adds a `Pending`-status entry whose `forward`
// back-reference is the key itself (convenient for callers that want
// to carry the entry around as a self-describing record rather than
// relying on map iteration to pair key and value). Re-inserting an
// existing key is idempotent: it returns the pre-existing entry
// without touching any of its fields. This matters for pipeline
// stages that may be called more than once per forward (for example,
// a second transpile pass that wants to double-check validation
// already ran).
SynthesisEntry* SynthesisRegistry::insert_forward(
    const clang::FunctionDecl* fwd) {
    if (fwd == nullptr) return nullptr;
    auto it = map_.find(fwd);
    if (it == map_.end()) {
        SynthesisEntry fresh;
        fresh.forward      = fwd;
        fresh.twin         = nullptr;
        fresh.adjoint_name = {};
        fresh.status       = SynthesisStatus::Pending;
        auto inserted = map_.emplace(fwd, std::move(fresh));
        // `inserted.first` is the iterator to the newly-created entry;
        // we must take the address AFTER emplace returns because
        // emplace may rehash and invalidate any pre-existing iterator
        // — but a freshly-returned one is valid.
        order_.push_back(fwd);
        return &inserted.first->second;
    }
    // Existing entry — return the pointer unchanged. Callers that
    // want to reset state should call a setter explicitly; silently
    // clobbering fields on re-insertion would defeat the whole
    // book-keeping point.
    return &it->second;
}

// ── Read-only queries ──────────────────────────────────────────────────────

bool SynthesisRegistry::contains(const clang::FunctionDecl* fwd) const {
    if (fwd == nullptr) return false;
    return map_.find(fwd) != map_.end();
}

const SynthesisEntry* SynthesisRegistry::lookup(
    const clang::FunctionDecl* fwd) const {
    if (fwd == nullptr) return nullptr;
    auto it = map_.find(fwd);
    if (it == map_.end()) return nullptr;
    return &it->second;
}

// ── Mutating setters ────────────────────────────────────────────────────────
//
// Each setter funnels through `find_mutable_entry` so the null-FD /
// missing-entry no-op contract stays identical. The return value is
// `true` on the successful mutation path and `false` otherwise,
// matching the shape callers expect — a false return means "we did
// nothing", never "we half-applied the change".

bool SynthesisRegistry::set_twin(const clang::FunctionDecl* fwd,
                                 const clang::FunctionDecl* twin) {
    SynthesisEntry* entry = find_mutable_entry(map_, fwd);
    if (entry == nullptr) return false;
    entry->twin = twin;
    return true;
}

bool SynthesisRegistry::set_adjoint_name(const clang::FunctionDecl* fwd,
                                         std::string name) {
    SynthesisEntry* entry = find_mutable_entry(map_, fwd);
    if (entry == nullptr) return false;
    entry->adjoint_name = std::move(name);
    return true;
}

bool SynthesisRegistry::set_status(const clang::FunctionDecl* fwd,
                                   SynthesisStatus status) {
    SynthesisEntry* entry = find_mutable_entry(map_, fwd);
    if (entry == nullptr) return false;
    entry->status = status;
    return true;
}

// ── Deterministic iteration ────────────────────────────────────────────────
//
// `entries()` and `forwards()` walk the insertion-order vector and
// look each key up in the map. We do the map lookup (rather than
// storing a parallel vector of entries) because the entry's fields
// mutate in place and we want iteration to reflect the current
// state, not a stale snapshot. The extra map lookup is O(1) per
// entry — the total cost is still linear in `size()`.

std::vector<SynthesisEntry> SynthesisRegistry::entries() const {
    std::vector<SynthesisEntry> out;
    out.reserve(order_.size());
    for (const auto* key : order_) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            // Value-copy the entry so the caller has a stable
            // snapshot that survives any intervening mutations to
            // the registry. `adjoint_name` is a `std::string` —
            // the copy is deep.
            out.push_back(it->second);
        }
    }
    return out;
}

std::vector<const clang::FunctionDecl*> SynthesisRegistry::forwards() const {
    std::vector<const clang::FunctionDecl*> out;
    out.reserve(order_.size());
    for (const auto* key : order_) {
        // Only report keys that still resolve through the map.
        // `clear()` wipes both containers, but we defend against a
        // future contract drift that might leave one desynced.
        if (map_.find(key) != map_.end()) {
            out.push_back(key);
        }
    }
    return out;
}

// ── Bridge to PI-1 RoutineRegistry (PRD §9 Q2) ──────────────────────────────
//
// The conflict check is deliberately one direction: we read from
// `RoutineRegistry::contains`, we never call its mutating methods.
// This matches the PRD §9 Q2 precedence rule — the hand-registered
// pair wins unconditionally, and the synthesis registry defers to
// it without trying to override. The caller's job is to *act* on
// the answer; no side effects fire inside this method. A null FD
// answers false because there is nothing to conflict with — the
// null-FD guard contract says "bogus input is a no-op".
bool SynthesisRegistry::conflicts_with_routine_registry(
    const clang::FunctionDecl* fwd,
    const RoutineRegistry& routine_reg) const {
    if (fwd == nullptr) return false;
    return routine_reg.contains(fwd);
}

// ── Reset ───────────────────────────────────────────────────────────────────
//
// `clear()` wipes both containers in lock-step. Primarily exercised
// from tests; production pipelines keep the registry alive for the
// full ASTContext lifetime.
void SynthesisRegistry::clear() {
    map_.clear();
    order_.clear();
}

} // namespace sturm::transpile
