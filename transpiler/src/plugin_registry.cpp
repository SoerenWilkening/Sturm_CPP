// plugin_registry.cpp — PM4-2 implementation of the `Registry` class
// declared in `sturm/transpile/plugin_api.hpp` plus the Meyer's-singleton
// vector (`registrars()`) the link-time `StaticRegistrar` path appends to.
//
// Ownership model (see epic sturm-4oyr + plan §5):
//   - A `Registry` instance is owned by the host `TranspileConsumer` (one
//     per translation unit). There is no global/static Registry. Two
//     consumers running concurrently in the same process (e.g. outer +
//     nested via PM1-4) each construct their own Registry, and each
//     Registry's state is completely isolated from the others.
//   - The link-time registrar vector returned by `registrars()` IS
//     process-wide (one Meyer singleton) because the `StaticRegistrar`
//     ctor runs exactly once per plugin TU in static-init order. The
//     consumer-owned Registry pulls from that shared vector during its
//     own construction, but never writes back into it.
//
// Collision-detection policy:
//   - Duplicate `register_matcher` name → hard error, `std::abort`.
//   - Duplicate `register_op` kind_id → hard error, `std::abort`.
//   - Hard error is the only option because the Registry has no
//     stateful return channel (both methods are `void`), and a silent
//     drop-the-second-registration policy would be a debugging nightmare
//     when a user ships two plugins that both want "my-kind". The
//     pre-abort stderr line names the offending key so the user can
//     diagnose it from the build log alone.
//   - `std::abort` (not `throw`) keeps the TU in the transpiler's
//     global "no exceptions" policy (see AGENT/CLAUDE notes — the
//     transpiler sources do not use exceptions; LLVM is linked with
//     `-fno-exceptions` when LLVM_ENABLE_EH is off upstream).
//
// Meyer's singleton rationale:
//   Static-init order across TUs is unspecified. A namespace-scope global
//   `std::vector<...>` defined in this TU would risk a `StaticRegistrar`
//   ctor (running in some other TU's static-init) firing BEFORE this TU's
//   vector is constructed — pushing onto an uninitialized object. The
//   Meyer pattern (function-local `static`) constructs the vector on the
//   first call to `registrars()`, so the very first `StaticRegistrar`
//   ctor's call into `registrars()` triggers the construction, and every
//   subsequent call finds the same object. This is the standard
//   textbook fix for the TU-to-TU static-init-order fiasco.

#include "sturm/transpile/plugin_api.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace sturm::transpile::plugin {

// ── Meyer's singleton ────────────────────────────────────────────────────────
// Function-local `static` vector; lazy-constructed on the first call. Every
// subsequent call returns a reference to the same underlying object. C++11's
// "magic statics" give us thread-safe lazy construction for free, which is
// a belt-and-suspenders safety net: the transpiler runs single-threaded
// during its AST walk, but a plugin's `StaticRegistrar` ctor may fire
// during static-init on a thread other than the one that eventually
// constructs the `TranspileConsumer`.
std::vector<LinkTimeRegisterFn>& registrars() {
    static std::vector<LinkTimeRegisterFn> vec;
    return vec;
}

// ── Runtime-dlopen Meyer's singleton (PM4-4) ────────────────────────────────
// Separate from `registrars()` so the consumer ctor's drain order stays
// deterministic per plan §6 (in-tree → runtime → link-time). ParseArgs
// under `load=<path>` appends one entry per successfully dlopen'd plugin;
// each entry wraps the dlsym'd `sturm_register_plugin_v1` pointer so the
// drain can invoke it against the per-consumer Registry. Same lazy-
// construction posture as `registrars()` — no static-init-order
// dependency between the runtime-append (fires during cc1 ParseArgs,
// which runs after all static-init) and the ctor drain.
std::vector<LinkTimeRegisterFn>& runtime_registrars() {
    static std::vector<LinkTimeRegisterFn> vec;
    return vec;
}

// ── Registry::register_matcher ────────────────────────────────────────────────
// Duplicate-name check runs BEFORE we insert `fn` into either the name set
// or the drain vector: if the check fires, neither side observes a partial
// mutation. The set is the canonical collision oracle; the drain vector is
// a write-only log that `invoke_all` iterates.
void Registry::register_matcher(std::string_view name, MatcherRegisterFn fn) {
    const std::string key(name);
    if (matcher_names_.count(key) > 0) {
        std::fprintf(stderr,
                     "sturm-transpile plugin: duplicate matcher "
                     "registration for name '%s' (second registration "
                     "rejected)\n",
                     key.c_str());
        std::abort();
    }
    matcher_names_.insert(key);
    matchers_.push_back(std::move(fn));
}

// ── Registry::register_op ─────────────────────────────────────────────────────
// Duplicate-kind_id check runs BEFORE we insert the renderer or append the
// matcher, for the same atomicity reason as `register_matcher`. The
// `render_fns_` map doubles as the collision oracle — a kind_id already
// present in the map means the same plugin already registered it (or a
// second plugin tried to claim it).
void Registry::register_op(std::string_view kind_id,
                           MatcherRegisterFn fn,
                           UncomputeRenderFn render_fn) {
    const std::string key(kind_id);
    if (render_fns_.count(key) > 0) {
        std::fprintf(stderr,
                     "sturm-transpile plugin: duplicate op registration "
                     "for kind_id '%s' (second registration rejected)\n",
                     key.c_str());
        std::abort();
    }
    render_fns_.emplace(key, std::move(render_fn));
    matchers_.push_back(std::move(fn));
}

// ── Registry::invoke_all ──────────────────────────────────────────────────────
// Insertion-order drain. The vector is not cleared afterward: a second
// call to `invoke_all` on the same Registry would fire every callback
// again. This matches the PM1-4 nested-invocation scenario — an outer
// consumer's Registry may be re-drained when the nested consumer is
// constructed — but also keeps the Registry a pure value type with no
// "consumed" state to track.
void Registry::invoke_all(clang::ast_matchers::MatchFinder& finder,
                          QUnit& unit) {
    for (const auto& fn : matchers_) {
        fn(finder, unit);
    }
}

// ── Registry::find_render_fn ──────────────────────────────────────────────────
// PM4-3 forward-compat lookup. Returns a pointer into the internal map so
// the caller does not pay a copy cost per dispatch. The pointer is valid
// for the life of the Registry; PM4-3's `render_uncompute` dispatch runs
// synchronously inside the same consumer that owns the Registry, so the
// pointer never outlives its target.
const UncomputeRenderFn* Registry::find_render_fn(
    std::string_view kind_id) const {
    const auto it = render_fns_.find(std::string(kind_id));
    if (it == render_fns_.end()) {
        return nullptr;
    }
    return &it->second;
}

// ── Registry::kind_ids ────────────────────────────────────────────────────────
// PM4-4 diagnostic helper. Walks the `render_fns_` map and returns every
// registered kind_id as a flat vector. Iteration order mirrors the
// unordered_map's internal layout (unspecified but stable across reads of
// the same map), which is fine for the one consumer: plugin.cpp's
// verbose-mode trace concatenates them into a single comma-separated
// line. Not intended for dispatch-hot paths — the caller pays a full
// copy of every key.
std::vector<std::string> Registry::kind_ids() const {
    std::vector<std::string> out;
    out.reserve(render_fns_.size());
    for (const auto& [k, _] : render_fns_) {
        out.push_back(k);
    }
    return out;
}

} // namespace sturm::transpile::plugin
