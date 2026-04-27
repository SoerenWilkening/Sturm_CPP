# Implementation Plan — Library Packaging & Public API Surface

**Status:** Draft (2026-04-27).
**Scope tag:** `packaging-export`.
**Companion PRD:** [`prd_packaging_export.md`](prd_packaging_export.md).

---

## Conventions (apply to every module)

- **TDD order:** write the failing test → make it pass → refactor. No code
  lands without a test that would catch its regression.
- **LOC budget:** ≤ 300 lines per source file (header or `.cpp`), measured with
  `wc -l`. If a unit grows past 300, split along an obvious seam (e.g. one
  matcher per file, one prelude alias group per file).
- **Build flag:** every step capped at `--parallel 6` / `-j6` per project
  policy.
- **bd issue per module:** each numbered module below is one bd issue. Epics
  group siblings.

---

## Epic E1 — Audit (PRD §3.1) — *blocks E2, E3*

### E1.M1 — emit-target enumeration script

- File: `tools/audit_emit_targets.py` (≤ 200 LOC).
- Walk `transpiler/src/matcher_*.cpp` and `transpiler/src/*_emitter.cpp`.
- For each file, regex-extract every `emit_*` / `os <<` / `replace_text` call
  that writes a symbol name (`add_mod`, `pow_mod`, `__sturm_*`, etc.).
- For each emitted symbol, grep `include/sturm/**` to locate its definition
  header.
- Output: TSV `(matcher_file, emitted_symbol, defining_header)`.
- **Tests:** `tests/tools/test_audit_emit_targets.py` — fixture transpiler-
  source snippets with known emit calls; assert TSV row count and contents.

### E1.M2 — `docs/transpiler_emit_targets.md`

- Deliverable doc, no LOC budget.
- Generated from E1.M1's TSV.
- Adds a column **classification:** `public` |
  `internal-public-template-dependency` | `move-to-algorithms`.
- Reviewer (human) fills classification; doc is checked in.
- **Acceptance gate:** doc exists; E1.M1 script is rerunnable in CI to flag
  drift (any matcher emitting a symbol not listed in the doc fails CI).

### E1.M3 — drift-check CI step

- ≤ 50 LOC shell.
- Re-runs E1.M1, diffs against committed TSV (the markdown's source-of-truth
  side-table), exits non-zero on diff.

---

## Epic E2 — Header reorganization (PRD §3.2) — *needs E1*

### E2.M1 — `include/sturm/detail/` directory + git mv plan

- ≤ 50 LOC of CMake changes.
- Per E1.M2 classifications, `git mv` every `internal-*` header into
  `include/sturm/detail/`.
- Update `#include` paths repo-wide via a single sed pass.
- Update `CMakeLists.txt` install rules to install both trees.
- **Tests:** existing test suite (`ctest --parallel 6`) must pass unchanged.
  Add `tests/packaging/test_detail_layout.cpp`: include only public headers,
  instantiate `qint<8>`, assert it compiles and links.

### E2.M2 — public header lint

- File: `tools/lint_public_headers.py` (≤ 150 LOC).
- For every header in `include/sturm/` (non-`detail/`), parse `#include` lines.
- Fail if a public header `#include`s a non-existent path, a `detail/` header
  from outside its own subtree's allowed list, or any header outside
  `include/sturm/`.
- Wire as a CI step.
- **Tests:** fixture headers with known violations; assert lint flags each.

---

## Epic E3 — Public surface headers (PRD §3.3, §3.4) — *needs E2*

### E3.M1 — `include/sturm/sturm.hpp` umbrella

- ≤ 100 LOC.
- Replace current minimal contents with explicit includes of every public
  header from E2 (qtypes, control, ops/qint_modular, routines/invert,
  uncompute_api).
- No `using` declarations.
- **Tests:** `tests/packaging/test_umbrella_only.cpp` — single TU includes
  only `<sturm/sturm.hpp>`, exercises `sturm::qint`, `sturm::qbool`, `WHEN`,
  `sturm::add_mod`, `sturm::invert`. Compiles and links.

### E3.M2 — `include/sturm/prelude.hpp`

- ≤ 30 LOC.
- Includes umbrella, then `using sturm::qint; using sturm::qbool;`.
- No free-function `using`.
- **Tests:** `tests/packaging/test_prelude.cpp` — TU uses unprefixed
  `qint`/`qbool` and verifies `add_mod` is *not* visible unprefixed
  (SFINAE/static-assert guard).

---

## Epic E4 — Versioning (PRD §3.8) — *parallel to E3*

### E4.M1 — `include/sturm/version.hpp.in`

- ≤ 30 LOC.
- `configure_file` template producing `STURM_VERSION_{MAJOR,MINOR,PATCH}` and
  `STURM_VERSION_STRING`.
- Source from top-level `project(sturm VERSION x.y.z …)`.
- Include from `sturm.hpp` (E3.M1).
- **Tests:** `tests/packaging/test_version.cpp` —
  `static_assert(STURM_VERSION_MAJOR >= 0)`, runtime check that the string
  matches CMake's `${PROJECT_VERSION}` (compiled in as a second macro fed
  through `-D` for the test only).

### E4.M2 — CMake config exports `sturm_VERSION`

- ≤ 20 LOC in `cmake/sturmConfig.cmake.in`.
- **Tests:** part of the smoke test (E6) — assert `${sturm_VERSION}`
  non-empty.

---

## Epic E5 — Driver + CMake function (PRD §3.5) — *parallel to E3, E4*

### E5.M1 — `add_sturm_executable` review

- ≤ 100 LOC delta to `cmake/SturmTranspile.cmake`.
- Audit current implementation; ensure it works against an *installed*
  prefix, not just in-tree.
