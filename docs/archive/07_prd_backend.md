# PRD — STURM Backend

Status: design lock from Socratic planning session. This PRD captures the decisions that govern backend implementation. It does not prescribe file-level layout or coding order — that belongs in an implementation plan.

Companion docs: `01_principles.md` (frontend principles — some are revised here), `04_prd_frontend.md` (frontend PRD), `05_spec_frontend.md` (frontend spec — some sections superseded here).

---

## 1. Scope and goals

The STURM backend is the execution layer below the C++ frontend. It receives gate requests emitted by library operations and dispatches them to one of three modes: count only, append to an intermediate representation, or simulate via the Orkan statevector backend. It is also responsible for mixed classical/quantum operand handling, operand promotion, gate counting, and the bookkeeping required for RAII-based uncomputation of temporaries.

Non-goals for this phase:

- No multi-threaded execution (design leaves room but single-threaded for now).
- No language bindings beyond C++ (no Python, Julia, etc.).
- No runtime gate decomposition. All decompositions are written out explicitly in library op source.
- No user-visible qubit allocation, no manual uncomputation API, no explicit destructor-style calls from user code.
- No support for more than 17 qubits total in SIMULATE mode (hardcoded; may be raised later).
- No mid-circuit measurement correctness outside SIMULATE mode (known gap, deterministic placeholder behavior).

## 2. Target users and distribution

STURM is consumed by users who write C++ quantum algorithms using the DSL exposed by the frontend, either by hand or via coding agents fed the DSL spec. There are no external language wrappers. Distribution is as a static library plus C++ headers, built from source by the user against Orkan via CMake `FetchContent`. AGPL-3.0 license inherited from Orkan; STURM adopts AGPL-3.0.

## 3. Architectural layering

```
┌─────────────────────────────────────────────────────────┐
│ User C++ code   c = (a + 5) >= 0                        │
├─────────────────────────────────────────────────────────┤
│ C++ frontend    operator+, operator>=, operator|, etc.  │
│                 qint_t<W>, qbool, RAII destructors,     │
│                 uncompute closures, promotion_mask      │
├─────────────────────────────────────────────────────────┤
│ Library ops     quantum_add / c_quantum_add,            │
│                 quantum_mul / c_quantum_mul, ...        │
│                 (two flavors: uncontrolled, controlled) │
├─────────────────────────────────────────────────────────┤
│ Layer A (C++)   dispatch_gate                           │
│                 classicality checks, mixed-operand      │
│                 rules, promotion, classical reduction   │
├─────────────────────────────────────────────────────────┤
│ Layer B (C ABI) execute_gate                            │
│                 counting, mode switch, Orkan calls      │
├─────────────────────────────────────────────────────────┤
│ Orkan           statevector simulation                  │
└─────────────────────────────────────────────────────────┘
```

- **Frontend / library ops**: C++, templated, owns type semantics and uncomputation.
- **Layer A (`dispatch_gate`)**: C++, intelligent. Receives `qint&`/`qbool&` handles and bit positions, inspects classicality, applies the dispatch rule table, performs promotion, and either handles the op classically or falls through to Layer B with physical qubit indices.
- **Layer B (`execute_gate`)**: C ABI. Dumb sink. Receives `(gate_kind, physical_indices, n, param)`, increments the gate counter, and routes to one of the three execution modes.
- **Orkan**: vendored via CMake `FetchContent`. Called directly from Layer B in SIMULATE mode.

## 4. Virtual gate set

STURM defines **18 primitive gates**. This set replaces the four-primitive sink contract from principle B4 of `01_principles.md`.

| Gate    | Arity | classical_effect | permutation |
|---------|-------|------------------|-------------|
| X       | 1     | FLIP             | yes         |
| Y       | 1     | FLIP             | yes         |
| Z       | 1     | NONE             | no          |
| H       | 1     | BRANCH           | no          |
| S       | 1     | NONE             | no          |
| T       | 1     | NONE             | no          |
| P(θ)    | 1     | NONE             | no          |
| Rx(θ)   | 1     | BRANCH           | no          |
| Ry(θ)   | 1     | BRANCH           | no          |
| Rz(θ)   | 1     | NONE             | no          |
| CX      | 2     | FLIP             | yes         |
| CY      | 2     | FLIP             | yes         |
| CZ      | 2     | NONE             | no          |
| CRx(θ)  | 2     | BRANCH           | no          |
| CRy(θ)  | 2     | BRANCH           | no          |
| CRz(θ)  | 2     | NONE             | no          |
| CCX     | 3     | FLIP             | yes         |
| SWAP    | 2     | FLIP             | yes         |

Taxonomy:

