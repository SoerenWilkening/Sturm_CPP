# LO — Implementation Plan for `prd_lossy_compound_reversibility.md`

**Status:** Landed (2026-04-25). All children LO-0…LO-5 of the sturm-lggp
epic are closed. Runtime `garbage_registry` / `when_scope_garbage` headers
deleted in sturm-pw2f (LO-4); this LO-5 commit retires the §5 / §6 entries of
`docs/TODO_reversibility_deferrals.md` and flips the PRD status to Landed.

Everything is **tests-first** and decomposed so each new module stays **≤ 300
LoC** (target budgets listed per module). All `cmake` / `ctest` / `make` /
`ninja` invocations MUST be capped at `--parallel 6` / `-j6` per `CLAUDE.md`.

---

## 0. Gating baseline

- Snapshot fixture directory: `tests/transpiler/fixtures/`.
- Snapshot driver: `tests/transpiler/CMakeLists.txt` (`check_example_*.cmake`
  pattern already in use).
- LIFO scope-exit plumbing: `transpiler/src/uncompute_pass.cpp` (currently
  582 LoC — **do not grow past 600**; extend via new modules).
- Fresh-name source: `transpiler/src/fresh_names.hpp` → use prefix
  `__sturm_tmp_<op>_<N>`.
- Parallel cap restated in every subagent prompt.

---

## 1. Epic tree

```
sturm-lo (epic, scope tag: lossy-reversibility)
├── LO-0  test scaffolding (RED → drives LO-1/2)
├── LO-1  DSL adjoint surface
├── LO-2  LO transpiler pass
├── LO-3  WHEN integration + registry-test rewrite
├── LO-4  pure deletion commit
└── LO-5  docs & deferrals cleanup
```

`bd dep add` so that:

- LO-2 blocked by LO-1
- LO-3 blocked by LO-2
- LO-4 blocked by LO-2 and LO-3
- LO-5 blocked by LO-4

---

## 2. LO-0 — Test scaffolding (all RED at first)

New fixtures under `tests/transpiler/fixtures/`; snapshot `.expected.cpp`
encodes the PRD §2 desugar shape.

| Child | Fixture / test | Purpose | Budget |
|---|---|---|---|
| **LO-0.1** | `mul_assign_qint.expected.cpp` (rewrite), `div_assign_qint.expected.cpp` (rewrite), `mod_assign_qint.expected.cpp` (rewrite), `and_assign_qint.{cpp,expected.cpp}` (new), `or_assign_qint.{cpp,expected.cpp}` (new) | Replace `uncompute_*_qint` stubs with the `qint tmp = …; swap(…); … ; swap(…); invert(op_dsl)(…);` shape | fixtures only |
| **LO-0.2** | `top_level_lossy_in_main.{cpp,expected.cpp}` | Assert **no** cleanup is emitted in `main`'s outermost scope (§4.3) | ≤ 60 |
| **LO-0.3** | `compound_nested_mul_and.{cpp,expected.cpp}` | `a *= (b & c)` → depth-first rewrite; inner AND first (§11 nested bullet) | ≤ 80 |
| **LO-0.4** | `when_lossy_and_assign.{cpp,expected.cpp}` + `tests/backend/test_when_lossy_gate_equiv.cpp` | Text-level shape + gate-equivalence under superposed `ctrl` (§5.1) | ≤ 200 |
| **LO-0.5** | `tests/backend/test_lossy_ancilla_cleaned.cpp` | End-to-end: allocate pool, run each of 5 ops in an inner block, assert post-scope qubit pool has zero leaked qubits (replacement for h5it tests) | ≤ 250 |

All of LO-0 lands on a branch that compiles but fails its new tests. This is
the red baseline that LO-1 / LO-2 turn green.

---

## 3. LO-1 — DSL adjoint surface (no rewrite yet)

Gate: `invert(<dsl>)(…)` compiles and zeros its ancilla.