- Use `$<TARGET_FILE:sturm::transpiler>` from exported targets.
- **Tests:** unit-style — call `add_sturm_executable` from a CMake script-
  mode harness with mocked transpiler; assert correct command line.

### E5.M2 — `sturmc` driver

- ≤ 200 LOC, Python preferred for portability.
- Args: input `.cpp`, `-o output`, `--cxx <compiler>`, `--include-dirs`,
  `--link sturm`.
- Runs transpiler → captures transpiled cpp → invokes `${CXX}` → links
  `libsturm`.
- Installed to `${CMAKE_INSTALL_BINDIR}/sturmc`.
- **Tests:** `tests/packaging/test_sturmc.py` — fixture `.cpp` source; run
  `sturmc` against installed prefix in a temp dir; assert binary runs and
  produces expected stdout. **Must run only after `cmake --install`.**

---

## Epic E6 — External smoke test (PRD §3.6) — *needs E3, E4, E5*

### E6.M1 — `tests/external_consumer/` skeleton

- ≤ 100 LOC CMake + ≤ 50 LOC source.
- Standalone `CMakeLists.txt` with `find_package(sturm REQUIRED)`.
- One source `main.cpp`: include `<sturm/prelude.hpp>`, build a routine
  using `qint`, `qbool`, `WHEN`, `sturm::add_mod`; print counter-mode gate
  count.
- **Tests:** **this project IS the test.** Build it via
  `add_sturm_executable`; run it; check stdout against a golden.

### E6.M2 — CI driver

- ≤ 100 LOC shell, `ci/run_external_smoke.sh`.
- `cmake --install` to a tmp prefix at `--parallel 6`.
- Configure + build `tests/external_consumer/` against that prefix at
  `--parallel 6`.
- Run the built binary; diff stdout vs. golden.
- Wire into the CI workflow.

### E6.M3 — second smoke variant: `sturmc` path

- ≤ 50 LOC shell.
- Same source, built via `sturmc` rather than the CMake function.
- Asserts both delivery paths in §3.5 work.

---

## Epic E7 — WHEN free-var mutation diagnostic (PRD §3.7) — *parallel to E2–E6*

### E7.M1 — read-set extractor

- Files: `transpiler/src/when_freevar_readset.{hpp,cpp}` (≤ 250 LOC).
- New utility that, given a `WHEN(expr) { body }` AST node, computes the
  set of `VarDecl`s read by `expr` (transitively through `CallExpr` callees,
  conservatively flagging through any function whose body the transpiler
  has not analyzed).
- **Tests:** `tests/transpiler/test_when_freevar_readset.cpp` — fixtures:
  literal expr (empty set), single-var, function-call, nested call. Pure
  unit, no diagnostic emission yet.

### E7.M2 — write-set checker

- Files: `transpiler/src/when_freevar_check.{hpp,cpp}` (≤ 250 LOC).
- Walks the body's `BinaryOperator` (assignment forms) and
  `CompoundAssignOperator` and `CXXOperatorCallExpr` for `^=`, etc.
- Emits hard-error diagnostic at the offending write's source location,
  naming the variable and the WHEN scope.
- **Tests:** `tests/transpiler/test_when_freevar_check.cpp` —
  golden-diagnostic fixtures: direct mutation, mutation via call, mutation
  of control variable, *negative* (mutation of body-local — must compile
  clean).

### E7.M3 — wire into matcher pipeline

- ≤ 100 LOC delta to `matcher_when_*` registration.
- Register E7.M2 as a hard-error pass; ensure it runs after WHEN scope is
  identified.
- **Tests:** integration — run the transpiler on each E7.M2 fixture
  end-to-end; assert exit code and diagnostic text.

---

## Epic E8 — Documentation (PRD §3.9) — *needs E1, E3*

### E8.M1 — `docs/public_api.md`

- Deliverable doc.
- Authoritative symbol list, generated from E1.M2's classifications +
  E3.M1's umbrella.
- **CI tie-in:** a script (≤ 80 LOC) re-emits the canonical list by parsing
  `sturm.hpp` includes and grepping for declarations; diffs against the
  doc; fails on drift.

### E8.M2 — `docs/getting_started.md`

- Deliverable doc.
- Mirrors `tests/external_consumer/` (E6.M1) line-for-line so the example
  is the test.
- **CI tie-in:** a `tools/extract_md_code_blocks.py` (≤ 80 LOC) extracts
  the first C++ block; diffs against the smoke-test source; fails on
  drift.

### E8.M3 — `README.md` updates

- Deliverable doc.
- Prereqs section (LLVM-17), install + consume flow, link to
  `getting_started.md` and `public_api.md`.

---

## Cross-cutting CI changes

- ≤ 80 LOC delta to the CI workflow.
- Order: lint → build (`-j6`) → ctest (`--parallel 6`) → install to prefix
  → external smoke (E6.M2, E6.M3) → drift checks (E1.M3, E2.M2, E8.M1,
  E8.M2).
- Every step capped at 6 threads.

---

## Dependency graph

```
E1 ──► E2 ──► E3 ──► E6 ◄── E5 ◄── (E4)
                              ▲
E7 (parallel, independent) ───┘
E8 needs E1 + E3
```

### Per-module test commands (template)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 6 --target <module-test-target>
ctest --test-dir build --parallel 6 -R <module-test-regex>
```

---

## bd issue layout

- 1 epic per `Epic E*`, 1 task per `M*`, dependencies as in the graph above.
- Priority: E1=P1 (blocker), E2/E3/E5/E6=P2, E4/E7/E8=P2, drift-check
  tasks P3.
