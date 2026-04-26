# PRD — Modular Arithmetic for `qint_t`

**Status:** Draft (2026-04-26).

**Scope tag:** `modular-arithmetic`.

**Owner:** _(unassigned)_

**Supersedes / retires:** none. This PRD adds new functionality.

---

## 0. One-sentence goal

Let users write classical-looking modular arithmetic — `(a + b) % N`,
`(a * b) % N`, `pow(a, x) % N` — on `qint_t` and have it lower to qubit- and
gate-efficient reversible primitives, with the same trust model as classical
C/C++ (user is responsible for ensuring operands satisfy
`a, b ∈ [0, N)`; library does not check).

---

## 1. Motivation

Algorithms such as Shor's factoring, RSA verification, ECC, and lattice
crypto are written in terms of modular addition, modular multiplication, and
modular exponentiation. The current `qint_t` API exposes `operator+`,
`operator*`, `operator%`, and `pow(qint, qint)`, but no fused modular forms.
Writing `(a * b) % N` today therefore lowers to:

1. `a * b` — allocates a `2W`-bit product register (in addition to the
   normal mul ancilla budget),
2. `% N` — does a full reversible divmod against that wide intermediate.

In a setting where qubits are the binding constraint (Shor on cryptographic
`N` already saturates the available register), the wide intermediate is
prohibitive. The standard remedy is *modular* primitives that interleave
reduction with the arithmetic, keeping the working register at `W+1` bits
throughout. Classical C/C++ programmers don't need this rewrite (hardware
multiply is one cycle), but in a reversible setting it is essential.

### 1.1 Why `pow` is its own case

