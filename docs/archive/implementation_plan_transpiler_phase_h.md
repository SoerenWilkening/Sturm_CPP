# Implementation Plan: Transpiler Phase H — Loops and Classical Control Flow

**Status:** Draft
**Date:** 2026-04-16
**Relates to:** `roadmap_transpiler_post_mvp.md` (Phase H), `01_principles.md` (P9, B6, B9)

## Context

Phase H of `roadmap_transpiler_post_mvp.md` covers classical `for` / `while` / `if` / `else` around quantum ops. Phases E–G's scope-based uncompute scheduler already handles the common case correctly: a braced loop body is a `CompoundStmt`, so a `qbool r = a | b;` declared inside the loop gets its `uncompute_or(...)` planted at the loop body's `}` — per-iteration, as the roadmap requires.

Phase H fills three concrete gaps:

1. **Braceless bodies are silently rejected today.** `transpiler/src/matcher_common.hpp:54-77`'s `enclosing_compound_stmt` walks up parents until it finds a `CompoundStmt`. For `for (...) qop;` / `if (c) qop;`, no `CompoundStmt` exists for the body, so every matcher's `if (!cs) return;` guard fires and the op is ignored. Users would get a transpiler that silently produces wrong gate streams for single-statement bodies.
2. **Outer-scoped variables mutated inside loop/branch/WHEN would require reverse-loop synthesis to uncompute correctly.** That violates **P9** ("routines are invertible by explicit adjoint" — the user writes the adjoint, not the transpiler) and is a large architectural project in its own right. Phase H rejects this pattern with a clear diagnostic.
3. **No snapshot / example / M12 coverage for control flow.** Phase H adds it so regressions show up immediately.

