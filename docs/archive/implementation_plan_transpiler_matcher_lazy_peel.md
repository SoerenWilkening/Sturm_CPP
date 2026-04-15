# Implementation Plan: Matcher Peel-Through for Lazy OR Expressions

**PRD:** `docs/prd_transpiler_matcher_lazy_peel.md`
**Predecessor:** `docs/archive/implementation_plan_transpiler_uncompute.md` (MVP complete)
**Principles:** `docs/01_principles.md` (esp. **B1b**, **B6**, **P9**)
**Roadmap placement:** pre-Phase A; unblocks observability of the transpiler on `examples/or_circuit.cpp`
**Approach:** Modular, strict TDD. Each module has a LOC budget of **≤ 300 lines** across all files it touches (including its own tests). Each module lands as one commit; each ships with its tests green.

---

## Goal

Widen `register_or_matcher` so that the transpiler fires on `qbool X = a | b;` under **both** initializer shapes:

1. **Eager** (`operator|` returns `qbool`) — current MVP.
2. **Lazy** (`operator|` returns `OrExpr<qbool>`, materialized via `OrExpr<qbool>::operator qbool()`) — triggered by `STURM_BACKEND_ENABLED`.

Evidence of success: after a clean build, `build/sturm_gen/examples/or_circuit.cpp` contains exactly one injected `uncompute_or(c, a, b);` before the closing `}` of `main()`.

---

## Scope guard

- **Only** `transpiler/src/matcher.cpp` changes under `transpiler/src/`. Anything else in `transpiler/src/` is out of scope.
- **Zero** changes under `include/sturm/` (runtime headers). If a module finds itself editing runtime code, STOP and re-scope.
- **Zero** changes to `transpiler/src/qir.*`, `emitter.cpp`, `uncompute_pass.cpp`, or `transpiler/include/sturm/transpile/matcher.hpp` (public API stable).
- Test infrastructure (`tests/transpiler/`) is the only other area that gets new files.

---

## Module dependency graph

```
LP1  Baseline verification           (0 LoC — reproduce the bug)
  │
LP2  AST calibration                 (0 LoC landed — disposable diagnostic)
  │
LP3  Lazy-path fixture (RED)         ~130 LoC new fixture + CMake wiring
  │
LP4  Matcher widening (GREEN)        ~35 LoC matcher edit
  │
  ├── LP5  Example observability     ~55 LoC new CMake test
  │
  ├── LP6  Idempotency guard         ~40 LoC new CMake test
  │
  └── LP7  Gate-stream equivalence   ≤ 60 LoC new runtime test (or reuse)
             │
LP8  Legacy-flag regression          (0 LoC — CI run only)
  │
LP9  Docs + archive                  ~15 LoC doc churn
```

LP5/LP6/LP7 may proceed in parallel after LP4. Everything else is strictly sequential.

---

## TDD Discipline

Every module follows: **red → green → refactor**.

1. **LP3 is explicitly the red step**: the fixture + test ship with the matcher *unchanged*. ctest must report `snapshot_or_single_backend` FAIL before LP4 starts.
2. **LP4 flips it green** with the minimum matcher change that also preserves `snapshot_or_single`.
3. **LP5, LP6, LP7** each add one independent failing assertion first, then make it pass.
4. Every module must leave the full suite green in both:
   - `STURM_TRANSPILE=ON` (transpile path under test).
   - `STURM_TRANSPILE=OFF; STURM_AUTO_UNCOMPUTE=ON` (legacy RAII path, regression gate).
5. The harness is the same as MVP: CMake-only driver (`tests/transpiler/run_snapshot.cmake`), byte-level `compare_files`, unified diff on mismatch. No googletest, no catch2.

---

## LP1 — Baseline verification

**Goal:** Prove the bug exists and the harness is healthy BEFORE writing code. This is the frame for every downstream assertion.

**Files touched:** none.

