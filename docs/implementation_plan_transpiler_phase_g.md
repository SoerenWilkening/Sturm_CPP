# Phase G — Nested `WHEN` AND-fold in the transpiler

**Status:** Draft (implementation plan)
**Date:** 2026-04-16
**Relates to:** `docs/roadmap_transpiler_post_mvp.md` §Phase G (lines 237-259)

## Context

Today, nested `WHEN(outer) { WHEN(inner) { body } }` is handled at runtime by
`WhenGuard`'s AND-fold (`include/sturm/control/when.hpp:99-145, 156-192`): on
entering an inner WHEN with an outer control already active, the guard allocates
an ancilla qubit and emits `CCX(outer, inner, ancilla)` to compute the combined
control, then uncomputes with a second CCX on destruction. This is the single
biggest source of runtime complexity in the WHEN machinery (≈68 LOC, a
dedicated `qbool ancilla_` member, and a paired `control_stack.pop()` /
`push()` dance to keep the stack depth at 1).

Phase G replaces this with a compile-time lowering. The transpiler rewrites
nested WHENs into an explicit AND temporary plus `uncompute_and`, reusing the
free function shipped in Phase E (`include/sturm/uncompute/uncompute_api.hpp:93`).
The runtime `WhenGuard` then only ever sees a single named control qbool per
WHEN — no ancilla, no CCX, no fold logic. Gate semantics stay identical;
the complexity moves from runtime to compile-time where it can be inspected
and optimized.

## User decisions (pinned before planning)

1. **Named+named only** — matcher fires only when both outer and inner WHEN
   args are bare `DeclRefExpr` to named qbools. Compound shapes
   (`WHEN(b | c) { WHEN(d) { ... } }`) stay on the runtime path for now.
2. **Pairwise cascade** for depth ≥ 3: `WHEN(a) { WHEN(b) { WHEN(c) { … } } }`
   produces two separate `__stu_ctrl` temps, one `uncompute_and` per level.
3. **Always lower regardless of siblings** — inject decl immediately before the
   inner WHEN and uncompute_and immediately after, even if the outer body
   contains other statements.
4. **Bundle the runtime delete into Phase G** — retiring the AND-fold is part
   of this phase, not a follow-up.
5. **Extend the M12 gate-equivalence harness** (`tests/transpiler/test_gate_equivalence.cpp`)
   with a `nested_when_*` fixture pair proving transpiled output matches a
   hand-written reference gate-stream byte-for-byte.

---

## Design

### Matcher (new file)

`transpiler/src/matcher_when_nested.cpp` — new TU, target ≤400 LOC per repo
convention. Keys on an outer WHEN IfStmt whose body contains one or more inner
WHEN IfStmts. Guards:

- **Macro-body expansion** — both outer and inner `IfLoc` must be inside a
  `WHEN` macro body (reuse `is_expansion_of_macro` from
  `matcher_when_lift.cpp`, promoted to `matcher_common.hpp`).
- **Named-only** — both `materialize_when` args, after
  `detail::peel_to_payload`, must be a bare `DeclRefExpr` to a non-synthetic
  named qbool. Any compound shape on either side → reject.

For each matched (outer, inner) pair:
1. Allocate `ctrl_name` via a new `FreshNameAllocator::next_ctrl()` returning
   `__stu_ctrl<M>` (independent counter from `next()`). The callback owns one
   allocator across all runs, so cascade levels get incrementing names.
2. Stage `UncomputeInsertion{insert_before = sm.getExpansionLoc(inner_if->getIfLoc()), code = "qbool <ctrl_name> = <outer_name> & <inner_name>;\n"}`.
3. Stage `QReplacement` over the inner `materialize_when` arg (spelling-loc
   normalised via `Lexer::makeFileCharRange`, same pattern as
   `matcher_when_lift.cpp:433-444`) with replacement text `<ctrl_name>`.
4. Push a synthetic `QOperation{kind=QOpKind::AND, result={ctrl_name},
   operands=[outer, inner], insert_before_override=post_inner_brace}` into the
   enclosing `QScope`. The existing M8 uncompute pass + emitter
   (`uncompute_pass.cpp:49-58`) already renders
   `uncompute_and(<ctrl>, <outer>, <inner>);` at the override anchor — no
   emitter changes needed.

