# Working with QRAM

**Status:** v1 (2026-05-03), tracks the closed frontend epic
[`sturm-u9ge`](archive/prd_qram_subscript.md) and the closed backend
epic `sturm-2w6h` ([`prd_qram_backend.md`](prd_qram_backend.md),
[`plan_qram_backend.md`](plan_qram_backend.md)).

**Audience.** Advanced users writing reversible / quantum routines who
want to read a `qint` element of a container indexed by another `qint`
without measuring either value. If you are new to STURM, read
[`getting_started.md`](getting_started.md) first.

**Companion docs.**
[`prd_qram_backend.md`](prd_qram_backend.md) (the v1 backend gate
emission contract — Status: Implemented),
[`archive/prd_qram_subscript.md`](archive/prd_qram_subscript.md) (the v1
frontend rewrite contract — Status: Implemented), and
[`plan_qram_backend.md`](plan_qram_backend.md) (the backend beat plan).

This document is the user-facing reference for QRAM. The PRDs above
are the spec; this is the what-it-looks-like-from-source view.

---

## 1. The shape — `qint b = a[i];`

STURM's QRAM is invoked via ordinary C++ array subscript:

```cpp
qint b = a[i];   // i is a qint, a is a container of qints
```

That single line is what the transpiler matches and rewrites. There is
no new container type to learn (no `qarray`), no new keyword, no
attribute. The lexical signal is **just the array-subscript
expression with a `qint` index on a container of `qint`** — the
discriminator is the implicit `qint → size_t` conversion the matcher
sees on the index position
([archive/prd_qram_subscript.md §7](archive/prd_qram_subscript.md)).

### What the transpiler does

The matcher detects `qint b = a[i];` and rewrites it to

```cpp
qint_t<W> b;
QRAM_read(a, i, b);
```

where `W` is the element width inferred from `a`'s element type
([archive/prd_qram_subscript.md §6, §8](archive/prd_qram_subscript.md)).
At runtime, `QRAM_read(a, i, b)` emits the unitary that performs
`b ^= a[i]` at the gate-stream level — without ever materialising `i`
as a classical integer. Each set bit of `a[i]` flips the
matching bit of `b`, and `i.super_mask` is preserved across the call
([prd_qram_backend.md §2 G1](prd_qram_backend.md)).

The runtime body for the v1 implemented path is the **QROM path**: every
element of `a` must be fully classical at call time. The body sweeps
`k = 0 .. N-1`, computes a 1-qubit equality predicate
`eq_k = (i == k)`, then applies `WHEN(eq_k) { b ^= a[k]; }`, then
uncomputes the predicate. Total cost is `O(N · W)` Toffolis per call
([prd_qram_backend.md §4](prd_qram_backend.md)). All emitted primitives
are `^=` (CNOT/Toffoli class) or `&=` (CCX class); no rotations, no
measurements, no raw gate calls.

### The forward / adjoint pair

`QRAM_read(a, i, b)` has a registered adjoint
`__QRAM_read_adj(a, i, b)`. The QROM body is its own inverse, so the
adjoint re-runs the forward sweep verbatim
([prd_qram_backend.md §4 paragraph 4, §6](prd_qram_backend.md)). If you
use `QRAM_read` inside a reversible routine, STURM's adjoint
synthesis will resolve the adjoint via the registered name (P9c).

---

## 2. Supported and unsupported shapes

### 2.1 Supported (matched and rewritten in v1)

The matcher recognises **exactly one source shape**:

```cpp
qint b = a[i];
```

— a fresh declaration of `b` on the LHS, no surrounding expression on
the RHS, with `i` a frontend `qint`. The container `a` may be one of
three shapes
([archive/prd_qram_subscript.md §7, §11.1](archive/prd_qram_subscript.md)):

| Shape                        | Container type                  | Notes                                         |
|------------------------------|---------------------------------|-----------------------------------------------|
| `std::array<qint, N>`        | `std::array<qint_t<W>, N>`      | Length `N` known at compile time              |
| C-style array                | `qint_t<W>[N]`                  | Length `N` known at compile time              |
| Pointer (decayed)            | `qint_t<W>*`                    | Length passed explicitly to `QRAM_read`       |

`std::vector<qint>` is not yet supported as a container shape (v2);
the pointer overload covers the runtime side via `.data()`
([prd_qram_backend.md §3](prd_qram_backend.md)).