- **`classical_effect`** (enum): `NONE` — phase-only; all-classical operands → skip. `FLIP` — permutation on computational basis; all-classical → mutate classical values. `BRANCH` — creates superposition; all-classical operands must be promoted before applying.
- **`permutation`** (bool): gate is a permutation of computational-basis states. Used to identify free-relabeling opportunities (not currently exploited but reserved).

Gate parameters are a single `double` (rotation angle θ) or unused. No multi-parameter gates.

Not in the primitive set, decomposed at library-op source time:

- CSWAP (Fredkin) → CCX chain.
- Shifts, rotates → register relabeling (uncontrolled) or SWAP/CCX chains (controlled).
- All multi-controlled gates beyond CCX.
- CH, CS, CT, CP, CCY, CCZ, CCRx, CCRy, CCRz, CCCX — when a controlled variant is needed but absent from the primitive set, the op author writes the decomposition inline.

## 5. Execution modes

Single enum, mutually exclusive:

```c
typedef enum { STURM_MODE_COUNT_ONLY, STURM_MODE_APPEND, STURM_MODE_SIMULATE } sturm_mode_t;
```

- **COUNT_ONLY** (default): Layer B increments a gate counter and returns. No state allocation, no IR buffer.
- **APPEND**: Layer B pushes `(gate_kind, qubits, param)` to an in-memory IR buffer owned by the `BackendContext`. Buffer shape inspired by the circuit/gate classes in the reference C backend but adapted for STURM's 18-gate set. The reference repo is guidance only; no code is copied.
- **SIMULATE**: Layer B calls the corresponding Orkan function. CRx/CRy/CRz are decomposed into `CX + Rx/Ry/Rz` identities at this point only (Orkan does not expose controlled rotations).

Gate counting is unconditional: Layer B increments the counter in every mode before the mode-dependent action.

### Mode selection

The mode is set once at program start via a CLI argument (`--mode=count|append|simulate`) parsed by `sturm::init_from_args(argc, argv)`. A programmatic `sturm::set_mode()` is also exposed for users who do not use the CLI helper. The active mode lives in a `thread_local` pointer to a `BackendContext`, with a process-wide default instance. Single-threaded now; the `thread_local` indirection costs nothing and leaves multi-threading open.

### Measurement by mode

Measurement is a separate op, not part of the 18-gate set. Per mode:

- **COUNT_ONLY**: returns the stored classical value of the qint. Deterministic placeholder.
- **APPEND**: same as COUNT_ONLY.
- **SIMULATE**: returns a real sampled result from Orkan.

User code that branches on measurement under COUNT_ONLY/APPEND will diverge from SIMULATE behavior. Accepted known gap.

## 6. Qubit management

- Max qubits: **17, hardcoded**. Exceeding the budget is a hard error. May be raised in a later revision.
- `QubitPool` is the single source of qubit indices. It hands out qubits initialized to |0⟩ and receives releases on destruction of `qint`/`qbool` objects.
- The `BackendContext` owns the Orkan `state_t`, pre-allocated to 17 qubits at initialization. No dynamic resize.
- Users never see, allocate, or name qubits (principle B7 stands).

## 7. Classical / quantum unification

`qint`/`qbool` are unified types carrying both a classical value and a per-bit "in superposition" mask (`super_mask`, already present in the frontend). Operations dispatch on operand classicality at the Layer A level using the rule table below.

### Dispatch rule table (Layer A)

Evaluated per-gate invocation, with operands being a set of `(qint&, bit_position)` references.

1. **Increment counter** at Layer B regardless of outcome (handled via fall-through to Layer B).
2. **All operands classical**:
   - `classical_effect == NONE` → no-op (skip).
   - `classical_effect == FLIP` → mutate classical values in-place per the gate's permutation; do not call Layer B.
   - `classical_effect == BRANCH` → promote all operands (see §8), then fall through as if all were quantum.
3. **Mixed operands** (some classical, some quantum):
   - **Classical control with value 0**: gate never fires. Skip (still count? no — Layer B is not called).
   - **Classical control with value 1**: strip the control, reduce to the base gate. Table-driven reduction, e.g. `CX(ctrl=1, q)` → `X(q)`, `CCX(c1=1, c2=1, q)` → `X(q)`, `CCX(c1=1, c2=q2, q)` → `CX(c2, q)`. The reduction table is a finite static map from `(gate_kind, pattern of classical 1s in control positions)` to a reduced `gate_kind`.
   - **Classical target with quantum operands**: promote the target (see §8).
   - After reduction / promotion, all remaining operands are quantum. Look up physical qubit indices via `qint.qubits[i]` and call Layer B.
4. **All operands quantum**: look up physical indices and call Layer B directly.

### Toffoli under classical controls

