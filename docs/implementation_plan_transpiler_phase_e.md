# Phase E — Compound Expressions with Named Intermediates

## Context

The STURM transpiler currently handles single-operator VarDecl initializers
(Phases A–D: `qbool tmp = a | b;`, `qbool c = a == b;`, compound-assigns on
`qint_t`). Phase E is the first phase that requires **real data-flow work**:
decomposing nested expressions like `qbool r = (b | c) & d;` into a flat
sequence with fresh-name intermediates, then uncomputing them in LIFO order
at scope exit.

Per the four design choices confirmed up-front:

- **Operator surface:** qbool `|` and qbool `&` only. XOR / NOT / qint ops
  can nest in a later follow-up; the roadmap example (`(b | c) & d`) is the
  only shape Phase E must handle.
- **Runtime addition:** `uncompute_and(qbool&, const qbool&, const qbool&)`
  ships *inside* Phase E, mirroring how Phase C bundled `uncompute_add_qint`
  et al. with the transpiler matcher.
- **Rewrite strategy:** the transpiler REPLACES the original compound
  VarDecl's source text with a flat decl sequence via
  `Rewriter.ReplaceText`. This is the first phase that mutates original
  source (A–D only *inserted* before `close_brace`).
- **Outer-result uncompute:** the outer VarDecl `r` is treated like any
  named qbool — `uncompute_and(r, __stu_t0, d);` lands at scope exit
  alongside `uncompute_or(__stu_t0, b, c);` in LIFO order. No liveness
  analysis (that's Phase H).

End goal — for input:
```cpp
qbool r = (b | c) & d;
```
the transpiler emits:
```cpp
qbool __stu_t0 = b | c;
qbool r = __stu_t0 & d;
uncompute_and(r, __stu_t0, d);
uncompute_or(__stu_t0, b, c);
```

---

## Architectural summary

### New IR / emitter machinery

- **New `QOpKind::AND`** in `qir.hpp`. The inverse render in
  `uncompute_pass.cpp` emits `uncompute_and(r, a, b);` — exact analogue of
  the existing OR case at `uncompute_pass.cpp:40-48`.

- **New emitter capability: text replacement.** Today the emitter only
  *inserts* before `close_brace` locations. Phase E needs
  `Rewriter.ReplaceText(source_range, new_text)` for the compound VarDecl.

  Add a sibling record `QReplacement { clang::SourceRange range; std::string replacement; }`
  alongside `UncomputeInsertion` in `uncompute_pass.hpp`. Extend
  `synthesize()` to return a pair (or a struct with both vectors). Extend
  `emit()` to apply replacements *before* insertions — Clang's Rewriter
  semantics preserve ReplaceText+InsertBefore in the expected order as long
  as the ranges don't overlap (the compound VarDecl range and the
  `close_brace` location are disjoint by construction).

- **Fresh-name helper.** New header
  `transpiler/src/fresh_names.hpp` with a small `FreshNameAllocator`
  (monotonic counter, `__stu_t<N>` format). Per-QUnit (i.e. per-translation
  unit) scope — simplest; no cross-scope collisions are possible because
  every generated name is globally unique within the TU.

### New matcher

- **`transpiler/src/matcher_qbool_compound.cpp`** (new file — keeps
  `matcher_qbool_bitwise.cpp` under 400 lines per the module-size rule).
  Matches `VarDecl` of type `qbool` whose initializer is a
  `cxxOperatorCallExpr` on `&` or `|` where at least one argument is itself
  a `cxxOperatorCallExpr` on `&` or `|` (i.e. a nested qbool bitwise op).
  Anchored via an `ignoringImplicit` / `cxxConstructExpr` peel — same
  pattern as Phase D's comparator matcher.

  On match, the callback walks the init expression tree post-order:
  - Each nested `cxxOperatorCallExpr` → one `QOperation` with a fresh
    `__stu_t<N>` result and `QOpKind::OR` or `QOpKind::AND`.
  - The outermost node → one `QOperation` whose result is the original
    VarDecl's name (`r`), not a fresh temp.
  - A single `QReplacement` for the original VarDecl's source range,
    containing the flat sequence
    `qbool __stu_t0 = b | c;\n    qbool r = __stu_t0 & d;`.

  **Disjointness:** the existing MVP OR matcher at
  `matcher_qbool_bitwise.cpp` keys on a VarDecl whose init is a single
  OR-call with *bare DeclRefExpr arguments*. The compound matcher requires
  at least one nested operator-call arg — the two guards are mutually
  exclusive, so no double-matching.

### New runtime function

- **`uncompute_and(qbool& r, const qbool& a, const qbool& b)`** declared in
  `include/sturm/uncompute/uncompute_api.hpp`, implemented in
  `src/sturm/uncompute/uncompute_api.cpp` alongside `uncompute_or`. Follows
  the same four-quadrant rules — by symmetry with `uncompute_or` but for
  the AND truth table. Forward AND on qbool-qbool emits a single CCX
  (self-inverse); the mixed/classical paths mirror
  `include/sturm/qtypes/bit_proxy.hpp`'s `materialize_and` decomposition,
  each branch re-emitted in reverse with every gate self-inversed.

---

## Critical files

