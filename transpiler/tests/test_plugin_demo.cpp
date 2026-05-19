// test_plugin_demo.cpp — PM4-7 focused unit test for the demo plugin under
// `examples/plugin_demo/plugin_demo.cpp`.
//
// Contract under test
// -------------------
// The PM4-7 demo plugin registers a single self-inverse "tag" op keyed
// by the string `kind_id = "pm4.demo.tag"` through the runtime-load
// entry point `sturm_register_plugin_v1`. This test:
//
//   1. Hands a stack-allocated `Registry` to the plugin's entry point.
//   2. Verifies `Registry::find_render_fn("pm4.demo.tag")` returns a
//      non-null pointer to the stored `UncomputeRenderFn`.
//   3. Hand-builds a sample `QOperation` with `result.name = "q"` and
//      invokes the render_fn, asserting the output matches the exact
//      expected text `"    pm4_demo_tag_inverse(q);\n"` (four-space
//      indent + trailing newline, matching the in-tree renderer
//      convention at `uncompute_pass.cpp:30-42`).
//   4. Pins the defensive guard: a `QOperation` with an empty
//      `result.name` must render as an empty string (malformed-op
//      posture; every in-tree renderer follows this rule).
//   5. Pins the kind_id string through a `find_render_fn` negative
//      probe (a lookup for a close-but-wrong key must return nullptr).
//
// Why not dlopen
// --------------
// PM4-4 (the `load=<path>` ParseArgs + dlopen hop in `plugin.cpp`) is
// not merged yet. We therefore compile the plugin's TU directly into
// this test binary alongside `plugin_registry.cpp` + `qir.cpp` and
// call `sturm_register_plugin_v1` by its C-linkage name — the same
// symbol the dlopen path will reach via `dlsym`. This exercises the
// Registry API contract end-to-end (which is the only gate PM4-7 has
// to clear, per the issue description), without depending on any
// runtime-load plumbing the host transpiler is still missing.
//
// What is deliberately NOT tested here
// ------------------------------------
// The PM4-7 issue notes: "If the `QOperation::plugin_kind_id` field
// doesn't exist yet (that comes from PM4-3 sturm-4oyr.4, not merged),
// just verify the Registry API contract: register_op stores the
// render_fn under the given kind_id and find_render_fn returns it."
// `QOperation::plugin_kind_id` is absent from the current `qir.hpp`
// (see line 225-253), so this test confirms the *Registry-side*
// contract only — the end-to-end `render_uncompute` dispatch over a
// `QOpKind::PLUGIN` op is PM4-3's territory.

#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using ::sturm::transpile::QOperation;
using ::sturm::transpile::QValueRef;
using ::sturm::transpile::plugin::Registry;
using ::sturm::transpile::plugin::UncomputeRenderFn;

// The demo plugin exposes `sturm_register_plugin_v1` with C linkage.
// Declared here locally so the test TU does not need to include the
// plugin's private headers; the symbol is resolved at link time from
// the plugin's `.cpp` compiled into this test binary.
extern "C" void sturm_register_plugin_v1(Registry& registry);

// ── Test harness ──────────────────────────────────────────────────────────────
// Lightweight pass-count macro mirroring the shape used by
// test_plugin_registry.cpp. One printed summary at main() exit.
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

// ── Test: register_plugin_v1 populates the Registry ─────────────────────────
// The plugin's entry point calls `register_op("pm4.demo.tag", ...)`; after
// it returns, `find_render_fn` must report a non-null renderer under that
// kind_id. This is the **primary** gate PM4-7 has to clear: the Registry
// API contract survives the `extern "C"` hop.
static void test_register_plugin_v1_registers_render_fn() {
    Registry r;
    sturm_register_plugin_v1(r);

    const UncomputeRenderFn* fn = r.find_render_fn("pm4.demo.tag");
    CHECK(fn != nullptr);
}