**Steps:**
```bash
cmake -S . -B build -DSTURM_TRANSPILE=ON -DSTURM_AUTO_UNCOMPUTE=OFF
cmake --build build --target sturm-transpile
ctest --test-dir build -R snapshot_or_single -V

cmake --build build --target example_or_circuit
diff examples/or_circuit.cpp build/sturm_gen/examples/or_circuit.cpp
```

**Exit criterion:**
- `snapshot_or_single` is GREEN (eager path works).
- `build/sturm_gen/examples/or_circuit.cpp` is **byte-identical** to `examples/or_circuit.cpp` — confirming the matcher misses the lazy path on the real example. This is the regression LP4 will cure.

**Why this is a module:** without a verified-failing baseline, LP4's "it works!" carries no information. If LP1 unexpectedly shows the example already rewritten, the PRD's premise is stale and the whole plan must be re-examined.

---

## LP2 — AST calibration (disposable)

**Goal:** Observe the actual Clang AST shape that the widened matcher needs to peel, before writing the matcher. De-risks LP4.

**Files touched:** none landed. Work happens in `/tmp/`.

**Steps:**
1. Write a self-contained probe to `/tmp/ast_probe.cpp` that mirrors the minimal lazy shape (mock `OrExpr<qbool>` with user-defined conversion + free `operator|`).
2. Dump the AST:
   ```bash
   clang++ -std=c++20 -Xclang -ast-dump -fsyntax-only /tmp/ast_probe.cpp 2>&1 \
     | sed -n '/FunctionDecl.*demo/,/^`-/p'
   ```
3. Record the actual nesting for `VarDecl tmp`. Specifically answer:
   - Does `MaterializeTemporaryExpr` sit between `CXXConstructExpr` and `CXXMemberCallExpr`?
   - Is the user-defined conversion expressed as `CXXMemberCallExpr` (method call on the temporary) or folded into an `ImplicitCastExpr` with `CK_UserDefinedConversion`?
   - What implicit-cast nodes wrap the inner `CXXOperatorCallExpr`?

**Exit criterion:** the AST dump is pasted into the LP4 commit message (or the PR description). LP4's pattern is designed against this evidence, not against the PRD's hypothetical tree.

**Why this is a module:** ~30 LoC of matcher pattern can fail silently if the assumed AST shape is slightly wrong. A 10-minute calibration is cheap insurance against a debug session that starts with "the pattern *should* match…"

---

## LP3 — Lazy-path fixture (RED)

**Goal:** Land a failing snapshot test that reproduces the matcher miss hermetically.

**Files touched (max ~130 LoC total):**

| File | Kind | LoC |
|---|---|---|
| `tests/transpiler/fixtures/or_single_backend.cpp` | NEW | ~55 |
| `tests/transpiler/fixtures/or_single_backend.expected.cpp` | NEW (placeholder until LP4) | ~65 |
| `tests/transpiler/CMakeLists.txt` | MODIFY | +10 |

**Input fixture** (`or_single_backend.cpp`) — mirrors the real `lazy_expr.hpp` shape minimally:
```cpp
// Hermetic fixture — lazy path: operator| returns OrExpr<qbool>,
// materialized to qbool via user-defined conversion.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
};

template<class T>
struct OrExpr {
    const T& a;
    const T& b;
    OrExpr(const T& a_, const T& b_) : a(a_), b(b_) {}
    operator T() const { return T{}; }   // user-defined conversion
};

