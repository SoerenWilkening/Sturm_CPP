# Implementation Plan: Frontend Simplification

- **Date:** 2026-05-07
- **Status:** Draft (pending user approval). **Archived 2026-05-19**
  alongside its PRD — all P1–P9 phases have landed (see
  `archive/prd_frontend_simplification.md` Status line). Moved to
  `docs/archive/impl_plan_frontend_simplification.md`.
- **Owner:** Soren Wilkening
- **Drives:** `docs/archive/prd_frontend_simplification.md`
- **Approach:** modular, test-driven, every implementation file ≤ 300 LOC.

## 0. Working agreements

### 0.1 — Module size rule

Every new or rewritten implementation file (`.hpp` / `.cpp`) must stay
≤ 300 LOC including comments and blank lines. If a phase trips the
ceiling, it splits along the natural seam called out in that phase's
"Split if oversize" note. Test files have no hard cap but should
follow the same spirit (one concern per file).

### 0.2 — Test-first cadence

Every phase lands in this order:

1. **Red** — write the failing test(s) that encode the phase's exit
   criteria; commit with `[red]` tag in the bd issue notes.
2. **Green** — minimal implementation to flip the test green; commit
   under the bd issue.
3. **Refactor** — only if green leaves obvious smell; otherwise stop.

`ctest --test-dir build --parallel 6 --output-on-failure -R <regex>`
runs the phase's tests in isolation; the full sweep at end-of-phase
catches collateral damage.

### 0.3 — Build/test command floor

Per `CLAUDE.md`: every `cmake`, `ctest`, `make`, `ninja` invocation
caps at 6 threads. Restate the rule in any spawned subagent prompt.

### 0.4 — Reversibility

Each phase is committed as a single bd issue with a one-line revert
recipe in its bd notes. Phases 2 (qubit cap removal) and 7 (transpiler
auto-injection) are the only ones whose revert is non-mechanical;
their entries flag this.

## 1. Open-question resolutions (lock before implementation)

| OQ | Decision | Rationale |
| --- | --- | --- |
| **OQ1** Auto-injection trigger | `__has_include("sturm.h")` **plus** sentinel macro `STURM_UMBRELLA_INCLUDED` set inside `sturm.h`. | Keeps the matcher cheap; sentinel makes the trigger explicit and grep-able. `PreprocessorCallback` adds plumbing for no extra signal. |
| **OQ2** Escape-hatch macro name | `STURM_NO_AUTO_LIFECYCLE`. | Verb-form ("no") matches the `STURM_BACKEND_ENABLED 0` precedent and reads correctly in a one-line `#define` block. |
| **OQ3** Examples migration policy | Migrate all examples to auto-injection. Add `examples/explicit_lifecycle.cpp` as the single didactic counter-example with `STURM_NO_AUTO_LIFECYCLE`. | Acceptance A1 requires `qram_demo.cpp` to match §4 exactly; uniform policy avoids per-example bikeshedding. One didactic example covers the "show me the manual form" need. |
| **OQ4** Renderer name collision | Confirmed via `grep -rn "print_ascii\|gate_count" include src tests` during phase 5; if collision found, rename to `sturm::ascii_print` / `sturm::ir_size`. Otherwise lock the §5.6 names. | Decision deferred to phase 5's red step — the grep is the test. |
| **OQ5** Build-flag rebuild semantics | Umbrella exposed via `INTERFACE` library `sturm::frontend`; `target_compile_definitions(sturm::frontend INTERFACE STURM_MODE_DEFAULT=...)`. Toggling `-DSTURM_MODE` reconfigures CMake, which invalidates the interface and forces rebuild of dependents. | Standard CMake idiom; verified by phase 1 test that flips `STURM_MODE` and asserts a recompile. |

## 2. Dependency graph

```
P1 (build flag)  ──┐
                   ├──> P3 (umbrella)  ──> P4 (opt-in headers)  ──┐
P2 (cap removal) ──┘                                              │
                                                                  ├──> P7 (auto-inject) ──> P8 (migration) ──> P9 (gates)
P5 (no-arg renderer)  ────────────────────────────────────────────┤
P6 (SIMULATE OOM guard)  ─────────────────────────────────────────┘
```

