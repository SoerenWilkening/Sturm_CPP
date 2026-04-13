# Backend Implementation Plan v3

Status: active (supersedes `10_implementation_plan_backend_v2.md`)

Companion to `11_prd_backend_v3.md`. Strictly modular, test-driven.
**Every module must stay under 300 lines of code.** If a module
grows past that, split it before continuing.

M1–M10 (from v2 plan) are complete and merged. This plan continues
from M11.

## Ground rules

- **TDD**: write the test first for each module, then the
  implementation, then make it green.
- **One module, one responsibility.** No catch-all files.
- **No module imports from a layer above it.** Primitives never
  import qbool; qbool never imports library; library never imports
  `qint_t` operators.
- **Primitive discipline**: library code never emits gates wider than
  `AND`. Wider controls are produced only by WHEN lift and resolved
  through `c_AND` / `c_n_AND`.
- **No `_when` variants.** Functions work transparently under WHEN
  because `qbool` operators read the control stack automatically.
- Each milestone ends green: all prior tests still pass.
- v2 library code (`sturm::v2` namespace) is retained read-only
  until all v3 replacements pass. Then removed in M20.

## Layer map

```
qint_t<W> operators        (M19)
   │
   ▼
Library (DSL-style)         (M14 – M17)
   │
   ▼
qbool operators + lazy expr (M13)
   │
   ▼
Primitives via execute_gate (M11)
   │
   ▼
execute_gate() → exec_count / exec_append / exec_simulate
```

## Dependency graph

```
M11 ─┐
     ├──► M13 ──► M14 ──► M15 ──► M16 ──► M17 ──► M19 ──► M20 ──► M21
M12 ─┘                                       ↗
                                   M18 ──────┘
```

M11 and M12 are independent (parallelisable). M13 depends on both.
M14–M17 are sequential (each library level uses the one below).
M18 (bit subscript) is independent of M14–M17 but must precede M19
(frontend wiring needs `a[i]`). M20 (cleanup) depends on all prior.
M21 (integration) depends on M20.

## Milestones

### M11 — Primitives through execute_gate

**Goal**: rewrite the five frozen primitives to call `execute_gate()`
via `BackendContext` instead of calling Orkan through `SimState`.

Modules:
- `include/sturm/backend/primitives_v3.hpp` — new overloads:
  `primitive_X(ctx, q)`, `primitive_XOR(ctx, c, t)`,
  `primitive_AND(ctx, c0, c1, t)`, `primitive_phase(ctx, q, theta)`,
  `primitive_phi_add(ctx, q, theta)`. Each is a one-liner calling
  `execute_gate()`. Target: <80 LoC.

Tests:
- `test_primitives_v3_count` — verify `gate_count` increments for
  each primitive in COUNT_ONLY mode.
- `test_primitives_v3_append` — verify correct `GateRecord` pushed
  to IR for each primitive in APPEND mode.
- `test_primitives_v3_simulate` — verify statevector matches
  expected amplitudes for X, CX, CCX, Ry, Rz in SIMULATE mode
  (use OrkanBridge).

**Exit**: five primitives callable via `BackendContext`. Old
`SimState` overloads still exist (v2 library still uses them).

### M12 — Control stack in BackendContext

**Goal**: move WHEN control tracking into `BackendContext` so `qbool`
operators can read it without depending on `WhenLift`.

Modules:
- `include/sturm/core/control_stack.hpp` — `ControlStack` class:
  `push_control(qubit)`, `pop_control()`, `depth()`, `top()`.
  Stored as a member of `BackendContext`. Thread-local accessor
  `current_control_stack()` returns the active context's stack.
  Target: <120 LoC.
- `src/sturm/core/control_stack.cpp` — thread-local implementation.
  Target: <40 LoC.

Tests:
- `test_control_stack_push_pop` — push 3 controls, verify depth and
  top at each level, pop all, verify empty.
- `test_control_stack_thread_local` — two contexts on different
  threads have independent stacks.

**Exit**: `ControlStack` available in `BackendContext`. Primitives_v3
consult the stack: X under 1 control → CX, XOR under 1 → CCX, etc.
(same lifting rules as v2 `WhenLift` but integrated).

### M13 — qbool operators + lazy expressions

**Goal**: `qbool` gets `^=`, `&` (lazy), `|` (lazy), `~`, `flip()`
operators that call v3 primitives through `BackendContext`.