inline OrExpr<qbool> operator|(const qbool& a, const qbool& b) {
    return OrExpr<qbool>{a, b};
}
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b) { qbool tmp = a | b; }
```

**Expected output** (`or_single_backend.expected.cpp`): same body, with `uncompute_or(tmp, a, b);` inserted before the closing `}` of `demo`, plus the generated-file header the M9 emitter prepends. **Do not hand-author this file in LP3** — commit a placeholder (e.g. one-line comment) so the test is guaranteed to fail. LP4 produces the real expected output by capturing the matcher's actual output and freezing it.

**CMake** (`tests/transpiler/CMakeLists.txt`, append, mirroring the existing `snapshot_or_single` entry):
```cmake
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_or_single_backend
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_backend.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_backend.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/or_single_backend
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_or_single_backend PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()
```

**Exit criterion:**
- `ctest -R snapshot_or_single_backend` **FAILS** with a non-empty unified diff.
- `ctest -R snapshot_or_single` **PASSES** (no regression in the eager-path fixture).

---

## LP4 — Matcher widening (GREEN)

**Goal:** Flip LP3 green by widening the matcher to cover the lazy initializer shape. Keep `OrCallback::run` untouched.

**Files touched:**
- `transpiler/src/matcher.cpp` — only the `pattern` expression inside `register_or_matcher` (currently lines 161–168). Net diff ~35 LoC.

**Proposed pattern** (exact shape contingent on LP2's AST evidence — the sketch below assumes the PRD's hypothesized tree):

```cpp
using namespace clang::ast_matchers;

// Inner operand-binding shape: `a | b` as CXXOperatorCallExpr.
// declRefExpr bindings are wrapped in ignoringImplicit so that
// lvalue-to-rvalue casts do not hide the operand.
auto or_call = cxxOperatorCallExpr(
    hasOverloadedOperatorName("|"),
    argumentCountIs(2),
    hasArgument(0, ignoringImplicit(declRefExpr().bind("lhs"))),
    hasArgument(1, ignoringImplicit(declRefExpr().bind("rhs"))));

// Eager shape: VarDecl's initializer IS the CXXOperatorCallExpr
// (modulo implicit glue).
auto eager_init = ignoringImplicit(or_call);

// Lazy shape: VarDecl's initializer is a qbool CXXConstructExpr
// whose 0th argument is the user-defined conversion call
// OrExpr<qbool>::operator qbool() whose implicit object is the
// CXXOperatorCallExpr we care about.
auto lazy_init = ignoringImplicit(cxxConstructExpr(
    hasArgument(0, ignoringImplicit(
        cxxMemberCallExpr(
            on(ignoringImplicit(or_call)))))));

auto pattern = varDecl(
    hasType(cxxRecordDecl(hasName("qbool"))),
    hasInitializer(anyOf(eager_init, lazy_init))
).bind("var");
```

**Callback invariant:** `"lhs"`, `"rhs"`, and `"var"` are bound by both `anyOf` branches (the lhs/rhs come from the shared `or_call`; `var` comes from the outer `varDecl`). `OrCallback::run` reads exactly these three names and needs **no** modification. This is the central design invariant; violating it means the module has drifted.

**Post-write step — produce the expected fixture output:**
1. Run `sturm-transpile tests/transpiler/fixtures/or_single_backend.cpp --output-dir /tmp/lp4_out`.
2. Read `/tmp/lp4_out/.../or_single_backend.cpp`. Confirm exactly one `uncompute_or(tmp, a, b);` before `}`. Confirm the file header is the canonical M9 "AUTO-GENERATED" banner.
3. Copy that file verbatim to `tests/transpiler/fixtures/or_single_backend.expected.cpp` (replace the LP3 placeholder).
4. Re-run `ctest -R snapshot`.

**Optional hardening** (Risk R3 mitigation, ~5 LoC): in `OrCallback::run`, add a debug-build `assert` that the bound `VarDecl`'s location has not already been emitted to the current QScope. This catches the unlikely but possible case of `anyOf` firing twice on one declaration.

**Exit criterion:**
- `snapshot_or_single_backend` **PASSES**.
- `snapshot_or_single` still **PASSES** (eager path unregressed).
- `transpiler/src/matcher.cpp` total LoC ≤ 215 (baseline 175 + budget 40).
- No other file under `transpiler/src/` changed. No runtime header changed.

---

## LP5 — Example observability

**Goal:** Pin the PRD's headline acceptance (`examples/or_circuit.cpp` is actually rewritten after a clean build) to a regression test. This is what LP4 was for.

**Files touched (~55 LoC):**
- NEW `tests/transpiler/check_example_or_circuit.cmake` (~30)
- MODIFY `tests/transpiler/CMakeLists.txt` (+10)
- MODIFY `examples/CMakeLists.txt` (+5, optional: ensure `example_or_circuit` is built before the test runs; may already be wired)

**Check script** (`check_example_or_circuit.cmake`):
```cmake
# Post-build assertion: sturm-transpile injected uncompute_or(c, a, b)
# into the generated sibling of examples/or_circuit.cpp, and did NOT
# contaminate the source file.

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR "Generated file missing: ${GENERATED}")
endif()

