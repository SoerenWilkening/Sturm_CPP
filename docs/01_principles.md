# Principles

## Programming Principles

**P1. Functions are channels.** A C++ function with quantum parameters *is* a completely positive map. There is no separate "channel" wrapper type. Composition of functions is composition of channels.

**P2. The quantum–classical boundary is a type boundary; superposition is an invisible runtime property of individual bits.** Quantum types are `qint` and `qbool`. Classical types are `int64_t` and `bool`. Measurement exists as an operation but has no user-callable name: it is invoked implicitly by type conversion from a quantum type to a classical type (e.g. `int64_t x = static_cast<int64_t>(a);` or assignment to a classical variable). There is no exposed `measure()` function. Within a quantum type, individual bits may or may not be in superposition; the user does not see or declare which. There is no source-language distinction between "quantum bit" and "classical bit" — there are only bits, some of which are currently superposed.

**P3. Operations are operations.** No syntactic distinction between unitary, noise, preparation, and measurement. All are channels and are spelled the same way at the source level.

**P4. Quantum control is lexical scope, unified with classical branching.** The construct `WHEN(expr) { ... }` specializes at each entry based on the runtime classicality of `expr`:
- If `expr` evaluates to a classical `false`, the body is skipped (dead code).
- If `expr` evaluates to a classical `true`, the body executes uncontrolled.
- If `expr` evaluates to a superposed bit, the body executes under quantum control, with `expr`'s value AND-folded into the enclosing control chain.

`if` on a quantum-typed value is a compile error; the user must write `WHEN`. The operands of a `WHEN` control expression must not be modified within the scope; violation is undefined behavior.

**P4a. Classical-to-quantum conversion is implicit and free.** A `qint` may be initialized or assigned from any classical integer with no ceremony: `qint a = 42;` is valid, produces a fully-classical `qint` (mask = 0, value = 42), emits nothing to the sink, and after inlining costs the same as `int64_t a = 42;`. The converting constructor `qint(int64_t)` is *not* `explicit`. The reverse direction (quantum → classical) is measurement and *is* explicit (see P2).

**P5. No gates, no qubits in user code.** The user works with `qint`, `qbool`, and exactly four primitives:
1. `qbool(p)` — preparation with `P(|1⟩) = p`
2. `q.theta += d` — amplitude rotation
3. `q.phi += d` — phase rotation
4. `a ^= b` — XOR-assignment (the CNOT/Toffoli/… family, unified)

Named gates (H, CNOT, Toffoli, X, Y, Z) do not exist in user code. If a DSL program reads like a circuit diagram, it is wrong. This applies equally to human and AI users.

**P6. QECC is a higher-order function.** Error correction has type `Channel → Channel`. It wraps a routine in encoding and decoding. It is not a pragma, an annotation, or a compiler flag.

**P7. The core is dimension-agnostic.** The `qint`/`qbool` types and the classicality-tracking machinery are a binary (d=2) library built on a dimension-agnostic core abstraction. Qutrit and anyonic libraries are peers, not subclasses.

**P8. Superposition is monotone.** Once a bit enters superposition, it remains in superposition until measurement. Uncomputation does not re-classicalize bits in the tracker. Carry propagation and other data flow widen classicality masks but never narrow them.

**P9. Routines are invertible by explicit adjoint.** Every routine has an adjoint form. Library routines ship with hand-written adjoints. User-written routines carry manual adjoints, which are themselves plain C++ functions written using dual operators (`+=` ↔ `-=`, `theta +=` ↔ `theta -=`, `phi +=` ↔ `phi -=`, `^=` self-adjoint) and named adjoint primitives where no operator exists. `invert(foo)` is a free function returning the registered adjoint.

---

## Backend Principles

**B1. Runtime dispatch, no stored sequences.** A quantum routine is an ordinary compiled C++ function. Each call re-executes its body. Overloaded operators dispatch on the current classicality masks of their operands and emit primitive operations to the active sink. There is no pre-built gate sequence, no cached DAG, and no separate "build phase." Variable-iteration algorithms (e.g. Grover with a runtime-determined iteration count) are expressed as ordinary classical loops around routine calls.

**B1a. Pluggable sinks, three standard modes:**
- *Counter mode (default)*: primitives increment per-op counters. Zero storage.
- *Circuit mode (test)*: primitives are appended to a circuit object for inspection.
- *Direct mode*: primitives are executed immediately against a simulator backend.

**B1b. Inversion is by explicit adjoint.** No source-level or runtime auto-inversion in the default path. Primitives ship with adjoint forms. Library routines ship with hand-written adjoints. `invert(foo)` performs a registered lookup. Macro-based or AST-based auto-generation may be added later as opt-in mechanisms.

**B2. Minimal state.**
- Per `qint`: an `int64_t` value and a `uint64_t` classicality mask (1 = bit is in superposition).
- Per `qbool`: a `bool` value and a `bool` classicality flag.
- Per thread: the active sink and the current control chain (collapsed to a single bit).

No entanglement graphs, no global state analysis, no per-call caching.

**B3. Dispatch-time specialization.** Each overloaded operator branches on its operands' classicality masks and emits the minimum necessary primitives. Fully classical operands produce zero quantum emissions and are handled as ordinary C++ arithmetic on the underlying `int64_t`/`bool`. The optimization happens where the mask information is live — at dispatch — not in a later pass.

**B4. Eighteen-gate execute_gate interface.** The backend sink accepts a fixed set of 18 primitive gates dispatched through a single `execute_gate(gate_kind, physical_indices, n, param)` C-ABI function. The gate set is: X, Y, Z, H, S, T, P(θ), Rx(θ), Ry(θ), Rz(θ), CX, CY, CZ, CRx(θ), CRy(θ), CRz(θ), CCX, SWAP. Gates are classified by `classical_effect` (NONE, FLIP, BRANCH) and a `permutation` flag. Higher-level operations (`qadd`, `qand`, `qnot`, `qmul`, …) are library functions that decompose into this gate set at source-code write time. No runtime decomposition or gate-synthesis pass exists.

**B5. Primitives have uncontrolled and singly-controlled forms only.** No multi-controlled variants exist in the primitive set. Nested `WHEN` scopes collapse their control chain into a single ancilla via AND at scope entry, so the innermost control is always one bit.

**B6. Ancillas and control temporaries are scope-bound via C++ RAII.** Classical control expressions allocate no ancillas; superposed control expressions allocate one ancilla per `WHEN` scope and uncompute it on scope exit. Allocation is invisible in the user-level IR — it is folded into the operations that need it.

**B7. Qubit indices are backend-managed.** User code never sees, allocates, or names qubits. The backend assigns physical indices to the bits of a `qint`/`qbool` at construction.

**B8. Mask transfer is per-operation and local.** Each arithmetic and logical operation defines a mask transfer function: how the output classicality mask derives from the input classicality masks. Carry propagation widens masks across bit positions. No global mask analysis is performed.

**B9. Two optimization layers, no global pass.** The C++ compiler optimizes the dispatch logic (inlined operator overloads, constant-folded branches, eliminated dead paths when masks are statically known). Classicality specialization optimizes the emitted primitive stream at dispatch time. No global circuit-optimization pass is required or provided. Cross-primitive optimization (constant folding of successive `+=` on superposed values, peephole rewrites) is deferred to a future stage.