### 2.2 Unsupported (diagnosed at compile time, not silently measured)

The following shapes use `qint` subscripting but are **not** rewritten
in v1. The transpiler emits a **compile-time error** for each, citing
the relevant PRD section number, rather than letting the implicit
`qint → size_t` fall through to a silent measurement
([archive/prd_qram_subscript.md §9](archive/prd_qram_subscript.md)):

| Shape                        | Why out of scope (v1)                                | Filed follow-up    |
|------------------------------|------------------------------------------------------|--------------------|
| `b = a[i];` (existing `b`)   | Needs uncompute of old `b` before QRAM writes        | `sturm-u9ge.6` (H1)|
| `a[i] = b;`                  | QRAM-write is a different unitary from QRAM-read     | `sturm-u9ge.7` (H2)|
| `a[i] += b;`                 | Read-modify-write: read + adjusted-write composition | `sturm-u9ge.8` (H3)|
| `c = a[i] + d;`              | Expression-position read: ancilla extract + uncompute| `sturm-u9ge.9` (H4)|

If you write one of these, you get a build error — not a runtime
measurement. That's intentional: the alias model exists so that any
shape the matcher misses surfaces as a *compile* error in the
generated file, never a silent runtime collapse (see §3 below).

### 2.3 The QROM precondition

The v1 backend only implements the **QROM path** — every element of
`a` must be fully classical at the time of the call (`super_mask == 0`
on every element). The runtime checks this via an OR-reduction over
the elements' masks at `QRAM_read` entry; mixed or fully-quantum
containers are routed to a `qreg` helper which is currently a
counter-bump stub
([prd_qram_backend.md §3, §11.2.2](prd_qram_backend.md)). When the
quantum-register path lands, it will be a sibling PRD with no
source-side change — `QRAM_read(a, i, b)` continues to be the
entry point.

### 2.4 Address-bit contract

`i` has width `W ≥ ⌈log₂ N⌉`. The high `W − ⌈log₂ N⌉` bits of `i`
must be zero at call entry; passing `i ≥ N` is **undefined behavior**,
mirroring classical out-of-range subscript
([prd_qram_backend.md §5](prd_qram_backend.md)). The body sweeps
`k = 0 .. N-1` only and does not consult bits of `i` above
`⌈log₂ N⌉`. No runtime range check is emitted.

---

## 3. The measurement footgun