Modules:
- `include/sturm/qtypes/qbool_ops.hpp` — operator bodies:
  `operator^=(const qbool&)` calls `primitive_XOR`.
  `operator^=(const AndExpr<qbool>&)` calls `primitive_AND` (single
  Toffoli). `operator^=(const OrExpr<qbool>&)` emits 2 CNOT + 1
  Toffoli. `flip()` calls `primitive_X`. `operator~()` returns
  flipped `qbool` (eager, emits X gate). All operators read the
  control stack and emit lifted gates when controls are active.
  Target: <200 LoC.
- `include/sturm/qtypes/lazy_expr.hpp` — `AndExpr<T>`, `OrExpr<T>`
  structs. `operator&(qbool, qbool)` returns `AndExpr<qbool>`.
  `operator|(qbool, qbool)` returns `OrExpr<qbool>`. Implicit
  conversion `operator T()` materialises with ancilla + RAII
  uncomputation. Also works for `qint_t<W>`. Target: <150 LoC.

Tests:
- `test_qbool_xor_assign` — `c ^= a` emits 1 CX. Verify gate
  count and IR.
- `test_qbool_and_expr_xor` — `c ^= (a & b)` emits 1 CCX. Verify
  gate count.
- `test_qbool_or_expr_xor` — `c ^= (a | b)` emits 2 CX + 1 CCX.
  Verify gate count.
- `test_qbool_and_materialize` — `qbool r = (a & b)` allocates
  ancilla. Destruction uncomputes and frees.
- `test_qbool_flip` — `a.flip()` emits 1 X.
- `test_qbool_not` — `~a` emits 1 X, returns new qbool.
- `test_qbool_under_when` — `c ^= a` inside WHEN(ctrl) emits 1
  CCX (lifted CX). `c ^= (a & b)` under WHEN emits `c_AND` fold.
- `test_qbool_non_owning` — non-owning qbool destruction does NOT
  release the qubit.

**Exit**: `qbool` is a fully operational DSL type with lazy
expressions and automatic WHEN handling.

### M14 — Cuccaro adder in DSL style

**Goal**: rewrite Cuccaro MAJ/UMA and the adder/subtractor using
`qbool` operators instead of direct primitive calls.

Modules:
- `include/sturm/lib/adder_dsl.hpp` — `maj_dsl(a, b, c)` and
  `uma_dsl(a, b, c)` using `qbool ^=` and `^= (a & b)`.
  `lib_add_dsl(a_bits, b_bits, carry_out, n)` — in-place `b += a`.
  `lib_sub_dsl(a_bits, b_bits, borrow_out, n)` — in-place `b -= a`
  via two's complement. Reads `BackendContext` from TLS; no explicit
  context parameter. No `_when` variant: WHEN is automatic.
  Target: <250 LoC.

Tests:
- `test_add_dsl_truth_table` — exhaustive 3-bit addition truth
  table (all 64 input pairs). Verify via SIMULATE mode.
- `test_sub_dsl_truth_table` — exhaustive 3-bit subtraction.
- `test_add_dsl_gate_count` — verify gate count matches expected
  Cuccaro cost (5n − 2 + carry copy).
- `test_add_dsl_under_when` — `b += a` inside WHEN(ctrl). Verify
  gates are lifted (all become controlled).
- `test_add_dsl_no_when_variant` — confirm same function works both
  inside and outside WHEN.

**Exit**: DSL-style adder/subtractor passes all truth-table tests.
v2 Cuccaro functions (`sturm::v2::lib_add_cuccaro`) still exist.

### M15 — Logic and c_AND in DSL style

**Goal**: rewrite OR, NAND, NOR, XNOR, `c_AND`, `c_n_AND` using
`qbool` operators.

Modules:
- `include/sturm/lib/logic_dsl.hpp` — all logic ops using `qbool`
  `^=`, `&`, `|`, `flip()`. OR: `c ^= (a | b)`. NAND:
  `c ^= (a & b); c.flip()`. NOR: `a.flip(); b.flip(); c ^= (a & b);
  b.flip(); a.flip()`. XNOR: `c ^= a; c ^= b; c.flip()`.
  Target: <120 LoC.