| Child | New / touched module | Test | Budget |
|---|---|---|---|
| **LO-1a** | `include/sturm/lib/mul_dsl.hpp` — `STURM_REGISTER_ADJOINT(lib_mul_dsl, __lib_mul_dsl_adj)` block | `tests/lib/test_mul_dsl_adjoint.cpp` — forward then invert zeros `tmp` | module ≤ 120; test ≤ 180 |
| **LO-1b** | `include/sturm/lib/div_dsl.hpp` + `mod_dsl.hpp` adjoint blocks | `tests/lib/test_div_mod_dsl_adjoint.cpp` — invariant `a == q·b + r` holds post-swap-undo, adjoint zeros `q,r` | module ≤ 120 each; test ≤ 220 |
| **LO-1c** | `include/sturm/lib/c_and_dsl.hpp` — confirm CCX sweep self-inverse; register explicit adjoint | `tests/lib/test_c_and_dsl_adjoint.cpp` | module ≤ 60; test ≤ 150 |
| **LO-1d** | `include/sturm/lib/logic_dsl.hpp` — `lib_or_dsl` adjoint | `tests/lib/test_or_dsl_adjoint.cpp` | module ≤ 60; test ≤ 150 |
| **LO-1e** | **New** `include/sturm/qtypes/divide_oop.hpp` exposing `sturm::detail::divide_oop(a, b, q, r)` if `operator/` does not already expose `(a,b,q,r)` form | `tests/qtypes/test_divide_oop.cpp` | module ≤ 80; test ≤ 180 |

Acceptance: existing test suite still green; new LO-1 unit tests green;
LO-0 still red.

---

## 4. LO-2 — LO transpiler pass (the core)

Pass name `LO`; runs before the adjoint-synthesis Phase-T family; per PRD §7
slots ahead of `uncompute_pass`.

Split into 5 modules so none exceeds 300 LoC.

| Child | New module (under `transpiler/src/`) | Responsibility | Unit test | Budget |
|---|---|---|---|---|
| **LO-2a** | `matcher_lossy_op.{hpp,cpp}` | `ASTMatcher` for `CompoundAssignOperator` with opcode ∈ {`*=`,`/=`,`%=`,`&=`,`|=`} whose LHS type is `qint_t<W>`. Returns `LossyOpHit{opcode, lhs, rhs, enclosing_block}`. No emission. | `transpiler/tests/test_matcher_lossy_op.cpp` | hpp ≤ 60, cpp ≤ 180, test ≤ 220 |
| **LO-2b** | `lossy_rewrite_emitter.{hpp,cpp}` | Consumes `LossyOpHit`; emits forward pair (`qint __sturm_tmp_<op>_<N> = a <op> b; swap(a, __sturm_tmp_<op>_<N>);`). Uses `fresh_names.hpp` + §2.4 table. No cleanup. | `transpiler/tests/test_lossy_rewrite_emitter.cpp` (snapshot: forward pair only) | hpp ≤ 60, cpp ≤ 200, test ≤ 220 |
| **LO-2c** | `lossy_scope_exit_emitter.{hpp,cpp}` | Walks up to enclosing `CompoundStmt`; registers LIFO cleanup with existing `uncompute_pass` queue. Cleanup emits `swap(a, tmp); invert(<dsl>)(a, b, tmp);`. | `transpiler/tests/test_lossy_scope_exit_emitter.cpp` (full desugar — drives LO-0.1 green) | hpp ≤ 60, cpp ≤ 200, test ≤ 250 |
| **LO-2d** | `lossy_main_exception` (method on `LossyScopeExitEmitter`) | Detect when enclosing `CompoundStmt` is `main`'s outermost body; suppress cleanup (§4.3). | snapshot LO-0.2 goes green | ≤ 80 total |
| **LO-2e** | `lossy_nested_rewrite.{hpp,cpp}` | Depth-first recursion for nested lossy ops. Inner rewrite runs first; inner `tmp_and` scope is the same C++ block. | snapshot LO-0.3 goes green | ≤ 120 total |

**Wiring changes** (all ≤ 30 LoC each):

- `transpiler/src/transpile_consumer.cpp` — register `MatcherLossyOp` before
  adjoint matchers; chain emitter.
- `transpiler/src/uncompute_pass.cpp` — accept additional cleanup entries
  via a new `register_external_cleanup()` hook; no structural change.
- `include/sturm/uncompute/uncompute_api.hpp` — **delete** the
  `uncompute_*_qint` stubs **in LO-4** (keep LO-2 additive only).

**Runtime fast path unchanged.** Per §5.1, the
`detail::current_control == nullptr && both-operands-classical` short-circuit
in each `operator*=`/etc. is kept; the LO pass runs only at compile time.

Acceptance: all LO-0 snapshots green; all existing snapshot tests still
green.

---

## 5. LO-3 — WHEN integration & registry-test rewrite

| Child | Action | Test | Budget |
|---|---|---|---|
| **LO-3a** | Extend `tests/transpiler/test_gate_equivalence.cpp` with controlled cases from LO-0.4 | gate-equivalence vs. naive reference for superposed `ctrl` | delta ≤ 150 |
| **LO-3b** | Rewrite `tests/test_when_scope_garbage_consume.cpp` → `tests/test_when_lossy_reversibility.cpp`. Drop all `garbage_registry::snapshot()` asserts; replace with post-scope-exit `|0⟩` ancilla asserts | controlled `&=`, `|=`, `*=`, `/=`, `%=` inside `WHEN(ctrl)` | ≤ 280 |
| **LO-3c** | Rewrite `tests/backend/test_mul_div_upperw_garbage.cpp` → `tests/backend/test_mul_div_upperw_clean.cpp`. Asserts the qubit pool has no leaked allocations after enclosing scope exits | full-W `*=` upper-W and `/=` remainder cleaned | ≤ 250 |

