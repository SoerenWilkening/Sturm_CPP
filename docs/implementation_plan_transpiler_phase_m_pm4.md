# Implementation Plan: Transpiler Phase M — PM4 Pluginization

**Status:** Draft (implementation plan skeleton, PM4-0)
**Date:** 2026-04-21
**Relates to:**
- `docs/roadmap_transpiler_post_mvp.md` §Phase M, item **"Transpiler pluginization"** (line 875)
- `docs/01_principles.md` (B1b, B6, B9, B10, P9 — the PM4 surface must not violate any of them)
- bd epic `sturm-4oyr` (this file is the PM4-0 deliverable, tracked as `sturm-4oyr.1`)
- `/home/agent/.claude/plans/okay-lets-start-with-swift-scott.md` (operational breakdown that produced `sturm-4oyr.1..11`)

## Context

Phase M of the post-MVP roadmap lists five stretch items. Three have landed: **PM1** in-memory transpile (v0.1.2, 2026-04-19), **PM2** `#line`-based source maps (2026-04-20), **PM3** quantum-specific diagnostics (epic `sturm-5btt`, 2026-04-21). Two remain: **transpiler pluginization** (this plan, PM4) and **peephole gate reordering** (deferred — its prerequisite alias analysis does not exist yet).

The roadmap clause at `docs/roadmap_transpiler_post_mvp.md:875` gates pluginization on the core being stable. PM3 was the last item locking down the diagnostics surface, so "core stable" is now true. PM4 is the correct next pick; peephole reordering stays parked.

**Goal.** Let third-party shared libraries register additional AST rewrite matchers + uncompute rules alongside the in-tree ones, without patching the transpiler source. Motivation is internal dogfooding first: migrate an existing in-tree matcher family (`matcher_qint_const.cpp`, Phase B PB-1..PB-4) through the new API to prove the extension points work before publishing them to ecosystem users.

---

## 1. Scope

PM4 ships **matchers + uncompute rules only** — two extension points, nothing more.

**In scope:**
- AST matcher registration: a plugin may call `finder.addMatcher(...)` via a `Registry` facade and push rewrite results into the same `QUnit` the in-tree matchers see.
- Uncompute rules: a plugin may register a **render function** keyed by a string `kind_id`; the transpiler's `render_uncompute` switch gains a single new `case QOpKind::PLUGIN:` that looks up and invokes the registered renderer. This preserves the "no `default:`" exhaustiveness contract at `transpiler/include/sturm/transpile/qir.hpp:73-75` that keeps missing cases as build failures.

**Explicitly out of scope for PM4:**
- **Diagnostics** — third-party plugins cannot emit through `DiagnosticsEngine`. PM3 owns the diagnostic surface; opening it to plugins would require version-stable category IDs and engine-facade ABI design that is not justified by current demand.
- **IR passes** — no `QUnit`-level transforms such as the PJ-1 fuse / PJ-3 hoist / PJ-4 dead-ancilla passes. Plugins operate at AST-match time and at render time; they do not participate in `synthesize()`'s iteration over `QScope`s.
- **Runtime** — no hooks into the `WhenGuard`, qubit pool, or classical specialization fast path. PM4 is a transpile-time-only surface.

Out-of-scope items may land later as separate phases (new entry-point names — `sturm_register_plugin_v2` / `..._v3` — to preserve the unstable-ABI discipline of §2).

---

## 2. ABI versioning via `sturm_register_plugin_v1`

The public ABI across `.so` boundaries is:

```cpp
// in transpiler/include/sturm/transpile/plugin_api.hpp
namespace sturm::transpile::plugin {
class Registry;  // opaque reference, never copied across the boundary
}

extern "C" void sturm_register_plugin_v1(sturm::transpile::plugin::Registry&);
```

**Versioning discipline.**
- The `_v1` suffix is part of the symbol name, not a struct field. Future breaking changes rename the entry point to `_v2` / `_v3`; `dlsym` returns null on mismatch, so the host prints a clear "plugin does not export `sturm_register_plugin_v1`; did you build it against an older sturm?" error.
- The API is deliberately **unstable** across sturm minor versions. Breaking changes within a `_v1` family are allowed; the symbol name bumps only when the *shape* of `Registry`'s methods changes enough that a recompile-without-source-change plugin would silently misbehave.
- No decorated ABI tag (no `[[gnu::abi_tag]]`, no Itanium version-script). The raw `extern "C"` symbol and one plain reference argument are the entire contract. This matches the minimal posture described in §3 (fail fast, never dlclose).