Critical path: P1 → P2 → P3 → P7 → P8 → P9. P4/P5/P6 can run in
parallel once their predecessors land.

## 3. Phase breakdown

Each phase below includes: **scope**, **files** (with LOC budget),
**tests**, **exit criteria**, **rollback**, **risk**.

---

### Phase 1 — `STURM_MODE` build flag

**Scope.** Introduce `-DSTURM_MODE=APPEND|COUNT|SIMULATE` (default
`APPEND`). Validate at configure time. Propagate to compile
definitions on a new `sturm::frontend` interface target. (PRD §5.3)

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `CMakeLists.txt` | +25 | Cache var + validation block. |
| `cmake/SturmFrontend.cmake` (new) | +40 | Defines `sturm::frontend` interface library; carries `STURM_MODE_DEFAULT`. |
| `tests/cmake/cmake_sturm_mode_flag.cmake` (new) | +60 | Configure-time test: invalid mode → fatal; valid mode → `STURM_MODE_DEFAULT` macro present in compile defs. |
| `tests/cmake/cmake_sturm_mode_rebuild.cmake` (new) | +50 | Toggle flag, assert TU recompiles. |
| `tests/CMakeLists.txt` | +6 | Register the two ctests under `STURM_FULL_TEST_SUITE`. |

**Tests (red → green)**

- `cmake_sturm_mode_flag` — configures with `STURM_MODE=GARBAGE`,
  expects `FATAL_ERROR` containing `STURM_MODE`.
- `cmake_sturm_mode_flag` — configures with `STURM_MODE=SIMULATE`,
  builds a probe TU that does
  `static_assert(STURM_MODE_DEFAULT == STURM_MODE_SIMULATE)`.
- `cmake_sturm_mode_rebuild` — touches a probe TU's mtime baseline,
  flips `STURM_MODE`, asserts the TU was rebuilt.

**Exit criteria.** All three ctests green. No other phase's tests
broken (full ctest sweep stays green; `sturm_backend_create` still
takes `(mode, max_qubits)`).

**Rollback.** Revert the commit; the flag has no run-time consumers
yet.

**Risk.** Low. Pure CMake/macro plumbing. The interface-library
rebuild semantics need verification on the host toolchain (OQ5).

---

### Phase 2 — Qubit cap removal

**Scope.** Drop `max_qubits` from `sturm_backend_create`,
`BackendContext`, `QubitPool`. Delete `STURM_ANCILLA_CAPACITY`,
`kCapExceededMsg`, `kCapacity`. Update all 126 caller files
mechanically. (PRD §5.5)

**Why two sub-phases.** The signature change breaks 126 files. Doing
it as one bd issue would hide bugs. Sub-phases isolate the API change
from the migration sweep.

**Files (P2.a — API change)**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `include/sturm/core/qubit_pool.hpp` | −25 | Remove `kCapacity`, cap field, cap-check, abort path. Single `acquire()` that grows on demand. Final size ≤ 120 LOC. |
| `include/sturm/core/core.h` | −2 | `sturm_backend_create(sturm_mode_t)`. |
| `include/sturm/core/context.hpp` | −2 | `BackendContext::BackendContext(sturm_mode_t)`. |
| `src/sturm/core/context.cpp` | −5 | Signature update; drop cap forwarding. |
| `tests/qtypes/test_qubit_pool_growth.cpp` (new) | +90 | Asserts pool grows past former cap (1024 qubits), no abort. |
| `tests/regressions/test_no_cap_artifacts.cpp` (new) | +40 | Greps the build artifacts for `STURM_ANCILLA_CAPACITY`, `kCapExceededMsg`, `max 17` — fails if any survive. |

