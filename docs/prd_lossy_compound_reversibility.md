# PRD — Compile-Time Reversibility for Lossy Compound Assignments

**Status:** Landed (2026-04-25). Supersedes the runtime `garbage_registry` /
`when_scope_garbage` approach shipped under sturm-h5it / sturm-pqs0 /
sturm-njul. The compile-time LO transpiler pass — matched by
`transpiler/src/matcher_lossy_op.{hpp,cpp}`, emitted by
`transpiler/src/lossy_rewrite_emitter.{hpp,cpp}` and
`transpiler/src/lossy_scope_exit_emitter.{hpp,cpp}`, with depth-first nesting
in `transpiler/src/lossy_nested_rewrite.{hpp,cpp}` — now performs the
allocate-compute-swap rewrite described below for every `*=`, `/=`, `%=`,
`&=`, `|=` on `qint_t`. Runtime headers `garbage_registry.hpp` and
`when_scope_garbage.hpp` were deleted in sturm-pw2f (LO-4).

**Scope tag:** `lossy-reversibility`.

**Supersedes / retires:**

- `include/sturm/control/garbage_registry.hpp` — to be deleted.
- `include/sturm/control/when_scope_garbage.hpp` — to be deleted.
- The CSWAP-and-leak controlled-path tails in
  `include/sturm/qtypes/qint_arith_v3.hpp` (`operator*=`, `/=`, `%=`) and
  `include/sturm/qtypes/qint_bitwise_v3.hpp` (`operator&=`, `|=`) — to be
  replaced.
- Sections 5 and 6 of `docs/TODO_reversibility_deferrals.md` — deferrals
  collapse into this PRD.

---

## 0. One-sentence goal

Every lossy compound assignment (`*=`, `/=`, `%=`, `&=`, `|=`) is rewritten by
the transpiler at compile time into a source-level *allocate–compute–swap*
statement plus a paired *scope-exit swap–uncompute* cleanup, eliminating the
runtime garbage registry and moving the last lossy-op uncomputation concern
onto B10's compile-time path.

---

## 1. Motivation

**B10 (`docs/01_principles.md`):** *Uncomputation is a compile-time concern,
not a runtime concern.* Inverses are emitted by the transpiler as explicit
`uncompute_*` / `invert(routine)(...)` calls in the generated source file.

**Current violation.** Controlled lossy compound assignments inside `WHEN`
scopes cannot be made in-place reversible without retained state. The
sturm-h5it epic retained that state as an anonymously-leaked W-qubit register
per call, tracked at runtime in a thread-local `garbage_registry`. sturm-njul
added a WhenGuard-owned scope-exit *consumer* that pops the per-scope records
and optionally logs a diagnostic; it emits **no gates** because the forward
lossy step has already destroyed the inputs needed to reverse it (see
`when_scope_garbage.hpp` "Honest scope" block). The leak is therefore
permanent at gate level, and the registry is a runtime bookkeeping layer with
no gate-level consumer — a direct inversion of B10.

**Design lever.** B1 makes the backend dumb: quantum routines are fixed at
compile time. B10 makes uncomputation compile-time. The missing step is to
desugar each lossy compound assignment into the Bennett-local
compute-use-uncompute pattern at the transpiler layer, so the ancilla is a
named source-level `qint` with ordinary C++ RAII scope (B6), and the scope
exit emits a gate-level uncomputation — same machinery used today for every
other reversible ancilla.

---

## 2. The unified rewrite

For every lossy compound assignment, the transpiler performs the same
statement-to-statement rewrite. User-facing source is unchanged (P2 / P3
ergonomics preserved; the user writes `a *= b` exactly as today).

### 2.1 Multiplicative and divisive forms

Source:

```cpp
a *= b;
```

Transpiled to (inserted at the statement):

```cpp
qint tmp_mul = a * b;    // OOP: fresh ancilla = a * b via lib_mul_dsl
swap(a, tmp_mul);         // a holds a*b, tmp_mul holds old_a
```

