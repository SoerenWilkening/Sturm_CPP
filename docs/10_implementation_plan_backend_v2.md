# Backend Implementation Plan v2

Status: active (supersedes `08_implementation_plan_backend.md`)

Companion to `09_prd_backend_v2.md`. Strictly modular, test-driven.
**Every module must stay under 300 lines of code.** If a module
grows past that, split it before continuing.

## Ground rules

- **TDD**: write the test first for each module, then the
  implementation, then make it green.
- **One module, one responsibility.** No catch-all files.
- **No module imports from a layer above it.** Backend never imports
  library; library never imports frontend.
- **Primitive discipline**: library code never emits gates wider than
  `AND`. Wider controls are produced only by `WHEN` lift and resolved
  through `c_AND` / `c_n_AND`.
- Each milestone ends green: all prior tests still pass.

## Layer map

```
Frontend DSL           (untouched by this plan except lowering hooks)
   │
   ▼
Library layer          (M3 – M9)
   │
   ▼
Backend primitives     (M1 – M2)   ← frozen after M2
```

## Milestones

### M1 — Backend primitive core

**Goal**: lock in the frozen primitive surface.

Modules:
- `backend/primitives.hpp/.cpp` — `X`, `XOR`, `AND`, `phase (R_y)`,
  `phi_add (R_z)`. No other ops. Target: <200 LoC.
- `backend/ancilla.hpp/.cpp` — `allocate_ancilla()`,
  `free_ancilla(q)`, with debug-mode check that freed ancillas are
  `|0⟩`. Target: <150 LoC.
- `backend/state.hpp/.cpp` — simulator state vector & gate
  application. Target: <300 LoC; split into `state_apply.cpp` if it
  grows.

Tests:
- `test_primitives_classical` — truth tables for X, XOR, AND on
  small states.
- `test_primitives_phase` — R_y / R_z rotation angles verified
  against analytic expected amplitudes.
- `test_ancilla_lifecycle` — borrow/return, double-free detection,
  dirty-free detection.

**Exit**: primitive surface frozen. No file in the backend folder
changes after this milestone except for bug fixes.

### M2 — `WHEN` lift machinery

**Goal**: generic control-count lifting over the classical
primitives, with phase-primitive support.

Modules:
- `backend/when_lift.hpp/.cpp` — context stack of active controls;
  each primitive call consults it. Target: <250 LoC.
- `backend/when_lift_dispatch.cpp` — dispatch table that knows which
  primitive becomes which wider-control op, and defers to library
  `c_AND` / `c_n_AND` for `AND` under control. Target: <200 LoC.

Tests:
- `test_when_single` — `WHEN a: X(b)` ≡ CNOT.
- `test_when_double` — `WHEN a: WHEN b: X(c)` ≡ CCNOT.
- `test_when_triple` — `WHEN a,b,c: X(d)` uses `c_AND` path.
- `test_when_phase` — controlled phase rotations correct.

### M3 — Logic library

**Goal**: all boolean operators as library functions over
`{X, XOR, AND}`.

Modules:
- `lib/logic_basic.hpp/.cpp` — `NOT_reg`, `OR`, `NAND`, `NOR`,
  `XNOR`. Each is a few lines. Target: <200 LoC total.
- `lib/c_and.hpp/.cpp` — `c_AND` (C³-X) via Nielsen-Chuang sandwich
  with one borrowed ancilla. Target: <150 LoC.
- `lib/c_n_and.hpp/.cpp` — recursive sandwich cascade for `C^n-X`.
  Target: <200 LoC.

Tests:
- Truth-table tests for each operator on all 2-bit / 3-bit inputs.
- `test_c_and_ancilla_clean` — verify borrowed ancilla returns to
  `|0⟩`.
- `test_c_n_and_scaling` — correctness for n = 3, 4, 5, 6.

### M4 — SWAP / Fredkin with context-aware dispatch

**Goal**: the dual-implementation SWAP.

Modules:
- `lib/swap.hpp/.cpp` — uncontrolled `SWAP` as index relabel on
  `qint`/`qbool` wrappers; controlled `c_SWAP` (Fredkin) as
  `XOR; AND; XOR`. `WHEN`-lift dispatch table routes `SWAP` to
  `c_SWAP` inside a controlled context. Target: <200 LoC.