**Files (P2.b — caller migration sweep)**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `tools/migrate_backend_create.py` (new, throwaway) | +60 | One-off codemod: regex-replace `sturm_backend_create(<mode>, <expr>)` → `sturm_backend_create(<mode>)`. Prints a diff summary. |
| 126 files (mostly `tests/`, `examples/`, a few `src/`) | mechanical | Driven by the codemod; commit as a single squashable change. |

**Tests (red → green)**

- P2.a red: `test_qubit_pool_growth` and `test_no_cap_artifacts` are
  written first against the *current* API and fail (cap still
  enforced; artifacts still present).
- P2.a green: API change lands; **everything else still red** (the
  126 callers don't compile).
- P2.b green: codemod runs; full ctest sweep green.

**Exit criteria.**

- `grep -rn "STURM_ANCILLA_CAPACITY\|kCapExceededMsg\|kCapacity\b" include src tests examples transpiler` → empty.
- `grep -rn "sturm_backend_create([^)]*,[^)]*)" include src tests examples transpiler` → empty.
- Full ctest green.

**Rollback.** `git revert` of P2.b first (re-introduces the second
arg via codemod-inverse), then P2.a. Non-mechanical because the
codemod erased the qubit-count argument that callers chose
deliberately — rollback restores a constant `8` everywhere, which
matches no original intent. **Mark in bd as "non-mechanical
rollback".**

**Risk.** Medium. The codemod must distinguish
`sturm_backend_create(MODE, N)` from
`sturm_backend_create(MODE, N, ...)` (no such overload exists today,
but transpiler-emitted code is a wildcard). Pre-flight: full grep,
human eyeball, then run.

---

### Phase 3 — `sturm.h` umbrella header

**Scope.** Single front-door header. (PRD §5.1)

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `include/sturm.h` (new) | +35 | Forwards listed headers; `using sturm::qint`, `using sturm::qbool`; sets `STURM_BACKEND_ENABLED 1` if undefined; sets `STURM_UMBRELLA_INCLUDED 1` (the OQ1 sentinel). |
| `tests/packaging/test_umbrella_only.cpp` (new) | +25 | A program that includes only `sturm.h`, builds a `qint a = 5;`, asserts at least one gate emitted. |
| `tests/packaging/CMakeLists.txt` | +8 | Register the test. |

**Tests (red → green)**

- `test_umbrella_only` red: `sturm.h` doesn't exist yet → compile
  failure.
- Green: header lands; test compiles and passes.

**Exit criteria (covers A5 first half).** Test green; full sweep
green. `grep -n "STURM_UMBRELLA_INCLUDED" include/sturm.h` returns
exactly one definition.

**Rollback.** Remove `include/sturm.h` and the test. Mechanical.

**Risk.** Low. Pure forwarding.

**Split if oversize.** Not expected (35 LOC). If transitive includes
balloon, factor inline `using` declarations into a small
`include/sturm/detail/aliases.hpp`.

---

### Phase 4 — Opt-in feature headers

**Scope.** `sturm/qram.h` and `sturm/draw_ascii.h` (declarations
only — implementation of no-arg renderer in P5). (PRD §5.2)

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `include/sturm/qram.h` (new) | +12 | Pulls `sturm/qram/qram_read.hpp`. Comment block listing the rewrite trigger. |
| `include/sturm/draw_ascii.h` (new) | +25 | Forwards `sturm/backend/draw_ascii.hpp`; declares `sturm::draw_ascii()`, `sturm::print_ascii()`, `sturm::gate_count()` (definitions in P5). |
| `tests/packaging/test_umbrella_qram.cpp` (new) | +35 | `#include "sturm.h"` + `#include "sturm/qram.h"` + `qint b = a[i]` lvalue load → asserts non-empty IR. |
| `tests/packaging/CMakeLists.txt` | +6 | Register. |

**Tests.** As above — packaging tests for the two new entry points.
Both red until P5 lands the renderer definitions; for P4 alone, the
qram test is the gate.

**Exit criteria (covers A5 second half).** `test_umbrella_qram`
green. Full sweep green.

**Rollback.** Mechanical — delete the two headers.

**Risk.** Low.

---

### Phase 5 — No-argument renderer entry points

**Scope.** Implement `sturm::draw_ascii()`, `sturm::print_ascii()`,
`sturm::gate_count()` against the thread-local context. Derive
canvas width from IR's max qubit index + 1. (PRD §5.6)

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `include/sturm/backend/draw_ascii.hpp` | +20 | Forward-declare new entry points; keep IR-taking overload. |
| `src/sturm/backend/draw_ascii_noarg.cpp` (new) | +120 | Implements the three new entry points. **Split** from the existing `draw_ascii.cpp` to keep both files ≤ 300 LOC. Asserts on null thread context (PRD §5.6). |
| `tests/backend/test_draw_ascii_noarg.cpp` (new) | +130 | Cases: empty IR → empty string; single 1-qubit gate → 1-rail diagram; multi-qubit IR → max-index+1 rails; gate count matches `ir.size()`; null context → assert fires (`EXPECT_DEATH` / `ASSERT_DEATH_IF_SUPPORTED`). |
| `tests/backend/CMakeLists.txt` | +5 | Register. |

**OQ4 settle step (red).** Run
`grep -rn "\\bprint_ascii\\b\\|\\bgate_count\\b" include src tests`
before writing the test. Empty result → keep PRD names. Non-empty →
rename per OQ4 fallback before P5.b lands.

**Tests (red → green).** All five test cases red until P5 implements;
green when implementation passes. `EXPECT_DEATH` requires gtest's
death-test linkage — confirm `tests/backend/CMakeLists.txt` already
links it (it does for existing assert tests; reuse pattern).

**Exit criteria.** Five test cases green. `tests/packaging/`
suites that use `sturm::print_ascii()` (added in P4) become green.

**Rollback.** Remove the new source + test; reverse the header
addition. Mechanical.

**Risk.** Low–medium. The "max qubit index" scan is a single pass
over IR ops; care needed if any op kind references qubits via a
non-standard accessor — confirm by enumerating `IROp` variants
during P5 design.

**Split if oversize.** If `draw_ascii_noarg.cpp` grows past 300 LOC
(it shouldn't), pull the canvas-width computation into
`src/sturm/backend/draw_ascii_canvas.cpp`.

---

### Phase 6 — SIMULATE-mode OOM guard

**Scope.** Wrap the SIMULATE statevector allocation in
`OrkanBridge` with `try { ... } catch (std::bad_alloc&) { ... }` →
`std::abort` after writing a stable message to `stderr`. (PRD §5.7)

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `include/sturm/backend/orkan_bridge.hpp` | +18 | Add the try/catch; `fprintf(stderr, ...)` then `std::abort()`. Final file ≤ 300 LOC. |
| `tests/backend/test_simulate_oom.cpp` (new) | +60 | `EXPECT_DEATH` (or `EXPECT_EXIT`) on a request for 64 qubits in SIMULATE; matches the stable message regex. |
| `tests/backend/CMakeLists.txt` | +4 | Register; only enabled when SIMULATE backend compiled in. |

**Tests.** Death test asserting the stable message
`"STURM: SIMULATE mode out of memory at N qubits — reduce qubit count"`.

**Exit criteria.** Death test green under
`-DSTURM_MODE=SIMULATE`. Other modes unaffected.

**Rollback.** Revert the `orkan_bridge.hpp` block + delete the test.

**Risk.** Low. The death test is the brittle bit — guard with
`ASSERT_DEATH_IF_SUPPORTED` for portability.

---

### Phase 7 — Transpiler auto-injection (`matcher_main_lifecycle`)

**Scope.** New AST matcher that rewrites `int main(...)` in TUs that
include `sturm.h`. (PRD §5.4)

**Why this is the largest phase.** The matcher must:
1. Detect the unique `main` `FunctionDecl` (forms: `int main()`,
   `int main(int, char**)`, `int main(int, char**, char**)`).
2. Skip when `STURM_NO_AUTO_LIFECYCLE` is defined.
3. Skip when sentinel `STURM_UMBRELLA_INCLUDED` is undefined
   (i.e., user didn't include `sturm.h`).
4. Source-rewrite the body into the IIFE form (PRD §5.4 listing).
5. Preserve user `argc`/`argv` access through the IIFE capture.
6. Be idempotent — re-running the transpiler on rewritten source
   must be a no-op (the sentinel `__sturm_ctx` already present
   suppresses).

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `transpiler/src/matcher_main_lifecycle.hpp` (new) | +60 | Class declaration + matcher registration helper. |
| `transpiler/src/matcher_main_lifecycle.cpp` (new) | +260 | Matcher body. **Split target** if exceeded — see below. |
| `transpiler/src/main.cpp` | +5 | Register the new matcher. |
| `tests/transpiler/fixtures/main_lifecycle_basic.cpp` (new) | +20 | `int main()` body with one `qint`; expected rewrite output committed alongside. |
| `tests/transpiler/fixtures/main_lifecycle_argc.cpp` (new) | +20 | `int main(int argc, char** argv)`; ensures arg capture works. |
| `tests/transpiler/fixtures/main_lifecycle_no_auto.cpp` (new) | +15 | Same body but with `#define STURM_NO_AUTO_LIFECYCLE`; expected output ≡ input. |
| `tests/transpiler/fixtures/main_lifecycle_no_umbrella.cpp` (new) | +15 | Body without `#include "sturm.h"`; expected output ≡ input. |
| `tests/transpiler/fixtures/main_lifecycle_idempotent.cpp` (new) | +25 | Already-rewritten source; expected output ≡ input. |
| `tests/transpiler/test_main_lifecycle.cpp` (new) | +180 | Drives the four fixtures via the existing transpile-and-compare harness. |
| `tests/transpiler/CMakeLists.txt` | +12 | Register fixtures + test. |

**Split if oversize.** If `matcher_main_lifecycle.cpp` exceeds 300
LOC, factor the source-text builder (the IIFE wrapper) into
`transpiler/src/main_lifecycle_emitter.cpp` (~120 LOC) and the
matcher itself stays ≤ 200 LOC.

**Tests (red → green)**

| Test | Asserts | Comes green when |
| --- | --- | --- |
| `test_main_lifecycle_basic` | rewrite emits the IIFE form, calls `sturm_backend_create(STURM_MODE_DEFAULT)`, calls `sturm_set_thread_context(nullptr)` + `destroy` post-body. | Emitter complete. |
| `test_main_lifecycle_argc` | rewritten body inside IIFE captures `argc`/`argv` by reference; output is compilable. | Capture clause uses `[&]`. |
| `test_main_lifecycle_no_auto` | round-trip: output ≡ input (modulo whitespace). | Skip-condition wired. |
| `test_main_lifecycle_no_umbrella` | round-trip. | Sentinel check wired. |
| `test_main_lifecycle_idempotent` | round-trip on already-rewritten source. | Idempotency probe (presence of `__sturm_ctx` in `main`'s first compound stmt) wired. |

**Exit criteria (covers A6).** All five fixtures pass. `qram_demo`
(post-P8 migration) compiles and produces byte-identical stdout vs.
the pre-PRD baseline.

**Rollback.** Revert all five test fixtures + matcher files; remove
the `main.cpp` registration line. Non-mechanical because the new
fixtures encode contracts, but operationally a single
`git revert <commit>`.

**Risk.** High. AST matcher edge cases — KR-style `main(int argc,
char* argv[])`, `int main(void)`, GCC-extension `auto main() ->
int`. Spec restricts to `int main(...)` so the GCC trailing-return
form is out of scope; flag in test fixture comments.

---

### Phase 8 — Examples + docs migration

**Scope.** Migrate examples (per OQ3) and rewrite three docs.
(PRD §5.8, §6.OQ3)

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `examples/qram_demo.cpp` | rewrite | Match PRD §4 "After" listing exactly (covers A1). |
| `examples/explicit_lifecycle.cpp` (new) | +35 | Didactic counter-example with `STURM_NO_AUTO_LIFECYCLE` + manual create/destroy. |
| `examples/*.cpp` (≈ 20 files) | mechanical | Strip explicit lifecycle blocks; switch to `sturm.h` + opt-in headers as needed. |
| `examples/CMakeLists.txt` | +5 | Add `explicit_lifecycle` target. |
| `docs/qram_user_intro.md` | rewrite | Drop `kNumQubits`; switch to `sturm.h` flow. |
| `docs/getting_started.md` | rewrite | Same. |
| `docs/public_api.md` | rewrite | Same; document the umbrella + opt-in headers. |
| `docs/public_api.txt` | rewrite | Mirror `public_api.md`. |
| `docs/archive/prd_qint_alias_completion.md` | unchanged | Preserve historical record. |
| `tests/regressions/test_qram_demo_byte_identical.cpp` (new) | +80 | Runs `qram_demo`, captures stdout, compares against the pre-PRD baseline file `tests/regressions/data/qram_demo_baseline.txt`. |
| `tests/regressions/data/qram_demo_baseline.txt` (new) | data | Generated once before P2 lands; committed. |

**Tests.** `test_qram_demo_byte_identical` covers A1.

**Exit criteria (covers A1, A7).** Test green; docs no longer
mention `kNumQubits` or the multi-include block; A1 listing matches
exactly.

**Rollback.** Revert per-file. Documentation rewrites are
non-mechanical to undo cleanly — the bd issue should attach the
pre-rewrite versions in its notes for safety.

**Risk.** Medium. The byte-identical baseline depends on stable
gate ordering — if any prior phase perturbs IR ordering, this test
becomes the canary. Generate the baseline at the **start of P8**,
not at PRD-draft time, so it captures any incidental ordering
shifts from P1–P7.

---

### Phase 9 — Acceptance gates

**Scope.** Wire all PRD acceptance criteria into ctest as named
tests. Most are already covered by phases 1–8; this phase is the
roll-up.

**Files**

| Path | Δ LOC | Notes |
| --- | --- | --- |
| `tests/regressions/test_acceptance_gates.cmake` (new) | +120 | One ctest per A1–A7. Most delegate to existing tests; A2 and A3 invoke configure-build-test sub-steps via `execute_process`. |
| `tests/CMakeLists.txt` | +4 | Register. |

**A1 — `qram_demo.cpp` matches §4 + byte-identical.** Delegates to
`test_qram_demo_byte_identical` (P8) plus a textual diff vs. an
in-test embedded copy of the §4 listing.

**A2 — default APPEND build green.**
`execute_process(cmake -S . -B _A2 && cmake --build _A2 -j6 &&
ctest --test-dir _A2 -j6)` in a fresh build dir.

**A3 — SIMULATE build green.** Same with `-DSTURM_MODE=SIMULATE`,
limited to the SIMULATE-mode test label.

**A4 — no cap artifacts.** Greps the source tree;
delegates to `test_no_cap_artifacts` (P2.a).

**A5 — packaging.** Delegates to `test_umbrella_only` (P3) and
`test_umbrella_qram` (P4).

**A6 — `STURM_NO_AUTO_LIFECYCLE` round-trip.** Delegates to
`test_main_lifecycle_no_auto` (P7).

**A7 — docs reflect new shape.** Greps
`docs/qram_user_intro.md`, `docs/getting_started.md` for forbidden
strings (`kNumQubits`, the eight-include block).

**Exit criteria.** All seven acceptance gates green in a clean
`-DSTURM_FULL_TEST_SUITE=ON` build. PRD status flips from "Draft"
to "Implemented" in a follow-up commit.

**Rollback.** Mechanical. Aggregator-only.

**Risk.** Low. Discovers-but-doesn't-cause bugs.

---

## 4. Module size audit

Files predicted to land near the 300-LOC ceiling, with the
declared split point if exceeded:

| File | Predicted LOC | Cap | Split target |
| --- | --- | --- | --- |
| `transpiler/src/matcher_main_lifecycle.cpp` | 260 | 300 | Move IIFE source-text builder → `main_lifecycle_emitter.cpp`. |
| `src/sturm/backend/draw_ascii_noarg.cpp` | 120 | 300 | Pull canvas-width scan → `draw_ascii_canvas.cpp` (only if it grows). |
| `tests/transpiler/test_main_lifecycle.cpp` | 180 | (test) | One file per fixture if it grows past 300. |

All other new files are ≤ 100 LOC.

## 5. Test inventory (cross-reference to acceptance criteria)

| Test | Phase | A-gate | What it pins |
| --- | --- | --- | --- |
| `test_qubit_pool_growth` | P2.a | A4 | Pool grows past former cap. |
| `test_no_cap_artifacts` | P2.a | A4 | No surviving cap symbols. |
| `cmake_sturm_mode_flag` | P1 | A3 (preflight) | Flag validated; macro propagated. |
| `cmake_sturm_mode_rebuild` | P1 | OQ5 | Flag flip rebuilds dependents. |
| `test_umbrella_only` | P3 | A5 | `sturm.h` alone compiles a qint program. |
| `test_umbrella_qram` | P4 | A5 | `sturm.h` + `sturm/qram.h` compiles `a[i]`. |
| `test_draw_ascii_noarg` | P5 | A1 (preflight) | No-arg renderer entry points behave. |
| `test_simulate_oom` | P6 | (none) | OOM message stable. |
| `test_main_lifecycle_basic` | P7 | A6 (preflight) | Rewrite emits IIFE form. |
| `test_main_lifecycle_argc` | P7 | A6 (preflight) | Argc/argv capture. |
| `test_main_lifecycle_no_auto` | P7 | **A6** | Escape hatch round-trips. |
| `test_main_lifecycle_no_umbrella` | P7 | A6 (preflight) | Sentinel skip. |
| `test_main_lifecycle_idempotent` | P7 | (none) | Re-run is no-op. |
| `test_qram_demo_byte_identical` | P8 | **A1** | Demo output unchanged. |
| `test_acceptance_gates` | P9 | A1–A7 | Roll-up gate. |

## 6. Risk register

| ID | Risk | Likelihood | Mitigation |
| --- | --- | --- | --- |
| R1 | P2.b codemod misses a malformed call site (e.g., transpiler-emitted code with extra args). | Medium | Codemod prints unmatched lines; reviewer reads list before commit. |
| R2 | P5's "max qubit index" scan misses a rare `IROp` variant. | Low–medium | P5 design step enumerates `IROp` exhaustively. |
| R3 | P7 matcher fires on test-fixture `main` (e.g., gtest's, in `test_*.cpp` files that include `sturm.h`). | High if not handled | Test fixtures don't include `sturm.h` directly; gtest provides its own `main`. Add explicit skip if `FunctionDecl` is in a `gtest`-named source file. **Phase 7 adds a fixture covering this.** |
| R4 | P8 baseline drifts due to ordering-sensitive IR output. | Medium | Generate baseline at start of P8, not at PRD time. |
| R5 | OQ5 rebuild semantics fail on Ninja (interface lib defines vs. source). | Low | P1's `cmake_sturm_mode_rebuild` test catches this in CI. |
| R6 | `sturm::print_ascii` collides with a user-defined symbol downstream. | Low | OQ4 grep step in P5 catches in-tree collisions; downstream is user-owned. |
| R7 | Auto-injected lifecycle skipped on `setjmp`/`longjmp`/`exit()` paths from user body. | By design | PRD §3 implicitly accepts this; document explicitly in P3's `sturm.h` comment block. |

## 7. Out-of-scope follow-ups

Carried from PRD §7. **File as bd issues at end of P9, do not let
them creep into this plan:**

- Per-context `super_mask` / `promotion_mask` widening (`uint32_t` →
  `uint64_t`).
- `classical_values[17]` removal or dynamic widening.
- Auto-injection for non-`main` entry points
  (`[[sturm::entry_point]]` attribute).
- Renderer formats: `sturm/draw_mermaid.h`, `sturm/draw_svg.h`,
  `sturm/draw_json.h`.
- Test-framework auto-injection (PRD §3 non-goal).

## 8. Issue filing checklist

Issues to create in bd before implementation begins, one per
phase/sub-phase. Suggested priorities (P0 = critical, P2 = medium):

| ID | Title | Priority | Depends on |
| --- | --- | --- | --- |
| F-1 | Frontend simpl. P1 — STURM_MODE build flag | P2 | — |
| F-2a | Frontend simpl. P2.a — qubit cap removal (API) | P2 | — |
| F-2b | Frontend simpl. P2.b — caller migration sweep | P2 | F-2a |
| F-3 | Frontend simpl. P3 — sturm.h umbrella | P2 | F-1, F-2a |
| F-4 | Frontend simpl. P4 — opt-in feature headers | P2 | F-3 |
| F-5 | Frontend simpl. P5 — no-arg renderer | P2 | — |
| F-6 | Frontend simpl. P6 — SIMULATE OOM guard | P2 | — |
| F-7 | Frontend simpl. P7 — matcher_main_lifecycle | P1 | F-3, F-4, F-5 |
| F-8 | Frontend simpl. P8 — examples + docs migration | P2 | F-7 |
| F-9 | Frontend simpl. P9 — acceptance gates roll-up | P1 | F-1…F-8 |

Each issue's description should link back to this plan section and
the corresponding PRD section. Each issue's notes should carry: (a)
the phase's "Exit criteria", (b) the rollback recipe, (c) the
Module size audit row if applicable.

## 9. PRD ↔ plan correctness check

| PRD section | Plan phase(s) | Status |
| --- | --- | --- |
| G1 — `sturm.h` umbrella | P3 | covered |
| G2 — opt-in headers | P4 | covered |
| G3 — mode build flag | P1 | covered |
| G4 — auto-injection | P7 | covered |
| G5 — qubit cap removal | P2 | covered |
| G6 — no-arg renderer | P5 | covered |
| §5.7 SIMULATE OOM clarity | P6 | covered |
| §5.8 migration | P8 | covered |
| A1 | P8 (`test_qram_demo_byte_identical`) | covered |
| A2 | P9 | covered |
| A3 | P9 | covered |
| A4 | P2.a (`test_no_cap_artifacts`) + P9 | covered |
| A5 | P3, P4 | covered |
| A6 | P7 (`test_main_lifecycle_no_auto`) | covered |
| A7 | P9 (doc grep) | covered |

## 10. Discrepancies between PRD and reality

To resolve before P2 begins:

- **Caller count.** PRD §5.5 estimates ~80 callers of
  `sturm_backend_create`; actual count via
  `grep -rln "sturm_backend_create" --include="*.cpp" --include="*.hpp" --include="*.h"`
  is **126** files. Plan budget assumes 126. Update PRD §5.5 in the
  P2.b commit.

- **Acceptance A4 wording.** PRD A4 forbids
  `kMaxClassicalQubits`-as-a-cap, but PRD §3 Non-goals keeps the
  symbol. Plan's `test_no_cap_artifacts` greps only the
  *cap-meaning* uses (`STURM_ANCILLA_CAPACITY`, `kCapExceededMsg`,
  `max 17`, `max_qubits` parameter). Recommend rewording A4 to
  "no surviving cap-enforcement code" before P9 lands.

- **§5.6 empty-IR diagram.** PRD says "return an empty diagram" —
  plan locks this to **empty string**, not a zero-rail canvas.
  Update PRD §5.6 in the P5 commit.

- **§5.7 OOM message destination.** PRD doesn't specify; plan locks
  to **`stderr`**. Update PRD §5.7 in the P6 commit.

- **§3 Non-goals — control-flow gaps.** Plan adds `setjmp`,
  `longjmp`, `exit()` to the auto-injection skip list as documented
  caveats. Recommend adding a one-line note in PRD §3 before P7
  lands.