**What "unstable" means in practice.**
- PM4 users must rebuild their plugin `.so` against the sturm version they compile against. Prebuilt plugin binaries are not supported, not promised, and not version-compatible across the project's minor releases. The README text landed by PM4-11 must state this plainly.

---

## 3. Runtime load — `dlopen(RTLD_LOCAL | RTLD_NOW)`

Runtime load is the **primary** path (per `sturm-4oyr` epic description). CMake drives it via `-Xclang -plugin-arg-sturm-transpile -Xclang load=<abs.so>` (see §8), which reaches `plugin.cpp`'s `ParseArgs` as `load=<path>`; `ParseArgs` then opens the `.so` inside the host transpiler process.

**Flags.**
- `RTLD_NOW` — resolve all symbols at load time. A plugin with an unresolved reference fails *immediately* with a clean error, not at some later inscrutable point during a matcher callback.
- `RTLD_LOCAL` — do not expose the plugin's symbols to subsequently loaded plugins. Prevents accidental symbol shadowing across plugins; keeps each plugin's internal helpers private to itself.

**Combined posture:** `dlopen(path, RTLD_LOCAL | RTLD_NOW)`. On failure, log a single stderr line `sturm-transpile plugin: failed to dlopen <path>: <dlerror()>` and return `false` from `ParseArgs` — Clang then aborts plugin setup, which the host build surfaces as a compile error. Smoke test PM4-9 (`pm4_missing_plugin_errors`) pins this exact diagnostic string.

**Entry-point lookup.** After a successful `dlopen`, call `dlsym(handle, "sturm_register_plugin_v1")`. Null result → fail with a distinct "missing sturm_register_plugin_v1 entry point; wrong ABI version or not a sturm plugin?" error. On a valid pointer, cast to `void (*)(Registry&)` and invoke with the host's Registry reference.

---

## 4. Clang version check on load

Every plugin compiled against a Clang version other than the host transpiler's is a ticking bug. `MatchFinder`, `SourceManager`, `ASTContext`, and the Rewriter APIs are **not** ABI-stable across Clang majors. A plugin built against Clang 17 that dlopens into a Clang-18 host is a latent crash or silent miscompile.

**Policy.** On entry to `sturm_register_plugin_v1`, the plugin returns (or the host reads from the plugin) the `CLANG_VERSION_STRING` it was compiled against. The host compares against its own `CLANG_VERSION_STRING`. Mismatch = refuse to register; emit

```
sturm-transpile plugin: <path> built against Clang <plugin-ver>, host is Clang <host-ver>; refusing to load.
```

and return `false` from `ParseArgs`.

**Mechanism.** The Registry exposes a `host_clang_version()` accessor; the plugin calls it inside its entry point and bails out (no matcher registration, no render-fn registration) if mismatched. The `StaticRegistrar` path (§5) does the same check inside its lambda.

The check is pessimistic on purpose: we compare full version strings (e.g. `17.0.6`), not just majors. A point-release mismatch is still rejected — sturm is small enough that this costs nothing, and catches the case where a distro ships a patched Clang the plugin author did not test against.

---

## 5. Meyer's singleton for link-time registrars

Link-time plugin registration is the **fallback** path. It exists for custom transpiler builds that bake plugin code directly into the `sturm-transpile` / `sturm-transpile-plugin` binary (e.g. via a CMake option such as `STURM_PM4_LINK_DEMO=ON` — smoke test PM4-10).

**Mechanism.**
```cpp
// in transpiler/include/sturm/transpile/plugin_api.hpp
namespace sturm::transpile::plugin {

// Meyer's singleton — first call to registrars() constructs the vector
// in function-local storage. No TU-to-TU static init order dependency.
std::vector<std::function<void(Registry&)>>& registrars();

struct StaticRegistrar {
    StaticRegistrar(std::function<void(Registry&)> fn) {
        registrars().push_back(std::move(fn));
    }
};

} // namespace

#define STURM_REGISTER_PLUGIN(TypeName) \
  static ::sturm::transpile::plugin::StaticRegistrar \
    _sturm_reg_##TypeName{[](::sturm::transpile::plugin::Registry& r) { \
        TypeName{}.register_all(r); \
    }};
```