Acceptance: all new tests green; both registry-based source tests gone;
`test_garbage_registry.cpp` still present (deleted in LO-4).

---

## 6. LO-4 — Pure-deletion commit

Only after LO-2 and LO-3 are green. Single commit, no semantic change
expected.

- **Delete headers**
  - `include/sturm/control/garbage_registry.hpp`
  - `include/sturm/control/when_scope_garbage.hpp`
- **Delete tests**
  - `tests/test_garbage_registry.cpp`
- **Edit in place (purely subtractive)**
  - `include/sturm/qtypes/qint_arith_v3.hpp` — remove CSWAP-and-leak tails
    at lines 138–162 (`*=`), 206–232 (`/=`), 260–277 (`%=`). Target
    post-edit size ≤ 200 LoC.
  - `include/sturm/qtypes/qint_bitwise_v3.hpp` — remove lines 97–120
    (`&=`) and symmetric `|=` block. Target post-edit size ≤ 120 LoC.
  - `include/sturm/control/when.hpp` — strip `WhenScopeGarbage` RAII member
    and consume helper.
  - `include/sturm/uncompute/uncompute_api.hpp` — remove the three
    classical-placeholder `uncompute_*_qint` stubs.
  - Root `CMakeLists.txt` / `docs/*` / any header index — strip
    `STURM_GARBAGE_REPORT` macro, env-var plumbing,
    `ScopedGarbageConsumeGuard`.

Gate: `ctest --parallel 6` 100% green; diff is subtractive only. Pre-commit:

```bash
grep -rn 'garbage_registry\|WhenScopeGarbage\|STURM_GARBAGE_REPORT\|ScopedGarbageConsumeGuard' include src tests transpiler
```

must return empty.

---

## 7. LO-5 — Docs & deferrals

- Remove §5 and §6 from `docs/TODO_reversibility_deferrals.md`. If all four
  sections become trivial, evaluate archiving per the CLAUDE.md "archiving"
  rule and remove its entry from the Required Reading list.
- Flip `docs/prd_lossy_compound_reversibility.md` status from **Proposed**
  → **Landed (<commit hash>)**.
- Close bd epics `sturm-h5it`, `sturm-pqs0`, `sturm-njul` with
  `--reason="superseded by sturm-lo"`.
- `bd remember` the LO pass's location in the matcher pipeline for future
  sessions.

---

## 8. Per-child workflow (restate to every subagent)

1. `bd update <id> --claim` → claim the issue.
2. Write / extend tests first (must fail).
3. Implement module; keep LoC under its budget. If it doesn't fit, split.
4. Build and test **with** `cmake --build build --parallel 6 &&
   ctest --test-dir build --parallel 6`.
5. All green → commit, `bd close <id>`.
6. Never skip hooks. Never amend. Never `rm -rf`.

---

## 9. Risk register

| Risk | Mitigation |
|---|---|
| `uncompute_pass.cpp` grows past 600 LoC when LO emitter hooks in | New cleanup queue lives in `lossy_scope_exit_emitter.cpp`; `uncompute_pass` only gains a `register_external_cleanup()` hook (~20 LoC) |
| `invert(lib_div_dsl)` does not currently compile in cleanup shape | LO-1b is a prerequisite gate with its own failing test before LO-2c lands |
| `main`-exception detection mis-classifies lambdas whose body is `main`'s outer `CompoundStmt` | LO-0.2 fixture adds a lambda-inside-main edge case |
| Nested lossy ops produce shadowing `__sturm_tmp_*` names | `fresh_names.hpp` counter is global per TU; LO-0.3 asserts `_0` vs `_1` suffixes |
| Deletion commit (LO-4) accidentally removes something still live | Pre-commit grep above must return empty |

---

## 10. References

- `docs/prd_lossy_compound_reversibility.md` — PRD this plan delivers.
- `docs/01_principles.md` — B1, B6, B10, P4, P9, P9c.
- `docs/TODO_reversibility_deferrals.md` §5, §6 — collapsed into this plan.
- `transpiler/src/uncompute_pass.cpp` — LIFO cleanup plumbing.
- `transpiler/src/fresh_names.hpp` — ancilla-name mangling source.
