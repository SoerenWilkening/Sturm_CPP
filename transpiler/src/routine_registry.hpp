// routine_registry.hpp — Phase I PI-1 context-wide forward/adjoint map.
//
// Purpose
// -------
// Phase I of the transpiler roadmap widens coverage to user-defined
// routines. Per principle P9, each user-written quantum routine ships
// with a *manual* adjoint, and the runtime header
// `include/sturm/routines/invert.hpp` (PI-0) exposes a
// `STURM_REGISTER_ADJOINT(fn, adj)` macro that records the pair via a
// specialization of `sturm::_detail::adjoint_of<decltype(&fn)>`.
//
// The transpiler needs to know, at match time, which forward routines
// have a registered adjoint so a call `my_routine(a, b)` can be paired
// with `invert(my_routine)(a, b)` during uncomputation. This module owns
// the data structure behind that lookup: a map from
// `const clang::FunctionDecl*` (the *forward* routine's canonical decl)
// to the source-level identifier of the adjoint function.
//
// The map is populated by `register_routine_registry_matcher`, which
// attaches a Clang ASTMatcher that fires on every
// `ClassTemplateSpecializationDecl` of `sturm::_detail::adjoint_of`.
// Downstream subphases (PI-2..PI-7) read from the populated registry
// but do not mutate it.
//
// Scope (PI-1 only)
// -----------------
// This header + its matcher are the *only* PI-1 deliverable. No
// routine-call matcher (PI-2+) or uncompute emission is implemented
// here — they consume this registry without modifying it.
//
// Lifetime + thread-safety
// ------------------------
// The registry stores raw `clang::FunctionDecl*` keys that are owned by
// the `clang::ASTContext` the matcher ran under. Callers MUST NOT use
// the registry after that ASTContext is destroyed. In the transpiler's
// main pipeline (`main.cpp`) the registry lives on the
// `TranspileConsumer` alongside the `QUnit`, and both are destroyed at
// the same time the ASTContext is, so the constraint is automatic.
//
// The matcher and registry are single-threaded: `clang::MatchFinder`
// runs all callbacks on one thread within `matchAST`, so no locking is
// required. This mirrors every other matcher module in the transpiler.
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers / 400 LOC for
// implementation files. Keeping the registry as a thin wrapper around
// a stable-insertion-order vector + unordered_map (so `entries()` can
// iterate deterministically) lets this header stay well under budget.

#ifndef STURM_TRANSPILE_ROUTINE_REGISTRY_HPP
#define STURM_TRANSPILE_ROUTINE_REGISTRY_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clang { class FunctionDecl; }

namespace sturm::transpile {

/// Context-wide forward/adjoint map populated by the Phase I PI-1 AST
/// matcher. The key is the canonical `clang::FunctionDecl*` of the
/// *forward* routine; the value is the source-level identifier of the
/// registered adjoint (e.g. `"my_routine_adj"`).
///
/// The storage is an `std::unordered_map` plus a parallel vector of
/// insertion order so iteration via `entries()` is deterministic. Tests
/// depend on deterministic ordering for their snapshot-style checks
/// (they compare `entries()` against a fixed sequence of expected
/// pairs); production consumers of the registry do not require
/// ordering, but determinism aids diagnostics regardless.
///
/// The public API is intentionally small:
///   - `insert_pair(fd, adj_name)` adds or overwrites an entry. The
///     matcher is the sole producer; overwrite semantics keep the
///     registry robust if a user accidentally registers the same
///     forward twice (which is ill-formed at compile time, but the
///     transpiler runs on potentially-broken source).
///   - `contains`, `lookup`, `size`, `empty` — the typical read-side
///     surface PI-2 (and downstream) need.
///   - `entries()` exposes the map for iteration without surfacing the
///     underlying storage type, so future refactors can swap the
///     container without breaking callers.
///   - `clear()` resets state — primarily for tests.
class RoutineRegistry {
public:
    using value_type = std::pair<const clang::FunctionDecl*, std::string>;

