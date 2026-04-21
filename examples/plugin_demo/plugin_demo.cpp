// plugin_demo.cpp — PM4-7 demo plugin exercising the PM4-1 / PM4-2
// extension surface (`Registry::register_op`) end-to-end without touching
// any production matcher.
//
// Scope
// -----
// The plugin registers a single **novel self-inverse "tag" op** keyed by
// the string `kind_id = "pm4.demo.tag"` through the runtime-load entry
// point `sturm_register_plugin_v1`. On registration the plugin wires:
//
//   1. A no-op AST-matcher callback (empty body). The demo deliberately
//      skips the full AST rewrite path — PM4-3 (`QOpKind::PLUGIN` +
//      `QOperation::plugin_kind_id`) is not merged yet, so there is no
//      clean way to seed a `QOpKind::PLUGIN` op from the plugin's matcher
//      side. The no-op matcher satisfies `register_op`'s positional-
//      arguments contract (the signature requires a matcher fn) while
//      keeping the demo focused on the **renderer** half of the extension
//      surface (which is what this issue exercises).
//
//   2. A render function that emits the tag op's self-adjoint inverse
//      as a single line
//          `    pm4_demo_tag_inverse(<q>);\n`
//      where `<q>` is the operand's source-level name (`QOperation::
//      result.name`). The four-space indent + trailing newline follow
//      the in-tree rendering convention documented at
//      `transpiler/src/uncompute_pass.cpp:30-42`. Rendering is robust to
//      malformed `QOperation` seeds (empty `result.name` → empty return,
//      mirroring the defensive guards in the in-tree renderers).
//
// Why a novel kind (not a dogfood migration of an existing op)
// ------------------------------------------------------------
// The dogfood migration (PB-1..PB-4) lives in PM4-6 (`sturm-4oyr.7`).
// PM4-7 is the "does the plugin API work for brand-new ops" counterpart:
// `pm4.demo.tag` intentionally has no in-tree analog so that a smoke test
// (PM4-8 / PM4-10) looking for `pm4_demo_tag_inverse(` in the rewritten
// buffer can attribute that string uniquely to the plugin path.
//
// Why self-inverse
// ----------------
// A self-inverse op is the simplest non-trivial render case: the uncompute
// text for `tag(q)` is the same call shape as the forward op, just with a
// disambiguating `_inverse` suffix so the runtime can tell which side of
// the scope it is being invoked from. Keeping the demo self-inverse lets
// the smoke test assert on one emitted line rather than a multi-line
// render body.
//
// Runtime load path
// -----------------
// The host transpiler is expected to `dlopen(path, RTLD_LOCAL|RTLD_NOW)`
// this `.so`, `dlsym("sturm_register_plugin_v1")`, and invoke the symbol
// against its per-consumer `Registry&`. That wiring belongs to PM4-4
// (`sturm-4oyr.5`) and is not merged yet; the PM4-7 deliverable is just
// the plugin body itself. The PM4-7 unit test (`test_plugin_demo`) links
// this TU directly against a test harness and calls
// `sturm_register_plugin_v1` by hand, bypassing the dlopen hop — the
// Registry API contract is what this test pins.
//
// Non-goals
// ---------
//   - NOT a dogfood migration. See PM4-6.
//   - NOT wired into any production `add_quantum_executable` target.
//   - NOT a link-time `STURM_REGISTER_PLUGIN` registrar; runtime-load is
//     the primary path the demo exercises. The link-time baking path
//     gets its own example (PM4-10 / `STURM_PM4_LINK_DEMO=ON`).
//
// LOC budget: ≤ 300 lines (CLAUDE.md). This TU contains only the `_v1`
// entry point body plus two short lambdas, well under budget.

#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"

// PM4-4 Clang-ABI gate: pull in `CLANG_VERSION_STRING` so
// `STURM_PLUGIN_DEFINE_CLANG_VERSION()` below expands to a valid C string.
// `<clang/Basic/Version.inc>` is the canonical source of the macro; it
// lives under the Clang install tree and is already on the include path
// for this TU via `examples/plugin_demo/CMakeLists.txt` (which adds
// `${CLANG_INCLUDE_DIRS}` and `${LLVM_INCLUDE_DIRS}`).
#include "clang/Basic/Version.inc"

#include <sstream>
#include <string>