| File | Change |
|---|---|
| `transpiler/include/sturm/transpile/qir.hpp` | Add `AND` to `QOpKind` enum (after `OR`) |
| `transpiler/include/sturm/transpile/uncompute_pass.hpp` | Add `QReplacement` struct; change `synthesize()` to return both insertions + replacements |
| `transpiler/include/sturm/transpile/emitter.hpp` | Extend `emit()` signature to accept replacements |
| `transpiler/include/sturm/transpile/matcher.hpp` | Declare `register_compound_qbool_matcher` |
| `transpiler/src/uncompute_pass.cpp` | Add `AND` case in `render_uncompute()`; pass through replacements |
| `transpiler/src/emitter.cpp` | Apply `Rewriter.ReplaceText` for each `QReplacement` before the insertion pass |
| `transpiler/src/fresh_names.hpp` | NEW — `FreshNameAllocator` header-only helper |
| `transpiler/src/matcher_qbool_compound.cpp` | NEW — Phase E matcher (≤ 400 lines per the module-size rule) |
| `transpiler/src/main.cpp` | Wire `register_compound_qbool_matcher` in `TranspileConsumer` |
| `transpiler/CMakeLists.txt` | Add the new `matcher_qbool_compound.cpp` source |
| `include/sturm/uncompute/uncompute_api.hpp` | Declare `uncompute_and` |
| `src/sturm/uncompute/uncompute_api.cpp` | Implement `uncompute_and` |
| `tests/transpiler/fixtures/compound_or_and.*` | NEW — roadmap example fixture (depth-2 mixed OR/AND) |
| `tests/transpiler/fixtures/compound_and_or.*` | NEW — depth-2 mirror shape (AND-of-OR variant) |
| `tests/transpiler/fixtures/compound_nested_or.*` | NEW — depth-2 pure OR (both args compound) |
| `tests/transpiler/CMakeLists.txt` | Register 3 new snapshot tests + injected/idempotent example tests |
| `examples/compound_expression.cpp` | NEW — end-to-end example (mirrors `examples/comparison.cpp`) |
| `examples/CMakeLists.txt` | Register the new example binary |
| `docs/roadmap_transpiler_post_mvp.md` | Add the `**2026-04-15:** Complete...` note atop Phase E |

Reusable helpers from prior phases (do not duplicate):
- `detail::enclosing_compound_stmt`, `detail::find_or_create_scope`,
  `detail::make_ref` — `transpiler/src/matcher_common.hpp:1-112`
- Callback-pool pattern — see `compare_callback_pool<Cb>()` at
  `transpiler/src/matcher_qint_compare.cpp:136-140`
- `ignoringImplicit` + `cxxConstructExpr` peel — see
  `matcher_qint_compare.cpp:163-183`
- `run_snapshot.cmake` snapshot-diff driver (reused without change)

---

## Implementation slices (for the bd queue)

Each slice is independently testable with its own snapshot or unit test.
Work in this order; slices to the right depend on slices to the left
within each row.

1. **PE-0 — Runtime: `uncompute_and`.** New free function + impl, following
   `uncompute_or`'s four-quadrant structure. A single unit test that
   drives a circuit-mode sink through forward `r = a & b;` + reverse
   `uncompute_and(r, a, b);` and asserts the combined gate stream is the
   identity on `r` across all eight classicality combinations. No
   transpiler changes. **Depends on:** nothing.

2. **PE-1 — IR: add `QOpKind::AND`.** Enum entry in `qir.hpp`; `AND` case
   in `render_uncompute()` emitting `uncompute_and(r, a, b);`. Hand-built
   QUnit unit test. No matcher yet. **Depends on:** PE-0 (so the emitted
   call has a symbol to link against — matters for the integration tests
   in PE-5).

3. **PE-2 — Emitter: replacement capability.** Add `QReplacement`; extend
   `synthesize()` + `emit()` plumbing; apply replacements before
   insertions. Hand-built QUnit unit test with one replacement + one
   insertion. **Depends on:** nothing (pure plumbing).

4. **PE-3 — Fresh-name helper.** `FreshNameAllocator`; unit test verifying
   monotonic allocation and formatting. **Depends on:** nothing.

5. **PE-4 — Compound matcher.** New `matcher_qbool_compound.cpp`; wire
   into `main.cpp`. Add three snapshot fixtures:
   - `compound_or_and` — the roadmap example `qbool r = (b | c) & d;`
   - `compound_and_or` — `qbool r = (b & c) | d;` (mirror shape)
   - `compound_nested_or` — `qbool r = (a | b) | (c | d);` (both args compound)
   **Depends on:** PE-1, PE-2, PE-3.

6. **PE-5 — End-to-end example.** `examples/compound_expression.cpp`
   exercising the roadmap shape; `transpiler_example_compound_expression_injected`
   and `transpiler_idempotent_example_compound_expression` CTests, modeled
   on the Phase D pattern (`transpiler_example_comparison_*`). **Depends
   on:** PE-0, PE-4.

7. **PE-6 — Docs.** Completion note atop Phase E in the roadmap, mirroring
   the Phases A–D completion notes. **Depends on:** PE-0 through PE-5.

---

## Verification

After all slices land:

```bash
cmake --build build --target sturm-transpile sturm-core
ctest --test-dir build -L transpiler          # all snapshot tests green
ctest --test-dir build -R compound_           # Phase E specifically
ctest --test-dir build -R transpiler_example_compound_expression_injected
ctest --test-dir build -R transpiler_idempotent_example_compound_expression
```

End-to-end golden path:
- Write `qbool r = (b | c) & d;` inside `examples/compound_expression.cpp`.
- Build with `-DSTURM_TRANSPILE=ON`; confirm the injected output contains
  the expected flat decl sequence + LIFO uncompute pair.
- Re-run the transpiler on its own output; byte-identical (idempotency).
- Circuit-mode gate trace: forward gates + reverse gates sum to identity
  on all qbools (`r`, `__stu_t0`); inputs `b, c, d` unmodified.
