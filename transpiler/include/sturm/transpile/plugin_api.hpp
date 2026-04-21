// plugin_api.hpp — PM4-1: public plugin extension surface for the
// sturm-transpile transpiler. Sole contact point between sturm-transpile
// and a third-party plugin `.so`. A plugin TU includes this header to:
//
//   1. Author a body for `extern "C" void sturm_register_plugin_v1(...)`
//      (runtime-dlopen primary path; plan §3), OR
//   2. Register a link-time `StaticRegistrar` via `STURM_REGISTER_PLUGIN`
//      (fallback, plan §5).
//
// Scope (plan §1):
//   - AST matchers — register callables handed a `MatchFinder&` + `QUnit&`
//     that own their `MatchCallback` lifetime; the host keeps plugin .so's
//     loaded for the life of the process (no `dlclose`, plan §7).
//   - Uncompute rules — register a string-keyed renderer consulted by
//     `render_uncompute`'s `case QOpKind::PLUGIN:` (PM4-3).
//
// NOT exposed: DiagnosticsEngine, IR passes (`synthesize()`), runtime.
// ABI versioning: `_v1` is part of the symbol name; breaking changes bump
// to `_v2`. Unstable across sturm minor versions — rebuild plugins per
// release. LOC budget: ≤ 300 lines (CLAUDE.md rule).

#ifndef STURM_TRANSPILE_PLUGIN_API_HPP
#define STURM_TRANSPILE_PLUGIN_API_HPP

#include "sturm/transpile/qir.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Forward-declare the Clang matcher bridge so this header does not drag
// the heavyweight `ASTMatchFinder.h` into every plugin TU that includes it.
// The plugin body is free to include `clang/ASTMatchers/ASTMatchFinder.h`
// on its own — the `Registry::register_matcher` callback receives a
// `MatchFinder&` by reference so the full type must be complete at the
// point of USE, but the Registry's SIGNATURE only needs a forward
// declaration for typecheck.
namespace clang::ast_matchers {
class MatchFinder;
} // namespace clang::ast_matchers

namespace sturm::transpile::plugin {

/// Signature of an AST-matcher registration callback.
///
/// The host invokes this callable once per plugin entry point, handing in
/// the MatchFinder the in-tree matchers are also registered on plus the
/// same `QUnit&` downstream passes consume. The plugin is expected to:
///
///   1. Instantiate one or more `clang::ast_matchers::MatchFinder::
///      MatchCallback` subclasses (owned by the plugin — the MatchFinder
///      stores raw pointers only).
///   2. Call `finder.addMatcher(<matcher>, <callback>)` for each pattern.
///   3. Arrange for every match hit to append one or more `QOperation`
///      / `QReplacement` / `UncomputeInsertion` records to `unit`.
///
/// Callback object lifetime extends to process exit: the host never
/// `dlclose`s plugin libraries in `_v1` (plan §7), so code-segment
/// addresses remain valid for every subsequent matcher dispatch.
using MatcherRegisterFn =
    std::function<void(clang::ast_matchers::MatchFinder& finder,
                       QUnit& unit)>;

/// Signature of an uncompute-renderer callback.
///
/// Invoked by the host's `render_uncompute` switch inside the newly-added
/// `case QOpKind::PLUGIN:` arm (PM4-3). The returned string must match the
/// shape of existing in-tree uncompute renderings — four-space indent,
/// terminated by '\n'. Returning an empty string on a malformed
/// `QOperation` (missing operands, etc.) is the convention the in-tree
/// renderers already follow; the plugin's renderer should do the same
/// rather than crash the host.
using UncomputeRenderFn = std::function<std::string(const QOperation& op)>;

/// Facade the host hands to a plugin's `sturm_register_plugin_v1(Registry&)`
/// entry point.
///
/// The Registry is **owned by the host** `TranspileConsumer` and lives for
/// the duration of one translation-unit consume cycle. A plugin must NOT
/// cache the `Registry&` past the return of `sturm_register_plugin_v1` — a
/// nested `CompilerInvocation` (PM1-4, see plan §5 / §6) constructs a
/// second consumer whose Registry is a different object.
///
/// Methods are declared here; the implementation lands in
/// `src/plugin_registry.cpp` in PM4-2.
class Registry {
public:
    Registry() = default;
    ~Registry() = default;

    // Non-copyable / non-movable: the host passes a reference across the
    // ABI; copy / move would silently break the assumption that every
    // `addMatcher(..., callback)` is rooted in the same object the plugin
    // saw.
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    /// Register a new AST-matcher callback family under a human-readable
    /// name. The name is used solely for diagnostics (e.g. "plugin X
    /// registered matcher 'foo' twice") — it is NOT looked up at match
    /// time. Collisions on `name` across two plugins cause a hard error
    /// from the host at the second registration.
    void register_matcher(std::string_view name, MatcherRegisterFn fn);