**Not in scope for Phase H:**
- Full per-variable liveness/escape analysis (Phase I's territory).
- Reverse-loop synthesis / Bennett's method (Phase M candidate; may never land).
- Changes to the "end-of-script shouldn't auto-uncompute" convention — already handled by the existing idiom of inner `{ ... }` blocks inside `main()`. No transpiler change needed.

## Sub-tasks

Each sub-task below is a standalone bd issue. `PH-0` is this document + the bd seeding itself (not a bd issue).

### PH-1 — Scope-finder refactor

Replace `enclosing_compound_stmt(...)` with `enclosing_scope(node, ctx)` that returns `{scope_anchor_stmt, kind}` where `kind ∈ {CompoundStmt, BracelessBody}`.

- `CompoundStmt` case: same as today — returns the enclosing `CompoundStmt`.
- `BracelessBody` case: the parent chain leads to a `ForStmt` / `WhileStmt` / `IfStmt` whose body (or `then` / `else`) is a non-compound Stmt and `node` is inside that body Stmt. Return the body Stmt itself.
- `find_or_create_scope` extended to key a synthetic `QScope` on the body Stmt's begin loc (instead of an lbrace). `open_brace = body_stmt->getBeginLoc()`, `close_brace = Lexer::getLocForEndOfToken(body_stmt->getEndLoc(), 0, sm, lang)` — just past the semicolon.
- All Phase A–G matchers switch from the old API to the new API. Every existing braced fixture stays byte-identical (the walker finds the same `CompoundStmt`); only previously-silently-rejected braceless contexts now succeed.

**Touches:** `transpiler/src/matcher_common.hpp`, every `transpiler/src/matcher_*.cpp`.
**Acceptance:** All pre-existing snapshot fixtures stay byte-identical. Unit tests exercise all four body shapes (braced for, braceless for, braced if, braceless if) plus at least one braced-WHEN to confirm the WHEN inner `if`s are NOT misidentified as braceless-body scopes.

### PH-2 — `matcher_brace_wrap.cpp` (new matcher module)

Auto-wrap braceless for/while/if/else bodies containing quantum ops.

- Match `forStmt(hasBody(stmt(unless(compoundStmt()))))`, `whileStmt(...)`, `ifStmt(hasThen(...))`, `ifStmt(hasElse(...))`.
- For each match where the body Stmt transitively contains a quantum op (heuristic: any `CXXOperatorCallExpr` on `qbool`/`qint`, any `varDecl` of quantum type, any WHEN macro expansion), schedule two `raw_insertions`:
  - `{` at `body_stmt->getBeginLoc()`.
  - `}` at `Lexer::getLocForEndOfToken(body_stmt->getEndLoc(), 0, sm, lang)`.
- Filter out macro-expanded inner `if`s (WHEN expands to nested `if`s): require `!body_loc.isMacroID()`.
- No interaction with Phase F/G WHEN brace handling: WHEN already requires `{ body }` as a precondition, so `WHEN(cond) qop;` is not a valid shape and PH-2 never targets it.

**Touches:** `transpiler/src/matcher_brace_wrap.cpp` (new), `transpiler/include/sturm/transpile/matcher.hpp` (register), `transpiler/src/main.cpp` (call register), `transpiler/CMakeLists.txt` (add TU).
**Acceptance:** Snapshot fixtures `for_intermediate_or_braceless`, `while_intermediate_or_braceless`, `if_then_intermediate_or_braceless`, `if_else_intermediate_or_braceless` round-trip to the expected braced + uncomputed output. Existing braced fixtures stay byte-identical.

### PH-3 — `matcher_outer_var_guard.cpp` (new matcher module)

Detect outer-scoped variables mutated inside loop/branch/WHEN and emit a diagnostic.

- Match the compound-assign kinds that mutate a named outer variable: `XOR_ASSIGN`, `ADD_ASSIGN_CONST`, `SUB_ASSIGN_CONST`, `MUL_ASSIGN_CONST`, `DIV_ASSIGN_CONST`, `ADD_ASSIGN_QINT`, `SUB_ASSIGN_QINT`, `MUL_ASSIGN_QINT`, `DIV_ASSIGN_QINT`, `MOD_ASSIGN_QINT`.
- For each match, walk up the parent chain. If a `ForStmt` / `WhileStmt` / `IfStmt` / WHEN-expanded `IfStmt` is encountered BEFORE the result's `decl_loc`'s enclosing `CompoundStmt`, the mutation is on an outer-scoped variable inside a loop/branch.
  - Detect WHEN via `is_expansion_of_macro(loc, sm, lang, "WHEN")` (already in `transpiler/src/matcher_common.hpp:107-125`).
- On match: emit `fprintf(stderr, ...)` diagnostic — `<file>:<line>:<col>: error: STURM: qbool/qint '<name>' (declared at <decl line>) is modified inside a for/while/if/WHEN body — automatic uncomputation would require reverse-loop synthesis. Provide a manual adjoint (P9) or restructure.\n`. Flag the `QOperation` with a new `skip_uncompute=true` boolean.
- `transpiler/src/uncompute_pass.cpp` (`render_uncompute`): skip ops with `skip_uncompute=true`, emit nothing. The op stays in the QIR for visibility and `dump()` renders it with a `[skip_uncompute]` suffix.
- Per-op flag, not per-scope — a scope can contain both "intermediate, uncompute normally" and "outer mutation, skipped" ops.

**Touches:** `transpiler/src/matcher_outer_var_guard.cpp` (new), `transpiler/include/sturm/transpile/qir.hpp` (add `bool skip_uncompute=false` on `QOperation`), `transpiler/src/qir.cpp` (dump suffix), `transpiler/src/uncompute_pass.cpp` (skip flagged ops), `transpiler/include/sturm/transpile/matcher.hpp` (register), `transpiler/src/main.cpp`, `transpiler/CMakeLists.txt`.
**Acceptance:** Fixture `for_outer_xor_reject` has expected output byte-identical to input (no uncompute injected); test harness asserts the PH-3 diagnostic appears on stderr. At least one mixed fixture exercises "inner intermediate + outer mutation in same scope" and confirms per-op flag granularity.

### PH-4 — Snapshot fixtures (normal path)

Add fixture pairs in `tests/transpiler/fixtures/`:

- `for_intermediate_or.{cpp,expected.cpp}` — `for(int i=0;i<n;i++) { qbool r = a | b; WHEN(r) { ... } }`, braced.
- `for_intermediate_or_braceless.{cpp,expected.cpp}` — single-statement body (exercises PH-2 brace wrap).
- `while_intermediate_or.{cpp,expected.cpp}`.
- `while_intermediate_or_braceless.{cpp,expected.cpp}`.
- `if_then_intermediate_or.{cpp,expected.cpp}`.
- `if_then_intermediate_or_braceless.{cpp,expected.cpp}`.
- `if_else_intermediate_or.{cpp,expected.cpp}` — distinct intermediates per branch.
- `if_else_intermediate_or_braceless.{cpp,expected.cpp}`.
- `for_outer_xor_reject.{cpp,expected.cpp}` — expected is byte-identical to input; runner asserts PH-3 stderr diagnostic.

Register in `tests/transpiler/CMakeLists.txt` following the Phase G `when_nested_*` pattern.

**Acceptance:** `ctest -R 'transpiler_snapshot_(for|while|if|outer_xor)'` — all pass.

### PH-5 — `examples/control_flow.cpp` (end-to-end example)

Single example file demonstrating a for-loop with per-iteration intermediate OR, plus an `if/else` with distinct intermediates per branch. Matches the Phase G `examples/nested_when.cpp` pattern (one example per phase, multiple shapes inside).

- `examples/CMakeLists.txt`: `add_quantum_executable(example_control_flow ...)` following existing Phase G registration.
- `tests/transpiler/CMakeLists.txt`: three CTests — `build_example_control_flow`, `transpiler_example_control_flow_injected`, `transpiler_idempotent_example_control_flow` — following the Phase G pattern.
- `tests/transpiler/check_example_control_flow.cmake` (new) mirrors `check_example_nested_when.cmake`.

**Acceptance:** `ctest -R 'transpiler_example_control_flow'` — injected + idempotent both pass. Manual: `./build/examples/example_control_flow` prints a sensible ASCII diagram.

### PH-6a — M12 for-loop pair

- `tests/transpiler/fixtures/for_loop_runtime.cpp` + `for_loop_reference.cpp`. Namespaces `m12_for_loop_transpiled::demo(...)` and `m12_for_loop_reference::demo(...)`.
- Wire into `tests/transpiler/CMakeLists.txt:1210-1280` (replicating the `nested_when_runtime`/`reference` pattern) and `test_gate_equivalence.cpp` main() (call both `demo`s, byte-compare `GateRecord` vectors).

**Acceptance:** `ctest -R 'test_gate_equivalence'` — byte-identical `GateRecord` streams.

### PH-6b — M12 if/else pair

- `if_branches_runtime.cpp` + `if_branches_reference.cpp`. Same harness plumbing as PH-6a.
- Rationale for splitting from PH-6a: byte-compare failures localize easier per control-flow kind.

**Acceptance:** `ctest -R 'test_gate_equivalence'` — byte-identical `GateRecord` streams for both pairs.

### PH-7 — Roadmap completion note + bd memory + commit

- Inline completion note in `docs/roadmap_transpiler_post_mvp.md` (Phase H section), mirroring the Phase G note: bd issue IDs, fixtures added, matcher modules added, roadmap pointer to Phase I.
- `bd remember` an entry capturing the "reverse-loop synthesis is out of scope; PH-3 diagnostic is the boundary" rule for future sessions.
- Archive this plan to `docs/archive/implementation_plan_transpiler_phase_h.md` and remove the entry from `CLAUDE.md` Required Reading (it's not a session dependency once archived).

## Dependency graph

```
PH-0 ──> PH-1 ──┬──> PH-2 ──┐
                │           ├──> PH-4 (fixtures) ──┐
                └──> PH-3 ──┤                      │
                            ├──> PH-5 (example) ──┼──> PH-7
                            │                     │
                            ├──> PH-6a (M12 for) ─┤
                            │                     │
                            └──> PH-6b (M12 if) ──┘
```

PH-2 and PH-3 run in parallel after PH-1. PH-4, PH-5, PH-6a, PH-6b run in parallel after PH-1/PH-2/PH-3.

## Critical files

- `transpiler/src/matcher_common.hpp` — scope-finder refactor (PH-1), reuse `is_expansion_of_macro` (PH-3).
- `transpiler/src/matcher_brace_wrap.cpp` — NEW (PH-2).
- `transpiler/src/matcher_outer_var_guard.cpp` — NEW (PH-3).
- `transpiler/src/matcher_{qbool_assign,qbool_bitwise,qbool_compound,qint_compare,qint_const,qint_qint,when_lift,when_nested}.cpp` — switch to `enclosing_scope` API (PH-1). No behavior change on braced fixtures.
- `transpiler/src/uncompute_pass.cpp` — skip ops with `skip_uncompute=true` (PH-3).
- `transpiler/src/qir.cpp` — `dump()` renders `[skip_uncompute]` suffix (PH-3).
- `transpiler/include/sturm/transpile/qir.hpp` — add `bool skip_uncompute=false` on `QOperation` (PH-3).
- `transpiler/include/sturm/transpile/matcher.hpp` — register `register_brace_wrap_matcher`, `register_outer_var_guard_matcher` (PH-2, PH-3).
- `transpiler/src/main.cpp` — call the two new registers in `TranspileConsumer` constructor.
- `transpiler/CMakeLists.txt` — add the two new TUs to the source list.
- `tests/transpiler/fixtures/*.cpp` + `*.expected.cpp` — new fixtures (PH-4, PH-6a, PH-6b).
- `tests/transpiler/CMakeLists.txt` — register fixtures + M12 pairs (PH-4, PH-6a, PH-6b).
- `tests/transpiler/test_gate_equivalence.cpp` — add for-loop + if-else demo calls in `main()` (PH-6a, PH-6b).
- `examples/control_flow.cpp` — NEW (PH-5).
- `examples/CMakeLists.txt` — register example (PH-5).
- `tests/transpiler/check_example_control_flow.cmake` — NEW (PH-5).
- `docs/roadmap_transpiler_post_mvp.md` — Phase H completion note (PH-7).

## Reused building blocks

- `transpiler/src/matcher_common.hpp:107-125` — `is_expansion_of_macro` for WHEN barrier detection in PH-3.
- `transpiler/src/matcher_common.hpp:150-168` — `compute_post_body_brace` (post-WHEN-body close-brace resolver); PH-3's WHEN-scope detection uses the same approach to find a WHEN's body Stmt.
- `transpiler/include/sturm/transpile/qir.hpp:156` — `insert_before_override` pattern as precedent for a new per-op flag (`skip_uncompute`) modulating the uncompute pass.
- `transpiler/include/sturm/transpile/qir.hpp:214-217` — `UncomputeInsertion` via `QUnit::raw_insertions` — PH-2 reuses this to schedule brace insertions verbatim.
- `Lexer::getLocForEndOfToken` (already used in `transpiler/src/matcher_common.hpp:167`) — PH-1 synthetic scope close_brace + PH-2 `}` insertion anchor.
- Existing `nested_when_runtime` / `nested_when_reference` M12 pattern at `tests/transpiler/CMakeLists.txt:1210-1280` — PH-6a/6b replicate verbatim.

## Verification

End-to-end after all sub-tasks land:

```bash
# 1. Full build.
cmake --build build --target sturm-transpile

# 2. Phase H snapshot fixtures — all pass.
ctest --test-dir build -L transpiler -R 'transpiler_snapshot_(for|while|if|outer_xor)'

# 3. Phase H example + idempotency.
ctest --test-dir build -R 'transpiler_example_control_flow'
ctest --test-dir build -R 'transpiler_idempotent_example_control_flow'

# 4. M12 gate-equivalence (byte-identical GateRecord streams).
ctest --test-dir build -R 'test_gate_equivalence'

# 5. Phase A–G regression — every prior snapshot / example stays green (byte-identical expected files).
ctest --test-dir build -L transpiler

# 6. Manual sanity: run the example and inspect the ASCII diagram.
./build/examples/example_control_flow

# 7. PH-3 diagnostic smoke test — point sturm-transpile at a fixture with an outer-scoped XOR_ASSIGN
#    inside a for-loop, observe stderr message.
./build/transpiler/sturm-transpile tests/transpiler/fixtures/for_outer_xor_reject.cpp \
    --output-dir /tmp/sturm_test 2>&1 | grep 'STURM: qbool'
```

## Sharp edges / risks

- **PH-1 synthetic scope + PH-2 brace insertions at same SourceLocation.** The uncompute pass's `InsertTextBefore(close_brace, uncompute_code)` and PH-2's `InsertTextBefore(close_brace, "}")` both target the body stmt's post-semicolon loc. Clang's `Rewriter::InsertTextBefore` stacks repeated calls at the same loc — order matters. Resolution: PH-2's `}` insertion must be appended AFTER the uncompute pass completes (i.e. scheduled via `raw_insertions` which the synthesize pass concatenates at the END of its insertion vector, per `transpiler/src/uncompute_pass.cpp:291-293`). This is already the order in `synthesize()`; PH-2 just needs to ensure its record goes into `raw_insertions` and not into an op's scope.
- **PH-3 WHEN barrier detection.** The Phase F/G WHEN macro expands to three nested `if`s, so a naive parent walk sees multiple `IfStmt`s that are NOT user-written. Use `is_expansion_of_macro(stmt->getBeginLoc(), sm, lang, "WHEN")` to collapse the chain to the single outermost user-visible WHEN body — the same mechanism `transpiler/src/matcher_when_lift.cpp` uses.
- **Two-pass transpile alternative.** If synthetic-scope + brace-insertion ordering proves fiddly in practice, fall back to running the matchers twice: pass 1 applies only PH-2 brace insertions via a standalone `RefactoringTool`, pass 2 runs all other matchers on the braced intermediate source. Adds ~150 LOC to `main.cpp` and doubles parse time. Document as the backup approach; commit to one-pass first.
- **Fixture regression surface from PH-1.** Every Phase A–G matcher switches to `enclosing_scope`. Regression guarantee: for braced bodies, the new helper's `CompoundStmt` branch returns exactly what `enclosing_compound_stmt` returned. Unit-test PH-1 with a fixture that exercises all four body shapes (braced for, braceless for, braced if, braceless if) plus at least one braced-WHEN to confirm the WHEN inner `if`s are NOT misidentified as braceless-body scopes.
