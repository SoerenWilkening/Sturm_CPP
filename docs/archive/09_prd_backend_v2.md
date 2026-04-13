# Backend PRD v2 — Minimal-Primitive Architecture

Status: design lock (supersedes `07_prd_backend.md`)

## 1. Motivation

Earlier backend iterations shipped a hand-written gate emitter per
high-level routine (`quantum_add`, `quantum_mul`, `c_quantum_div`, …).
Each routine produced a gate sequence that the Python layer then
executed. With the move to C++ and an execute-on-call simulator (no
stored sequences), that architecture no longer pays for itself:

- There is no sequence to optimise after the fact, so fusing adders
  with their uncompute gains nothing at runtime.
- Every new op doubles the surface area that must also grow a
  *controlled* variant, an *adjoint* variant, and — eventually — a
  fault-tolerant rewrite.
- A future Clifford+T / error-corrected backend would need to rewrite
  every hand-written emitter individually.

The v2 design collapses the backend to a tiny, frozen primitive set
and builds *everything* else as a C++ library on top of it.

## 2. Architectural principle

Three layers, each with one job:

1. **Backend** — executes a tiny, frozen set of primitives. Never
   grows.
2. **Library** — pure C++ functions that call *only* backend
   primitives. All arithmetic, all logic, all comparisons live here.
3. **Frontend DSL** — ergonomic surface (`c = a | b`, `x += y`,
   `WHEN cond: …`). Lowers to library calls.

Fault-tolerance payoff: when error correction is added, only the
backend's `AND` implementation changes (Toffoli → Clifford+T / magic
state). The entire library inherits fault tolerance for free.

## 3. Minimal backend primitive set

The backend executes exactly these ops. Nothing else.

### 3.1 Classical-reversible primitives

| Op     | Gate     | Purpose                                  |
|--------|----------|------------------------------------------|
| `X`    | NOT      | Bit flip, constant load, two's complement|
| `XOR`  | CNOT     | Copy/xor workhorse — every adder needs it|
| `AND`  | CCNOT    | The only non-Clifford classical op       |

`X` is retained (rather than derived) because it is Clifford — free
under FTQC — and so ubiquitous that deriving it would be pure
friction.

### 3.2 Phase primitives (non-classical)

Phase operations **cannot** be expressed in `{X, XOR, AND}` because
those are permutation matrices on the computational basis. Phase gates
introduce complex amplitudes and are a structurally different kind of
operation. They are first-class backend primitives.

| Op        | Gate            | Notes                                 |
|-----------|-----------------|---------------------------------------|
| `phase`   | `R_y(θ)`        | Single-qubit rotation                 |
| `phi_add` | `R_z(θ)` family | Phase-domain addition primitive       |

(Convention for this project: `phase = R_y`, `phi_add = R_z`. Any
higher-level phase-domain arithmetic — Draper-style additions, QFT
building blocks — is built from these two in the library layer.)

### 3.3 Qubit resource management

| Op                   | Purpose                               |
|----------------------|---------------------------------------|
| `allocate_ancilla()` | Borrow a scratch qubit in \|0⟩        |
| `free_ancilla(q)`    | Return a scratch qubit (must be \|0⟩) |