Source:

```cpp
a /= b;
```

Transpiled to:

```cpp
qint tmp_q, tmp_r;               // fresh ancillas
divide(a, b, tmp_q, tmp_r);      // OOP: tmp_q = a/b, tmp_r = a%b
swap(a, tmp_q);                   // a holds quotient, tmp_q holds old_a
```

Source:

```cpp
a %= b;
```

Transpiled to:

```cpp
qint tmp_q, tmp_r;
divide(a, b, tmp_q, tmp_r);
swap(a, tmp_r);                   // a holds remainder, tmp_r holds old_a
```

### 2.2 Bitwise lossy forms

Source:

```cpp
a &= b;
```

Transpiled to:

```cpp
qint tmp_and;
and_oop(a, b, tmp_and);     // tmp_and ^= (a & b) via lib_c_AND_dsl / CCX sweep
swap(a, tmp_and);            // a holds a&b, tmp_and holds old_a
```

Source:

```cpp
a |= b;
```

Transpiled to:

```cpp
qint tmp_or;
or_oop(a, b, tmp_or);       // tmp_or ^= (a | b) via lib_or_dsl
swap(a, tmp_or);
```

### 2.3 Scope-exit cleanup

At the end of `tmp_*`'s enclosing lexical scope (the C++ block in which the
compound assignment appeared), the transpiler emits the matching cleanup in
LIFO order *before* the block's closing brace:

```cpp
// ... end of block ...
swap(a, tmp_mul);                  // undo the forward swap
invert(operator*)(a, b, tmp_mul);  // OOP multiplication adjoint: zeros tmp_mul
// tmp_mul's destructor releases its qubits (now all |0>).
```

For division/modulo the adjoint is `invert(divide)(a, b, tmp_q, tmp_r)`; both
ancillas are returned to `|0>` by construction since the divide invariant
`a == tmp_q * b + tmp_r` holds after the swap is undone. For AND/OR, the
adjoint is the gate-reverse CCX sweep (self-inverse when `tmp_and == a & b`,
which holds after the swap is undone).

### 2.4 One rewrite shape, five operators

| Source          | OOP primitive (existing DSL) | Ancillas | Swap target |
|-----------------|------------------------------|----------|-------------|
| `a *= b`        | `lib_mul_dsl`                | 1 × 2W   | lower-W of product |
| `a /= b`        | `lib_div_dsl`                | 2 × W    | `tmp_q`     |
| `a %= b`        | `lib_div_dsl`                | 2 × W    | `tmp_r`     |
| `a &= b`        | `lib_c_AND_dsl` (bitwise)    | 1 × W    | `tmp_and`   |
| `a \|= b`       | `lib_or_dsl`                 | 1 × W    | `tmp_or`    |

The scope-exit cleanup is mechanically derived from the same DSL primitive
via `invert(·)` (P9).

---

## 3. Forward-and-uncompute semantics

### 3.1 State trace (AND, worked example)

```
                                          (a,        b, tmp)
start:                                    (old_a,    b, 0      )
and_oop(a, b, tmp):                       (old_a,    b, old_a&b)   invariant: tmp == a&b ✓
swap(a, tmp):                             (old_a&b,  b, old_a  )   invariant broken

// ... scope body sees a as old_a&b ...

swap(a, tmp):                             (old_a,    b, old_a&b)   invariant restored
invert(and_oop)(a, b, tmp):               (old_a,    b, 0      )   CCX sweep zeros tmp ✓
// tmp destructor releases qubits.
```

The CCX sweep that forms `tmp ^= a & b` is its own gate-reverse; once the
swap is undone the invariant `tmp == a & b` holds and a second application
zeros `tmp`. The same structural argument holds for OR, MUL, DIV, MOD — the
primitive's own adjoint zeros the ancilla *after the swap is undone*.

