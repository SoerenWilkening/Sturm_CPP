# Principles

## Programming Principles

**P1. Functions are channels.** A C++ function with quantum parameters *is* a completely positive map. There is no separate "channel" wrapper type. Composition of functions is composition of channels.

**P2. The quantum–classical boundary is a type boundary; superposition is an invisible runtime property of individual bits.** Quantum types are `qint` and `qbool`. Classical types are `int64_t` and `bool`. Measurement exists as an operation but has no user-callable name: it is invoked implicitly by type conversion from a quantum type to a classical type (e.g. `int64_t x = static_cast<int64_t>(a);` or assignment to a classical variable). There is no exposed `measure()` function. Within a quantum type, individual bits may or may not be in superposition; the user does not see or declare which. There is no source-language distinction between "quantum bit" and "classical bit" — there are only bits, some of which are currently superposed.

**P3. Operations are operations.** No syntactic distinction between unitary, noise, preparation, and measurement. All are channels and are spelled the same way at the source level.

**P4. Quantum control is lexical scope, unified with classical branching.** The construct `WHEN(expr) { ... }` specializes at each entry based on the runtime classicality of `expr`:
- If `expr` evaluates to a classical `false`, the body is skipped (dead code).
- If `expr` evaluates to a classical `true`, the body executes uncontrolled.
- If `expr` evaluates to a superposed bit, the body executes under quantum control, with `expr`'s value AND-folded into the enclosing control chain.

`if` on a quantum-typed value is a compile error; the user must write `WHEN`. The control expression `expr` is live across the whole scope: neither `expr` itself nor any free variable it reads — directly or through a function call — may be modified within the body. The transpiler materializes `expr` into an ancilla at scope entry and schedules its uncompute at scope exit; the uncompute call re-reads every free variable of `expr`, so mutating any of them between entry and exit produces an incorrect adjoint. Violation is undefined behavior.

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

**P9. Routines are invertible by synthesized adjoint.** Every reversible routine has an adjoint form. Library primitives and library routines ship with hand-written adjoints. User-written routines do **not** require a hand-written adjoint: the transpiler synthesizes a named `__fn_adj` companion from the forward body using the dual-operator rules (`+=` ↔ `-=`, `theta +=` ↔ `theta -=`, `phi +=` ↔ `phi -=`, `^=` self-adjoint), reversing statement order within each scope and reversing loop iteration order where applicable. `invert(foo)` is a free function returning the synthesized (or hand-registered) adjoint.

**P9a. Return-style and out-param forms are both accepted.** A user may declare `qbool marked(qint x, int T)` (returns a freshly allocated `qbool`) or `void marked(qbool& a, qint x, int T)` (writes an existing out-param). The transpiler synthesizes an out-param companion from a return-style declaration; adjoint synthesis always targets the out-param shape, so the injected uncompute call is `__marked_adj(a, x, T)`. This mirrors the existing treatment of `c = a | b` vs `uncompute_or(c, a, b)`.

**P9b. Input immutability is declared by const-ness, not inferred.** A quantum parameter passed by value (`qint x`) is read-only inside the callee — the local copy may be rewritten, but the caller's original is untouched. A parameter passed by non-const reference (`qint& x`) may be mutated, and the synthesized adjoint un-mutates it in reverse order alongside un-writing the output. `const qint&` is read-only.

**P9c. The transpiler always emits a distinct `__fn_adj` companion.** Even when the forward body is self-inverse (e.g. a single `^=` assignment), the transpiler emits a named adjoint function and registers it via `STURM_REGISTER_ADJOINT(fn, __fn_adj)`. Audit tooling name-matches `__*_adj` tokens at uncompute sites, so collapsing self-adjoint routines to self-registration would destroy the placement audit. Users who want the optimization may still write `STURM_REGISTER_ADJOINT(fn, fn)` by hand.

**P9d. Bodies that cannot be inverted are diagnosed at definition.** The transpiler rejects a reversible routine whose body contains: (i) measurement (quantum → classical conversion, explicit or implicit), (ii) classical I/O or any observable classical side effect, or (iii) a call to an unregistered user routine whose adjoint cannot be synthesized transitively. The diagnostic fires at the forward-function definition site, not at the `invert(fn)` use site, so the error points at the code the user would fix.

---

## Backend Principles

**B1. Runtime dispatch, no stored sequences.** A quantum routine is an ordinary compiled C++ function. Each call re-executes its body. Overloaded operators dispatch on the current classicality masks of their operands and emit primitive operations to the active sink. There is no pre-built gate sequence, no cached DAG, and no separate "build phase." Variable-iteration algorithms (e.g. Grover with a runtime-determined iteration count) are expressed as ordinary classical loops around routine calls.

**B1a. Pluggable sinks, three standard modes:**
- *Counter mode (default)*: primitives increment per-op counters. Zero storage.
- *Circuit mode (test)*: primitives are appended to a circuit object for inspection.
- *Direct mode*: primitives are executed immediately against a simulator backend.

**B1b. Inversion is by explicit adjoint.** No source-level or runtime auto-inversion in the default path. Primitives ship with adjoint forms. Library routines ship with hand-written adjoints. `invert(foo)` performs a registered lookup. Macro-based or AST-based auto-generation is the default: the transpiler pass at compile time registers inverse operations for every construct. Runtime auto-inversion has been retired.

