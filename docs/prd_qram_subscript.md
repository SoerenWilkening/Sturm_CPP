# PRD: QRAM via Array Subscript with Quantum Index

**Status:** Draft (2026-05-02).
**Scope tag:** sturm-u9ge (epic id; children sturm-u9ge.1 .. sturm-u9ge.17).
**Related:** `docs/01_principles.md` (P2, P4a, P9, B5/B5a, B7);
`include/sturm/qtypes/qint_core.hpp`.

---

## §1 Problem statement

Users want to express QRAM reads as ordinary array subscript:

```cpp
qint b = a[index];   // index is quantum
```

Today this does not parse:

1. `qint_t<W>::operator int64_t()` is `explicit` (correct, by P2: measurement
   must be explicit), so the integer required by built-in subscript /
   `std::array::operator[](size_type)` cannot be produced implicitly.
2. Even if conversion were allowed, runtime evaluation would *measure* the
   index — destroying superposition and producing a classical lookup, not a
   QRAM.

The goal is to keep the source spelling `qint b = a[index];` exactly, and
have the transpiler rewrite it to a QRAM call (illustratively
`QRAM_read(a, index, b)`) so that no measurement occurs in the emitted code.

## §2 Goals

- Source syntax matches plain C++ array subscript: `qint b = a[i];`.
- No new container type required from users (no `qarray`).
- Zero measurement at runtime in transpiled output for matched shapes.
- Audience: advanced users; no lexical signal in source that "this line is a
  QRAM".

## §3 Non-goals (v1)

- Write: `a[i] = b;`
- Read-modify-write: `a[i] += b;`, etc.
- Assignment into a pre-existing target: `b = a[i];` (requires uncompute of
  old `b` before the QRAM writes into it).
- Expression-position reads: `c = a[i] + d;` (requires ancilla extraction
  + uncompute).
- Source-as-plain-C++ runnability without transpilation. Untranspiled source
  silently measures the index; this is accepted per §10.1.

Each non-goal is filed as a follow-up bd issue (§12.6).

## §4 Approach: frontend alias + backend real class

Two layers:

1. **Frontend `qint`** — the alias class users write. Non-templated.
   Carries:
   - **Implicit** `operator size_t() const noexcept;`. This is the load-bearing
     mechanism: it is what makes `a[qint_idx]` parse uniformly across
     `std::array<qint, N>`, C-style arrays (`qint a[10]`), and pointers
     (`qint *a`). The body performs a measurement; in transpiled output it
     is never reached for matched shapes (§7) because the transpiler
     replaces the call site.
   - Stub overloads mirroring `qint_t<W>` (arithmetic, compare, bitwise).
     Either thin forwards through measurement-then-classical, or
     `__builtin_unreachable()` for shapes only the transpiler is allowed to
     produce.

2. **Backend `qint_t<W>`** — existing class, unchanged. Retains
   `explicit operator int64_t()` per P2. This is what the transpiler emits.

The transpiler substitutes `qint` → `qint_t<W>` for an inferred or
annotated `W` (see §6) and rewrites matched subscript expressions (§7–§8).

## §5 Why an alias class (and not a single class with implicit `size_t`)

The two-layer split is justified by a **post-transpile compile-time safety
net**:

- *Pre-transpile*, both designs are equivalent: implicit `qint → size_t`
  exists wherever the user-facing type lives, and any unintended integral-
  context use silently measures.
- *Post-transpile*:
  - **Alias model (this PRD).** The emitted class is `qint_t<W>` with
    `explicit operator int64_t()`. Any conversion site the matcher missed
    becomes a **compile error in the generated file**. The bug surfaces at
    build time, before execution.
  - **Single-class model.** The emitted class still carries the implicit
    conversion. Missed sites become **silent measurements at runtime**, with
    plausible-looking output collapsed from one branch — the bug class
    hardest to detect in quantum code.

The alias carries a real maintenance cost (every new `qint_t<W>` operator
needs a matching stub on `qint`), but the compile-time gate on missed sites
is worth it given how silently quantum measurement bugs propagate.

## §6 Width inference

Frontend `qint` is non-templated; backend `qint_t<W>` requires `W`. The
transpiler must pick `W` per declaration.

**Open question.** v1 is expected to combine:

- **(a) Global default.** A single configurable default width
  (candidate: 32 or 64).
- **(b) RHS-driven inference.** If `qint b = a[i];` and `a` has element type
  `qint_t<W>`, propagate `W` to `b`.
- **(c) Annotation form `qint<W>`.** Reserved as future syntax for cases
  where (a)/(b) are insufficient or ambiguous; not required for v1.

The exact rules — when (b) takes precedence over (a), and what diagnostic
fires on ambiguity — are deferred to the design phase of the matcher
issue (§12.M2).

## §7 v1 matcher contract

The transpiler recognises exactly one source shape:

```cpp
qint b = a[i];
```

where `i` is a frontend `qint` and `a` is one of:

- `std::array<qint, N> a;` — AST node: `CXXOperatorCallExpr` on
  `array::operator[]`.
- `qint a[N];` — AST node: `ArraySubscriptExpr` (built-in subscript).
- `qint *a = ...;` — AST node: `ArraySubscriptExpr` (built-in subscript).