### 3.2 Gate cost

Roughly 2× forward-op gates, plus `2 × swap` (one forward, one reverse).

- Uncontrolled context: swap is a qubit-index relabel (zero gates), so the
  added cost is exactly `forward + invert(forward)`.
- `WHEN(ctrl)` context: swap is `W × Fredkin` (each Fredkin = 3 × CCX via the
  `a^=b; b^=a; a^=b` BitProxy idiom under active control), uncompute runs
  under the same control chain.

The doubled cost is the irreducible Bennett price of reversing a lossy
operation. The current garbage-registry path appeared cheaper only because
it was incorrect (silently leaked entangled registers).

### 3.3 Scope boundary

`tmp_*`'s lexical scope is the smallest enclosing C++ block (`{ ... }`)
containing the compound-assign statement. The transpiler emits the
swap-and-uncompute pair immediately before the block's closing brace, placed
in LIFO order with respect to any other transpiler-emitted uncomputes in the
same scope (consistent with `uncompute_pass.cpp`).

`main`'s top-level scope is the single exception: no scope-exit cleanup is
emitted at end of main (program terminus; see §4.3).

---

## 4. Semantic consequences

### 4.1 Net effect inside a function body

A lossy compound assignment on a function parameter, with `tmp_*` falling out
of scope at the function's closing brace, makes the function **identity on
that parameter**. The swap-and-uncompute at function exit reverts `a` to its
pre-call value.

Example:

```cpp
void mask(qint& a, const qint& m) {
    a &= m;          // mutation is observable here and below...
    // ...body...
}                    // ...but reverted at the closing brace.
// After mask(x, m): x unchanged.
```

This is **the correct behaviour** under reversibility: a function that
destructively consumes information about a parameter cannot exist without
either (i) returning a fresh qint that carries the information away, or (ii)
accepting an explicit out-parameter where the garbage register lives. The
transpiler enforces that discipline by construction.

### 4.2 User idioms that persistently mutate

Three supported idioms for writing a function that persistently mutates via a
lossy op:

1. **Inline at the call site.** The lossy op lives in the caller's scope;
   its `tmp_*` scope extends to the caller's closing brace. Simplest.

2. **Out-of-place return.** Write `qint masked(const qint& a, const qint& m)`
   returning a fresh qint produced by `and_oop`. No swap, no tmp cleanup —
   the returned qint is the "new a". The caller binds the return value to a
   fresh `qint`.

3. **Explicit garbage out-param.** Write `void mask_with_save(qint& a, const
   qint& m, qint& saved_old_a)`. The saved-copy XOR of `a` goes into
   `saved_old_a`, lives in the caller's scope, and becomes the caller's
   responsibility.

