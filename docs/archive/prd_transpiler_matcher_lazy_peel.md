# PRD: Matcher Peel-Through for Lazy OR Expressions

**Status:** Not Started
**Date:** 2026-04-15
**Predecessors:** `archive/prd_transpiler_uncompute.md` (MVP complete)
**Relates to:** `roadmap_transpiler_post_mvp.md`

## Context

The transpiler MVP (`archive/prd_transpiler_uncompute.md`) is green. Its single AST pattern fires on:

```cpp
qbool tmp = a | b;
```

and injects `uncompute_or(tmp, a, b);` before the enclosing scope's close brace. Acceptance tests pass on `tests/transpiler/fixtures/or_single.cpp`.

**The MVP matcher silently misses the one place we want to observe it working: `examples/or_circuit.cpp`.**

Root cause: `examples/or_circuit.cpp:1` defines `STURM_BACKEND_ENABLED 1`. Under that flag, `operator|(const qbool&, const qbool&)` at `include/sturm/qtypes/lazy_expr.hpp:88` returns `OrExpr<qbool>`, **not** `qbool`. A `qbool` is then produced by `OrExpr<qbool>::operator qbool() const` at `include/sturm/qtypes/qbool_ops.hpp:203`, a user-defined conversion.

The resulting AST for `qbool c = a | b;` under the backend flag is:

```
VarDecl 'c'  (type qbool)
└─ CXXConstructExpr         (qbool copy/move ctor)
   └─ … implicit glue …     (MaterializeTemporaryExpr / ImplicitCastExpr)
      └─ CXXMemberCallExpr   (OrExpr<qbool>::operator qbool)
         └─ implicit object:
            CXXOperatorCallExpr '|'
            ├─ DeclRefExpr 'a'
            └─ DeclRefExpr 'b'
```

The current matcher (`transpiler/src/matcher.cpp:161-168`) binds `hasInitializer(cxxOperatorCallExpr(...))` **directly**, so the extra `CXXConstructExpr → CXXMemberCallExpr` layer causes the pattern to miss. No `uncompute_or` is injected. `build/sturm_gen/examples/or_circuit.cpp` is byte-identical to the source.

This is a matcher problem, not a runtime problem. `lazy_expr.hpp` is listed for deletion in Phase K of the roadmap, but Phase K is blocked on Phases A–J. Widening the matcher to peel through the lazy path unblocks the example now, and the widened matcher continues to work unchanged after Phase K removes the lazy types.

## Goals

1. The transpiler fires on `qbool X = a | b;` **regardless of whether `operator|` returns an owning `qbool` (eager path) or an `OrExpr<qbool>` (lazy path)**.
2. After a clean build, `build/sturm_gen/examples/or_circuit.cpp` contains a single injected `uncompute_or(c, a, b);` before the closing `}` of `main()`, and is observably different from the input.
3. The existing eager-path snapshot (`tests/transpiler/fixtures/or_single.cpp`) still passes byte-for-byte.
4. A new snapshot fixture exercises the lazy path, so the peel-through is regression-protected.

## Non-Goals

1. **No runtime changes.** `lazy_expr.hpp`, `qbool_ops.hpp`, and the `OrExpr<qbool>::operator qbool()` materializer are unchanged. Deletion is Phase K work and is explicitly out of scope here.
2. **`operator&` / `AndExpr` is not covered.** The MVP only handled `|`; this PRD keeps that surface. `&` coverage is a Phase A concern and will be a separate PRD.
3. **No new `QOpKind`s.** The emitter continues to produce only `uncompute_or`.
4. **No new runtime uncompute functions.** `uncompute_or` (already in `include/sturm/uncompute/uncompute_api.hpp`) is the only call site.
5. **No change to the idempotency contract.** Running the transpiler on the generated file still produces a byte-identical result.
6. **No change to the opt-out contract.** The `// sturm-transpile: skip` magic comment still bypasses transpilation.

## Input Contract

Input is ordinary C++ accepted by the existing MVP pipeline, with one addition:

- The translation unit may define `STURM_BACKEND_ENABLED` (directly or via a CMake option), which routes `operator|` through `lazy_expr.hpp`.

Both of the following must match:

```cpp
// Eager path (no STURM_BACKEND_ENABLED): operator| returns qbool.
qbool tmp = a | b;

// Lazy path (STURM_BACKEND_ENABLED=1): operator| returns OrExpr<qbool>,
// materialized via user-defined conversion to qbool.
qbool tmp = a | b;
```