The reduction table handles Toffoli's mixed-control cases by listing every pattern of (classical-0, classical-1, quantum) across its two control slots. At most nine combinations, all static.

### const-correctness

A `const qint&` cannot be promoted. If a library op may need to promote an operand, it must take the operand as non-const. The `promotion_mask` and `super_mask` fields are not declared `mutable`.

## 8. Operand promotion

When Layer A determines that a classical operand must become quantum (BRANCH gate on classical, or classical target under quantum control), it promotes the operand in place.

- **Partial promotion**: only the bits needed for the current operation become quantum. Unused bits remain classical. The `super_mask` tracks per-bit quantum-ness; `qubits[i]` holds the physical index for bits whose mask bit is set, and is unused (sentinel) for classical bits.
- **Initialization**: newly promoted bits are allocated from the `QubitPool` (which hands out |0⟩). For each bit whose classical value is 1, Layer A emits an `X` through Layer B to set the state to |1⟩. Each `X` emission is a normal gate and counts.
- **Promotion mask**: each `qint`/`qbool` carries a `promotion_mask` field (one word). Bits set in the mask are those that were |1⟩ at promotion time and must be flipped back to |0⟩ on release.
- **Release-time zeroing**: when a `qint`/`qbool` is destroyed, before releasing qubits to the pool, Layer A applies `X` to every bit set in `promotion_mask` (via Layer B). This ensures the pool only ever receives |0⟩-state qubits. The pool itself performs no runtime state checks.
- **One-way**: promotion is permanent until measurement. No automatic demotion.
- **Measurement**: on classical conversion, the `promotion_mask` is cleared; the measurement op handles the state-release path.
- **const**: promotion is a mutation; `const` operands cannot be promoted.

## 9. Uncomputation (Strategy B)

Adopted: **RAII-triggered inverse replay via semantic uncompute closures**. No gate-log replay. No AST / expression templates.

### Model

Every `qint`/`qbool` carries an `uncompute_op` field: a tagged union encoding the semantic inverse of the operation that produced this object. On destruction, the destructor runs the uncompute op, then applies the `promotion_mask` X gates, then releases qubits to the pool.

Strategy B is chosen over Strategy A (expression templates + Bennett tree) because:
- The frontend already evaluates eagerly; expression templates would require rewriting every operator.
- C++ destruction order of full-expression temporaries (reverse order of construction) gives correct uncompute ordering for free.
- Closures require only additive changes to existing operator return types, not a rewrite.

### Tagged-union uncompute entries

`uncompute_op` is a fixed-size struct with a small tag and inline data. No heap allocation. No `std::function`. Shape:

```cpp
struct uncompute_op {
    enum class kind : uint8_t {
        NONE,
        ADD_CONST,    // inverse of +=c is -=c (data: int64_t c)
        SUB_CONST,
        BITWISE_SELF, // bitwise ops are self-inverse (data: op kind, ref to inputs)
        COMPARE,      // re-run the comparison (data: op kind, refs to lhs/rhs)
        ADD_QINT,     // subtraction of source (data: ref to source qint)
        DIV_INVERSE,  // bespoke per-op inverse for division
        MUL_INVERSE,  // bespoke per-op inverse for multiplication
        MOD_INVERSE,
        // ... one entry per operator that produces an uncomputable result
    } tag;
    union { ... } data;
};
```

The exact list of tag values tracks the set of operators that produce results. New operators that produce results add entries here.

### Bennett discipline

Every operator implementation **must leave its inputs pristine** and write its output into fresh ancilla allocated from the pool. This is a correctness invariant: the uncompute closure of a temporary references its inputs, so the inputs must be in their original state when the closure runs. Non-invertible operations (measurement) clear the `uncompute_op` tag to `NONE`.

### Scope unit and user rules

- Uncomputation applies to **anonymous temporaries inside a statement**. C++'s full-expression destruction rules give correct order.
- **Named variables** uncompute when they go out of block scope. The user is responsible for not hoarding quantum state they do not intend to measure.
- **No manual uncompute API**. No `uncompute(c)` helper, no explicit destructor call. If the user wants earlier uncomputation, they introduce a block scope `{ auto t = …; use(t); }`.
- **Reassignment to a named variable** uncomputes the old value first (then releases), then assigns the new value. Long uncompute chains are the user's responsibility to avoid.
- **WHEN-blocks** inherit uncomputation: temporaries allocated inside the block die at block exit and run their closures automatically. The existing `WhenGuard` destructor is extended to trigger this.

### Known semantic note

Operators may widen the `super_mask` as they execute (e.g. addition propagates carries, widening the set of bits touched by superposition). The corresponding inverse operator must undo exactly the same gates that were applied forward, which may act on a wider set of bits than the original inputs. This is correct and expected: uncomputation returns touched qubits to |0⟩, not "apply fewer gates."