    /// Register an uncompute-rendering function under a string `kind_id`.
    /// The `kind_id` is the string the plugin's own matcher stores in
    /// `QOperation::plugin_kind_id` (PM4-3) when it records a
    /// `QOpKind::PLUGIN` op. When the host's `render_uncompute` switch
    /// reaches that op, it looks up the registered `render_fn` by
    /// `kind_id` and invokes it.
    ///
    /// `fn` is an optional matcher-registration companion to `render_fn`
    /// — most plugins will want to seed the op kind at AST-match time,
    /// and passing the matcher side-by-side with the renderer keeps the
    /// two in one call site. A plugin that seeds its ops through a
    /// separate `register_matcher(name, fn)` call may pass a no-op
    /// closure here.
    ///
    /// Collisions on `kind_id` across two plugins cause a hard error
    /// from the host at the second registration (plan §Verification
    /// smoke 5, `pm4_two_plugins_independent`).
    void register_op(std::string_view kind_id,
                     MatcherRegisterFn fn,
                     UncomputeRenderFn render_fn);

    /// Drain every matcher-registration callback accumulated by the
    /// `register_matcher` / `register_op` calls above, in insertion order.
    /// Called by `TranspileConsumer`'s constructor (PM4-3) after the
    /// in-tree matcher pool is seeded and after the runtime-dlopen /
    /// link-time drains have filled this Registry (plan §6).
    void invoke_all(clang::ast_matchers::MatchFinder& finder, QUnit& unit);

    /// Look up the uncompute renderer registered for a given `kind_id`.
    /// Returns `nullptr` when no renderer is registered. The PM4-3
    /// `render_uncompute` dispatch (`case QOpKind::PLUGIN:`) uses this
    /// against `QOperation::plugin_kind_id`.
    const UncomputeRenderFn* find_render_fn(std::string_view kind_id) const;

    /// PM4-4 diagnostic helper: return every kind_id currently registered
    /// (insertion order not preserved — iteration order mirrors the
    /// unordered_map's internal bucket layout). Used by plugin.cpp's
    /// verbose-mode trace to print which ops a runtime-dlopen plugin
    /// claimed, so a test harness can assert the plugin registered the
    /// expected kind without round-tripping through PM4-3's consumer-ctor
    /// drain. Not part of the plugin-side ABI — plugin code never calls
    /// this, only the host.
    std::vector<std::string> kind_ids() const;

private:
    // `register_matcher` and `register_op` share no key domain — a plugin
    // may register a matcher named "foo" AND an op with `kind_id="foo"`
    // without a collision. The two sets below are independent.
    std::unordered_set<std::string> matcher_names_;
    std::unordered_map<std::string, UncomputeRenderFn> render_fns_;

    // Single vector drained by `invoke_all` in insertion order (§6
    // ordering matters when two runtime-dlopen plugins compete for the
    // same AST anchor).
    std::vector<MatcherRegisterFn> matchers_;
};

/// Holds one link-time registration function registered via
/// `StaticRegistrar`. Each entry is invoked by the host's consumer
/// constructor (plan §5 / §6) against the consumer's Registry.
using LinkTimeRegisterFn = std::function<void(Registry&)>;

/// Meyer's singleton returning the process-wide vector of link-time
/// registration functions. Defined in `plugin_registry.cpp`. The first
/// call constructs the vector in function-local storage; every
/// `StaticRegistrar` ctor appends to it; the consumer ctor drains it in
/// insertion order. Lazy construction avoids the TU-to-TU static-init-
/// order fiasco described in the plan §5: a `StaticRegistrar` ctor
/// running before this function's first call constructs the vector on
/// demand, so no registrar ever observes uninitialized storage.
std::vector<LinkTimeRegisterFn>& registrars();

/// PM4-4 runtime-dlopen counterpart to `registrars()`. Populated by
/// `plugin.cpp`'s `ParseArgs` after a successful `dlopen` + version check +
/// `dlsym("sturm_register_plugin_v1")`. Each entry wraps the dlsym'd entry
/// point so the consumer's drain can invoke it against its per-consumer
/// Registry. Drain order per plan §6 is in-tree → runtime → link-time,
/// which PM4-3's consumer ctor enforces by draining this vector before
/// `registrars()`.
std::vector<LinkTimeRegisterFn>& runtime_registrars();

/// Helper object a plugin creates at namespace scope to enqueue a
/// link-time registration. The typical usage is through the
/// `STURM_REGISTER_PLUGIN` macro below — direct instantiation is fine
/// for bespoke layouts, but the macro covers the 95% case.
///
/// The ctor runs during static-init of the plugin's TU (before `main`,
/// in whichever order the linker picks). It pushes `fn` onto the
/// Meyer's-singleton vector so the host's consumer ctor can invoke it
/// when a translation unit is being consumed.
struct StaticRegistrar {
    explicit StaticRegistrar(LinkTimeRegisterFn fn) {
        registrars().push_back(std::move(fn));
    }
};

} // namespace sturm::transpile::plugin