Tests:
- `test_swap_uncontrolled_zero_gates` — assert zero primitives
  emitted.
- `test_c_swap_fredkin` — truth table on all 8 inputs.
- `test_swap_under_when` — `WHEN a: SWAP(b,c)` produces Fredkin, not
  three CCNOTs.

### M5 — Move semantics & garbage manager

**Goal**: support out-of-place-with-move for assignment operators.

Modules:
- `lib/move.hpp/.cpp` — `move_result(dest, src, ctx)`:
  uncontrolled → index relabel; controlled → per-qubit Fredkin
  cascade. Target: <200 LoC.
- `lib/garbage.hpp/.cpp` — track displaced registers, schedule
  uncomputing, integrate with ancilla manager. Target: <250 LoC.

Tests:
- `test_move_uncontrolled_relabel` — zero gates, qint index
  swapped.
- `test_move_controlled_fredkin` — n Fredkins for n-qubit register.
- `test_garbage_uncompute` — full round-trip: allocate, use,
  uncompute, verify ancilla pool clean.

### M6 — ADD / SUB (Cuccaro, in-place)

Modules:
- `lib/add_cuccaro.hpp/.cpp` — in-place `b += a`. Target: <250 LoC.
- `lib/sub.hpp/.cpp` — two's-complement wrapper around ADD. Target:
  <100 LoC.

Tests:
- Exhaustive 4-bit addition / subtraction truth tables.
- Controlled ADD under `WHEN` — verify against expected.
- Overflow / carry behaviour documented and tested.

### M7 — MUL, DIV, MOD (out-of-place + move)

Modules:
- `lib/mul.hpp/.cpp` — shift-and-add using controlled ADD. Target:
  <250 LoC.
- `lib/div_nonrestoring.hpp/.cpp` — non-restoring division producing
  quotient + remainder. Target: <300 LoC; split if needed.
- `lib/mod.hpp` — thin wrapper returning only the remainder.
  Target: <50 LoC.

Tests:
- Exhaustive 4-bit multiplication truth table.
- Exhaustive 4-bit division / modulo with zero-divisor handling
  documented.
- Assignment form `a *= b` exercises the move path (M5).

### M8 — POW, CMP, EQ, LT

Modules:
- `lib/pow.hpp/.cpp` — repeated squaring via MUL. Target: <200 LoC.
- `lib/compare.hpp/.cpp` — `EQ`, `LT`, `LE`, `GT`, `GE` via
  tree-of-ANDs on XOR'd bits. Target: <250 LoC.

Tests:
- Exhaustive 4-bit comparison truth tables.
- `pow` tested for small bases and exponents.

### M9 — Frontend lowering hooks

**Goal**: wire the DSL operators to library calls.

Modules:
- `frontend/lower_assign.cpp` — dispatch `a op= b` to in-place vs
  out-of-place-with-move library calls. Target: <200 LoC.
- `frontend/lower_expr.cpp` — dispatch `c = a op b` expressions.
  Target: <200 LoC.

Tests:
- End-to-end DSL tests: `a |= b`, `c = a + b`, `if (a == b) …`,
  `SWAP(a,b)`, each verified against the expected gate-level
  behaviour.

### M10 — Integration & acceptance

- `test_integration_arithmetic` — composite expressions like
  `c = (a + 5) * b` exercising ADD + MUL + move.
- `test_integration_pow_mod` — `pow(a, e) mod m` using POW + MOD.
- `test_integration_when_nested` — deep `WHEN` nesting drives
  `c_n_AND` and confirms ancilla pool cleanliness.
- `bd preflight` green; all module LoC budgets respected.

## Module size budget

Every file listed above has a target LoC budget. CI or `bd preflight`
should flag files >300 LoC. If a module must exceed the budget, split
along a clear seam (e.g. `div_core.cpp` + `div_restore.cpp`) before
merging.

## Definition of done

- All milestones M1 – M10 merged.
- All tests green.
- Backend folder unchanged since M2.
- No library file exceeds 300 LoC.
- `09_prd_backend_v2.md` still accurately describes the shipped
  backend; any deviation is either fixed or recorded as an amendment
  in the PRD before the milestone closes.