file(READ "${GENERATED}" content)
string(REGEX MATCH
    "uncompute_or[ \t]*\\([ \t]*c[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    hit "${content}")
if(NOT hit)
    message(FATAL_ERROR "uncompute_or(c, a, b) NOT found in ${GENERATED}")
endif()

file(READ "${SOURCE}" src_content)
string(REGEX MATCH "uncompute_or" src_hit "${src_content}")
if(src_hit)
    message(FATAL_ERROR
        "uncompute_or appeared in SOURCE ${SOURCE}; transpiler contaminated input")
endif()
```

**CMake registration:**
```cmake
if(STURM_TRANSPILE)
    add_test(
        NAME transpiler_example_or_circuit_injected
        COMMAND "${CMAKE_COMMAND}"
                -DSOURCE=${CMAKE_SOURCE_DIR}/examples/or_circuit.cpp
                -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/or_circuit.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_or_circuit.cmake)
    set_tests_properties(transpiler_example_or_circuit_injected PROPERTIES
        LABELS "transpiler"
        DEPENDS "snapshot_or_single_backend")
endif()
```

**Pre-run hook:** since the test assertion needs the generated file to exist, either (a) add a `DEPENDS` on a build-of-example fixture test, or (b) document that the test requires `cmake --build build --target example_or_circuit` first (CI can just do that).

**Exit criterion:**
- `cmake --build build --target example_or_circuit && ctest -R transpiler_example_or_circuit_injected` passes.
- `diff examples/or_circuit.cpp build/sturm_gen/examples/or_circuit.cpp` is non-empty.
- Grep of the generated file hits `uncompute_or(c, a, b)` **exactly once**.

---

## LP6 — Idempotency guard

**Goal:** Re-running the transpiler on its own output must produce a byte-identical file. PRD acceptance criterion #4.

**Files touched (~40 LoC):**
- NEW `tests/transpiler/check_idempotent.cmake` (~25)
- MODIFY `tests/transpiler/CMakeLists.txt` (+15, two test registrations)

**Check script** (`check_idempotent.cmake`):
```cmake
# Run sturm-transpile on a previously-generated file; assert byte equality.
execute_process(
    COMMAND "${TRANSPILE_BIN}" "${INPUT}" --output-dir "${OUTPUT_DIR}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sturm-transpile failed: rc=${rc}")
endif()

file(RELATIVE_PATH rel "${INPUT_BASE}" "${INPUT}")
set(regen "${OUTPUT_DIR}/${rel}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${INPUT}" "${regen}"
    RESULT_VARIABLE diff_rc)
if(NOT diff_rc EQUAL 0)
    message(FATAL_ERROR "Idempotency failure: ${INPUT} != ${regen}")
endif()
```

**Register two idempotency tests:**
- `transpiler_idempotent_or_single_backend` — on the snapshot test's generated output.
- `transpiler_idempotent_example_or_circuit` — on `build/sturm_gen/examples/or_circuit.cpp`.

**Exit criterion:** both tests green.

---

## LP7 — Gate-stream equivalence (re-assertion)

**Goal:** The compiled `example_or_circuit` under `STURM_TRANSPILE=ON; STURM_AUTO_UNCOMPUTE=OFF` produces the same gate stream as a hand-written reference using explicit `uncompute_or(c, a, b);`. PRD acceptance criterion #5.

**Pre-requisite — reuse, don't duplicate:** the `tests/transpiler/fixtures/` directory already contains `or_single_reference.cpp` and `or_single_runtime.cpp` (listed in LP0-gathered context). Before writing any new code, read those files and determine whether they implement an equivalence harness that can be extended to `or_circuit`. If yes, **extend**; if no, write a minimal new one.

**Files touched (budget ≤ 60 LoC, possibly 0 if reuse works):**
- Likely NEW `tests/transpiler/example_or_circuit_equivalence.cpp` (~50 LoC): builds two circuit-mode runs (transpiler-generated `example_or_circuit` and a hand-written reference), captures primitive streams, asserts sequence + operand equality.
- MODIFY `tests/transpiler/CMakeLists.txt` (+10).

**Exit criterion:** equivalence test green. Same gate count, same order, same operands.

---

## LP8 — Legacy-flag regression

**Goal:** Confirm `STURM_AUTO_UNCOMPUTE=ON; STURM_TRANSPILE=OFF` still green (legacy RAII path, PRD acceptance #6).

**Files touched:** none.

**Steps:**
```bash
rm -rf build-legacy
cmake -S . -B build-legacy -DSTURM_AUTO_UNCOMPUTE=ON -DSTURM_TRANSPILE=OFF
cmake --build build-legacy
ctest --test-dir build-legacy --output-on-failure
```

**Exit criterion:** all legacy tests pass. If any fail, the widening broke something it shouldn't have — return to LP4 and re-audit.

---

## LP9 — Docs + archive

**Goal:** Retire this PRD and plan now that the work is complete.

**Files touched (~15 LoC total diff):**
- MOVE `docs/prd_transpiler_matcher_lazy_peel.md` → `docs/archive/prd_transpiler_matcher_lazy_peel.md`
- MOVE `docs/implementation_plan_transpiler_matcher_lazy_peel.md` → `docs/archive/implementation_plan_transpiler_matcher_lazy_peel.md`
- MODIFY `CLAUDE.md` — remove the PRD's line under **Required Reading** (per CLAUDE.md's own rule: archived docs must not be session dependencies).
- OPTIONAL: append a one-line note to `docs/roadmap_transpiler_post_mvp.md` under "Phase K" rationale noting the matcher already peels lazy `|` so Phase K need not re-audit the eager path.

**Exit criterion:** `grep -rn "prd_transpiler_matcher_lazy_peel\|implementation_plan_transpiler_matcher_lazy_peel" docs/ CLAUDE.md` only hits the archive paths. `CLAUDE.md`'s Required Reading list contains no stale entries.

---

## Verification matrix

| PRD acceptance | Binding check | Module(s) | ctest filter |
|---|---|---|---|
| #1 Eager snapshot unchanged | `or_single.expected.cpp` byte-match | LP1, LP3, LP4 | `-R snapshot_or_single$` |
| #2 Lazy snapshot passes | `or_single_backend.expected.cpp` byte-match | LP3, LP4 | `-R snapshot_or_single_backend` |
| #3 Example rewritten | Generated file contains `uncompute_or(c, a, b)` | LP5 | `-R transpiler_example_or_circuit_injected` |
| #4 Idempotent | Re-transpile byte-equal | LP6 | `-R transpiler_idempotent` |
| #5 Gate-stream equivalent | Runtime diff | LP7 | `-R or_circuit_equivalence` |
| #6 Legacy not regressed | Full suite under legacy flags | LP8 | `ctest --test-dir build-legacy` |
| #7 Eager unaffected | Single match per VarDecl | LP4 (debug assertion) | covered by #1 |

---

## Risks & mitigations

**R1 — Mock `OrExpr` AST differs from the real one.** A hermetic mock could produce a subtly different tree than `include/sturm/qtypes/lazy_expr.hpp`, letting LP3+LP4 pass while `examples/or_circuit.cpp` still isn't rewritten. *Mitigation:* LP5 is a canary for exactly this divergence. If LP3+LP4 green but LP5 red, the mock is lying — widen the mock (or fall back to including the real header under `STURM_BACKEND_ENABLED=1` in the fixture).

**R2 — `ignoringImplicit` doesn't peel `MaterializeTemporaryExpr`.** Clang's matcher surface treats this node specially across versions. *Mitigation:* LP2's AST dump decides this empirically. If the dump shows `MaterializeTemporaryExpr` inline, add an explicit `materializeTemporaryExpr(has(...))` peel in LP4.

**R3 — Double match under `anyOf`.** Overload resolution makes this impossible (eager returns `qbool`, lazy returns `OrExpr<qbool>`; a single overload set cannot produce both for one call). *Mitigation:* cheap debug-build `assert` in `OrCallback::run`, added in LP4.

**R4 — Stale `build/sturm_gen/examples/or_circuit.cpp` masks a true failure.** A cached rewrite from a prior LP4 attempt could make LP5 pass for the wrong reason. *Mitigation:* LP5's test should either `file(REMOVE ${GENERATED})` before invoking (via an extra pre-step) or confirm that `cmake/SturmTranspile.cmake` tracks the `sturm-transpile` binary as a dependency of the generated file (read that CMake module during LP5 to verify; if missing, add the dependency).

---

## Out of scope (deferred)

- **`operator&` / `AndExpr`.** Roadmap Phase A. The matcher keeps `hasOverloadedOperatorName("|")`; do not generalize here.
- **Deletion of `lazy_expr.hpp`.** Roadmap Phase K. The widened matcher continues to work post-Phase K because the eager branch of `anyOf` still fires when `operator|` returns `qbool` directly.
- **New `QOpKind`s or new `uncompute_*` runtime functions.** Emitter and IR are unchanged.
- **Formatting of the injected call.** Inherited from the M9 emitter.
- **In-memory transpile / alternative backends / source maps.** Roadmap Phase M.

---

## Pre-implementation checklist

Before claiming LP1:
- [ ] `cmake -S . -B build -DSTURM_TRANSPILE=ON` succeeds (LLVM/Clang dev headers discoverable).
- [ ] `ctest -R snapshot_or_single` green on current HEAD.
- [ ] `build/sturm_gen/examples/or_circuit.cpp` exists and is byte-identical to source (the bug we're fixing).
- [ ] `bd` issues filed for LP3 through LP9 (LP1, LP2, LP8 are one-shot verification steps; no separate issues needed unless you want a paper trail).

---

## Estimated total diff

| Module | Kind | LoC |
|---|---|---|
| LP3 | Fixture `or_single_backend.cpp` | ~55 new |
| LP3 | Fixture `or_single_backend.expected.cpp` | ~65 new |
| LP3 | CMake — snapshot registration | ~10 new |
| LP4 | `transpiler/src/matcher.cpp` — widening + optional assert | ~35 modify |
| LP5 | `check_example_or_circuit.cmake` + CMake wiring | ~55 new |
| LP6 | `check_idempotent.cmake` + CMake wiring | ~40 new |
| LP7 | Equivalence test (or 0 if reuse) | 0–60 new |
| LP9 | Doc moves + CLAUDE.md trim | ~15 modify |
| **Total** | | **~235–320 new + ~50 modify** |
| **Largest single module** | LP3 | **~130 LoC** |

Well under the 300 LoC per-module cap. Largest risk is over-budget if LP7 needs a brand-new equivalence harness; if so, split LP7 into LP7a (harness scaffolding) and LP7b (this test) — but only if `or_single_runtime.cpp` genuinely cannot be extended.