`TranspileConsumer`'s constructor drains `registrars()` (per §6 ordering) by invoking each entry against the host Registry. Meyer's singleton (function-local `static` vector lazily constructed on first call) avoids the TU-to-TU static-init-order fiasco: `StaticRegistrar`'s constructor runs before `main` in whichever order the linker picks, but the Meyer vector is constructed lazily on the first `registrars()` call, so no registrar ever observes an uninitialized vector.

**Why not a global variable?** Because the PM1-4 nested `CompilerInvocation` at `transpiler/src/plugin.cpp:242-439` constructs a second Clang front-end inside the same process. A namespace-scope global would compose badly with that (double-registration risk); function-local static composes correctly — a second `TranspileConsumer` sees the same populated vector and *still* processes it once per consumer lifetime (PM4-2's Registry implementation takes care of idempotency by guarding re-registration).

---

## 6. Ordering: in-tree → runtime dlopen → link-time

Three sources of matcher + render-fn registrations:

1. **In-tree** — the registrations hard-coded in `transpile_consumer.cpp` (lines 32-317). These run first, in the order the constructor currently spells out. That order is load-bearing (see the PJ-1d / PH-3 edge documented at lines 160-173); PM4 does not change it.
2. **Runtime dlopen** — plugins loaded via `load=<path>` `ParseArgs` tokens, in the order they appear on the command line. Each `.so`'s `sturm_register_plugin_v1` body is invoked against the Registry in load order.
3. **Link-time** — Meyer's singleton drain. After all `dlopen` plugins have been processed, `TranspileConsumer::TranspileConsumer` iterates `registrars()` and invokes each lambda in vector order.

**Why this order.** In-tree first ensures existing snapshot fixtures are byte-identical (in-tree matchers run the same callbacks in the same order as before PM4). Runtime plugins second so they can observe the "in-tree is already registered" state. Link-time last so that link-time registrars — typically used for custom-build-specific ops — run with full knowledge of the matcher pool below them.

**Why no inter-group ordering overrides.** Plugins do NOT declare priorities or dependencies in v1. If a user ships two plugins whose matchers interact, the recommended fix is to ship one plugin that registers both. Priority APIs are a v2 concern; punting them keeps v1 ABI trivially small.

**Invariant maintained.** No in-tree ordering edge at `transpile_consumer.cpp:59-317` references anything that would run in the plugin tail, so plugin matchers running after all in-tree ones is safe. This is documented in §4 of the `okay-lets-start-with-swift-scott.md` design decisions and verified by smoke test PM4-9 (`pm4_dogfood_snapshot` — the Phase B snapshots are byte-identical after PM4-6's dogfood migration).

---

## 7. Never `dlclose` in v1

The `dlopen` handle returned for each plugin is **leaked** until process exit. The host transpiler does not track handles beyond "remember the pointer so we can diagnose double-load", and never calls `dlclose` on them.

**Rationale.**
- **Callback lifetime.** A plugin's registered matcher callbacks and render functions are function pointers / `std::function`s whose targets live inside the plugin's code segment. `dlclose` unmaps that segment. Any subsequent invocation (from a matcher callback fired during AST traversal, from a `render_uncompute` case, from a PM1-4 nested invocation) segfaults.
- **Static destructor ordering.** If a plugin's static objects have destructors that reference the Registry (or indirectly reach any sturm symbol), `dlclose` triggers destruction in unknown order relative to the host's own statics. The singleton-avoidance posture in §5 does not extend across dlclose boundaries.
- **Host process is short-lived.** The transpiler runs as `clang++` per translation unit; the process exits seconds later. OS reclamation is a clean, sufficient teardown path. "Leak until exit" is a feature, not a bug.

**What this means for plugin authors.** Plugins may assume their static state lives for the lifetime of the process and is not re-initialized. They must NOT rely on `__attribute__((destructor))` running before host teardown begins — the host may be in mid-TU-emission when the process exits. This is a lower bar than typical shared-library discipline, and PM4's README language (PM4-11) must pin it explicitly.

**Forward compatibility.** A future `_v2` ABI may introduce explicit teardown hooks. PM4 does not file such hooks because the design cost is non-trivial (callback-revocation protocol, `std::function` erasure across the ABI, matcher de-registration from `MatchFinder`), and no current consumer needs them.

---

## Sub-tasks (mirrors bd children under `sturm-4oyr`)

| bd ID | Title | Depends | Parallelizable with |
|---|---|---|---|
| PM4-0 (`sturm-4oyr.1`) | **This plan doc** | — | — |
| PM4-1 (`sturm-4oyr.2`) | Public `plugin_api.hpp` header (Registry / StaticRegistrar / `sturm_register_plugin_v1`) | PM4-0 | — |
| PM4-2 (`sturm-4oyr.3`) | Registry implementation `plugin_registry.cpp` + collision detection | PM4-1 | — |
| PM4-3 (`sturm-4oyr.4`) | `QOpKind::PLUGIN` + `plugin_kind_id` field + `render_uncompute` dispatch; thread `Registry&` through `synthesize` | PM4-2 | PM4-4 |
| PM4-4 (`sturm-4oyr.5`) | `plugin.cpp` `ParseArgs` `load=<path>` + `dlopen(RTLD_LOCAL\|RTLD_NOW)` + Clang-version check | PM4-2 | PM4-3 |
| PM4-5 (`sturm-4oyr.6`) | `SturmTranspile.cmake` `PLUGINS` argument to `add_quantum_executable` | PM4-4 | — |
| PM4-6 (`sturm-4oyr.7`) | Dogfood: migrate PB-1..PB-4 in `matcher_qint_const.cpp` to Registry API | PM4-3 | PM4-7 |
| PM4-7 (`sturm-4oyr.8`) | Demo plugin `examples/plugin_demo/` (novel self-inverse "tag" op; MODULE target) | PM4-1, PM4-2 | PM4-6 |
| PM4-8 (`sturm-4oyr.9`) | Smoke 1 — `pm4_smoke_dlopen` (end-to-end dlopen → register_op → rewrite) | PM4-5, PM4-7 | — |
| PM4-9 (`sturm-4oyr.10`) | Smokes 3–5 — `pm4_dogfood_snapshot` + `pm4_missing_plugin_errors` + `pm4_two_plugins_independent` (collision) | PM4-6, PM4-8 | PM4-10 |
| PM4-10 (`sturm-4oyr.11`) | Smoke 2 — `pm4_smoke_linktime` + link-time path via `STURM_PM4_LINK_DEMO` CMake option | PM4-7 | PM4-9 |
| PM4-11 (`sturm-4oyr.12`) | Roadmap `roadmap_transpiler_post_mvp.md:875` completion note + CHANGELOG | PM4-8, PM4-9, PM4-10 | — |

**Parallel pairs** (safe to spawn two bd-workers concurrently): PM4-3 ∥ PM4-4, PM4-6 ∥ PM4-7, PM4-9 ∥ PM4-10.

## Dependency graph

```
PM4-0 ──> PM4-1 ──> PM4-2 ──┬──> PM4-3 ──┬──> PM4-6 ──┐
                            │            │            │
                            │            └──> PM4-8 ──┼──> PM4-9 ──┐
                            │                         │            │
                            ├──> PM4-4 ──> PM4-5 ─────┘            ├──> PM4-11
                            │                                      │
                            └──> PM4-7 ──> PM4-10 ─────────────────┘
```

## Critical files

### Modified
- `transpiler/include/sturm/transpile/qir.hpp` — extend `QOpKind` with `PLUGIN` (line 76+), extend `QOperation` with `std::string plugin_kind_id{}` (line 225+). Empty string default preserves all existing snapshot fixtures byte-identical — same invisibility trick PI-2 used with `routine_name` at line 244.
- `transpiler/src/uncompute_pass.cpp` — add `case QOpKind::PLUGIN:` to `render_uncompute` at line 43+; thread `Registry&` through `synthesize()` (line 266+).
- `transpiler/src/transpile_consumer.cpp` — constructor wires Registry (lines 32-317); `HandleTranslationUnit` threads it to `synthesize()`.
- `transpiler/src/plugin.cpp` — extend `ParseArgs` at line 182+ with a `load=<path>` branch that dlopens and dlsyms; extend the entry-point invocation ordering per §6.
- `transpiler/src/matcher_qint_const.cpp` — migrate all four `register_*_matcher` one-liners at lines 148-166 to Registry API (dogfood, PM4-6).
- `cmake/SturmTranspile.cmake` — add `PLUGINS path1.so path2.so …` optional argument to `add_quantum_executable` (lines 94-247); mirror the `dump-to=` pattern at lines 229-233.
- `transpiler/CMakeLists.txt` — new `STURM_PM4_LINK_DEMO` option for link-time smoke test.

### New
- `transpiler/include/sturm/transpile/plugin_api.hpp` — the public Registry / StaticRegistrar / `sturm_register_plugin_v1` surface.
- `transpiler/src/plugin_registry.cpp` — Registry implementation.
- `examples/plugin_demo/plugin_demo.cpp` + `examples/plugin_demo/CMakeLists.txt` — demo plugin (novel self-inverse "tag" op + `pm4_demo_tag_inverse` renderer).
- `transpiler/tests/pm4_smoke_dlopen.cpp` + `.cmake` (smoke 1)
- `transpiler/tests/pm4_smoke_linktime.cpp` + `.cmake` (smoke 2)
- `transpiler/tests/pm4_dogfood_snapshot.cpp` + `.cmake` (smoke 3)
- `transpiler/tests/pm4_missing_plugin_errors.cpp` + `.cmake` (smoke 4)
- `transpiler/tests/pm4_two_plugins_independent.cpp` + `.cmake` (smoke 5 — collision detection)

### Reused (do not reinvent)
- `FrontendPluginRegistry::Add` registration at `transpiler/src/plugin.cpp:477-478` — the existing plugin-load hook.
- `ParseArgs` cc1 spelling discipline documented at `transpiler/src/plugin.cpp:178-200` and `cmake/SturmTranspile.cmake:190-197` — plugins must use `-Xclang -plugin-arg-sturm-transpile -Xclang <arg>`, not `-fplugin-arg-` (the driver mis-parses hyphenated plugin names).
- `enclosing_scope` / `find_or_create_scope` / `make_ref` helpers in `matcher_common.hpp` — plugin matchers reuse these rather than reinvent.
- Idempotency-header emission at `transpiler/src/plugin.cpp:321-335` — the demo plugin does not need to touch it.
- `STURM_ANCILLA_CAPACITY` wiring at `cmake/SturmTranspile.cmake:109-117` — propagates to plugin builds via existing CMake mechanics.

## Verification

**Build-command hard limit.** Every `cmake`, `cmake --build`, `ctest`, `make`, `ninja` invocation MUST cap at 6 threads — `--parallel 6` / `-j6` / `CTEST_PARALLEL_LEVEL=6`. This is a project-wide rule (`CLAUDE.md`).

### Per-bd-item (inside bd-worker)

```bash
cmake --build build --parallel 6 --target <target>
CTEST_PARALLEL_LEVEL=6 ctest --test-dir build -R '<item-regex>' --output-on-failure
```

### End-to-end (after epic closes)

```bash
cmake -S . -B build -DSTURM_PM4_LINK_DEMO=ON
cmake --build build --parallel 6
CTEST_PARALLEL_LEVEL=6 ctest --test-dir build --output-on-failure
```

**Expected.** All existing tests pass byte-identical (confirming the dogfood migration is transparent to the snapshot surface), plus five new `pm4_smoke_*` / `pm4_dogfood_*` tests pass. Specifically:

- **Smoke 1** (`pm4_smoke_dlopen`, PM4-8): user code compiled via `add_quantum_executable(... PLUGINS $<TARGET_FILE:sturm-pm4-demo-plugin>)` shows `pm4_demo_tag_inverse(` in the rewritten buffer.
- **Smoke 2** (`pm4_smoke_linktime`, PM4-10): same assertion as Smoke 1, but via link-time registration — no `PLUGINS` argument, `-DSTURM_PM4_LINK_DEMO=ON` reconfig.
- **Smoke 3** (`pm4_dogfood_snapshot`, PM4-9): existing Phase B snapshot of `a += k;` / `a -= k;` / `a *= k;` / `a /= k;` remains byte-identical post-migration.
- **Smoke 4** (`pm4_missing_plugin_errors`, PM4-9): `PLUGINS /nonexistent.so` fails the compile with a recognizable `sturm-transpile plugin: failed to dlopen` stderr line.
- **Smoke 5** (`pm4_two_plugins_independent`, PM4-9): two plugins registering the same `kind_id` string cause a hard error at the second registration with a diagnosable message.

### Principle check (before PM4-11 closes)

Reread `docs/01_principles.md`. The PM4 extension surface does not touch any B-principle (B1b, B6, B9, B10) or P9 — all remain valid. Reasoning:

- **B1b** stays intact: plugins emit forward rewrites and register an *explicit* adjoint render function. No runtime auto-inversion is introduced.
- **B6** stays intact: plugin-registered matchers use the shared `enclosing_scope` / `find_or_create_scope` helpers, so the RAII + scope-bound ancilla invariant is preserved.
- **B9** stays intact: plugins inject matchers and render rules, not new global optimization passes. The "one global optimization pass at transpile time" count is unchanged.
- **B10** stays intact: plugin-supplied inverses are emitted as source text at compile time, exactly like in-tree uncomputes. Nothing moves to runtime.
- **P9** stays intact: the plugin author writes the adjoint render function explicitly; the transpiler remains a consumer of manual adjoints.

If an unexpected principle revision emerges during implementation (e.g. "third-party rewrites must run after in-tree"), file it as a sub-bullet under PM4-11. Otherwise no `01_principles.md` edit.

### Session close (mandatory per `CLAUDE.md`)

```bash
bd close <PM4-epic-id>
git pull --rebase
bd dolt push || true   # local-only; no remote configured for bd
git push
git status  # MUST report "up to date with origin/main"
```

## Sharp edges / risks

- **`render_uncompute` switch + plugin tail.** The no-`default:` contract in `qir.hpp:73-75` is load-bearing documentation. Adding `case QOpKind::PLUGIN:` preserves it; the Registry lookup inside that case must handle "kind_id not found" gracefully (emit nothing, log stderr) — a missing renderer for a plugin op is a build-by-build mismatch, not a runtime crash.
- **`Registry&` threading.** `render_uncompute` is today a free function in an anonymous namespace of `uncompute_pass.cpp:43`. Threading `Registry&` through `synthesize(unit, sm, registry)` keeps this free-function discipline — no globals, no singletons beyond the Meyer drain in §5 (which `TranspileConsumer::TranspileConsumer` processes ONCE per consumer lifetime, not per op).
- **PM1-4 nested invocation.** A plugin-loaded matcher runs inside a `TranspileConsumer` which may live inside a nested `CompilerInvocation` (plugin.cpp:242-439). The Registry object is per-consumer, not per-process — plugin registrations observed by the outer consumer are re-observed by the nested consumer via the same Meyer vector (and the same `dlopen` handles, which remain open per §7). Double-registration of the *same* plugin is detected and collapsed by the Registry (PM4-2 is responsible for this).
- **Clang-version string brittleness.** `CLANG_VERSION_STRING` is set at build time from LLVM's `CMakeLists.txt`. If a downstream distro patches LLVM and bumps the version string, plugins compiled before the patch refuse to load with the host Clang post-patch — which is the *correct* behavior, but may surprise users. The PM4-11 README language must foreground this.
- **Two-pass transpile interaction.** The "synthetic scope + brace-insertion ordering" hazard documented in Phase H's plan (archived at `docs/archive/implementation_plan_transpiler_phase_h.md:190`) applies if a plugin registers a matcher that schedules raw brace insertions. Plugins emitting text through `QReplacement` / `UncomputeInsertion` go through the same emitter pass that already handles in-tree matchers; plugins should not invent a parallel rewrite channel. This is a convention, not an enforced limit — PM4-11 README must cover it.
