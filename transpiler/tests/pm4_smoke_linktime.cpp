// pm4_smoke_linktime.cpp — PM4-10 smoke test for the link-time
// `STURM_PM4_LINK_DEMO=ON` registration path.
//
// Relationship to smoke 1 (`pm4_smoke_dlopen`)
// --------------------------------------------
// Per the PM4 implementation plan §Verification:
//
//   - Smoke 1 (PM4-8): user code compiled with
//     `add_quantum_executable(... PLUGINS $<TARGET_FILE:sturm-pm4-demo-plugin>)`
//     shows `pm4_demo_tag_inverse(` in the rewritten buffer. Proves the
//     dlopen → `register_op` → `render_uncompute` pipeline end-to-end.
//
//   - Smoke 2 (PM4-10, **this file**): same rewritten-output assertion as
//     smoke 1 but **without** a `PLUGINS` argument — the demo plugin's
//     registrar is statically linked into `sturm-transpile-plugin` via the
//     `STURM_REGISTER_PLUGIN` macro when the build is reconfigured with
//     `-DSTURM_PM4_LINK_DEMO=ON`. Proves the Meyer's-singleton link-time
//     path.
//
// Why this test runs at the unit level rather than end-to-end
// -----------------------------------------------------------
// The end-to-end rewrite assertion (`pm4_demo_tag_inverse(` appearing in
// the generated sibling) depends on PM4-3 (`QOpKind::PLUGIN` variant +
// `QOperation::plugin_kind_id` field + `render_uncompute` dispatch case +
// `Registry&` threaded through `synthesize()`), PM4-4 (`plugin.cpp`
// `ParseArgs` `load=<path>` + drain of `registrars()` inside
// `TranspileConsumer`'s ctor), and PM4-5 (`PLUGINS` argument in
// `add_quantum_executable`). None of those land yet. Attempting an end-
// to-end assertion today would require stubbing three separate downstream
// pipelines and would mis-calibrate the reviewer against what PM4-10 is
// actually supposed to prove.
//
// What PM4-10 IS supposed to prove
// --------------------------------
// Per the issue description and the plan §5, PM4-10's deliverable is the
// **link-time path** — the narrow contract that:
//
//   (a) A plugin author writes `STURM_REGISTER_PLUGIN(MyPlugin)` at
//       namespace scope in a TU that is statically linked into
//       `sturm-transpile-plugin`.
//   (b) The macro's `StaticRegistrar` ctor runs during the
//       `sturm-transpile-plugin` library's static-init and pushes a
//       `LinkTimeRegisterFn` onto the Meyer's-singleton vector returned
//       by `registrars()`.
//   (c) The consumer that later drains that vector (PM4-3's consumer
//       ctor) observes the registrar and invokes it against its
//       `Registry&`, populating the Registry with the plugin's matcher +
//       renderer pair.
//
// This test pins (a), (b), and (c) end-to-end **at the plugin/registry
// boundary**: the same boundary the consumer drain in PM4-3 will rely
// on, byte-for-byte. The test binary links:
//
//   - `plugin_demo.cpp` — the PM4-7 demo plugin TU, providing
//     `sturm_register_plugin_v1` + the `render_tag_inverse` renderer.
//   - `pm4_link_demo_registrar.cpp` — the PM4-10 shim TU, which
//     invokes the demo's registration via `STURM_REGISTER_PLUGIN`. This
//     is the exact TU that, when `STURM_PM4_LINK_DEMO=ON`, is compiled
//     into `sturm-transpile-plugin`. Linking it here mirrors the
//     production posture without requiring a full end-to-end compile.
//
// The test then "replays" the consumer-ctor drain (iterate `registrars()`,
// call each entry against a fresh `Registry`) and asserts the Registry
// now resolves `pm4.demo.tag` and its renderer produces the expected
// `pm4_demo_tag_inverse(<q>);\n` text — the exact rewritten-output
// content smoke 1 would look for in the generated buffer.
//
// When smoke 1 (`pm4_smoke_dlopen`) lands (PM4-8), and the end-to-end
// pipeline is available (PM4-3 / PM4-4 / PM4-5), this file should be
// revisited so the top-level assertion mirrors smoke 1's rewritten-
// output check byte-for-byte. The unit-level shape here is a bridge,
// not the permanent PM4-10 contract.

#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

using ::sturm::transpile::QOperation;
using ::sturm::transpile::QValueRef;
using ::sturm::transpile::plugin::Registry;
using ::sturm::transpile::plugin::UncomputeRenderFn;
using ::sturm::transpile::plugin::LinkTimeRegisterFn;
using ::sturm::transpile::plugin::registrars;

// The demo plugin's kind_id constant is defined in a plugin-private
// namespace inside `plugin_demo.cpp` (see `kTagKindId` at line 81). We
// pin the spelling here so that any drift of the constant on the plugin
// side is caught as a test failure rather than a silent lookup miss.
// This mirrors the same approach in `test_plugin_demo.cpp:88` where the
// literal is retyped by hand instead of exposed via a public header.
namespace {
constexpr const char* kDemoKindId = "pm4.demo.tag";
}

// ── Test harness ──────────────────────────────────────────────────────────────
// Lightweight pass-count macro mirroring the shape used by every other
// PM4 unit test in this directory.
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