namespace sturm::transpile::plugin_demo {

// Kind identifier used across `register_op` and every downstream lookup.
// Defined as a named constant at namespace scope so the PM4-7 unit test
// can reference the same string without risking a typo drift between the
// registration side and the test's `find_render_fn` lookup.
inline constexpr const char* kTagKindId = "pm4.demo.tag";

// Render the self-inverse for a single "tag" op. Called by the host's
// `render_uncompute` dispatch once PM4-3 lands (`case QOpKind::PLUGIN`
// consults `Registry::find_render_fn(op.plugin_kind_id)` and invokes the
// returned `UncomputeRenderFn`). Until PM4-3 merges, the call site is the
// PM4-7 unit test, which hand-builds a `QOperation` and checks the
// returned string byte-for-byte.
//
// Output contract:
//   - Four-space leading indent (matches the in-tree renderers at
//     `uncompute_pass.cpp:51`).
//   - Single source line `pm4_demo_tag_inverse(<name>);` followed by
//     `\n`.
//   - Defensive empty-string return on malformed op (no result name).
std::string render_tag_inverse(const QOperation& op) {
    // Defensive: every in-tree renderer bails out on a shape mismatch
    // (operand count, missing result name) by returning an empty string
    // rather than emitting half-formed C++. We mirror that posture so
    // malformed plugin-seeded ops never introduce a syntax error into the
    // rewritten buffer. The tag op's only invariant is a non-empty
    // `result.name` — the inverse call substitutes that name into its
    // single argument slot.
    if (op.result.name.empty()) {
        return {};
    }
    std::ostringstream os;
    os << "    pm4_demo_tag_inverse(" << op.result.name << ");\n";
    return os.str();
}

} // namespace sturm::transpile::plugin_demo

// ── Runtime-load entry point ────────────────────────────────────────────────
//
// `extern "C"` keeps the symbol mangling-stable across the `.so` boundary
// so the host's `dlsym(handle, "sturm_register_plugin_v1")` resolves the
// exact name declared in `plugin_api.hpp`. The host passes its per-
// consumer `Registry&`; we forward into `register_op` with:
//
//   - kind_id           : `"pm4.demo.tag"`
//   - matcher callback  : a no-op lambda (see file-level comment)
//   - renderer callback : `render_tag_inverse`
//
// No error handling: `register_op` aborts the host on a duplicate-kind_id
// collision, which is a build-by-build mismatch (the host should have
// refused to dlopen the second `.so` claiming the same key) rather than
// a runtime concern.
extern "C" void sturm_register_plugin_v1(
    ::sturm::transpile::plugin::Registry& registry) {
    using ::sturm::transpile::QUnit;
    using ::sturm::transpile::plugin_demo::kTagKindId;
    using ::sturm::transpile::plugin_demo::render_tag_inverse;

    registry.register_op(
        kTagKindId,
        // Matcher half of the registration. Intentionally empty: the
        // demo skips the AST-rewrite leg of the pipeline (see file-level
        // comment). The Registry's `invoke_all` drain will fire this
        // lambda during `TranspileConsumer`'s setup, and the lambda
        // adds zero matchers to the MatchFinder — a no-op from the
        // downstream parser's point of view.
        [](clang::ast_matchers::MatchFinder&, QUnit&) {
            // intentional: PM4-7 exercises the renderer, not the matcher.
        },
        // Renderer half: forwards to the free function above so the
        // renderer implementation is unit-testable without spinning up
        // a Registry. Captures nothing — `render_tag_inverse` is
        // stateless and the `std::function` erasure stores a pointer to
        // the function, not a closure.
        &::sturm::transpile::plugin_demo::render_tag_inverse);
}

// ── PM4-4 Clang-ABI gate ────────────────────────────────────────────────────
// The host `dlsym`s this symbol before calling `sturm_register_plugin_v1`
// and refuses to load the plugin if the returned string differs from its
// own `CLANG_VERSION_STRING`. See `plugin_api.hpp` for the full contract.
// The macro expands to an `extern "C"` function returning the Clang
// version the plugin was compiled against; the host's pessimistic full-
// string compare (e.g. "17.0.6" vs "17.0.7" is a refuse) catches every
// distro-patched mismatch at load time.
STURM_PLUGIN_DEFINE_CLANG_VERSION()
