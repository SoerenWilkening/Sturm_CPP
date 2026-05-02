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
4. **Adjoint registration** for QRAM read (§10.2). **Resolved by D0d
   (`sturm-u9ge.4`); see §11.4 below.**
5. **Container support beyond v1.** `std::vector<qint>`? Custom user
   containers? Today's contract restricts to the three forms in §7;
   extension is a v2 question.

### §11.4 Decision: D0d — `QRAM_read` adjoint registration

**Status.** Resolved (2026-05-02, bd `sturm-u9ge.4`).
**Resolves.** PRD §11 item 4 / §10.2.
**Gates.** Beat D2 (`sturm-u9ge.15`, the rewrite emitter), which now
knows what token to plant at uncompute sites.

#### D0d.1 Naming

The adjoint sibling for `QRAM_read` is named **`__QRAM_read_adj`**. Both
the user-facing forward and its adjoint live at the global
`::sturm::` namespace.

This follows P9c verbatim: "the transpiler always emits a distinct
`__fn_adj` companion … and registers it via
`STURM_REGISTER_ADJOINT(fn, __fn_adj)`". The `__<name>_adj` underscore
prefix is the established convention across the codebase
(`__lib_mod_dsl_adj`, `__lib_or_dsl_adj`, `__lib_div_dsl_adj`,
`__marked_adj`, etc.); audit tooling already name-matches `__*_adj`
tokens at uncompute sites (per P9c rationale), so adopting the same
shape for `QRAM_read` keeps the placement audit working without
extension.

The earlier "by analogy with `+= / uncompute_add_qint`" suggestion in
§10.2 is **rejected** for the registered name. Rationale:

- `uncompute_add_qint` is a *thin user-facing convenience wrapper*
  (`include/sturm/uncompute/uncompute_api.hpp:137`) layered on top of
  the underlying `-=` operator, not the registered adjoint identity.
  The actual P9c-style binding for the addition family is the
  per-width `add_qint_t<W>` ↔ `__add_qint_t<W>_adj` pairing emitted by
  the transpiler.
- Spelling the registered adjoint `uncompute_QRAM_read` would diverge
  from P9c and break the `__*_adj` audit tooling.
- A user-facing `uncompute_QRAM_read(a, i, b)` convenience wrapper MAY
  later be added in a separate sibling header (mirroring how
  `uncompute_or` and `uncompute_add_qint` sit alongside the registered
  `__lib_or_dsl_adj` / `add_qint_t<W>`-family adjoints). It is NOT
  required for v1 and is explicitly out of scope for this decision.

#### D0d.2 Signature

The adjoint signature mirrors the forward one-for-one (P9b: out-param
shape; P9: same parameter list, reverse direction). The exact
template parameter list and per-container overload set are pinned by
D0a (`sturm-u9ge.1`), which is still open at the time of this
decision; this note fixes the *shape* and the *naming/registration
contract*, leaving the per-container template instantiation list to
D0a.

For each `QRAM_read` overload that D0a settles on, the adjoint sibling
has the identical parameter list and storage class. Concretely, given
the three forward overloads anticipated by Plan §7 D1
(`include/sturm/qram/qram_read.hpp`):

```cpp
namespace sturm {

// Forward (D0a will pin the exact template heads; the parameter list
// and the (a, i, b) order are fixed).

template <typename Idx, std::size_t W, std::size_t N>
void QRAM_read(const std::array<qint_t<W>, N>& a,
               const Idx& i,
               qint_t<W>& b);

template <typename Idx, std::size_t W, std::size_t N>
void QRAM_read(const qint_t<W> (&a)[N],
               const Idx& i,
               qint_t<W>& b);

template <typename Idx, std::size_t W>
void QRAM_read(const qint_t<W>* a,
               std::size_t n,
               const Idx& i,
               qint_t<W>& b);

// Adjoint sibling — one-for-one with the forward.

template <typename Idx, std::size_t W, std::size_t N>
void __QRAM_read_adj(const std::array<qint_t<W>, N>& a,
                     const Idx& i,
                     qint_t<W>& b);

template <typename Idx, std::size_t W, std::size_t N>
void __QRAM_read_adj(const qint_t<W> (&a)[N],
                     const Idx& i,
                     qint_t<W>& b);

template <typename Idx, std::size_t W>
void __QRAM_read_adj(const qint_t<W>* a,
                     std::size_t n,
                     const Idx& i,
                     qint_t<W>& b);

}  // namespace sturm
```

`Idx` here stands for whatever D0a chooses for the index parameter
type (`qint_t<W_idx>`, `qint`, or a constraint-templated alias);
the *adjoint contract* is independent of that choice. `a` is `const`
in both the forward and the adjoint (P9b: read-only inputs); `b` is
the out-param that the forward wrote into and the adjoint un-writes
back to |0⟩, exactly mirroring the `c = a | b` ↔
`uncompute_or(c, a, b)` pattern in `uncompute_api.hpp:68` and the
modular family's `__lib_*_adj` pattern. If D0a chooses a different
container-decay form (e.g. drops the explicit pointer-plus-length
overload in favour of `std::span`), the adjoint overload set
contracts identically.

#### D0d.3 Registration

Each forward overload that D0a pins is registered with a sibling
`STURM_REGISTER_ADJOINT(...)` line at namespace scope in
`include/sturm/qram/qram_read.hpp` (the runtime header that D1
introduces). Following the existing per-instantiation convention in
`include/sturm/detail/qtypes/lossy_oop.hpp` lines 426–460 and the
gated form in `include/sturm/detail/lib/mod_dsl_adj.hpp` lines 83–86,
each concrete `(W, N)` (and, where applicable, `Idx`) instantiation
that the v1 emitter can produce is enrolled explicitly. Sketch (final
instantiation list — concrete widths and N — is pinned by D0a /
D2; the macro shape is fixed by P9c and is final here):