**B2. Minimal state.**
- Per `qint`: an `int64_t` value and a `uint64_t` classicality mask (1 = bit is in superposition).
- Per `qbool`: inherits `qint_t<1>` — an `int64_t` value and a `uint64_t` classicality mask (bit 0 only).
- Per thread: the active sink and the current control chain (collapsed to a single bit).

No entanglement graphs, no global state analysis, no per-call caching.

**B3. Dispatch-time specialization.** Each overloaded operator branches on its operands' classicality masks and emits the minimum necessary primitives. Fully classical operands produce zero quantum emissions and are handled as ordinary C++ arithmetic on the underlying `int64_t`/`bool`. The optimization happens where the mask information is live — at dispatch — not in a later pass.

**B4. Eighteen-gate execute_gate interface.** The backend sink accepts a fixed set of 18 primitive gates dispatched through a single `execute_gate(gate_kind, physical_indices, n, param)` C-ABI function. The gate set is: X, Y, Z, H, S, T, P(θ), Rx(θ), Ry(θ), Rz(θ), CX, CY, CZ, CRx(θ), CRy(θ), CRz(θ), CCX, SWAP. Gates are classified by `classical_effect` (NONE, FLIP, BRANCH) and a `permutation` flag. Higher-level operations (`qadd`, `qand`, `qnot`, `qmul`, …) are library functions that decompose into this gate set at source-code write time. No runtime decomposition or gate-synthesis pass exists.

**B5. Primitives have uncontrolled and singly-controlled forms only.** No multi-controlled variants exist in the primitive set. Nested `WHEN` scopes collapse their control chain into a single ancilla via AND at scope entry, so the innermost control is always one bit.

**B5a. Depth-1 control-stack invariant (sturm-a3t4).** The thread-local control stack is bounded to depth ≤ 1: at any point in execution, at most one control bit is live on the stack. Library and user code that needs effectively higher-arity control must lift via the *outer flag + `WHEN`* pattern — compute an outer `qbool` flag (e.g. `qbool f = a & b;`), then enter a single `WHEN(f) { ... }` whose body uses at most one further `WHEN` level. The lifted emitters (`emit_X_lifted`, `emit_CX_lifted`, `emit_CCX_lifted` in `include/sturm/qtypes/qbool_ops.hpp`) assert `depth <= 1`; depth ≥ 2 is a programmer error, not a supported runtime path.

**B6. Ancillas and control temporaries are scope-bound via C++ RAII.** Classical control expressions allocate no ancillas; superposed control expressions allocate one ancilla per `WHEN` scope and uncompute it on scope exit. Allocation is invisible in the user-level IR — it is folded into the operations that need it.

**B7. Qubit indices are backend-managed.** User code never sees, allocates, or names qubits. The backend assigns physical indices to the bits of a `qint`/`qbool` at construction.

**B8. Mask transfer is per-operation and local.** Each arithmetic and logical operation defines a mask transfer function: how the output classicality mask derives from the input classicality masks. Carry propagation widens masks across bit positions. No global mask analysis is performed.

**B9. Two optimization layers in the default runtime path (inlined dispatch, classicality specialization) and one global optimization pass at transpile time.** The C++ compiler inlines dispatch logic and constant-folds branches; classicality specialization optimizes the primitive stream at dispatch time; the transpiler performs whole-function rewrites (PJ-1 fusion, PJ-3 hoisting, PJ-4 dead-ancilla elimination, PM5 peephole reordering).

**B10. Uncomputation is a compile-time concern, not a runtime concern.** Inverses are emitted by the transpiler as explicit uncompute_* / ccnot_inplace / invert(routine)(...) calls in the generated source file. Destructors release qubit indices to the pool; they do not emit gates. Ancilla scope (B6) still uses C++ RAII, but scope exit and uncomputation are now separate concerns.

**B11. Adjoint synthesis reverses statement order AND loop iteration order.** Within a scope, ops are uncomputed in LIFO order (already enforced by `uncompute_pass.cpp`). Within a classical loop whose body contains quantum mutations, the synthesized adjoint reverses the iteration (negates stride, swaps init/cond bounds) and emits the adjoint of each body op in reverse iteration order. Nested loops reverse innermost-first. Gate parameters in the adjoint reference the *value* the forward emitted (captured at dispatch or recomputed from unchanged inputs), not a re-derived expression over mutated state — this guarantees `Rθ(v) · Rθ(-v) = I` bit-exactly at the gate-stream level.

---

## Modular arithmetic contract

Modular arithmetic primitives on `qint_t` (`add_mod`, `mul_mod`, `pow_mod`, and the transpiler-folded forms `(a + b) % n`, `(a * b) % n`, `pow(a, x) % n`) require that all operands satisfy the precondition `a, b ∈ [0, n)` (and for `pow_mod`, the base `a ∈ [0, n)`; the exponent `x` may be any non-negative integer up to the register width). The library does not check this precondition at runtime; supplying out-of-range operands is undefined behavior, in the same trust model as classical C/C++ modular reduction. See `docs/prd_modular_arithmetic.md` for the full specification, the rationale for omitting the check, and the per-primitive contract details.