## Output Contract

Identical to the MVP: before the enclosing scope's close brace, append

```cpp
uncompute_or(tmp, a, b);
```

with `tmp`, `a`, `b` replaced by the spelled names of the bound `VarDecl` and the two `DeclRefExpr` operands. The generated-file header, idempotency marker, LIFO ordering, and scope anchoring are unchanged.

## Architecture

Single-file change to the matcher's AST pattern. The matcher must recognize two initializer shapes, anchored at the same `VarDecl`:

1. **Eager shape** — the current MVP pattern:
   `hasInitializer(ignoringImplicit(cxxOperatorCallExpr(|, a, b)))`

2. **Lazy shape** — the user-defined-conversion chain:
   `hasInitializer(ignoringImplicit(cxxConstructExpr(hasArgument(0, ignoringImplicit(cxxMemberCallExpr(on(ignoringImplicit(cxxOperatorCallExpr(|, a, b)))))))))`

The two are combined with `anyOf(...)`. The `lhs` / `rhs` `DeclRefExpr` bindings come from the inner `cxxOperatorCallExpr`, so the callback (`OrCallback::run`) requires **no changes** — it keeps reading `"lhs"`, `"rhs"`, `"var"` from the match result.

Reasoning for the `on()` choice: `OrExpr<qbool>::operator qbool()` is a non-static member function. Its implicit object argument IS the `OrExpr<qbool>` temporary, which is the return value of `operator|(qbool, qbool)`. The `on()` matcher binds against that implicit object, which is the `CXXOperatorCallExpr` we care about.

The `ignoringImplicit` wrappers absorb `MaterializeTemporaryExpr`, `CXXBindTemporaryExpr`, and `ImplicitCastExpr` nodes the frontend inserts between stages. Without them the pattern is brittle to minor AST-shape changes between Clang versions.

## Acceptance Criteria

1. **Existing snapshot:** `tests/transpiler/fixtures/or_single.cpp` → generated output byte-identical to `or_single.expected.cpp`. (Regression guard for the eager path.)
2. **New snapshot:** a new fixture pair `tests/transpiler/fixtures/or_single_backend.cpp` / `.expected.cpp`. The input forces the lazy path (either by including the real `lazy_expr.hpp` under `STURM_BACKEND_ENABLED=1`, or by defining a minimal mock `OrExpr<qbool>` with the same conversion shape — the latter keeps the test hermetic). Transpiler output must match `.expected.cpp` byte-for-byte and must contain the injected `uncompute_or`.
3. **Example observability:** after `cmake --build build --target example_or_circuit`, the file `build/sturm_gen/examples/or_circuit.cpp` exists and contains a single line of the form `uncompute_or(c, a, b);` (modulo whitespace) before the `}` of `main()`. The line is absent from `examples/or_circuit.cpp`.
4. **Idempotency:** running `sturm-transpile` on `build/sturm_gen/examples/or_circuit.cpp` produces a byte-identical output. Same for the new fixture's generated output.
5. **Gate-stream equivalence (re-assertion):** under `STURM_AUTO_UNCOMPUTE=OFF` (transpiler mode), running the compiled `example_or_circuit` produces the same gate stream as a hand-written reference using explicit `uncompute_or(c, a, b);`. No extra gates, no missing gates.
6. **No regression under legacy flag:** with `STURM_AUTO_UNCOMPUTE=ON` (legacy RAII mode) and `STURM_TRANSPILE=OFF`, all existing runtime tests still pass unchanged.
7. **Eager path unaffected:** `operator|` on the eager path must not match both branches of the `anyOf`. At most one match per `VarDecl`.

## Critical Files

**Modified:**
- `transpiler/src/matcher.cpp` — widen the `pattern` in `register_or_matcher` to `anyOf(eager, lazy)`.

**New:**
- `transpiler/tests/fixtures/or_single_backend.cpp` — lazy-path input fixture.
- `transpiler/tests/fixtures/or_single_backend.expected.cpp` — expected output.
- (Possibly) `transpiler/tests/fixtures/or_single_backend_support.hpp` if a hermetic mock `OrExpr` is preferred over pulling in the full runtime headers.

**Unchanged (deliberately):**
- `transpiler/src/matcher.hpp` — public API stable.
- `transpiler/src/qir.hpp`, `transpiler/src/qir.cpp` — IR unchanged.
- `transpiler/src/emitter.cpp` — emitter unchanged.
- `transpiler/src/uncompute_pass.cpp` — pass unchanged.
- `OrCallback::run` in `transpiler/src/matcher.cpp` — the three bindings are the same.
- All of `include/sturm/qtypes/`, `include/sturm/uncompute/`, `include/sturm/control/`.