/// Convenience macro: declare a static-storage registrar for a plugin
/// `TypeName` whose `register_all(Registry&)` method performs every
/// `register_matcher` / `register_op` call the plugin needs.
///
/// Expected shape of the plugin type:
///
///     namespace my_plugin {
///     struct MyPlugin {
///         void register_all(::sturm::transpile::plugin::Registry& r) {
///             r.register_matcher("my-matcher", /* ... */);
///             r.register_op("my-kind", /* ... */, /* ... */);
///         }
///     };
///     } // namespace my_plugin
///     STURM_REGISTER_PLUGIN(my_plugin::MyPlugin)
///
/// The macro expands to a namespace-scope `static StaticRegistrar`
/// whose identifier interpolates the caller-supplied type to make
/// multiple registrations in the same TU collision-free. The lambda
/// default-constructs a `TypeName` instance so plugin authors can hold
/// per-plugin state as member variables without needing a separate
/// factory.
///
/// Defined as an object-like macro outside the plugin namespace so it
/// is callable as plain `STURM_REGISTER_PLUGIN(...)` from any scope.
/// The `_sturm_reg_` prefix on the generated identifier avoids user-
/// name collisions; a token paste on `TypeName` keeps the identifier
/// short and debuggable even for namespace-qualified names (the `::`
/// in `ns::T` tokenizes cleanly inside an identifier suffix).
#define STURM_REGISTER_PLUGIN(TypeName)                                      \
    static ::sturm::transpile::plugin::StaticRegistrar                       \
        _sturm_reg_##TypeName{                                               \
            [](::sturm::transpile::plugin::Registry& r) {                    \
                TypeName{}.register_all(r);                                  \
            }}

/// Entry point every runtime-loaded plugin must export (plan §2).
///
/// The host locates this symbol via `dlsym(handle,
/// "sturm_register_plugin_v1")` after a successful `dlopen`. A null
/// result is treated as "not a sturm plugin" and the host fails loudly
/// (plan §3). The plugin body calls
/// `registry.register_matcher(...)` / `registry.register_op(...)` for
/// each extension it contributes.
///
/// The declaration is `extern "C"` so the symbol survives C++ name
/// mangling across the .so boundary. The `Registry` argument is passed
/// by non-const reference: the host owns the object and the plugin
/// mutates it in-place.
///
/// Plugins must NOT define this entry point AND register via
/// `STURM_REGISTER_PLUGIN` in the same TU — the two paths are mutually
/// exclusive for a given .so (runtime dlopen vs. link-time bake-in).
/// Mixing them would cause double-registration: the link-time path
/// fires at static-init time, then `dlopen` invokes the entry point,
/// and every matcher / op gets pushed twice. The host's collision
/// detection (PM4-2) will turn this into a hard error at the second
/// registration.
extern "C" void sturm_register_plugin_v1(
    ::sturm::transpile::plugin::Registry& registry);

/// PM4-4 Clang-ABI gate. Every runtime-loaded plugin must also export
/// `sturm_plugin_clang_version_v1` — a C-linkage accessor returning the
/// `CLANG_VERSION_STRING` the plugin was built against. The host
/// `dlsym`s this symbol before calling `sturm_register_plugin_v1` and
/// refuses to register the plugin if the returned string differs from
/// its own. Mismatch = refuse to load; missing = refuse to load (plan
/// §4). The plugin author invokes `STURM_PLUGIN_DEFINE_CLANG_VERSION()`
/// at namespace scope in its TU; the macro expands to an `extern "C"`
/// function definition returning `CLANG_VERSION_STRING`, which requires
/// `<clang/Basic/Version.inc>` (or a transitive include that pulls it,
/// e.g. the plugin's `ASTMatchFinder.h` include via `Version.h`) to be
/// in scope.
#define STURM_PLUGIN_DEFINE_CLANG_VERSION()                                  \
    extern "C" const char* sturm_plugin_clang_version_v1() {                 \
        return CLANG_VERSION_STRING;                                         \
    }

#endif // STURM_TRANSPILE_PLUGIN_API_HPP
