# PRD: Per-Bit Lazy WHEN Promotion via BitProxy

**Status:** Not Started
**Date:** 2026-04-13
**Predecessors:** qbool Unification (complete), Backend v3 DSL layer (complete)

## Problem Statement

When `WHEN(superposed_c) { a += b; }` executes with classical operands `a` and `b`, the result stays classical. The dispatch fast-path and compound-assign fast-path both return before consulting the WHEN control qubit, so the operation is never conditioned on `c`.

This violates principle **P4** (quantum control is lexical scope): any operation inside a superposed WHEN must produce a result that depends on the control qubit.

### Current behavior

```
WHEN(qbool(0.5)) {
    qint<4> a(3), b(2);
    a += b;           // fast-path: a.super_mask == 0, returns classical 5
}
// BUG: a is classical, should be in superposition
```

### Desired behavior

```
WHEN(qbool(0.5)) {
    qint<4> a(3), b(2);
    a += b;           // controlled addition: a.super_mask != 0
}
// a is quantum: |5> in the c=1 branch, |3> in the c=0 branch
```

## Design Principles

1. **Per-bit lazy promotion.** Only allocate qubits for bits that are actually touched by a gate. Do not promote entire registers.

2. **Promote at the primitive level.** The four bit-level primitives (XOR/CNOT, AND/Toffoli, phi rotation, theta rotation) are the atomic operations from which all higher-level circuits are built. Promotion logic belongs here, not in every operator.

3. **Classical operand folding.** When one operand of a gate is classical, fold its value into the gate selection instead of allocating a qubit:
   - `XOR(target, classical_0)` = identity (skip)
   - `XOR(target, classical_1)` = `X_lifted(target)` (controlled by WHEN stack)
   - `AND(a, classical_0, target)` = identity
   - `AND(a, classical_1, target)` = `CX_lifted(a, target)`

4. **Lazy ancilla allocation.** Internal DSL ancilla (carry, borrow, scratch) start as classical qbools with no qubit. They are promoted only when the circuit actually writes a quantum value to them.

## Architecture

```
User code:   WHEN(c) { a += b; }
                 |
Compound-assign: operator+= bypasses fast-path (current_control != nullptr)
                 |
                 builds BitProxy arrays via operator[]
                 |
DSL layer:   lib_add_dsl → maj_dsl / uma_dsl
                 |
                 calls bit-level operators on BitProxy
                 |
Primitives:  BitProxy::operator^=  (CNOT with per-bit promotion)
             BitProxy::operator^=(AndExpr)  (Toffoli with classical folding)
                 |
Gate emission: emit_CX_lifted / emit_CCX_lifted / emit_X_lifted
```

### BitProxy

A lightweight reference-like view into a single bit of a `qint_t<W>` register. Holds pointers to the parent's `qubits[i]`, `super_mask`, and `value`, so any allocation writes back to the parent register.

Also constructible from a standalone `qbool&`, enabling DSL functions to uniformly handle both register bits and ancilla bits.

### Two build paths

| Path | Guard | Promotion mechanism |
|------|-------|---------------------|
| Non-backend (`!STURM_BACKEND_ENABLED`) | `dispatch_binary` / `dispatch_unary` / `dispatch_shift` in `dispatch.hpp` | Existing WHEN promotion in dispatch (full-width mask when `new_mask == 0 && current_control != nullptr`) |
| Backend (`STURM_BACKEND_ENABLED`) | Compound-assigns in `qint_arith_v3.hpp`, `qint_bitwise_v3.hpp`, `qint_shift_backend.hpp` | BitProxy per-bit lazy promotion via templated DSL |

The non-backend path already works (dispatch.hpp changes from the initial fix). The backend path is the focus of this PRD.

## Scope

### In scope

- BitProxy struct with per-bit promotion and classical folding
- Non-const `operator[]` on `qint_t<W>` returning BitProxy
- Template all DSL library functions on `Bit` type (20 functions across 9 headers)
- Update 10 compound-assign operators to use BitProxy arrays
- Update 6 backend free operators with fast-path bypass
- Update 4 shift operators with fast-path bypass
- Update comparison helper `make_dsl_compare_result` with fast-path bypass
- Tests for per-bit WHEN promotion

### Out of scope

- Non-backend path changes (already handled by dispatch.hpp)
- `pow_dsl` (uses OrkanBridge directly for SIMULATE mode — different pattern)
- Multi-level WHEN nesting tests (existing tests in `test_e2e_when_nested.cpp` cover this)
- Measurement inside WHEN

## Acceptance Criteria

1. `WHEN(qbool(0.5)) { a += b; }` with classical a, b produces `a.super_mask != 0`
2. `WHEN(qbool(0.5)) { a ^= b; }` with classical a, b where `b == 0b0100` promotes only bit 2 of a (not all bits)
3. Outside WHEN: classical fast-path still works (`super_mask == 0`, no gates)
4. All existing backend tests pass (regression)
5. Gate count for WHEN-promoted `a ^= b` equals gate count for pre-promoted `a ^= b` (same circuit)
6. Carry/borrow ancilla are only allocated when actually needed by carry propagation
7. `sizeof(BitProxy) <= 40` bytes
