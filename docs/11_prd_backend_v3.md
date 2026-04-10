# Backend PRD v3 — DSL-Native Gate Execution

Status: design lock (supersedes `09_prd_backend_v2.md`)

## 1. Motivation

PRD v2 delivered a frozen five-primitive backend and a library of
reversible arithmetic circuits built from those primitives. Two
execution chains now exist:

- **Chain A (backend):** primitives (X, XOR, AND, phase, phi_add)
  operate on `SimState` and call Orkan directly. Library functions
  (Cuccaro adder, MUL, DIV, etc.) compose these primitives. Neither
  chain touches `execute_gate()`.

- **Chain B (frontend):** `qint_t<W>` operators dispatch through the
  Sink interface or through stub backend-enabled bodies that stamp
  `uncompute_op` tags but emit no real circuits. `execute_gate()`
  exists with three modes (COUNT_ONLY, APPEND, SIMULATE) but is
  never reached from DSL code.

The two chains are disconnected. When a user writes `a += b` in the
DSL, no gates are counted, recorded, or simulated. The v3 design
unifies these chains by making `execute_gate()` the single chokepoint
and rewriting the library in DSL style using `qbool` operators.

## 2. Architectural principle

Four layers, strict downward dependency:

1. **`execute_gate()`** — the single chokepoint. Every gate passes
   through it. Mode dispatch (COUNT_ONLY, APPEND, SIMULATE) happens
   here and nowhere else.

2. **Primitives** — five frozen ops (X, XOR, AND, phase, phi_add).
   Each is a one-line call to `execute_gate()` via `BackendContext`.
   `SimState` is removed; primitives no longer call Orkan directly.

3. **`qbool` operators** — `^=`, `&`, `|`, `~`, `flip()`. Each
   operator calls primitives internally. WHEN (controlled) context
   is handled automatically by reading the control stack.

4. **Library + Frontend** — library functions (Cuccaro, MUL, DIV,
   etc.) are rewritten using `qbool` operators. `qint_t<W>` operators
   call library functions. Self-bootstrapping: each level uses only
   the level below it.

## 3. SimState replaced by BackendContext

`SimState` (`sturm::v2::SimState`) is removed. All primitives take
`BackendContext&` instead and call `execute_gate()`:

```cpp
inline void primitive_X(BackendContext& ctx, uint32_t qubit) {
    execute_gate(ctx, STURM_GATE_X, &qubit, 1, 0.0);
}
```

`BackendContext` already exists with mode, gate_count, IR buffer, and
`orkan_state_ptr`. `execute_gate()` already dispatches to `exec_count`
/ `exec_append` / `exec_simulate`. The primitives simply stop
bypassing it.

`WhenLift`, `AncillaManager`, and all library functions switch from
`SimState&` to `BackendContext&`. The `sturm::v2` namespace is
retired; new code lives in `sturm::`.

## 4. `qbool` as DSL type

`qbool` gains operators that call primitives internally:

```cpp
qbool& operator^=(const qbool& other);           // CNOT
qbool& operator^=(const AndExpr<qbool>& expr);   // single Toffoli
qbool& operator^=(const OrExpr<qbool>& expr);    // 2 CNOT + Toffoli
qbool  flip();                                    // X gate
```

### 4.1 Ownership

- **Owning `qbool`**: destructor releases qubit (created by
  materialization, explicit allocation, ancilla).
- **Non-owning `qbool`**: destructor does NOT release (returned by
  `a[i]`, aliases a register qubit). Distinguished by an `owning_`
  flag.

### 4.2 WHEN handling

`qbool` operators read the control stack (thread-local, stored in
`BackendContext`). Based on control depth they emit the appropriately
lifted gate:

- 0 controls: direct primitive (X / CX / CCX)
- 1 control: CX / CCX / CRy / CRz
- 2+ controls: `c_AND` fold (library code, uses one borrowed ancilla)

No separate `_when` variant of any function is needed. The control
stack is always consulted.

## 5. Lazy `AndExpr` / `OrExpr`

`operator&` and `operator|` on `qbool` return lazy expression types
rather than materializing immediately:

```cpp
template<typename T> struct AndExpr { const T& a; const T& b; };
template<typename T> struct OrExpr  { const T& a; const T& b; };
```

These also work at the `qint_t<W>` level (bitwise AND/OR return
`AndExpr<qint_t<W>>` / `OrExpr<qint_t<W>>`).

### 5.1 Efficient consumption via `^=`

- `c ^= (a & b)` — single Toffoli. No ancilla.
- `c ^= (a | b)` — 2 CNOT + 1 Toffoli. No ancilla.
  Identity: `a | b = a ^ b ^ (a & b)` for single bits, so
  `c ^= (a | b)` = `CNOT(a,c) + CNOT(b,c) + Toffoli(a,b,c)`.