## 10. Control propagation through WHEN blocks

WHEN-blocks install a thread-local control qubit. Library operations that may be called inside a WHEN block come in two hand-written flavors:

- `quantum_add(…)`: uncontrolled implementation using permutation gates.
- `c_quantum_add(…, qbool& ctrl)`: controlled implementation, hand-decomposed. Every gate that would have been `X` is written as `CX`; every `CX` is written as `CCX`; `CCX` under a control is written as a hand-coded ancilla-based decomposition.

The public op entry point checks the active WHEN control and dispatches to one of the two flavors. No runtime gate synthesis or decomposition tables. The `c_*` variants contain fully explicit gate sequences.

Library ops are preferentially written in terms of permutation gates (Toffoli-based adders, comparators, multipliers — as in the reference repo). This keeps `c_*` variants simple because permutation gates are closed under single control within the 18-gate set.

Nested WHEN blocks collapse to a single ancilla control per principle B5. There is always at most one active control at a time; therefore the primitive set only needs single-controlled variants.

## 11. C ABI and header organization

Shape B: C++ is the primary user API, with a stable C ABI as the internal boundary.

- `include/sturm/core.h` — `extern "C"` header exposing the Layer B sink, `BackendContext`, `QubitPool`, mode selection, and measurement. Opaque handle types. POD parameters only.
- `include/sturm/qtypes/*.hpp` — C++ templated frontend (`qint_t<W>`, `qbool`, operator overloads, destructors, uncompute closures, promotion_mask). Includes `core.h`. This is what users write against.

The C ABI gains no functionality for the C++ user, but isolates ABI-stable code (the sink + state) from the templated layer that must be rebuilt with the user's compiler. `extern "C"` adds zero call overhead on the hot path.

## 12. Orkan integration

- Vendored via CMake `FetchContent_Declare` + `FetchContent_MakeAvailable`, pinned to a specific commit of `github.com/Timo59/orkan`.
- STURM built as a **static library**. Orkan statically linked into it. Avoids PIC complications and simplifies distribution.
- License: STURM is AGPL-3.0 to match Orkan's license (inherited via linking).
- 18-gate mapping: STURM's X/Y/Z/H/S/T/P/Rx/Ry/Rz/CX/CY/CZ/CCX/SWAP map 1:1 to Orkan calls. STURM's CRx/CRy/CRz are decomposed at the SIMULATE call site into `CX + rotation` identities (Orkan has no native controlled rotations). Decomposition in other modes is not performed; COUNT_ONLY and APPEND see the original gate kind.
- `BackendContext` owns the `state_t`, initialized to 17 qubits at startup. No dynamic resize.

## 13. Compile-time optimization notes

- **Explicit template instantiation** of common `qint_t<W>` widths in the library to avoid re-instantiating them in every user translation unit. This is the primary mechanism by which STURM reduces user compilation time, independent of static-vs-shared library choice.
- **Const-correctness** prevents accidental promotion of const operands, which is a compile-time check.
- **No runtime gate decomposition** means no lookup tables or synthesis passes in the hot path — all decomposition happens at source-code write time and benefits from normal compiler optimization.

## 14. Spec edits required

Concrete edits to existing docs implied by this PRD:

- `docs/01_principles.md` §B4: rewrite the four-primitive sink contract to the 18-gate `execute_gate` interface.
- `docs/01_principles.md` §B5: stands (single control, CCX exception noted).
- `docs/01_principles.md` §B6: stands; Strategy B refinement added.
- `docs/04_prd_frontend.md` §3 non-goals: "no uncomputation, no ancilla" is superseded by the Strategy B section of this PRD.
- `docs/04_prd_frontend.md` §11 stubs list: ditto.
- `docs/05_spec_frontend.md` line 349: delete the "virtual indices 0/1–64/65–128/ancilla" text. Replace with: library ops iterate over `qint.qubits[i]` by relative bit position; all physical indices flow through `execute_gate`; there is no global virtual-index encoding.

These edits are not performed in this PRD. They are tracked as bd issues.

## 15. Open items (explicitly deferred)

These were discussed and intentionally left for later:

- Per-op opt-out from uncomputation for hot paths.
- Multi-threaded backend.
- Raising the 17-qubit limit.
- Correct measurement semantics in non-SIMULATE modes (mid-circuit measurement is a known gap).
- Aliasing detection for operations like `qadd(a, a)` (principle 03_issues_and_pitfalls #2 still stands).
- Three-register op (div/mod/mul) virtual gate-set layout was flagged as a concern but dissolves once the primitive sink is defined to operate on physical qubit indices with 1–3 operands per gate; library ops above the sink handle arbitrary register counts.

---

End of PRD.
