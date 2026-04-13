# Plan: PRD v3 + Implementation Plan v3

## Context

M1-M10 (v2 plan) are complete. The backend has two functional but **disconnected** chains:

1. **Library chain** (`lib/` layer): `lib_add_cuccaro()` etc. call `primitive_X/XOR/AND(SimState&, ...)` which go straight to Orkan. Gates are never counted, never appended to IR, never routed through `execute_gate()`.

2. **Frontend chain** (`qtypes/` layer): `operator+`, `operator&`, etc. go through `dispatch_binary` → `current_sink()` (non-backend) or backend stubs with `// TODO(backend): emit actual adder circuit` that do nothing (backend-enabled path).

**The bridge between them is missing.** When DSL code like `a += b` runs, no gates are actually counted/recorded/simulated because the frontend operators never call the library functions, and the library functions never go through `execute_gate()`.

This plan creates two new documents + updates CLAUDE.md to design and schedule the wiring.

## Files to create/modify

| File | Action |
|------|--------|
| `docs/11_prd_backend_v3.md` | Create (new PRD, supersedes 09) |
| `docs/12_implementation_plan_backend_v3.md` | Create (new impl plan, supersedes 10) |
| `CLAUDE.md` | Edit Required Reading (lines 7-9) |

No code files are changed. Existing tests remain untouched.

## What goes in each document

### `docs/11_prd_backend_v3.md` — PRD v3

Sections:

1. **Motivation** — Why v3: the two-chain disconnect described above. The v2 library is correct but unreachable from DSL code. v3 wires them together.

2. **Architectural principle** — Same three layers (Backend / Library / Frontend DSL), but now with a single execution path:
   ```
   DSL operator  →  library function  →  execute_gate(BackendContext&, ...)
                                               ↓
                              COUNT / APPEND / SIMULATE dispatch
   ```

3. **SimState → BackendContext** — Primitives change signature from `primitive_X(SimState&, q)` to calling `execute_gate(BackendContext&, STURM_GATE_X, &q, 1, 0.0)`. SimState is removed; the Orkan statevector lives inside BackendContext (via OrkanBridge, already there as `orkan_state_ptr`). Library functions take `BackendContext&` instead of `SimState&`.

4. **qbool as DSL type** — Gets operators:
   - `^=` (XOR-assign, in-place: single CNOT or Toffoli under WHEN)
   - `operator&` (lazy AND, returns `AndExpr`)
   - `operator|` (lazy OR, returns `OrExpr`)
   - `operator~` (NOT, returns flipped qbool)
   - `flip()` (in-place X gate)
   - WHEN context is automatic: ops check `WhenGuard::active_control()` and emit controlled variants

5. **Lazy AndExpr / OrExpr** — `operator&` / `operator|` return lazy expression types, not materialized qbools:
   - `a ^= (b & c)` → consumed efficiently: single Toffoli (AND into target)
   - `a ^= (b | c)` → De Morgan: `a ^= ~(~b & ~c)` → 2 NOT + Toffoli + 2 NOT (or equivalent optimal decomposition)
   - Any other use (passing to function, storing in variable, using as WHEN condition) → implicit conversion to `qbool` → materialization with ancilla + RAII uncompute on scope exit

6. **Library in DSL style** — Self-bootstrapping, no `_when` variants needed:
   - Cuccaro adder uses `^=` and `&` on qbool (MAJ = `b ^= c; a ^= c; c ^= (a & b)`)
   - Multiplication uses `+=` (which calls adder)
   - Division uses `-=` and comparisons
   - No explicit `SimState&` or `WhenLift&` parameters — ops read WHEN context from TLS automatically

7. **Frontend wiring** — `qint_t<W>::operator+=` calls Cuccaro adder on its qubit arrays. `operator&=` calls the logic library. The `dispatch_binary` / `current_sink()` path is removed for backend-enabled builds; operators call library functions directly.