Everything else — `OR`, `NAND`, `c_AND`, `c_n_AND`, `SWAP`, `c_SWAP`,
`ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `POW`, `CMP`, `EQ`, `LT`, … — is
**library code**, not backend.

## 4. Control handling (`WHEN` lift)

`WHEN` blocks lift the control count of every enclosed primitive by
one. The lift machinery is recursive and terminates at the backend
primitives:

- `X` under `WHEN a` → `XOR(a, target)`
- `XOR(b → c)` under `WHEN a` → `AND(a, b, c)`
- `AND(b, c → d)` under `WHEN a` → `C^3-X`, which is library code
  (`c_AND`) using the Nielsen-Chuang sandwich pattern and one borrowed
  ancilla. No leaked ancillas: the mirror half uncomputes.
- Deeper nesting → recursive cascade through `c_n_AND`.

Because `c_AND` and `c_n_AND` are library code built from backend
`AND` calls, the backend itself never sees a multi-controlled gate.

**Discipline requirement:** every library routine must be written so
its widest emitted primitive is `AND` (CCNOT). Wider controls only
ever appear *transiently* during `WHEN` lift and are resolved by
`c_AND`/`c_n_AND`.

## 5. Phase ops and `WHEN`

Phase primitives also lift under `WHEN`. Controlled phase rotations
are emitted directly by the backend (they are standard two-qubit
gates). The library may offer multi-controlled phase rotations built
from phase + `AND` scaffolding, but those decompositions live in the
library, not the backend.

## 6. SWAP — two implementations, same name

`SWAP(a, b)` has a cheap uncontrolled form and a structurally
different controlled form. The library must dispatch on context.

- **Uncontrolled `SWAP`**: *zero gates*. Swap the qubit-index arrays
  inside the `qint` / `qbool` wrappers at compile time. Index relabel,
  nothing emitted.
- **Controlled `c_SWAP` (Fredkin)**: index relabel is impossible
  because the relabel would have to be conditional on a runtime qubit
  value. The library emits `XOR(a,b); AND(ctrl, b, a); XOR(a,b);` —
  2 XORs and 1 AND per qubit pair.

**Trap to avoid:** never implement `SWAP` as "three XORs always" and
then rely on `WHEN` lift to control it. That would turn three CNOTs
into three CCNOTs (three ANDs) instead of one. `WHEN` lift must
dispatch `SWAP` to the Fredkin form directly.

General rule: any op whose uncontrolled form is a *compile-time
relabel* must have a separate emit-path for the controlled case.
`SWAP` is the canonical example; the "move result into place" pattern
(next section) is the other major consumer.

## 7. In-place semantics for out-of-place operations

Most non-trivial quantum logic operations cannot write their result
onto an input register. `a |= b` is classically in-place, but
quantumly `a` cannot be overwritten with `a | b` — the operation is
not reversible with respect to `a` alone.

The only way to realise `a |= b` is:

1. Allocate fresh register `c`.
2. Compute `c = a | b` out-of-place (reversible: `c` starts |0⟩).
3. *Move* the result into `a`'s slot.

Step 3 has two regimes:

- **Uncontrolled context**: step 3 is *free*. Swap the qubit-index
  arrays of the `qint` wrappers. The old `a` qubits (now held by `c`)
  carry garbage/entangled history and are handed to the garbage
  manager for later uncomputing.
- **Controlled context** (`WHEN cond: a |= b`): step 3 is *expensive*.
  Emit `n` Fredkin gates (one per qubit pair) gated on `cond`. Each
  Fredkin costs 2 XORs + 1 AND.

**Ops that are genuinely in-place** (write directly back onto an
input, no move needed):

- `XOR` / `b ^= a` — one CNOT
- `NOT` / `a = ~a` — X on each qubit
- `ADD` / `SUB` (Cuccaro-style) — `b += a` leaves `a` untouched and
  overwrites `b`. This is the principal reason Cuccaro is the standard
  adder.

**Ops that are out-of-place-with-move** (essentially everything
else):

- `OR`, `NAND`, `NOR`, `XNOR`
- `AND` as assignment: `c = a & b`
- `MUL`, `DIV`, `MOD`, `POW`
- Comparisons producing a qbool: `c = (a < b)`

The library's assignment-operator lowering must know which category
each op falls in and emit the correct move (relabel vs Fredkin
cascade).

**Garbage management:** after a relabel-move, the displaced register's
qubits still hold data entangled with the inputs. The ancilla/garbage
manager tracks these and schedules uncomputing.

## 8. Algorithm choices

Baseline algorithms for the library. All expressed in {X, XOR, AND}.

| Operation | Baseline                        | Upgrade path            |
|-----------|----------------------------------|-------------------------|
| ADD / SUB | Cuccaro ripple-carry, in-place   | Draper CLA (log-depth)  |
| MUL       | Shift-and-add (controlled ADD)   | Karatsuba-style         |
| DIV / MOD | Non-restoring division           | —                       |
| POW       | Repeated squaring via MUL        | —                       |
| CMP/EQ/LT | Tree-of-ANDs on XOR'd bits       | —                       |
| c_AND     | Nielsen-Chuang sandwich, 1 borrow| —                       |
| c_n_AND   | Recursive sandwich cascade       | —                       |

No QFT adder. Phase-domain arithmetic is *available* via `phi_add` for
algorithms that genuinely need phase kickback (Shor-style modular
arithmetic), but is not the default path.

## 9. Frontend surface (unchanged in spirit)

The DSL continues to expose rich operators:

```
a &= b;         // AND-assign  → out-of-place + move
c = a | b;      // OR           → out-of-place + move
x += y;         // ADD          → in-place (Cuccaro)
if (a == b) {}  // EQ + WHEN
SWAP(a, b);     // uncontrolled → index relabel
```

All sugar lowers to library calls, which in turn lower to backend
primitives.

## 10. Non-goals

- No QFT adder.
- No hand-written gate emitters for high-level operations.
- No backend-level multi-controlled gates (`C^n-X` is library).
- No runtime circuit optimisation (the simulator executes on call).
- No separate "controlled" backend op for anything except phase — all
  classical control is handled by `WHEN` lift over the three classical
  primitives.