This is the **only** optimized consumption path. Only `operator^=`
needs overloads for the expression types.

### 5.2 Implicit conversion (materialization)

Any other use of an `AndExpr`/`OrExpr` triggers implicit conversion
to `qbool`/`qint_t<W>`. This allocates ancilla qubit(s), computes
the result, and returns an owning value. The destructor uncomputes
and frees ancillas (RAII).

This is safe because quantum arithmetic (Cuccaro, etc.) is
non-destructive to input operands: the temporary is still in its
original state when the destructor runs uncomputation.

Materialization cost:
- `AndExpr`: 1 Toffoli + ancilla (mirrored on destruction).
- `OrExpr`: De Morgan expansion with ancilla (mirrored on
  destruction).

### 5.3 Scope of the optimization

The `c ^= (a & b)` pattern (Toffoli without ancilla) appears
primarily in the Cuccaro adder's MAJ/UMA gates. All other library
operations either create fresh AND values (`qbool r = a & b`, one
Toffoli, clean) or build on top of `+=`/`-=` which handle it
internally.

## 6. Bit subscript

`a[i]` returns a non-owning `qbool` aliasing `qubits[i]`. No gate
is emitted — it is a pure index copy. The returned `qbool`
participates in all `qbool` operators.

Classical semantics would be `a & (1 << i)`, but the `&` in the
quantum case would introduce an unnecessary CNOT gate. Direct index
access avoids this.

Lifetime: the `qint_t<W>` must outlive any non-owning `qbool`
derived from it.

## 7. Library in DSL style

Library functions are rewritten using `qbool` operators instead of
direct primitive calls. This makes them self-bootstrapping:

```cpp
void maj_gate(qbool& a, qbool& b, qbool& c) {
    b ^= c;           // CNOT (or lifted under WHEN)
    a ^= c;           // CNOT
    c ^= (a & b);     // single Toffoli via AndExpr
}
```

The `_when` variants (`maj_gate_when`, `lib_add_cuccaro_when`, etc.)
are eliminated. All functions work transparently under WHEN because
`qbool` operators read the control stack automatically.

### 7.1 Self-bootstrapping hierarchy

```
Level 1: primitive_X, primitive_XOR, primitive_AND, primitive_phase,
         primitive_phi_add
         Each calls execute_gate(ctx, ...).

Level 2: qbool operators (^=, &, |, ~, flip)
         Call primitives. Handle WHEN automatically.

Level 3: Library (Cuccaro adder, SUB, MUL, DIV, CMP, etc.)
         Use qbool operators. No direct primitive calls.

Level 4: qint_t<W> operators (+=, -=, *=, /=, %=, ==, <, etc.)
         Call library functions.
```

### 7.2 Ancilla access

Library functions allocate ancilla qubits as owning `qbool`
instances. The `qbool` destructor releases the qubit.
`AncillaManager` is replaced by direct allocation from
`BackendContext`'s qubit pool.

## 8. Frontend wiring

`qint_t<W>` compound-assign operators call library functions:

| Operator | Library call | Category |
|----------|-------------|----------|
| `+=`     | Cuccaro ripple-carry adder | in-place |
| `-=`     | Two's complement via Cuccaro | in-place |
| `^=`     | Per-bit CNOT | in-place |
| `~`      | Per-bit X | in-place |
| `*=`     | Shift-and-add (controlled +=) | out-of-place + move |
| `/=`     | Non-restoring division | out-of-place + move |
| `%=`     | Division remainder | out-of-place + move |
| `&=`     | Per-bit Toffoli | out-of-place + move |
| `\|=`    | Per-bit OR | out-of-place + move |
| `==`     | Tree-of-XNOR + `c_n_AND` | comparison -> qbool |
| `<`      | Subtraction sign bit | comparison -> qbool |

The old `dispatch_binary` / Sink path is retired for backend-enabled
builds.

## 9. Uncompute strategy

The existing `uncompute_op` mechanism (RAII destructors, tagged union)
is preserved. Out-of-place operations (MUL, DIV, AND-assign, etc.)
stamp an uncompute tag on the result. The destructor emits the inverse
circuit. For in-place operations (ADD, SUB, XOR) no uncompute tag is
needed because the operation is directly invertible.

## 10. Non-goals

- No QFT adder (`phi_add` available for explicit use; not default).
- No runtime circuit optimisation.
- No separate controlled-op variants (WHEN lift is automatic).
- No change to the 18-gate virtual gate set in `gate_kind.h`.
- No change to the three execution modes (COUNT_ONLY, APPEND,
  SIMULATE).
- No `a.toffoli(b, c)` — the lazy `AndExpr` approach via
  `c ^= (a & b)` handles this case cleanly.