// ── Test: the Meyer-singleton vector is non-empty at process start ──────────
// When `STURM_PM4_LINK_DEMO=ON`, the shim TU's `STURM_REGISTER_PLUGIN(...)`
// expansion emits a namespace-scope `StaticRegistrar` whose ctor runs
// during the test binary's static-init. That ctor pushes one
// `LinkTimeRegisterFn` onto `registrars()`. By the time `main` runs, the
// vector has observable size >= 1.
//
// Pinning "at least 1" rather than "exactly 1" keeps the test robust if
// other TUs link more registrars in a future build. The specific
// contract PM4-10 cares about is "did OUR registrar land here?", which
// is covered by the replay test below — this check just pins the size
// contract the plan §5 spells out.
static void test_registrars_has_at_least_one_entry() {
    CHECK(!registrars().empty());
}

// ── Test: replaying the drain registers the demo's kind_id ──────────────────
// Mirrors the drain loop that lives inside PM4-3's
// `TranspileConsumer::TranspileConsumer`: iterate `registrars()` and
// invoke each entry against a stack-allocated Registry. Post-drain, the
// Registry must resolve `pm4.demo.tag` to a non-null renderer.
//
// This is the **primary** PM4-10 assertion. It proves:
//   - The shim TU's `StaticRegistrar` ctor ran (vector populated).
//   - The registered `LinkTimeRegisterFn` forwards into the demo's
//     `register_op("pm4.demo.tag", ...)` call (renderer reachable).
//   - The Registry's `find_render_fn` lookup returns the exact same
//     `UncomputeRenderFn` the dlopen path (PM4-8 smoke 1) would reach.
static void test_drain_registrars_populates_demo_renderer() {
    Registry r;
    for (const auto& fn : registrars()) {
        fn(r);
    }
    const UncomputeRenderFn* render = r.find_render_fn(kDemoKindId);
    CHECK(render != nullptr);
}

// ── Test: the drained renderer emits the expected uncompute text ────────────
// The "same rewritten-output assertion" smoke 1 would run over the
// generated sibling: the plugin-supplied inverse for a tag op on qbool
// `q` must be the exact string `"    pm4_demo_tag_inverse(q);\n"`. PM4-3
// will plumb this through the `case QOpKind::PLUGIN:` arm in
// `render_uncompute`; the byte sequence is identical.
//
// Pinning the byte sequence here gives the reviewer the same grounding
// smoke 1 will give: a change to the demo renderer's output is caught
// at the PM4-10 gate, not only after PM4-3 + PM4-4 + PM4-5 land.
static void test_drained_renderer_emits_expected_uncompute_text() {
    Registry r;
    for (const auto& fn : registrars()) {
        fn(r);
    }
    const UncomputeRenderFn* render = r.find_render_fn(kDemoKindId);
    if (render == nullptr) {
        CHECK(false);
        return;
    }
    QOperation op{};
    op.result = QValueRef{"q", {}};
    const std::string got      = (*render)(op);
    const std::string expected = "    pm4_demo_tag_inverse(q);\n";
    if (got != expected) {
        std::fprintf(stderr,
                     "FAIL  link-time renderer output mismatch\n"
                     "  expected: %s"
                     "  got:      %s",
                     expected.c_str(), got.c_str());
    }
    CHECK(got == expected);
}

// ── Test: `registrars()` is idempotent across repeat calls ──────────────────
// Meyer's-singleton identity: every call returns a reference to the same
// underlying vector. PM4-3's consumer ctor will call `registrars()` once
// per consumer construction (and PM1-4's nested-invocation path
// constructs a second consumer in the same process); both calls must see
// the same populated vector. This pins the contract across the PM1-4
// boundary without spinning up a second CompilerInvocation.
static void test_registrars_is_meyer_singleton_identity() {
    auto& a = registrars();
    auto& b = registrars();
    CHECK(&a == &b);
    CHECK(a.size() == b.size());
}

// ── Test: drain is re-runnable without double-registration ──────────────────
// The shim's `LinkTimeRegisterFn` forwards into the demo's `register_op`
// which will hard-abort on a duplicate `kind_id`. Draining the Meyer
// vector INTO THE SAME Registry twice would hit that abort path. We
// instead verify the documented recovery shape: each drain targets a
// FRESH Registry, and a second drain into a fresh Registry produces the
// same observable state as the first. This mirrors the PM1-4 nested-
// invocation posture where the outer consumer has one Registry and the
// nested consumer constructs its own.
static void test_drain_into_fresh_registry_is_idempotent() {
    Registry r1;
    for (const auto& fn : registrars()) {
        fn(r1);
    }
    const UncomputeRenderFn* a = r1.find_render_fn(kDemoKindId);

    Registry r2;
    for (const auto& fn : registrars()) {
        fn(r2);
    }
    const UncomputeRenderFn* b = r2.find_render_fn(kDemoKindId);

    CHECK(a != nullptr);
    CHECK(b != nullptr);
    // The render_fn callable targets live in the per-Registry
    // unordered_map, so the two pointers are distinct. The OBSERVABLE
    // behavior — the emitted uncompute text for a sample op — must
    // match byte-for-byte.
    if (a != nullptr && b != nullptr) {
        QOperation op{};
        op.result = QValueRef{"q", {}};
        CHECK((*a)(op) == (*b)(op));
    }
}

int main() {
    test_registrars_has_at_least_one_entry();
    test_drain_registrars_populates_demo_renderer();
    test_drained_renderer_emits_expected_uncompute_text();
    test_registrars_is_meyer_singleton_identity();
    test_drain_into_fresh_registry_is_idempotent();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