This section is the full version of the warning that appears as a
1-paragraph callout in
[`getting_started.md`](getting_started.md#%EF%B8%8F-measurement-footgun-qint--integer-is-destructive)
and is mandated by
[archive/prd_qram_subscript.md §10.1](archive/prd_qram_subscript.md).

### 3.1 What the footgun is

The frontend `qint` alias carries an **implicit** `operator size_t() const`
([archive/prd_qram_subscript.md §4 layer 1](archive/prd_qram_subscript.md)).
That implicit conversion is the load-bearing mechanism that makes the
matched shape `qint b = a[i];` parse uniformly across `std::array`,
C-style arrays, and pointers — without it the call would not even
compile, never mind transpile.

The same implicit conversion fires in **any** integral context:

```cpp
qint q = some_expression();

int x = q;                              // SILENT MEASUREMENT
std::vector<int> v(q);                  // SILENT MEASUREMENT
for (size_t i = 0; i < q; ++i) { ... }  // SILENT MEASUREMENT
if (q == 5) { ... }                     // SILENT MEASUREMENT (q → size_t for ==)
foo(q);                                 // SILENT MEASUREMENT if foo takes int
```

In each of these untranspiled lines, the `qint`'s `operator size_t()`
body runs at runtime and **measures** `q`, collapsing its
superposition. The output then propagates as a classical integer —
which means the surrounding code keeps working, the test suite keeps
passing on classical inputs, and the bug is only visible to whoever
goes back and reads the gate stream looking for missing primitives.

This is **the** bug class quantum code is hardest to detect: code that
looks correct, runs, produces plausible numbers, and has silently
collapsed the quantum state.

### 3.2 Why STURM accepts the footgun anyway

The decision is pinned by
[archive/prd_qram_subscript.md §10.1](archive/prd_qram_subscript.md):

> **Decision.** Accept this footgun under the "be a competent user"
> principle inherited from C/C++. The alias model (§5) localises the
> breach: silent measurement is only possible *pre-transpile*; missed
> sites become compile errors in the emitted output.

The reasoning is the alias / two-class model
([archive/prd_qram_subscript.md §5](archive/prd_qram_subscript.md)):

- *Pre-transpile*, the implicit `qint → size_t` exists, so any
  unintended integral-context use silently measures.
- *Post-transpile*, the emitted class is `qint_t<W>` with **`explicit`
  operator int64_t()** (per principle P2). Any conversion site the
  matcher missed becomes a **compile error in the generated file**.
  The bug surfaces at build time, before execution.

So the footgun window is: source files that have not yet been run
through the transpiler. The trade is an explicit one — implicit
conversion is required to make `a[i]` parse, and the alias-class
safety net catches missed sites at build time rather than execution
time.

### 3.3 Rules for users

1. **Use `static_cast<int64_t>(q)` when you intend to measure.** That
   is the explicit, post-transpile-safe way to collapse a `qint` to a
   classical integer. The example in
   [`getting_started.md`](getting_started.md) uses it deliberately for
   readout.

2. **For QRAM access write the exact shape `qint b = a[i];`.** A fresh
   declaration on the LHS, no surrounding expression on the RHS. Any
   other shape (`b = a[i];` with existing `b`, `a[i] = b;`,
   `a[i] += b;`, `c = a[i] + d;`) is a compile-time error from the
   transpiler — not a silent measurement (§2.2 above).

3. **Avoid passing a `qint` into any function or expression you have
   not audited.** A function taking `int`, `size_t`, `std::size_t`,
   `int64_t`, `unsigned`, etc. will accept a `qint` via the implicit
   conversion and measure it. This includes standard library calls
   (`std::vector<int> v(q);`, `std::min(q, n);`, `printf("%d", q);`),
   loop counters (`for (size_t i = 0; i < q; ++i)`), and comparisons
   against integer literals (`if (q == 5)`).

4. **If you actually want to measure**, do so explicitly *and* reason
   about the rest of the routine. Once `q` is measured, any use of
   `q` later in the same scope is operating on a classical residue —
   any "quantum" behaviour you expected from later operations on `q`
   is gone.

5. **Trust the build, not the run.** If your file transpiles cleanly
   *and* compiles cleanly post-transpile, the matcher caught every
   QRAM site. If it transpiles cleanly but the post-transpile compile
   fails with an `explicit conversion` error on a `qint_t<W>`, the
   matcher missed a site — fix it at the source level, do not silence
   the error.

### 3.4 Optional belt-and-braces — the transpile-time warning

The transpiler can be configured to emit a **warning** (not an error)
on every `UserDefinedConversion` site for `qint → integer` whose
enclosing expression is **not** a subscript. This is the
implicit-conversion warning landed at `sturm-u9ge.14`
([archive/prd_qram_subscript.md §10.1 "Optional follow-up", §12.7](archive/prd_qram_subscript.md)).
Cheap to enable, catches obvious mistakes, costs nothing for code
that already uses `static_cast<int64_t>` deliberately.

---

## 4. Where to read more

- [`prd_qram_backend.md`](prd_qram_backend.md) — the v1 backend gate
  emission contract. §2 (goals), §3 (non-goals), §4 (algorithm), §5
  (address-bit contract), §6 (file layout), §7 (telemetry).
- [`plan_qram_backend.md`](plan_qram_backend.md) — the backend beat
  plan (B0–B5) and the gate-budget cheat sheet (§4).
- [`archive/prd_qram_subscript.md`](archive/prd_qram_subscript.md) —
  the closed frontend PRD. §7 (matcher contract), §8 (rewrite), §9
  (out-of-scope shapes), §10.1 (this document's source for the
  measurement-footgun rule), §11.1 (the three `QRAM_read` overloads).
- [`archive/plan_qram_subscript.md`](archive/plan_qram_subscript.md) —
  the closed frontend beat plan; H1–H4 are the four out-of-scope
  shape follow-ups (§2.2 above).
- [`01_principles.md`](01_principles.md) — P2 (measurement is
  explicit), P5 (DSL primitive set), P9 / P9c (adjoint synthesis),
  B5a (depth-1 control invariant).