```cpp
// In include/sturm/qram/qram_read.hpp, after the forward and adjoint
// definitions, gated on backend visibility the same way mod_dsl_adj.hpp
// gates lib_mod_dsl<sturm::BitProxy>:

#ifdef STURM_BACKEND_ENABLED
// std::array overload — one registration per (W, N, Idx) the emitter
// can produce. D0a's instantiation list determines the concrete rows.
STURM_REGISTER_ADJOINT(
    sturm::QRAM_read<sturm::qint, 32u, 4u>,
    sturm::__QRAM_read_adj<sturm::qint, 32u, 4u>)
// ... one row per concrete (Idx, W, N) instantiation D0a / D2 emits ...

// C-array overload — same shape, different forward template head.
STURM_REGISTER_ADJOINT(
    sturm::QRAM_read<sturm::qint, 32u, 4u>,   // overload resolved by ADL
    sturm::__QRAM_read_adj<sturm::qint, 32u, 4u>)
// ...

// Pointer overload — no N template parameter.
STURM_REGISTER_ADJOINT(
    sturm::QRAM_read<sturm::qint, 32u>,
    sturm::__QRAM_read_adj<sturm::qint, 32u>)
// ...
#endif
```

The gate `#ifdef STURM_BACKEND_ENABLED` mirrors the existing pattern
in `mod_dsl_adj.hpp` and keeps the registration out of the
counter-only / circuit-only build configurations where the backend
type is not introduced.

If, after D0a, the three overloads collapse into a single template
(e.g. all three container shapes route through one
`std::span`-style entry point), the registration list collapses
analogously — the macro shape `STURM_REGISTER_ADJOINT(QRAM_read<...>,
__QRAM_read_adj<...>)` is unchanged.

#### D0d.4 P9c consistency

This resolution is consistent with principle P9c on three counts:

1. **Distinct sibling.** The adjoint is a separately named function
   (`__QRAM_read_adj`), not a self-registration of `QRAM_read`. P9c
   forbids collapsing self-inverse routines to `STURM_REGISTER_ADJOINT(fn, fn)`
   at synthesis time precisely so the `__*_adj` token survives at
   every uncompute site for the placement audit. `QRAM_read` is not
   self-inverse anyway (the forward writes into a presumed-zero `b`;
   the adjoint un-writes), so the distinction is doubly motivated
   here.
2. **Standard macro.** Registration goes through
   `STURM_REGISTER_ADJOINT(fn, __fn_adj)` exactly as P9c specifies
   and as the macro is defined in
   `include/sturm/routines/invert.hpp:148`. There is no bespoke
   registration path.
3. **Audit-friendly token.** The literal `__QRAM_read_adj` token
   appears verbatim at every uncompute site the D2 emitter plants
   (via `sturm::invert<&QRAM_read<...>>()(a, i, b)`, which resolves
   to `__QRAM_read_adj<...>` by trait lookup). Audit tooling
   name-matching `__*_adj` will see it without modification.

#### D0d.5 Emitter contract for D2

For each `QRAM_read` call site D2 emits inside a reversible scope,
the matching uncompute call planted at scope exit is:

```cpp
sturm::invert<&::sturm::QRAM_read<...>>()(a, i, b);
```

i.e. the same shape `adjoint_emitter.cpp` already uses for every
other registered library forward (cf. the `mul_oop` example in
`include/sturm/detail/qtypes/lossy_oop.hpp:10` and the modular family
in `include/sturm/detail/lib/pow_mod_dsl_adj.hpp:44`). D2 does NOT
need a QRAM-specific code path; it reuses the existing
`invert<&fn>()` planting machinery.

#### D0d.6 Cross-references

- `docs/01_principles.md` — P9 (adjoint by synthesised companion),
  P9b (input immutability via const), P9c (distinct `__fn_adj`
  companion + `STURM_REGISTER_ADJOINT` is mandatory).
- `include/sturm/routines/invert.hpp` — `STURM_REGISTER_ADJOINT` macro
  definition (NTTP-keyed trait specialisation).
- `include/sturm/detail/lib/mod_dsl_adj.hpp` — closest existing
  template-adjoint analogue (`__lib_mod_dsl_adj` +
  `STURM_REGISTER_ADJOINT(... <sturm::BitProxy>, ... <sturm::BitProxy>)`
  inside an `#ifdef STURM_BACKEND_ENABLED` block).
- `include/sturm/detail/qtypes/lossy_oop.hpp:420-460` — closest
  existing per-instantiation registration block (per-`W` rows for
  `and_oop`, `mul_oop`, `or_oop`, `divide_oop`).
- `include/sturm/uncompute/uncompute_api.hpp:68,137` — pattern for
  optional user-facing `uncompute_*` convenience wrapper that may be
  added later, sibling to (not in lieu of) the registered adjoint.
- bd `sturm-u9ge.1` (D0a, still open) — pins the per-container
  template head and concrete `(W, N, Idx)` instantiation list;
  D0d's registration table consumes whatever D0a settles on.
- bd `sturm-u9ge.13` (D1, blocked) — declares `QRAM_read` and
  `__QRAM_read_adj` per this decision and plants the
  `STURM_REGISTER_ADJOINT` rows.
- bd `sturm-u9ge.15` (D2, blocked) — emitter; planted uncompute call
  is `sturm::invert<&QRAM_read<...>>()(a, i, b)` per §D0d.5.

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