// ── Test: render_fn emits `pm4_demo_tag_inverse(<name>);` for a sample op ──
// Hand-build a `QOperation` with `result.name = "q"` — the simplest
// non-malformed input the renderer will see. The expected output is a
// single line with four-space leading indent, the `pm4_demo_tag_inverse(`
// identifier, the substituted operand name, `);`, and a trailing newline.
// Any drift from this exact byte sequence is a plugin-contract regression
// and will be caught by the PM4-8 / PM4-10 smoke tests downstream.
static void test_render_fn_output_matches_expected() {
    Registry r;
    sturm_register_plugin_v1(r);

    const UncomputeRenderFn* fn = r.find_render_fn("pm4.demo.tag");
    if (fn == nullptr) {
        // If registration did not populate the render fn, skip the
        // byte-compare to avoid a null-deref. The register-side test
        // already flagged the regression.
        CHECK(false);
        return;
    }

    QOperation op{};
    op.result = QValueRef{"q", {}};
    const std::string rendered = (*fn)(op);

    const std::string expected = "    pm4_demo_tag_inverse(q);\n";
    if (rendered != expected) {
        std::fprintf(stderr,
                     "FAIL  render mismatch\n"
                     "  expected: %s"
                     "  got:      %s",
                     expected.c_str(), rendered.c_str());
    }
    CHECK(rendered == expected);
}

// ── Test: render_fn is defensive against malformed QOperation (empty name) ──
// Every in-tree renderer returns an empty string when the input op is
// shape-malformed (operand count mismatch, missing result name). The demo
// plugin's renderer follows that posture: `result.name.empty()` ⇒ the
// render function emits nothing rather than a half-formed
// `pm4_demo_tag_inverse();\n` line. Pinning this posture here keeps the
// demo a useful template for third-party plugin authors.
static void test_render_fn_empty_on_empty_result_name() {
    Registry r;
    sturm_register_plugin_v1(r);

    const UncomputeRenderFn* fn = r.find_render_fn("pm4.demo.tag");
    if (fn == nullptr) {
        CHECK(false);
        return;
    }

    QOperation op{};  // default-constructed: result.name is empty.
    const std::string rendered = (*fn)(op);
    CHECK(rendered.empty());
}

// ── Test: find_render_fn returns nullptr for a wrong kind_id ────────────────
// A close-but-wrong key (case, prefix, suffix) must NOT resolve to the
// demo plugin's renderer. This pins the exact kind_id string
// ("pm4.demo.tag") and guards against silent drift where a future refactor
// normalizes kind_ids or case-folds them.
static void test_find_render_fn_rejects_wrong_kind_id() {
    Registry r;
    sturm_register_plugin_v1(r);

    CHECK(r.find_render_fn("pm4.demo.Tag") == nullptr);     // case drift
    CHECK(r.find_render_fn("pm4-demo-tag") == nullptr);     // dash vs dot
    CHECK(r.find_render_fn("demo.tag") == nullptr);         // prefix drop
    CHECK(r.find_render_fn("pm4.demo.tag.x") == nullptr);   // suffix add
    CHECK(r.find_render_fn("") == nullptr);                 // empty key
}

// ── Test: render_fn handles non-trivial operand names verbatim ──────────────
// Confirms the renderer does not massage, escape, or re-case the
// `result.name` — it substitutes the string byte-for-byte into the call
// site. This is the contract every in-tree renderer follows (the matcher
// side is responsible for normalizing names via Lexer::getSourceText).
// A plugin that silently munges names would cause "invisible" rewrite
// drift: a fixture with an identifier `__stu_t0` (produced by the Phase E
// FreshNameAllocator) must render as `pm4_demo_tag_inverse(__stu_t0);`
// with underscore / digit preservation intact.
static void test_render_fn_preserves_nontrivial_operand_name() {
    Registry r;
    sturm_register_plugin_v1(r);

    const UncomputeRenderFn* fn = r.find_render_fn("pm4.demo.tag");
    if (fn == nullptr) {
        CHECK(false);
        return;
    }

    QOperation op{};
    op.result = QValueRef{"__stu_t0", {}};
    const std::string rendered = (*fn)(op);
    const std::string expected =
        "    pm4_demo_tag_inverse(__stu_t0);\n";
    CHECK(rendered == expected);
}

int run_test_plugin_demo(int /*argc*/, char** /*argv*/) {
    test_register_plugin_v1_registers_render_fn();
    test_render_fn_output_matches_expected();
    test_render_fn_empty_on_empty_result_name();
    test_find_render_fn_rejects_wrong_kind_id();
    test_render_fn_preserves_nontrivial_operand_name();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