Pairwise cascade falls out naturally: each (outer, inner) pair fires
independently against the original AST, with its own `__stu_ctrl` temp and
its own uncompute. For `WHEN(a) { WHEN(b) { WHEN(c) { body } } }`: pass 1
handles (a, b) → `__stu_ctrl0 = a & b`; pass 2 handles (b, c) →
`__stu_ctrl1 = b & c`. Both pairs emit into their respective scopes, and
every runtime WHEN ends up controlled by one named qbool.

**Disjointness with Phase F**: Phase F fires only when the `materialize_when`
arg is *not* a bare `DeclRefExpr`; Phase G fires only when *both* are bare
`DeclRefExpr`. A given inner WHEN is processed by at most one of them. The
existing `when_nested_passthrough` fixture (compound inner under named outer)
stays byte-identical.

### Runtime simplification

`include/sturm/control/when.hpp`:
- Delete constructor AND-fold branch (lines **98-129**) — the
  `if (prev_control_ != nullptr && prev_control_->qubits[0] >= 0)` block and
  its ancilla allocation + CCX emission.
- Delete destructor AND-fold branch (lines **156-192**) — the
  `if (and_folded_)` block and its reverse-CCX + ancilla release.
- Delete members `and_folded_` (line 71), `ancilla_` (line 80, under
  `STURM_BACKEND_ENABLED`), `inner_expr_qubit_` (line 226, under
  `STURM_BACKEND_ENABLED`), plus the `and_folded_(false)` initializer (line 84).
- **Keep** the `control_stack.pop_control()` + `push_control(expr.qubits[0])`
  swap when `prev_control_ != nullptr`. After deletion, unify the non-fold and
  former-AND-fold paths into a single "if prev control exists, pop it; always
  push expr" path. The inner WHEN's `expr.qubits[0]` now goes on the stack
  directly (no ancilla substitution). Depth-1 invariant preserved.
- Update top-of-file M24 / principle-B5 comment to reflect that nested-WHEN
  fold moved to the transpiler.

File drops from ~298 to ~230 LOC.

### Regression test