For `pow(a, x) % N` with cryptographic `x`, the naïve form is not just
inefficient — it is silently wrong. `pow(a, x)` on `qint_t<W>` produces
`a^x mod 2^W` (the register wraps), and
`((a^x) mod 2^W) mod N ≠ a^x mod N` in general. This matches the classical
C/C++ pitfall exactly: `pow_u64(a, x) % N` is silently wrong for any case
that overflows `uint64_t`. The classical convention is to provide an
explicit `pow_mod(a, x, N)` (Python's `pow(a, b, c)`); we adopt the same
convention.

### 1.2 Design lever

The transpiler already rewrites operator-level expressions at the source
level (matchers + emitters under `transpiler/src/`). The DSL layer
(`include/sturm/lib/*_dsl.hpp`) already provides `lib_add_dsl`,
`lib_mul_dsl`, `lib_div_dsl`, `lib_mod_dsl`, `lib_pow_dsl`, each with a
sibling `*_dsl_adj.hpp` registering the synthesized adjoint. New modular
primitives plug into the same machinery: a new `lib_*_mod_dsl` primitive
plus its `_adj` sibling, and a new transpiler matcher that recognizes the
operator form and rewrites it to the primitive call.

---

## 2. The unified rewrite

### 2.1 Operator forms (always rewritten — no flag)

Source the user writes:

```cpp
qint_t<W> a, b, n;
qint_t<W> r = (a + b) % n;   // modular add
qint_t<W> p = (a * b) % n;   // modular mul
```

Transpiled to (statement-level rewrite):

```cpp
qint_t<W> r;
lib_add_mod_dsl(a.bits(), b.bits(), n.bits(), W, r.bits());

qint_t<W> p;
lib_mul_mod_dsl(a.bits(), b.bits(), n.bits(), W, p.bits());
```

The transpiler matcher recognizes the AST shape `(qint OP qint) % qint`
(and also the compound forms `r = a + b; r %= n;` collapsed by an existing
peephole pass) and rewrites it before gate emission. Because the rewrite is
unconditional, the wide-intermediate path is never built.

**Precondition (user responsibility):** `a, b ∈ [0, n)`. Behavior is
undefined otherwise. The library does not check. This matches the trust
model already accepted for `add_mod` / `mul_mod` in classical bignum
libraries (GMP, OpenSSL).

### 2.2 `pow(a, x) % N` — gated rewrite

Default behavior (no flag): `pow(a, x) % N` lowers to
`lib_pow_dsl(a, x) → tmp; lib_mod_dsl(tmp, N) → r`. This wraps at `2^W`
and matches classical `pow_u64(a, x) % N`. Documented as such.

With `STURM_MODULAR_POW` flag set, the transpiler substitutes the pattern
`pow(a, x) % N` with a call to `lib_pow_mod_dsl(a, x, N) → r`, which never
allocates a wide intermediate.

Independent of the flag, `pow_mod(a, x, N)` is also available as an
explicit free function — users writing crypto code may prefer the
explicit call site for clarity.

---

## 3. API surface

### 3.1 New free functions (always available)

```cpp
template <std::size_t W>
qint_t<W> add_mod(const qint_t<W>& a, const qint_t<W>& b, const qint_t<W>& n);

template <std::size_t W>
qint_t<W> mul_mod(const qint_t<W>& a, const qint_t<W>& b, const qint_t<W>& n);

template <std::size_t W>
qint_t<W> pow_mod(const qint_t<W>& base, const qint_t<W>& exp, const qint_t<W>& n);
```

Out-of-place forms returning a fresh `qint_t<W>`. Compound forms
(`add_mod_inplace`, etc.) deferred — see §7.

### 3.2 New DSL primitives (under `include/sturm/lib/`)

| Primitive | Header | Adjoint sibling |
|---|---|---|
| `lib_add_mod_dsl` | `lib/add_mod_dsl.hpp` | `lib/add_mod_dsl_adj.hpp` |
| `lib_mul_mod_dsl` | `lib/mul_mod_dsl.hpp` | `lib/mul_mod_dsl_adj.hpp` |
| `lib_pow_mod_dsl` | `lib/pow_mod_dsl.hpp` | `lib/pow_mod_dsl_adj.hpp` |

Each follows the existing convention: `<200 LoC` target, takes raw `Bit*`
arrays, allocates ancillas via `QubitPool`, releases in LIFO order, and is
registered through `STURM_REGISTER_ADJOINT(fn, __fn_adj)` from the sibling
`_adj` header that is auto-included from the bottom of the forward header.

### 3.3 Transpiler matcher

New matcher at `transpiler/src/matcher_modular_op.{hpp,cpp}` recognizes:

- `(qint OP qint) % qint` for `OP ∈ {+, *}`
- `pow(qint, qint) % qint` and `pow(qint, int64_t) % qint`

Rewrites are emitted by `transpiler/src/modular_rewrite_emitter.{hpp,cpp}`.
The pow rewrite is gated on `STURM_MODULAR_POW`; the add/mul rewrites are
unconditional.

### 3.4 Compiler flag

- **Name:** `STURM_MODULAR_POW`
- **Scope:** build-time CMake option, propagated as a preprocessor define
  to the transpiler driver.
- **Default:** OFF.
- **Effect:** Transpiler substitutes `pow(a, x) % N` ⇒
  `pow_mod(a, x, N)`. No effect on `add` / `mul` rewrites (those are
  always on).

The narrow naming (`STURM_MODULAR_POW`, not `STURM_FAST_MATH`) preserves
room for future per-region scope if a real use case demands it (§7).

---

## 4. Layering on existing DSL primitives

**Hard requirement (user-stated):** the new modular primitives must be
implemented on top of the existing DSL rules and primitives — they may not
emit gates directly, fork the dispatch path, or duplicate logic that
already lives in `lib_add_dsl` / `lib_mul_dsl` / etc.

| New primitive | Built from |
|---|---|
| `lib_add_mod_dsl(a, b, n)` | `lib_add_dsl` (W+1-bit add) → `lib_compare_dsl` vs `n` → controlled `lib_sub_dsl(n)` (one subtract) |
| `lib_mul_mod_dsl(a, b, n)` | shift-and-add loop calling `lib_add_mod_dsl` per partial product, OR direct interleaved-reduction multiplier — implementation detail |
| `lib_pow_mod_dsl(a, x, n)` | repeated-squaring loop calling `lib_mul_mod_dsl` |

Each layer exists already in non-modular form; the modular layer is a thin
wrapper that interleaves the reduction step. Adjoints synthesize naturally
from the same machinery used today for `lib_pow_dsl` and `lib_mod_dsl`
(B11 — adjoint synthesis reverses statement order and loop iteration order).

This satisfies B10 (uncomputation is a compile-time concern), B11 (loop
adjoints), and the lossy-compound PRD (no leaked garbage — every ancilla
allocated inside a modular primitive is released in LIFO order before the
primitive returns).

---

## 5. Trust model and precondition contract

The library does **not** check the precondition `a, b ∈ [0, n)`. Calling
`add_mod` / `mul_mod` / `pow_mod` (or the operator forms that lower to
them) with unreduced operands is undefined behavior — the result will be a
representable value but not the mathematical answer.

This is a deliberate choice for parity with classical C/C++ conventions
(GMP, Python's `pow(a,b,c)`, OpenSSL). The contract is documented in
three places:

1. The Doxygen header for each `*_mod` free function and each
   `lib_*_mod_dsl` primitive.
2. A new section in `docs/01_principles.md` (§ "Modular arithmetic
   contract"): one paragraph stating the precondition and pointing at this
   PRD.
3. The CMake help text for `-DSTURM_MODULAR_POW=ON`.

A future `qint_mod<N>` type could enforce the precondition statically by
construction; that work is explicitly out of scope here (§7) but the API
surface above does not preclude adding it later.

---

## 6. Acceptance criteria

1. **Primitive correctness.** `lib_add_mod_dsl`, `lib_mul_mod_dsl`,
   `lib_pow_mod_dsl` produce the mathematically correct result for
   randomly sampled `(a, b, n)` (resp. `(a, x, n)`) with `a, b < n`,
   verified by simulation against a classical reference.
2. **Adjoint correctness.** Forward followed by adjoint restores the
   computational-basis state bit-exactly for each primitive
   (round-trip test, mirroring the existing `test_div_mod_dsl_adjoint`
   pattern).
3. **Operator-form lowering.**
   - `(a + b) % n` ⇒ `lib_add_mod_dsl` (always).
   - `(a * b) % n` ⇒ `lib_mul_mod_dsl` (always).
   - `pow(a, x) % n` ⇒ `lib_pow_mod_dsl` iff `STURM_MODULAR_POW=ON`,
     else falls back to `lib_pow_dsl + lib_mod_dsl`.
   Verified by transpiler snapshot tests.
4. **Qubit budget.** Each modular primitive's peak ancilla usage stays
   within `W + O(1)` for add/mul and `O(W)` for pow (vs. `2W + O(W)` for
   the naïve compose-then-reduce path). Verified by counter-sink test.
5. **No leaked ancillas.** Every primitive releases all allocated qubits
   to the pool by the time it returns (existing
   `test_lossy_ancilla_cleaned` framework, extended).
6. **Cross-validation oracle.** A test fixture runs the same source
   program under both modes (default and `-DSTURM_MODULAR_POW=ON`) on
   inputs satisfying the precondition and asserts identical output
   distributions for `pow_mod`. (For add/mul, both lowerings are the
   same once the rewrite is unconditional, so no separate fixture is
   required.)
7. **Documentation.** Precondition is documented at all three sites
   listed in §5.

---

## 7. Non-goals / explicitly deferred

- **`qint_mod<N>` type.** A type-safe wrapper that constructs reduced
  values and statically enforces the precondition. Useful for compile-time
  fixed-modulus code (ECC over a fixed curve, fixed-prime fields). Not
  useful for Shor's (modulus is a runtime value). Defer until a concrete
  caller asks for it.
- **Per-region or per-translation-unit modular flag scope.** `pragma`-
  scoped or attribute-scoped opt-in for `pow_mod` substitution. Build-time
  is sufficient for the prototype. Naming (`STURM_MODULAR_POW` rather than
  `STURM_FAST_MATH`) does not preclude this addition.
- **Compound modular assigns** (`a += b mod n`-style sugar). The free
  functions and operator-on-result-binding cover the use cases we know
  about. If a real caller wants `add_mod_inplace(a, b, n)`, file a
  follow-up.
- **Modular subtraction / negation** (`(a - b) % N`). Covered trivially by
  `add_mod(a, n - b, n)` for the moment; a dedicated primitive can be
  added if a hot path appears.
- **Precondition checking mode.** A debug build option that inserts
  reversible `compare(a, n)` / `compare(b, n)` guards before each modular
  primitive. Not in scope; users can write the assertion at the call site
  if desired.

---

## 8. Open questions

1. **`mul_mod` implementation strategy.** Shift-and-add over
   `add_mod` (smallest LoC, reuses existing DSL fully) vs. interleaved
   subtract-on-overflow during a Karatsuba-style multiplier (smaller
   gate count, more code). Pick during implementation; both satisfy the
   API contract. Default to shift-and-add for the first pass.
2. **`pow_mod` window size.** Repeated squaring is the baseline. Sliding-
   window or fixed-window variants reduce mul count at the cost of
   precomputed table ancillas. Defer until benchmarks show the baseline
   is the bottleneck.
3. **Behavior when `n == 0`.** Classical `% 0` is UB. We mirror that:
   `*_mod` with `n == 0` is undefined, no diagnostic. (Consistent with
   `lib_div_dsl` / `lib_mod_dsl` behavior today.)

---

## 9. Implementation order (suggested)

1. `lib_add_mod_dsl` + adjoint + tests (smallest, exercises the layering
   contract).
2. `lib_mul_mod_dsl` + adjoint + tests (uses #1).
3. `lib_pow_mod_dsl` + adjoint + tests (uses #2).
4. `add_mod` / `mul_mod` / `pow_mod` free functions wrapping the DSL
   primitives.
5. Transpiler matcher + emitter for the always-on `(OP) % n` rewrites.
6. CMake `STURM_MODULAR_POW` option + transpiler gate for `pow(a,x) % n`.
7. Cross-validation fixture for `pow_mod` (default vs. flagged).
8. Documentation: principles snippet, Doxygen on free functions, CMake
   help text.

Each step is a separate `bd` issue, dependencies in order listed.