The discriminator is the **`UserDefinedConversion` ImplicitCastExpr** at
the index position originating in `qint::operator size_t()`. This is a
strong, unambiguous signal because it cannot be produced by any other
type in the surface language.

## §8 v1 rewrite

```cpp
qint b = a[i];
```

emits as

```cpp
qint_t<W> b;
QRAM_read(a, i, b);
```

The runtime entry-point name and signature are placeholders — see
§11.1. The semantic contract is: the call must produce the QRAM-read
unitary on `(a, i, b)` without ever materialising `i` as a classical
integer.

## §9 Out-of-scope shapes — diagnostics, not silent fallthrough

The following shapes *use* frontend `qint` subscripting but are **not**
rewritten in v1. The matcher MUST emit a compile-time error citing this
PRD's section number, rather than letting them fall through to a silent
measurement at runtime:

| Shape                       | Reason out of scope                                |
|-----------------------------|----------------------------------------------------|
| `b = a[i];` (existing `b`)  | needs uncompute of old `b` before QRAM writes      |
| `a[i] = b;`                 | QRAM-write is a different unitary from QRAM-read   |
| `a[i] += b;`                | RMW: read + adjusted-write composition             |
| `c = a[i] + d;`             | expression-position read: ancilla extract + uncompute |

Each gets its own follow-up bd issue (§12.6).

## §10 Risks and mitigations

### §10.1 Silent measurement on missed conversion sites

Once frontend `qint` carries implicit `operator size_t()`, any integral
context accepts a `qint` and silently measures: `int x = q;`,
`std::vector<int> v(q);`, `for (size_t i = 0; i < q; ++i)`, etc.

**Decision.** Accept this footgun under the "be a competent user" principle
inherited from C/C++. The alias model (§5) localises the breach: silent
measurement is only possible *pre-transpile*; missed sites become compile
errors in the emitted output.

**Doc rule (mandatory).** Any `qint → integer` conversion is a destructive
measurement. This must be prominently documented in the user-facing
introduction to QRAM.

**Optional follow-up.** A transpile-time **warning** (not error) on
`UserDefinedConversion` sites for `qint → integer` whose enclosing
expression is *not* a subscript. Cheap to add, catches obvious mistakes,
costs nothing for users who know what they are doing. Filed as
§12.7.

### §10.2 Adjoint of QRAM read (P9 interaction)

QRAM read inside a reversible routine must have a synthesised or
hand-registered adjoint. The shape `QRAM_read(a, i, b)` suggests
`uncompute_QRAM_read(a, i, b)` by analogy with the existing `+=` /
`uncompute_add_qint` convention. Concrete contract is open (§11.4).

### §10.3 Control-stack interaction (B5a)

QRAM expansion is gate-heavy; the lowering must respect the depth-1
control-stack invariant. Concrete plan deferred to runtime-design issue
(§12.M4).

## §11 Open questions

1. **Runtime entry point.** Exact signature of `QRAM_read`. Templated on
   container type and element width? Single overload or family?
2. **What is `a`?** Classical lookup table (QROM) of pre-known constants
   versus quantum register of qubits — fundamentally different gate
   budgets. The matcher contract in §7 admits both syntactically; the
   semantic distinction must be settled before the runtime is implemented.
3. **Width inference precedence rules** (§6).
4. **Adjoint registration** for QRAM read (§10.2).
5. **Container support beyond v1.** `std::vector<qint>`? Custom user
   containers? Today's contract restricts to the three forms in §7;
   extension is a v2 question.

## §12 Milestones

Each becomes a bd issue under the §0 scope tag.

1. **M1.** Frontend alias class `qint` (header, stub operators, implicit
   `operator size_t()`).
2. **M2.** Width inference (default + RHS-driven; annotation reserved).
3. **M3.** v1 matcher: decl-init read shape across the three container
   forms in §7.
4. **M4.** Rewrite emitter + runtime entry-point stub
   (`QRAM_read(a, i, b)`).
5. **M5.** Out-of-scope shape diagnostics per §9.
6. **M6 (follow-ups, post-v1).** One bd issue per shape in §9.
7. **M7 (post-v1).** Optional transpile-time warning on non-subscript
   `qint → integer` conversions (§10.1).

## §13 References

- `docs/01_principles.md`
  - P2 — quantum/classical boundary; measurement is implicit by
    quantum→classical conversion, no exposed `measure()`. The alias-class
    implicit `operator size_t()` is the *only* implicit measurement site
    introduced by this PRD; the backend class continues to honour P2 with
    `explicit operator int64_t()`.
  - P4a — classical→quantum implicit; this PRD does not modify it.
  - P9 / P9a–d — adjoint synthesis; relevant to §10.2.
  - B5 / B5a — depth-1 control-stack invariant; relevant to §10.3.
  - B7 — qubit-index management; user code never names qubits, so the
    `QRAM_read` runtime owns any ancilla allocation under the hood.
- `include/sturm/qtypes/qint_core.hpp` — backend `qint_t<W>` definition.
- `transpiler/src/` — existing matcher infrastructure that this PRD's
  matcher will plug into.
