// pm4_collision_plugin.cpp — PM4-9 smoke-5 fixture: a second runtime-
// loadable plugin whose `sturm_register_plugin_v1` entry registers the
// SAME `kind_id = "pm4.demo.tag"` string the first demo plugin at
// `examples/plugin_demo/plugin_demo.cpp` already claims.
//
// Scope
// -----
// The `pm4_two_plugins_independent` smoke (PM4-9 smoke 5 per the plan
// §Verification) pins the Registry's duplicate-`kind_id` collision
// detection from PM4-2 (`plugin_registry.cpp:108-113`): two runtime-
// dlopen'd plugins that BOTH call `register_op("pm4.demo.tag", ...)`
// must hard-abort the second registration with the documented stderr
// line
//
//     "sturm-transpile plugin: duplicate op registration for kind_id "
//     "'pm4.demo.tag' (second registration rejected)"
//
// This TU is built as a standalone MODULE library
// `sturm-pm4-collision-plugin` (see `transpiler/tests/CMakeLists.txt`)
// — the plan §5 discipline that runtime-load plugins are MODULE
// artifacts while the in-tree clang plugin is SHARED. The module has
// no DT_NEEDED entry against clang / LLVM; its only undefined
// references (`Registry::register_op`) resolve against the already-
// loaded host plugin at dlopen time, same shape as the first demo
// plugin at `examples/plugin_demo/plugin_demo.cpp`.
//
// Why this lives under `transpiler/tests/fixtures/` rather than
// `examples/plugin_demo/`
// -------------------------------------------------------------------
// The first demo plugin at `examples/plugin_demo/plugin_demo.cpp` is
// the canonical "here is what a third-party plugin looks like"
// reference. This collision fixture has the OPPOSITE shape — its
// entire reason to exist is to hit the Registry's hard-abort path
// on a duplicate-kind_id. Parking it alongside the other PM4 smoke
// test fixtures keeps the example directory's posture clean: a
// casual reader copying plugin_demo.cpp as a starting point does
// NOT encounter the collision-intent body by mistake.
//
// Mirroring the first demo's structure
// ------------------------------------
// Body is intentionally near-verbatim to the first demo at
// `examples/plugin_demo/plugin_demo.cpp`:
//   - Same `extern "C"` `sturm_register_plugin_v1` entry point.
//   - Same `kTagKindId = "pm4.demo.tag"` constant string — this is
//     the collision payload.
//   - Same no-op matcher lambda, same self-inverse renderer pattern
//     (different output text: `pm4_collision_tag_inverse(<q>)` rather
//     than `pm4_demo_tag_inverse(<q>)` — a deliberate drift so the
//     test harness can distinguish which plugin's renderer was
//     reached if the abort were ever incorrectly bypassed).
//   - Same `STURM_PLUGIN_DEFINE_CLANG_VERSION()` ABI-version macro
//     expansion so the host's `dlopen` → version-check chain accepts
//     the module identically to plugin_demo.
//
// LOC budget: ≤ 300 lines (CLAUDE.md). This TU is well under.

#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/Basic/Version.inc"

#include <sstream>
#include <string>

namespace sturm::transpile::pm4_collision {

// Collision payload: same `kind_id` string `examples/plugin_demo/
// plugin_demo.cpp:89` registers. Loading both plugins into one
// process triggers the Registry's duplicate-registration abort.
inline constexpr const char* kTagKindId = "pm4.demo.tag";

// Render a visibly DIFFERENT uncompute text than the first demo
// plugin so that, if the abort were ever incorrectly bypassed, a
// grep of the rewritten buffer could identify which plugin's
// renderer landed there. The `pm4_collision_tag_inverse(` token is
// not emitted by the first demo plugin and is not part of any
// in-tree renderer.
std::string render_collision_tag_inverse(const QOperation& op) {
    if (op.result.name.empty()) {
        return {};
    }
    std::ostringstream os;
    os << "    pm4_collision_tag_inverse(" << op.result.name << ");\n";
    return os.str();
}

} // namespace sturm::transpile::pm4_collision

// Runtime-load entry point — same shape as plugin_demo.cpp. The
// host's `plugin.cpp:load_runtime_plugin` dlsym's this symbol and
// invokes it against the per-consumer `Registry&` (plan §4). The
// second registration of `pm4.demo.tag` is what drives the smoke's
// expected abort path.
extern "C" void sturm_register_plugin_v1(
    ::sturm::transpile::plugin::Registry& registry) {
    using ::sturm::transpile::QUnit;
    using ::sturm::transpile::pm4_collision::kTagKindId;
    using ::sturm::transpile::pm4_collision::render_collision_tag_inverse;

    registry.register_op(
        kTagKindId,
        // No-op matcher: the plan §4 mirror of plugin_demo — this
        // smoke exercises the Registry collision detector, not the
        // AST-rewrite path. The host's `invoke_all` drain fires
        // this lambda during `TranspileConsumer`'s setup and it
        // adds zero matchers to the MatchFinder.
        [](clang::ast_matchers::MatchFinder&, QUnit&) {
            // intentional: the collision smoke's contract lives on
            // the `register_op` side, not the matcher side.
        },
        // Renderer forward (stateless; std::function erases to a
        // pointer to the free function, not a closure).
        &::sturm::transpile::pm4_collision::render_collision_tag_inverse);
}

// PM4-4 Clang-ABI gate: expand to `extern "C" const char*
// sturm_plugin_clang_version_v1()` returning the
// `CLANG_VERSION_STRING` this TU was compiled against, so the host's
// `dlopen → version check → dlsym` chain at plugin.cpp:206-230 accepts
// this module identically to plugin_demo. Without this macro
// expansion the host refuses to load the plugin and the smoke's
// second-registration collision never fires.
STURM_PLUGIN_DEFINE_CLANG_VERSION()