Extend `tests/transpiler/test_gate_equivalence.cpp` `main()` with a third
pair (don't create a new binary). New fixtures under
`tests/transpiler/fixtures/`:
- `nested_when_runtime.cpp` —
  `namespace m12_nested_transpiled { void demo(const qbool&, const qbool&, qbool&); }`
  with source `WHEN(outer) { WHEN(inner) { target ^= true; } }`. Routed through
  `sturm-transpile` under `STURM_TRANSPILE=ON`.
- `nested_when_reference.cpp` — `namespace m12_nested_reference { … }`
  spelling out the hand-written lowering:
  `qbool __stu_ctrl0 = outer & inner; WHEN(__stu_ctrl0) { target ^= true; } sturm::uncompute_and(__stu_ctrl0, outer, inner);`.

Harness runs both under a fresh `BackendContext`, captures `ctx->ir`,
byte-compares. CMake: extend the existing `_m12_*` block at
`tests/transpiler/CMakeLists.txt:970-1060` with a second `add_custom_command`
producing `${CMAKE_BINARY_DIR}/sturm_gen/.../nested_when_runtime.cpp`, and add
the generated + reference TUs to the existing
`test_gate_equivalence_or_single` target sources (one binary, three pairs).

### Existing-test reconciliation

- `tests/backend/test_e2e_when_nested.cpp` — **no change**. This test pushes
  directly onto `control_stack`, bypassing `WhenGuard`; asserts
  `depth == 1, 2, 3` and drives `lib_c_n_AND_dsl` via `emit_CX_lifted`.
  Orthogonal to the `WhenGuard` AND-fold.
- `tests/backend/test_when_control_stack_bridge.cpp` — update
  `test_nested_when_depth_invariant` (lines 140-199): flip the assertion at
  line 170 from `!=` to `==` so it reads
  `top_inner == inner_flag.qubits[0]` (after the swap, the inner expr qubit
  IS on top). Update the message from "AND-fold" to "swap". Rename the
  function to `test_nested_when_swap_invariant`; update the call site in
  `main()`. Depth checks stay (`depth_outer == 1`, `depth_inner == 1`,
  `depth_after_inner == 1`) — the swap preserves this.

### End-to-end example

`examples/nested_when.cpp` — two- and three-level named-named nested WHENs
with superposed qbool inputs, emitting the expected CCX/CX stream under
`STURM_BACKEND_ENABLED`. Plus `tests/transpiler/check_example_nested_when.cmake`
asserting: `qbool __stu_ctrl0 = a & b;` appears between `WHEN(a) {` and
`WHEN(__stu_ctrl0)` in the generated sibling; `WHEN(__stu_ctrl0) {` replaces
`WHEN(b)`; `uncompute_and(__stu_ctrl0, a, b);` appears after the inner `}`;
source is byte-identical pre/post transpile. Three new CTests:
`build_example_nested_when`, `transpiler_example_nested_when_injected`,
`transpiler_idempotent_example_nested_when`, wired exactly like the
`example_when_integration` block at `tests/transpiler/CMakeLists.txt:937-968`.

---

## bd Issues (PG-0 … PG-9)

| Issue | Scope | Depends on |
|-------|-------|-----------|
| **PG-0** | Add `FreshNameAllocator::next_ctrl()` returning `__stu_ctrl<M>` with independent counter (`transpiler/src/fresh_names.hpp`) | — |
| **PG-1** | New `matcher_when_nested.cpp` skeleton + detection counter (bind outer+inner IfStmts with macro-body + bare-DRE guards; increment counter only). Register in `transpiler/src/main.cpp`. Promote `compute_post_body_brace` + `is_expansion_of_macro` to `matcher_common.hpp` as `inline`. Unit tests on the counter. | PG-0 |
| **PG-2** | Emission: decl injection + argument replacement. Snapshot fixture `when_nested_named` byte-matches. | PG-1 |
| **PG-3** | Emission: uncompute_and scheduling + pairwise cascade. Snapshot fixtures `when_nested_cascade` and `when_nested_siblings` byte-match. | PG-2 |
| **PG-4** | Simplify `include/sturm/control/when.hpp`: delete AND-fold code per spec above; unify non-fold + ex-AND-fold into single swap path. Lands atomically with PG-5. | PG-1 |
| **PG-5** | Update `test_when_control_stack_bridge.cpp::test_nested_when_depth_invariant` → flip assertion, rename. Lands in same PR as PG-4. | PG-4 |
| **PG-6** | Verify `test_e2e_when_nested.cpp` is untouched by Phase G (read-only verification; add a one-line comment noting it bypasses `WhenGuard`). | PG-4 |
| **PG-7** | M12 harness extension: `nested_when_runtime.cpp` + `nested_when_reference.cpp` fixtures, extend `test_gate_equivalence.cpp` main() with third pair, extend CMake. `ctest -R gate_equivalence_or_single` passes. | PG-3, PG-4 |
| **PG-8** | End-to-end example: `examples/nested_when.cpp` + `check_example_nested_when.cmake` + 3 CTests. | PG-3 |
| **PG-9** | Roadmap completion note at top of Phase G in `docs/roadmap_transpiler_post_mvp.md`; update Phase F's "Next up" tail to Phase H. | PG-0..PG-8 |

**Atomic bundle**: PG-4 + PG-5 land as one commit — PG-4 alone breaks the
depth-invariant test; PG-5 restores it.

### Ordering

```
PG-0 ──▶ PG-1 ──▶ PG-2 ──▶ PG-3 ──┬──▶ PG-7
                │                  ├──▶ PG-8
                └──▶ PG-4 ──▶ PG-5 │
                         └──▶ PG-6 │
                                   └──▶ PG-9 (after all prior)
```

---

## Critical files

**New:**
- `transpiler/src/matcher_when_nested.cpp` (~300-400 LOC)
- `tests/transpiler/fixtures/when_nested_named.cpp` + `.expected.cpp`
- `tests/transpiler/fixtures/when_nested_cascade.cpp` + `.expected.cpp`
- `tests/transpiler/fixtures/when_nested_siblings.cpp` + `.expected.cpp`
- `tests/transpiler/fixtures/nested_when_runtime.cpp`
- `tests/transpiler/fixtures/nested_when_reference.cpp`
- `examples/nested_when.cpp`
- `tests/transpiler/check_example_nested_when.cmake`

**Modified:**
- `transpiler/src/fresh_names.hpp` — add `next_ctrl()` (PG-0)
- `transpiler/src/matcher_common.hpp` — promote `compute_post_body_brace`,
  `is_expansion_of_macro` to shared `inline` (PG-1)
- `transpiler/src/matcher_when_lift.cpp` — remove the helpers that moved
  (PG-1, net no LOC change)
- `transpiler/include/sturm/transpile/matcher.hpp` — declare
  `register_when_nested_matcher`, detection helpers (PG-1)
- `transpiler/src/main.cpp` — register new matcher (PG-1)
- `include/sturm/control/when.hpp` — delete AND-fold code, unify swap path (PG-4)
- `tests/backend/test_when_control_stack_bridge.cpp` — flip assertion, rename
  function (PG-5)
- `tests/backend/test_e2e_when_nested.cpp` — add 1-line clarification comment (PG-6)
- `tests/transpiler/test_gate_equivalence.cpp` — add third pair to main() (PG-7)
- `tests/transpiler/CMakeLists.txt` — 3 snapshot ctests, M12 extension,
  3 example ctests (PG-2, PG-3, PG-7, PG-8)
- `examples/CMakeLists.txt` — `add_quantum_executable` entry (PG-8)
- `docs/roadmap_transpiler_post_mvp.md` — Phase G completion note, Phase F
  "Next up" update (PG-9)

**Reused verbatim (no change):**
- `include/sturm/uncompute/uncompute_api.hpp:93` —
  `uncompute_and(qbool&, const qbool&, const qbool&)`
- `transpiler/src/uncompute_pass.cpp:49-58` — `QOpKind::AND` rendering to
  `uncompute_and(...)`
- `transpiler/src/matcher_qbool_compound.cpp` — pattern reference for
  `QOpKind::AND` + `insert_before_override`

---

## Verification

**Unit / snapshot:**
```bash
cd build && cmake --build . -j && \
  ctest -R 'snapshot_when_nested|test_matcher_when_nested' --output-on-failure
```
All 3 snapshot tests (`when_nested_named`, `when_nested_cascade`,
`when_nested_siblings`) byte-match their `.expected.cpp`, detection-counter
unit tests pass.

**Runtime regression:**
```bash
ctest -R 'test_when|test_e2e_when' --output-on-failure
```
`test_when_control_stack_bridge` (with PG-5's flipped assertion),
`test_e2e_when_nested`, and all other existing `WHEN`-related tests pass.
The depth-invariant test confirms `control_stack.depth() == 1` at every
nesting level and that the top-of-stack is the inner expr qubit (not an
ancilla).

**M12 gate-equivalence:**
```bash
ctest -R 'gate_equivalence_or_single' --output-on-failure
```
All three fixture pairs in the harness (or_single, or_circuit, nested_when)
byte-compare IR streams and pass. This is the single most important Phase G
test: it proves transpiled output and hand-written reference produce identical
gate streams.

**End-to-end example:**
```bash
ctest -R 'example_nested_when' --output-on-failure
```
`build_example_nested_when`, `transpiler_example_nested_when_injected`,
`transpiler_idempotent_example_nested_when` all pass.

**LOC check:**
```bash
wc -l transpiler/src/matcher_when_nested.cpp  # expect < 400
wc -l include/sturm/control/when.hpp           # expect ~230, down from 298
```

**Manual inspection of transpiled output for `examples/nested_when.cpp`:**
```bash
build/transpiler/sturm-transpile examples/nested_when.cpp --output-dir /tmp/pg-check
diff examples/nested_when.cpp /tmp/pg-check/examples/nested_when.cpp
```
Source is byte-identical (no in-place modification); the generated sibling
file contains the injected `qbool __stu_ctrl<N>` decls, rewritten
`WHEN(__stu_ctrl<N>)`, and `uncompute_and(...)` calls in cascade order.