## Verification

Same harness as the MVP, extended by one fixture and one example check:

1. **Unit / snapshot tests:**
   - `ctest --test-dir build -R transpiler_snapshot_or_single` (existing) — passes.
   - `ctest --test-dir build -R transpiler_snapshot_or_single_backend` (new) — passes.
2. **Example diff:**
   - `diff examples/or_circuit.cpp build/sturm_gen/examples/or_circuit.cpp` — non-empty; single added line is `uncompute_or(c, a, b);` inside `main()`.
3. **Idempotency:**
   - `sturm-transpile build/sturm_gen/examples/or_circuit.cpp --output-dir /tmp/idem/ && diff build/sturm_gen/examples/or_circuit.cpp /tmp/idem/examples/or_circuit.cpp` — empty diff.
4. **Gate-stream equivalence:** as in MVP acceptance #4, now also applied to the example binary.
5. **Legacy regression:** existing `ctest` suite under `STURM_AUTO_UNCOMPUTE=ON` / `STURM_TRANSPILE=OFF` — green.

## Design Decisions (Rationale)

- **`anyOf(eager, lazy)` rather than `findAll` / `hasDescendant`.** `findAll` would also match nested `|` operations inside unrelated sub-expressions (e.g. arguments of a classical function call in the same initializer), which is not the intent. Two explicit shapes are more honest and easier to reason about during review.
- **Peel in the matcher, not in a pre-pass.** The uncompute pass and emitter are shape-agnostic; moving the peel there would couple them to the runtime's lazy-expr design. Keeping the peel at matcher level isolates the change to the AST boundary.
- **Hermetic mock fixture over real-backend fixture.** The MVP fixtures are hermetic (they don't pull the backend). A minimal mock `OrExpr` in the fixture preserves that property. Accept real-backend fixtures only if the mock cannot reproduce the AST shape.
- **Deferred: matcher widening for `operator&`.** `AndExpr` has the same shape as `OrExpr`, and extending the matcher is mechanical. Deferring keeps this PRD's blast radius to a single operator and lets Phase A own the full self-inverse / bitwise expansion.
- **Deferred: deletion of lazy_expr.** The roadmap's Phase K removes `lazy_expr.hpp` outright. Doing it now would force a runtime audit (every caller of `OrExpr` / `AndExpr`, every `WHEN((b | c) & d)` in tests) that is unrelated to today's goal of observing the transpiler. Route 1 today, Route 2 under Phase K.

## Open Questions (tracked, not blocking)

- **Fixture realism vs. hermeticity.** If a mock `OrExpr` produces a measurably different AST shape from the real one (e.g. missing `MaterializeTemporaryExpr` in some Clang versions), the mock is lying. A second fixture under the real backend can serve as a canary. Revisit only if flakes appear.
- **Diagnostic when both `anyOf` branches match.** Should be impossible by construction (eager returns `qbool`, lazy returns `OrExpr<qbool>`; they can't coexist for one overload resolution), but a duplicate-match assert in debug builds is cheap insurance.
- **`STURM_BACKEND_ENABLED` vs. CMake flag.** `examples/or_circuit.cpp` defines the macro via `#define` at the top of the file. The fixture should do the same for hermeticity — avoid adding `target_compile_definitions` to the fixture build system for a single file.

## Session Hand-Off

A fresh agent starting this work should:

1. Read this PRD, `docs/roadmap_transpiler_post_mvp.md`, and `docs/01_principles.md`.
2. Read `transpiler/src/matcher.cpp` (especially `register_or_matcher` at line 154) and `include/sturm/qtypes/lazy_expr.hpp`.
3. Run the existing snapshot test and confirm it is green before touching anything.
4. Build `examples/or_circuit.cpp` and confirm `build/sturm_gen/examples/or_circuit.cpp` is byte-identical to the source (the problem this PRD solves).
5. Land the matcher widening + lazy-path fixture, in that order, with tests green after each step.
6. Re-run the example build and confirm the injected `uncompute_or(c, a, b);` appears.
7. Run the full `ctest` suite on both flag combinations.

No runtime headers should be modified. If the implementation touches anything outside `transpiler/src/matcher.cpp` and `transpiler/tests/fixtures/`, that's a signal the scope has drifted and the agent should stop and re-scope.