    RoutineRegistry() = default;
    // Non-copyable: the map stores raw AST pointers tied to one
    // ASTContext; accidental copies would silently duplicate the
    // lifetime hazard. Moves are fine.
    RoutineRegistry(const RoutineRegistry&)            = delete;
    RoutineRegistry& operator=(const RoutineRegistry&) = delete;
    RoutineRegistry(RoutineRegistry&&)                 = default;
    RoutineRegistry& operator=(RoutineRegistry&&)      = default;

    /// Insert or overwrite the adjoint name registered for `fwd`.
    /// `fwd == nullptr` is a no-op — the matcher passes a null pointer
    /// when it cannot resolve the forward FunctionDecl from the
    /// specialization's template argument, and the registry must not
    /// store a phantom entry in that case.
    void insert_pair(const clang::FunctionDecl* fwd, std::string adj_name) {
        if (!fwd) return;
        auto it = map_.find(fwd);
        if (it == map_.end()) {
            order_.push_back(fwd);
            map_.emplace(fwd, std::move(adj_name));
        } else {
            it->second = std::move(adj_name);
        }
    }

    /// Read-only accessors for the read side of the contract.
    bool empty() const noexcept       { return map_.empty(); }
    std::size_t size() const noexcept { return map_.size(); }
    bool contains(const clang::FunctionDecl* fwd) const {
        return fwd != nullptr && map_.find(fwd) != map_.end();
    }

    /// Return a pointer to the stored adjoint name, or nullptr if
    /// `fwd` is not registered. Pointer stability holds until the
    /// next `insert_pair` / `clear` call mutates the map.
    const std::string* lookup(const clang::FunctionDecl* fwd) const {
        if (!fwd) return nullptr;
        auto it = map_.find(fwd);
        if (it == map_.end()) return nullptr;
        return &it->second;
    }

    /// Deterministic iteration: yields `{FunctionDecl*, adj_name}`
    /// pairs in insertion order. Callers must not retain the returned
    /// vector past the next `insert_pair` / `clear` call — it is
    /// materialised on demand from `order_` and `map_`.
    std::vector<value_type> entries() const {
        std::vector<value_type> out;
        out.reserve(order_.size());
        for (const auto* key : order_) {
            auto it = map_.find(key);
            if (it != map_.end()) out.emplace_back(key, it->second);
        }
        return out;
    }

    /// Reset state. Primarily for tests.
    void clear() {
        map_.clear();
        order_.clear();
    }

private:
    std::unordered_map<const clang::FunctionDecl*, std::string> map_;
    std::vector<const clang::FunctionDecl*> order_;
};

/// Register the Phase I PI-1 routine-registry matcher against `finder`,
/// directing every match into `registry`. `registry` must outlive the
/// MatchFinder's run. Call at most once per registry.
///
/// Matched shape — exactly what `STURM_REGISTER_ADJOINT(fn, adj)`
/// expands to:
///
///     namespace sturm { namespace _detail {
///     template <> struct adjoint_of<decltype(&::fn)> {
///         static constexpr auto value = &::adj;
///     };
///     }}
///
/// The matcher anchors on `classTemplateSpecializationDecl` whose
/// specialized template is `::sturm::_detail::adjoint_of`. On match,
/// the callback:
///
///   1. Resolves the *forward* routine by walking the template
///      argument's `TypeSourceInfo`. The argument is written as
///      `decltype(&::fn)`, so the walk descends through
///      `DecltypeTypeLoc::getUnderlyingExpr()` → `UnaryOperator(&)`
///      → `DeclRefExpr` → `FunctionDecl`.
///   2. Resolves the *adjoint* name by walking the `value` VarDecl's
///      initializer: `UnaryOperator(&)` → `DeclRefExpr`, then calling
///      `getNameAsString()` on the referenced function decl. Falls
///      back to the raw spelling of the DeclRefExpr's name info if
///      the decl pointer is unavailable.
///   3. Calls `registry.insert_pair(fwd_decl, adj_name)`.
///
/// Any structural deviation (missing `value` field, initializer is not
/// an `AddrOf` over a `DeclRefExpr`, template argument is not a
/// `DecltypeType`-bearing shape) is treated as "not our macro" and the
/// callback early-returns without mutating the registry.
void register_routine_registry_matcher(
    clang::ast_matchers::MatchFinder& finder, RoutineRegistry& registry);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_ROUTINE_REGISTRY_HPP
