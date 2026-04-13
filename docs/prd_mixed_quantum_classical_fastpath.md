# PRD: Mixed Quantum/Classical Fast-Path Bypass

**Status:** Not Started
**Date:** 2026-04-13
**Predecessors:** BitProxy WHEN Promotion (complete)

## Problem Statement

When a binary operation (e.g. `a ^= b`) is performed on two `qint` values where one is in superposition and the other is classical, the operation takes a "classical fast path" and emits zero quantum gates. The classical value is updated correctly, but the quantum state is not modified.

This violates principle **B3** (dispatch-time specialization): each overloaded operator should branch on its operands' classicality masks and emit the minimum necessary primitives. The current guard emits zero primitives when it should emit classically-controlled gates.

### Verified behavior

Using `examples/or_circuit.cpp` with `a` quantum (via `theta() += 2`) and `b` classical:

```cpp
qint_t<3> a, b;
a.value = 1; b.value = 1;
a.theta() += 2;    // a is now quantum (3 qubits allocated)
                    // b remains classical (no qubits)
a ^= b;            // Emits 0 gates. Expected: X gate on a's qubit 0
```

Output shows only 4 gates from `a.theta()` setup (X + 3 Ry). The `^=` emits nothing.

### Expected behavior

`a ^= b` where `a` is quantum and `b` is classical with value `0b001`:
- Bit 0: b=1 (classical) -> emit X gate on a's qubit 0 (classically-controlled flip)
- Bit 1: b=0 (classical) -> no gate (XOR with 0 is identity)
- Bit 2: b=0 (classical) -> no gate

Total: 1 additional X gate. Output should show 5 gates.

### Root cause

The classical fast-path guard in all compound-assign and free binary operators uses `||` (OR) instead of `&&` (AND):

```cpp
// qint_bitwise_v3.hpp, operator^= line 50:
if ((qubits[0] < 0 || b.qubits[0] < 0) && detail::current_control == nullptr) {
    value ^= b.value;  // skip gates entirely
    return *this;
}
```

`qubits[0] < 0` means "no qubits allocated" (classical qint). The OR triggers the fast path when EITHER operand is classical. Correct behavior: fast path only when BOTH are classical (`&&`).

When the fast path is bypassed, BitProxy handles mixed quantum/classical correctly via classical folding:

| Source | Target | Gate emitted |
|--------|--------|-------------|
| quantum | quantum | CNOT(source, target) |
| quantum | classical | promote target, CNOT(source, target) |
| classical=1 | quantum | X(target) |
| classical=0 | quantum | skip (identity) |
| classical=1 | classical | skip (no quantum effect, inside WHEN: promote + X) |
| classical=0 | classical | skip |

### Scope of the bug

The `||` condition exists in **13 locations** across 3 files:

| File | Operators | Count |
|------|-----------|-------|
| `qint_bitwise_v3.hpp` | `^=`, `&=`, `\|=` | 3 |
| `qint_arith_v3.hpp` | `+=`, `-=`, `*=`, `/=`, `%=` | 5 |
| `qint_arith_backend.hpp` | `+`, `-`, `*`, `/`, `%` (free) | 5 |

Additionally, the free bitwise operators (`operator&`, `operator|`) in `qint_bitwise_backend.hpp` have a secondary bug: they create `qbool::make_non_owning(b.qubits[i])` where `b.qubits[i]` can be -1, producing a qbool wrapping an invalid qubit index (0xFFFFFFFF after `uint32_t` cast).

### What works correctly

- Compound-assign operators when BOTH operands are quantum (fast path not taken)
- Operations inside WHEN scope (the `&& detail::current_control == nullptr` guard handles this)
- The BitProxy per-bit gate logic itself (classical folding, promotion, all correct)
- The `operator~` (unary NOT) -- uses a different pattern, not affected

## Design

**Primary fix:** One-character change (`||` to `&&`) at all 13 fast-path locations.

**Secondary fix:** Free bitwise operators `operator&` and `operator|` in `qint_bitwise_backend.hpp` -- replace direct qbool gate loop with BitProxy-based logic, matching the compound-assign pattern. This handles -1 qubit indices via `is_quantum()` checks and classical folding.

## Acceptance Criteria

1. `a ^= b` with quantum `a`, classical `b` emits X gates for each bit where b=1
2. `a ^= b` with classical `a`, quantum `b` promotes `a` and emits CNOT gates
3. `a &= b` and `a |= b` with mixed operands emit correct gate sequences
4. `c = a & b` and `c = a | b` (free operators) with mixed operands emit correct gates
5. `a += b` with quantum `a`, classical `b` emits adder circuit
6. Both-classical fast path still works (zero gates, no qubits allocated)
7. All existing backend tests pass (regression)
8. `examples/or_circuit.cpp` with one-quantum one-classical shows correct gate output