- `include/sturm/lib/c_and_dsl.hpp` — `c_AND` (2-control) and
  `c_n_AND` (n-control) using `qbool` operators and ancilla `qbool`.
  Nielsen-Chuang sandwich: `anc ^= (c0 & c1); tgt ^= anc;
  anc ^= (c0 & c1)`. Target: <150 LoC.

Tests:
- `test_logic_dsl_truth_tables` — truth tables for OR, NAND, NOR,
  XNOR on all 4 input combinations.
- `test_c_and_dsl_ancilla_clean` — verify borrowed ancilla returns
  to |0⟩.
- `test_c_n_and_dsl_scaling` — correctness for n = 3, 4, 5, 6
  controls.

### M16 — SWAP, MUL, DIV in DSL style

**Goal**: rewrite SWAP/Fredkin, multiplication, and division using
`qbool` operators and DSL-style library functions.

Modules:
- `include/sturm/lib/swap_dsl.hpp` — uncontrolled = index relabel
  (zero gates). Under WHEN: Fredkin per qubit pair
  (`a ^= b; b ^= (ctrl & a); a ^= b`). Target: <100 LoC.
- `include/sturm/lib/mul_dsl.hpp` — shift-and-add multiplication
  using `WHEN(b[i]) { acc += (a << i); }` where `+=` calls DSL
  adder. Target: <150 LoC.
- `include/sturm/lib/div_dsl.hpp` — non-restoring division using
  DSL subtractor + compare. Target: <300 LoC (split to
  `div_dsl_core.hpp` + `div_dsl_correct.hpp` if needed).

Tests:
- `test_swap_dsl_zero_gates` — uncontrolled SWAP emits zero gates.
- `test_c_swap_dsl_fredkin` — controlled SWAP: truth table on all
  8 inputs.
- `test_mul_dsl_truth_table` — exhaustive 2-bit multiplication.
- `test_div_dsl_truth_table` — exhaustive 2-bit division (quotient
  + remainder).

### M17 — Compare, MOD, POW in DSL style

**Goal**: rewrite comparisons, modulo, and exponentiation using DSL
library functions.

Modules:
- `include/sturm/lib/compare_dsl.hpp` — EQ, LT, LE, GT, GE using
  qbool XNOR (`c ^= a; c ^= b; c.flip()`) and `c_n_AND`.
  Target: <200 LoC.
- `include/sturm/lib/mod_dsl.hpp` — thin wrapper over DSL division
  returning only remainder. Target: <60 LoC.
- `include/sturm/lib/pow_dsl.hpp` — repeated squaring via DSL
  multiplication. Target: <200 LoC.

Tests:
- `test_compare_dsl_truth_tables` — exhaustive 3-bit comparison
  truth tables for all six operators.
- `test_mod_dsl_truth_table` — exhaustive 2-bit modulo.
- `test_pow_dsl_small` — small base/exponent pairs (2^3, 3^2, etc.)

### M18 — `a[i]` returns non-owning qbool

**Goal**: `qint_t<W>::operator[](i)` returns a non-owning `qbool`
that aliases `qubits[i]`.

Modules:
- Modify `include/sturm/qtypes/qint_core.hpp` — `operator[](size_t)`
  returns `qbool` with `owning_ = false`. Target: delta <30 LoC.
- Add `owning_` flag to `qbool` if not already added in M13.

Tests:
- `test_subscript_non_owning` — `a[2]` returns `qbool`; modifying
  via `^=` emits a gate on the correct qubit. Destruction does NOT
  release the qubit.
- `test_subscript_in_library` — `maj_dsl(a[0], b[0], c[0])` works
  correctly with non-owning `qbool`s.
- `test_subscript_when` — `WHEN(a[0]) { b[1] ^= a[2]; }` emits a
  controlled CX.

### M19 — Frontend wiring

**Goal**: wire `qint_t<W>` compound-assign operators to DSL library
functions. Remove the Sink-based dispatch for backend-enabled builds.

Modules:
- `include/sturm/qtypes/qint_arith_v3.hpp` — `operator+=` calls
  `lib_add_dsl`. `operator-=` calls `lib_sub_dsl`. `operator*=`
  calls `lib_mul_dsl`. `operator/=` calls `div_dsl`.
  `operator%=` calls `mod_dsl`. Each operator extracts `qbool`
  references via `a[i]` and passes them to the library.
  Target: <250 LoC.
- `include/sturm/qtypes/qint_bitwise_v3.hpp` — `operator&=`,
  `|=`, `^=` call logic_dsl per bit. `operator~` calls `flip()`
  per bit. Target: <150 LoC.