The transpiler does **not** silently promote `tmp_*` to an implicit
out-parameter (no hidden ABI change; P4's "composition of channels is
composition of functions" rule requires honest signatures).

### 4.3 End of main

`main`'s top-level scope terminates the program; no gates execute after it.
The transpiler accordingly does not emit scope-exit cleanup for `tmp_*`
ancillas whose scope is `main`'s outermost body. Top-level lossy ops in
`main` therefore persist, matching the user expectation that measurements at
end of `main` observe the post-operation state.

### 4.4 User discipline

Follows from §4.1 and §4.3. Quoting the redesign discussion: *the user should
be clever to not let complicated qints run out of scope* where persistence is
wanted. The transpiler does not guess; it honours C++ scope.

---

## 5. `WHEN` integration

### 5.1 Classicality branching of `swap`

**Status:** Landed (sturm-arce, 2026-04-25). The behaviour described below is
the implementation, not a target. `sturm::swap(qint_t<W>&, qint_t<W>&)` lives
at `include/sturm/qtypes/lossy_oop.hpp:79-100` and consults
`sturm_get_thread_context()->control_stack.depth()` to dispatch:

- `WHEN(expr)` body where `expr` is classical `true`: equivalent to no
  control; control-stack depth is 0 and `swap` is a qubit-index relabel
  (zero gates) — `value` / `super_mask` / `qubits` / `owning_` are
  exchanged via `std::swap`.
- `WHEN(expr)` body where `expr` is classical `false`: body skipped; nothing
  emitted.
- `WHEN(expr)` body where `expr` is superposed: control-stack depth is
  `>= 1` and `swap` emits a per-bit controlled-SWAP (Fredkin) by calling
  `lib_swap_dsl(BitProxy(a, i), BitProxy(b, i))` for each `i ∈ [0, W)`,
  lifted into the active control chain via BitProxy. Each `lib_swap_dsl`
  call is the standard 3-CNOT chain `a^=b; b^=a; a^=b`, so the per-bit
  cost under control is `3 × CCX` (Fredkin). The scope-exit reverse swap
  and the uncompute are lifted identically.

The uncontrolled fast path inside the current
`operator*=` / `operator/=` / `operator%=` / `operator&=` / `operator|=`
(release + pointer relabel, no gates) is preserved *at runtime dispatch* —
when `detail::current_control == nullptr` and both operands are classical,
the operator short-circuits to classical C++ arithmetic without emitting
anything, and no `tmp_*` is allocated. This matches B3 (dispatch-time
specialisation) and B10 together: the transpiler's compile-time rewrite
produces the ancilla + cleanup pair, and the runtime short-circuit is a
`detail::current_control == nullptr && both-classical` early return inside
each operator.

### 5.2 Control-lifted uncompute

Inside `WHEN(ctrl)`, the scope-exit reverse `swap(a, tmp_*)` is a per-bit
CSWAP on `ctrl`, and `invert(and_oop)` / `invert(lib_mul_dsl)` /
`invert(lib_div_dsl)` are likewise lifted through the active control stack.
All of this happens through the existing BitProxy and qbool operator lifting
— no new control primitives required.

### 5.3 No more CSWAP-and-leak

The controlled path that today does *compute → CSWAP → leak*
(`qint_arith_v3.hpp` lines 138–162 for `*=`, 206–232 for `/=`, 260–277 for
`%=`; `qint_bitwise_v3.hpp` lines 97–120 for `&=` and the symmetric block
for `|=`) is removed. Its replacement is the transpiler-emitted pair
described in §2.

---

## 6. DSL invariants (unchanged)

**The DSL is the source of truth for each forward op. The redesign adds no
new primitives; it only pairs each existing OOP DSL entry with its synthesized
adjoint.**

Forwards (already in `include/sturm/lib/`):

- `lib_mul_dsl` (shift-and-add, OOP 2W result register).
- `lib_div_dsl` (non-restoring, OOP quotient + remainder).
- `lib_mod_dsl` (same divide kernel, remainder as primary output).
- `lib_c_AND_dsl` (Toffoli sweep, OOP target register) — used per-bit for
  `a &= b`.
- `lib_or_dsl` (`c ^= (a | b)`, OOP target register) — used per-bit for
  `a |= b`.

Adjoints (existing from P9c synthesis, confirmed ships with every OOP
primitive): `invert(lib_mul_dsl)`, `invert(lib_div_dsl)`,
`invert(lib_mod_dsl)`, `invert(lib_c_AND_dsl)`, `invert(lib_or_dsl)`. These
must be callable at the cleanup site with the same argument shape as the
forward call; if any is currently only registered as a self-adjoint or
missing a concrete `__*_adj` companion, that is a prerequisite fix
(trackable as a child issue of the migration epic; see §9).

The operators `qint_t::operator*=`, `/=`, `%=`, `&=`, `|=` keep calling the
DSL in their runtime-dispatch bodies — the compile-time rewrite does not
replace the DSL call, it surrounds the OOP DSL call with the `qint tmp =
... ; swap(a, tmp);` framing and the matching cleanup, using `invert(·)` to
reach the same DSL's adjoint.

---

## 7. Transpiler pass (new)

A new compile-time pass, tentatively `LO` (lossy-op rewrite), runs before the
existing adjoint-synthesis passes (Phase T family) and performs the textual
rewrites of §2. Sketch:

1. **Match.** Find every `CompoundAssignOperator` node whose opcode is one of
   `*=`, `/=`, `%=`, `&=`, `|=` and whose operands are `qint_t<W>` (or
   qbool-compatible).
2. **Emit forward pair.** Replace the statement with the `qint tmp_* = …;
   swap(a, tmp_*);` expansion. The `tmp_*` name is mangled to avoid collision
   with user identifiers (`__sturm_tmp_<opcode>_<counter>`).
3. **Emit scope-exit cleanup.** Locate the enclosing `CompoundStmt`
   (lexical block). Insert the matching `swap(a, tmp_*); invert(<op>)(a, b,
   tmp_*);` sequence at the end of that block, after every existing
   transpiler-emitted uncompute so LIFO order is preserved. For `main`'s
   outermost `CompoundStmt`, skip emission (§4.3).
4. **Propagate through WHEN.** The pass is control-chain-agnostic; it emits
   source-level `swap` and `invert(op)(...)` calls, and the existing BitProxy
   / qbool lifting + the compile-time uncompute pass handle WHEN
   control-lifting as a downstream concern. No dedicated CSWAP path in the
   LO pass itself.
5. **Refuse inverse synthesis of the enclosing function naively.** A
   reversible function whose body contains a lossy compound assignment gets
   its adjoint synthesized in the usual way from the *rewritten* source
   (after LO runs). No special case required.

Tracking: the LO pass should be gated by the same annotation /
translation-unit scope as the existing reversible-synthesis passes; no
cross-TU requirement (the rewrite is local per statement).

---

## 8. Deletions

The following artefacts are deleted as part of this PRD landing:

**Headers.**

- `include/sturm/control/garbage_registry.hpp`
- `include/sturm/control/when_scope_garbage.hpp`

**Runtime wiring.**

- Every `detail::garbage_registry::register_garbage(...)` call site in
  `include/sturm/qtypes/qint_arith_v3.hpp` and
  `include/sturm/qtypes/qint_bitwise_v3.hpp`.
- The `WhenScopeGarbage` RAII member of `WhenGuard` and its consume helper
  in `include/sturm/control/when.hpp`.
- The `STURM_GARBAGE_REPORT` compile-time macro and env-var plumbing.
- `sturm::detail::ScopedGarbageConsumeGuard` (the TLS opt-out flag).

**Tests.**

- `tests/test_when_scope_garbage_consume.cpp` (njul).
- `tests/backend/test_mul_div_upperw_garbage.cpp` (pqs0).
- Any `tests/h5it*` / `tests/sturm-h5it.*` discoverability tests that assert
  post-op registry state — replaced with correctness tests that measure the
  cleaned ancilla is `|0>` after the enclosing scope exits.

**Documentation.**

- Sections 5 and 6 of `docs/TODO_reversibility_deferrals.md` are removed;
  their subject matter is resolved by this PRD.

---

## 9. Preserves (unchanged)

- **`docs/01_principles.md`**: P1–P9d, B1–B11 all still hold. This PRD is
  the delivery of B10 for the last operator family that wasn't already on
  compile-time uncomputation.
- **`include/sturm/lib/*_dsl.hpp`**: all DSL primitives unchanged. The
  redesign routes through them, not around them.
- **Uncontrolled fast paths**: the `detail::current_control == nullptr &&
  both-operands-classical` short-circuit in each operator stays as-is —
  that branch never allocates `tmp_*` and never emits.
- **`sturm-njul` / `sturm-pqs0` scopes of concern** are resolved here — no
  separate follow-up epic for gate-level uncomputation is needed, because
  this PRD *is* the gate-level uncomputation.

---

## 10. Migration plan

Suggested epic structure (to be filed as bd issues):

1. **LO-1.** Add or confirm registered adjoints for every DSL OOP primitive
   used by §4 (`lib_mul_dsl`, `lib_div_dsl`, `lib_mod_dsl`, `lib_c_AND_dsl`,
   `lib_or_dsl`). Gate: `invert(<dsl>)(…)` compiles and zeros ancillas on
   synthetic tests. No behaviour change for the compound operators yet.

2. **LO-2.** Implement the LO transpiler pass (match + forward-pair emission
   + cleanup emission + `main` exception). Unit tests on ASTs that do not
   exercise WHEN. Gate: rewritten source compiles, full test suite green.

3. **LO-3.** WHEN integration tests — controlled lossy ops go through the
   same rewrite; BitProxy / qbool lifting handles the CSWAP and controlled
   uncompute. Gate: controlled tests that currently rely on
   `garbage_registry::snapshot()` are rewritten to assert cleaned
   `|0>`-state ancillas post-scope-exit.

4. **LO-4.** Delete the runtime registry (§7). Cannot land until LO-2 and
   LO-3 are green, because existing tests and the `WhenScopeGarbage`
   consumer depend on the registry being present. This is a pure-deletion
   commit.

5. **LO-5.** Retire `docs/TODO_reversibility_deferrals.md` §5 and §6
   (remove those sections); update the doc's cross-references and the
   "Required Reading" list in `CLAUDE.md` if the entry becomes stale.

Each step is independently reviewable. LO-4 is the commitment point; until
it lands, both systems coexist.

---

## 11. Open design points (non-blocking)

These are spelled out as decisions to confirm during implementation, not
questions that gate this PRD.

- **OOP division signature.** Does `operator/` (free function) ship today
  with the `(a, b, q, r)` OOP form that LO-1 needs? If not, the minimal
  surface is a `sturm::detail::divide_oop(a, b, q, r)` free function used by
  the rewrite — not user-visible.
- **Name mangling.** The generated `__sturm_tmp_*` identifiers must not
  collide with user code. Stick to the double-underscore-prefix convention
  already used by `__fn_adj` (P9c).
- **Nested lossy ops.** `a *= (b & c)` nests two rewrites; the inner `&`
  produces an OOP result that feeds `*=`. The LO pass handles this by
  recursing depth-first — innermost first — so each `tmp_*`'s scope is
  consistent.

---

## 12. References

- **Principles:** `docs/01_principles.md` §B6 (RAII ancillas), §B10
  (compile-time uncomputation), §P9 / §P9c (adjoint synthesis), §P4 (`WHEN`
  scoping).
- **Deferrals being closed:** `docs/TODO_reversibility_deferrals.md` §5
  (controlled-lossy compound assigns leak inside WHEN) and §6 (`*=` upper-W
  / `/=` remainder).
- **Headers to be deleted:**
  `include/sturm/control/garbage_registry.hpp`,
  `include/sturm/control/when_scope_garbage.hpp`.
- **Headers to be rewritten:**
  `include/sturm/qtypes/qint_arith_v3.hpp`,
  `include/sturm/qtypes/qint_bitwise_v3.hpp`.
- **DSL primitives retained verbatim:** `include/sturm/lib/mul_dsl.hpp`,
  `include/sturm/lib/div_dsl.hpp`, `include/sturm/lib/mod_dsl.hpp`,
  `include/sturm/lib/c_and_dsl.hpp`, `include/sturm/lib/logic_dsl.hpp`,
  `include/sturm/lib/adder_dsl.hpp`.
- **Tracker issues that collapse into this PRD:** bd `sturm-h5it`
  (closed children 1–5 remain valid history; new work is LO-1…LO-5),
  bd `sturm-pqs0`, bd `sturm-njul`.
