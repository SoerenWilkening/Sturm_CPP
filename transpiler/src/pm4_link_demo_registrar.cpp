// pm4_link_demo_registrar.cpp — PM4-10 shim TU that compiles into
// `sturm-transpile-plugin` only when `STURM_PM4_LINK_DEMO=ON`.
//
// Why a separate shim TU?
// -----------------------
// The demo plugin at `examples/plugin_demo/plugin_demo.cpp` is designed
// for the dlopen path: its only externally-visible entry point is
// `extern "C" void sturm_register_plugin_v1(Registry&)`, invoked by the
// host through `dlsym`. The demo's file-level comment explicitly forbids
// defining that `extern "C"` entry point AND a `STURM_REGISTER_PLUGIN`
// registrar in the SAME TU — the two paths are mutually exclusive per
// `plugin_api.hpp:273-278` because the link-time path fires at static-
// init and a subsequent `dlopen` would double-register every matcher and
// kind_id.
//
// The link-time path (`STURM_REGISTER_PLUGIN`) therefore needs a
// companion TU that:
//
//   1. Leaves the demo plugin's `extern "C"` entry point untouched (so
//      the dlopen path, `libsturm-pm4-demo-plugin.so`, still works when
//      it ships separately).
//   2. Adds a namespace-scope `StaticRegistrar` whose lambda forwards
//      into the demo's renderer + matcher registrations.
//
// The cleanest shape: forward-declare the demo's `sturm_register_plugin_v1`
// entry and call it from the link-time lambda. This reuses the demo's
// canonical registration logic verbatim (one source of truth for the
// kind_id, the renderer function pointer, and the no-op matcher
// callback). Any future change to the demo's `register_op` call body
// propagates to both the dlopen path AND the link-time path without a
// second edit.
//
// Important build-time coupling
// -----------------------------
// This TU is ONLY compiled into `sturm-transpile-plugin` when
// `STURM_PM4_LINK_DEMO=ON`. The `transpiler/CMakeLists.txt` option
// gates both this TU and `examples/plugin_demo/plugin_demo.cpp` behind
// the same `if()` branch, so the `sturm_register_plugin_v1` symbol this
// file forward-declares is guaranteed present on the link line. When
// the option is OFF (the default), neither TU is compiled into the
// transpiler plugin — this file is inert and the demo plugin remains a
// standalone MODULE library reachable only through dlopen.
//
// Rationale for living under `transpiler/src/` rather than
// `examples/plugin_demo/`
// -----------------------------------------------------------------------
// `examples/plugin_demo/plugin_demo.cpp` is the canonical demo. This
// shim is build-variant glue that embeds the demo into the transpiler
// plugin for the PM4-10 link-time path. Keeping the shim out of the
// `examples/plugin_demo/` directory preserves the demo's "here is what
// a third-party plugin looks like" posture — a plugin author copying
// the demo TU as a starting point does NOT need to also understand
// the shim's build-variant story.
//
// LOC budget: well under the 300-line per-TU ceiling.

#include "sturm/transpile/plugin_api.hpp"

namespace sturm::transpile {
namespace plugin {
class Registry;  // forward-decl to keep the include minimal; the full
                 // class declaration is pulled via plugin_api.hpp above
                 // — this note is documentary.
} // namespace plugin
} // namespace sturm::transpile

// Forward-declare the demo plugin's canonical entry point. Resolved at
// link time against `examples/plugin_demo/plugin_demo.cpp` (also
// compiled into `sturm-transpile-plugin` when `STURM_PM4_LINK_DEMO=ON`
// — see `transpiler/CMakeLists.txt`).
extern "C" void sturm_register_plugin_v1(
    ::sturm::transpile::plugin::Registry& registry);

namespace sturm::transpile::pm4_link_demo {

/// Link-time plugin adaptor for the PM4-7 demo.
///
/// The `STURM_REGISTER_PLUGIN(TypeName)` macro expands into a namespace-
/// scope `static StaticRegistrar` whose lambda default-constructs a
/// `TypeName` and calls `TypeName{}.register_all(r)`. This struct
/// satisfies that contract by forwarding into the demo plugin's
/// `sturm_register_plugin_v1` entry point — the same function the
/// runtime-dlopen path (PM4-4) will dlsym and invoke against its
/// per-consumer Registry.
///
/// Keeping the forward-into-`sturm_register_plugin_v1` indirection
/// (rather than duplicating the demo's `register_op` call site verbatim
/// here) ensures a single source of truth for the kind_id / matcher /
/// renderer triple. A future refactor of the demo's registration body
/// propagates to both registration paths automatically.
struct LinkTimeDemoPlugin {
    void register_all(::sturm::transpile::plugin::Registry& r) const {
        sturm_register_plugin_v1(r);
    }
};

} // namespace sturm::transpile::pm4_link_demo

// The `STURM_REGISTER_PLUGIN(TypeName)` macro's token-paste on
// `TypeName` (`_sturm_reg_##TypeName` in `plugin_api.hpp`) rejects
// fully-qualified names — `::` is not a valid preprocessor-identifier
// token. Introduce a file-local `using` alias so the macro sees an
// unqualified identifier. The alias is placed in the anonymous namespace
// to keep it TU-local — two TUs that each register their own
// `LinkTimeDemoPlugin` would not clash at link time.
namespace {
using LinkTimeDemoPluginAlias =
    ::sturm::transpile::pm4_link_demo::LinkTimeDemoPlugin;
} // anonymous namespace

// Namespace-scope registration. The `StaticRegistrar` ctor runs during
// static-init of this TU (before `main`, in whichever order the linker
// picks). It pushes the forwarding lambda onto the Meyer-singleton
// vector returned by `::sturm::transpile::plugin::registrars()`. The
// host's `TranspileConsumer` drains that vector during construction
// (per plan §5 / §6), at which point the lambda default-constructs a
// `LinkTimeDemoPlugin` (via the `LinkTimeDemoPluginAlias` passed to the
// macro) and calls its `register_all(Registry&)` method — which in turn
// calls `sturm_register_plugin_v1(r)`, seeding the Registry with
// `register_op("pm4.demo.tag", ...)` exactly as the dlopen path would.
STURM_REGISTER_PLUGIN(LinkTimeDemoPluginAlias);