- `include/sturm/qtypes/qint_compare_v3.hpp` — `operator==`, `<`,
  etc. call `compare_dsl`. Return `qbool`.
  Target: <150 LoC.

Tests:
- `test_qint_add_e2e` — `qint_t<4> a = 3; qint_t<4> b = 5;
  a += b;` verify `a == 8` and `gate_count > 0`.
- `test_qint_mul_e2e` — 2-bit multiplication end-to-end.
- `test_qint_compare_e2e` — comparison returns correct `qbool` and
  emits gates.
- `test_qint_bitwise_e2e` — XOR, AND, OR assign emit correct gate
  counts.

### M20 — Cleanup: remove SimState, v2 library code

**Goal**: remove v2 library code, `SimState`, old namespaces.

Modules:
- Delete `include/sturm/backend/state.hpp` (`SimState`).
- Delete `include/sturm/backend/primitives.hpp` (old `SimState`
  overloads). Rename `primitives_v3.hpp` → `primitives.hpp`.
- Delete `include/sturm/backend/when_lift.hpp`,
  `when_lift_dispatch.hpp`.
- Delete v2 library headers: `add_cuccaro.hpp`, `sub.hpp`,
  `logic_basic.hpp`, `c_and.hpp`, `c_n_and.hpp`, `compare.hpp`,
  `mul.hpp`, `div_nonrestoring.hpp`, `mod.hpp`, `pow.hpp`,
  `swap.hpp`, `move.hpp`, `garbage.hpp`.
- Delete `include/sturm/frontend/lower_assign.hpp`,
  `lower_expr.hpp`.
- Update all `#include` paths.
- Target: net negative LoC.

Tests:
- All M11–M19 tests still pass.
- `test_no_simstate_refs` — grep-based check: no file outside
  tests/ references `SimState`.
- `test_no_v2_namespace` — grep-based check: no file outside
  tests/ uses `sturm::v2::`.

### M21 — Integration & acceptance

**Goal**: end-to-end DSL programs exercising the full stack.

Tests:
- `test_e2e_arithmetic` — `c = (a + 5) * b` exercises ADD + MUL +
  move. Verify `gate_count > 0` and correct classical result.
- `test_e2e_pow_mod` — `pow(a, e) % m` exercises POW + MOD.
- `test_e2e_when_nested` — deep WHEN nesting (3+ levels) drives
  `c_n_AND` and confirms ancilla pool cleanliness.
- `test_e2e_all_modes` — same program runs in COUNT_ONLY, APPEND,
  and SIMULATE modes. Gate count matches across modes. SIMULATE
  produces correct statevector.
- All module LoC budgets verified.

## Module size budget

| Milestone | Module | Target |
|-----------|--------|--------|
| M11 | `primitives_v3.hpp` | <80 |
| M12 | `control_stack.hpp` | <120 |
| M12 | `control_stack.cpp` | <40 |
| M13 | `qbool_ops.hpp` | <200 |
| M13 | `lazy_expr.hpp` | <150 |
| M14 | `adder_dsl.hpp` | <250 |
| M15 | `logic_dsl.hpp` | <120 |
| M15 | `c_and_dsl.hpp` | <150 |
| M16 | `swap_dsl.hpp` | <100 |
| M16 | `mul_dsl.hpp` | <150 |
| M16 | `div_dsl.hpp` | <300 |
| M17 | `compare_dsl.hpp` | <200 |
| M17 | `mod_dsl.hpp` | <60 |
| M17 | `pow_dsl.hpp` | <200 |
| M18 | `qint_core.hpp` delta | <30 |
| M19 | `qint_arith_v3.hpp` | <250 |
| M19 | `qint_bitwise_v3.hpp` | <150 |
| M19 | `qint_compare_v3.hpp` | <150 |

## Definition of done

- All milestones M11–M21 merged.
- All tests green.
- `SimState` removed; no `sturm::v2::` references remain.
- `execute_gate()` is the single chokepoint for all gate execution.
- Every DSL operation (`qbool` operator, `qint_t` operator) produces
  gates that are counted, recorded, and simulated correctly.
- No module file exceeds 300 LoC.
- `11_prd_backend_v3.md` accurately describes the shipped
  architecture; any deviation is recorded as an amendment.