8. **Bit subscript** — `a[i]` returns a non-owning qbool that aliases qubit index `i` from `a`'s register. No gate emitted. The qbool's destructor must not release the qubit (it doesn't own it).

9. **Uncompute strategy** — RAII-based: lazy expressions that materialize an ancilla carry an uncompute closure. When the materialized qbool goes out of scope, the destructor re-runs the computation to uncompute the ancilla. (This extends the existing `uncompute_op` pattern already in the codebase.)

10. **Non-goals** — No QFT adder. No hand-written gate emitters. No runtime circuit optimization. No breaking change to the C ABI surface (`sturm_execute_gate` continues to work).

### `docs/12_implementation_plan_backend_v3.md` — Implementation Plan v3

Milestones M11-M21, continuing from completed M1-M10. Ground rules carry forward from v2 (TDD, ≤300 LoC per module, one module one responsibility, no upward imports).

#### Milestone dependency graph

```
M11 ─┐
     ├──→ M13 → M14 → M15 → M16 → M17 ──→ M19 → M20 → M21
M12 ─┘                                ↗
                               M18 ──┘
```

#### Milestone details

**M11 — Primitives through execute_gate** (~80 LoC)
- File: `include/sturm/backend/primitives_v3.hpp`
- Change primitive signatures to take `BackendContext&` and call `execute_gate()` instead of `SimState::apply_*`
- Existing `primitives.hpp` (SimState-based) retained as reference until M20
- Tests: verify gate_count increments, verify SIMULATE mode produces correct statevector via OrkanBridge

**M12 — Control stack in BackendContext** (~120 LoC)
- File: `include/sturm/backend/control_stack.hpp`
- Move control-qubit stack into BackendContext (replacing TLS `current_control` for library-layer use)
- `push_control(qubit)` / `pop_control()` / `control_depth()` / `top_control()`
- Primitives_v3 consult the stack: X under 1 control → CX, XOR under 1 → CCX, etc. (same lifting rules as v2 WhenLift but integrated into BackendContext)
- Tests: replicate test_when_single/double/triple using new stack

**M13 — qbool operators + lazy expressions** (~350 LoC total)
- Files: `include/sturm/qtypes/qbool_ops.hpp` (~200), `include/sturm/qtypes/lazy_expr.hpp` (~150)
- `qbool::operator^=(qbool)` → calls `execute_gate(ctx, STURM_GATE_CX, ...)` (or STURM_GATE_CCX under WHEN)
- `qbool::flip()` → `execute_gate(ctx, STURM_GATE_X, ...)`
- `operator&(qbool, qbool)` → returns `AndExpr{a, b}` (lazy)
- `operator|(qbool, qbool)` → returns `OrExpr{a, b}` (lazy)
- `operator~(qbool)` → returns flipped qbool (eager, emits X gate)
- `qbool::operator^=(AndExpr)` → single Toffoli
- `qbool::operator^=(OrExpr)` → optimized (2 CNOT + Toffoli or De Morgan)
- `AndExpr::operator qbool()` → materialize with ancilla + RAII uncompute
- `OrExpr::operator qbool()` → materialize with ancilla + RAII uncompute
- Tests: truth tables for all operators, verify gate counts, verify ancilla cleanup after materialization

**M14 — Cuccaro adder in DSL style** (~250 LoC)
- File: `include/sturm/lib/adder_dsl.hpp`
- MAJ: `b ^= c; a ^= c; c ^= (a & b);` (3 ops, last one consumes AndExpr efficiently)
- UMA: `c ^= (a & b); a ^= c; b ^= a;` (mirror)
- `dsl_add_cuccaro(a_bits, b_bits, carry_out, n)` — reads BackendContext from TLS, no explicit context parameter
- No `_when` variant needed — WHEN control is automatic via the control stack
- Tests: exhaustive 4-bit addition, carry behavior, controlled addition under WHEN

**M15 — Logic + c_AND in DSL style** (~270 LoC total)
- Files: `include/sturm/lib/logic_dsl.hpp` (~120), `include/sturm/lib/c_and_dsl.hpp` (~150)
- NOT_reg, OR, NAND, NOR, XNOR expressed using qbool ops
- c_AND (C³-X) via Nielsen-Chuang sandwich using qbool `^=` and `&`
- c_n_AND recursive cascade
- Tests: truth tables, ancilla cleanliness

**M16 — SWAP, MUL, DIV in DSL style** (~550 LoC total)
- Files: `include/sturm/lib/swap_dsl.hpp` (~100), `include/sturm/lib/mul_dsl.hpp` (~150), `include/sturm/lib/div_dsl.hpp` (~300)
- SWAP: uncontrolled = index relabel; controlled = Fredkin via qbool ops
- MUL: shift-and-add using DSL adder (M14)
- DIV: non-restoring division using DSL subtractor + compare
- Tests: exhaustive 4-bit truth tables for each

**M17 — Compare, MOD, POW in DSL style** (~460 LoC total)
- Files: `include/sturm/lib/compare_dsl.hpp` (~200), `include/sturm/lib/mod_dsl.hpp` (~60), `include/sturm/lib/pow_dsl.hpp` (~200)
- EQ, LT, LE, GT, GE via tree-of-ANDs on XOR'd bits using qbool ops
- MOD: thin wrapper over DIV
- POW: repeated squaring via MUL
- Tests: exhaustive 4-bit truth tables

**M18 — a[i] returns non-owning qbool** (~30 LoC delta)
- File: modify `include/sturm/qtypes/qint_core.hpp`
- `operator[](size_t i)` returns a qbool that aliases `qubits[i]` without ownership
- Add `owns_qubit` flag to qbool (or use a separate `qbool_ref` type) so destructor skips release
- Tests: verify no gate emitted, verify no double-free

**M19 — Frontend wiring** (~550 LoC total)
- Files: `include/sturm/qtypes/qint_arith_v3.hpp` (~250), `include/sturm/qtypes/qint_bitwise_v3.hpp` (~150), `include/sturm/qtypes/qint_compare_v3.hpp` (~150)
- `operator+=` calls `dsl_add_cuccaro` on qubit arrays
- `operator*=` calls `dsl_mul`
- `operator&=` calls DSL AND
- `operator==` calls DSL compare
- Remove `dispatch_binary` / `current_sink()` path for backend-enabled builds
- Tests: end-to-end DSL expressions with gate counting in all 3 modes

**M20 — Cleanup: remove SimState, v2 library code** (net negative LoC)
- Delete `include/sturm/backend/state.hpp` (SimState)
- Delete `include/sturm/backend/primitives.hpp` (v2 primitives)
- Delete `include/sturm/backend/when_lift.hpp` / `when_lift_dispatch.hpp`
- Delete v2 library files that have DSL replacements (add_cuccaro.hpp, logic_basic.hpp, etc.)
- Delete `include/sturm/frontend/lower_assign.hpp`, `lower_expr.hpp`
- Update all `#include` paths
- All tests must pass against v3 code only

**M21 — Integration & acceptance**
- End-to-end tests in all 3 modes (COUNT_ONLY, APPEND, SIMULATE)
- `c = (a + 5) * b` → verify correct gate count and statevector
- `pow(a, e) % m` → verify correctness
- Deep WHEN nesting → verify c_n_AND path and ancilla cleanliness
- All module LoC budgets respected

### `CLAUDE.md` change

Lines 7-9 (Required Reading section), replace:
```
- `docs/09_prd_backend_v2.md` — backend PRD v2 (design lock, supersedes 07)
- `docs/10_implementation_plan_backend_v2.md` — modular, test-driven backend implementation plan v2 (supersedes 08)
```
with:
```
- `docs/11_prd_backend_v3.md` — backend PRD v3 (design lock, supersedes 09)
- `docs/12_implementation_plan_backend_v3.md` — implementation plan v3 (supersedes 10)
```

## Verification

After creating documents:
1. Review PRD v3 covers all design decisions: no `a.toffoli()`, lazy expr, qbool ownership, WHEN handling via control stack, non-owning qbool ref
2. Review impl plan milestones are correctly ordered per dependency graph, all ≤300 LoC per file
3. Verify CLAUDE.md points to new docs 11 and 12
4. Run existing tests to confirm no code changes: `cmake --build build && ctest --test-dir build`
5. Git diff should show only 3 files changed (2 new, 1 edited)