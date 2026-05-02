# Implementation Plan: QRAM via Array Subscript with Quantum Index

**Status:** Draft (2026-05-02).
**Tracks:** `docs/prd_qram_subscript.md` §12 milestones M1–M7.
**Scope tag:** `sturm-u9ge` — epic id; children `sturm-u9ge.1` .. `sturm-u9ge.17`. Plan-beat → bd-id map at §1a below.
**Related principles:** P2 (measurement explicit), P4a (classical→quantum
implicit), P9/P9a–d (adjoint synthesis), B5/B5a (depth-1 control),
B7 (qubit-index ownership).

---

## §0 Reading guide

This plan decomposes PRD §12 into **bd issues**, grouped into **beats**.
Each beat is a self-contained, test-driven landing:

- One *production* module per beat. Hard cap **≤ 300 LoC** including
  comments and blank lines.
- One *test* module per beat with the production module's test surface;
  test files have the same 300-LoC cap unless explicitly noted.
- Beats land in dependency order. Every beat is green (full transpiler
  + library test suite passing under `ctest --parallel 6`) before the
  next is opened.

LoC budgets are advisory ceilings. If a module would exceed 300 LoC,
split it along the seam noted in its description; do not relax the cap.

The plan was filed as bd epic `sturm-u9ge`; children use numeric suffixes
(`sturm-u9ge.1` … `sturm-u9ge.17`). The mnemonic beat names below
(D0a–d, A1/A2, B1, C1, D1/D2, E1, F1, G1, H1–H4) survive only as
documentation; the bd ids are the authoritative work tracker. See §1a.

---

## §1 Module map (bird's-eye view)

| Layer       | New file                                                   | Cap | Beat   |
|-------------|------------------------------------------------------------|-----|--------|
| Header      | `include/sturm/qtypes/qint_alias.hpp`                      | 300 | A1     |
| Header      | `include/sturm/qtypes/qint_alias_ops.hpp`                  | 300 | A2     |
| Runtime     | `include/sturm/qram/qram_read.hpp`                         | 200 | D1     |
| Runtime     | `src/qram/qram_read.cpp` (counter-mode stub)               | 200 | D1     |
| Transpiler  | `transpiler/src/width_inference.{hpp,cpp}`                 | 300 | B1     |
| Transpiler  | `transpiler/src/matcher_qram_subscript.{hpp,cpp}`          | 300 | C1     |
| Transpiler  | `transpiler/src/qram_emitter.{hpp,cpp}`                    | 300 | D2     |
| Transpiler  | `transpiler/src/matcher_qram_oos.{hpp,cpp}`                | 300 | E1     |
| Transpiler  | `transpiler/src/matcher_qint_implicit_warn.{hpp,cpp}`      | 200 | F1 *   |
| Tests       | `tests/qtypes/test_qint_alias.cpp`                         | 300 | A1     |
| Tests       | `tests/qtypes/test_qint_alias_ops.cpp`                     | 300 | A2     |
| Tests       | `transpiler/tests/test_width_inference.cpp`                | 300 | B1     |
| Tests       | `transpiler/tests/test_matcher_qram_subscript.cpp`         | 300 | C1     |
| Tests       | `transpiler/tests/test_qram_emitter.cpp`                   | 300 | D2     |
| Tests       | `tests/qram/test_qram_read_stub.cpp`                       | 200 | D1     |
| Tests       | `transpiler/tests/test_matcher_qram_oos.cpp`               | 300 | E1     |
| Tests       | `transpiler/tests/test_qram_e2e.cpp`                       | 300 | G1     |
| Fixtures    | `transpiler/tests/fixtures/qram_read_*.{cpp,expected.cpp}` | n/a | C1, D2 |
| Fixtures    | `transpiler/tests/fixtures/qram_oos_*.cpp`                 | n/a | E1     |

`*` F1 is post-v1, optional (PRD §10.1 / M7).

The 300-LoC cap on test files is the ordinary case; if a single fixture
matrix demonstrably needs more, split into `_part1.cpp`/`_part2.cpp`
along an axis (e.g. one file per container shape).

---

## §1a Beat → bd-issue id map

| Beat | bd id           | What it is                              |
|------|-----------------|-----------------------------------------|
| —    | `sturm-u9ge`    | epic                                    |
| D0a  | `sturm-u9ge.1`  | runtime entry-point signature           |
| D0b  | `sturm-u9ge.2`  | QROM vs quantum-register semantics      |
| D0c  | `sturm-u9ge.3`  | width-inference precedence rules        |
| D0d  | `sturm-u9ge.4`  | adjoint registration                    |
| A1   | `sturm-u9ge.5`  | header `qint_alias.hpp`                 |
| H1   | `sturm-u9ge.6`  | follow-up: `b = a[i];`                  |
| H2   | `sturm-u9ge.7`  | follow-up: `a[i] = b;`                  |
| H3   | `sturm-u9ge.8`  | follow-up: `a[i] += b;`                 |
| H4   | `sturm-u9ge.9`  | follow-up: `c = a[i] + d;`              |
| A2   | `sturm-u9ge.10` | operator stubs `qint_alias_ops.hpp`     |
| B1   | `sturm-u9ge.11` | width inference                         |
| C1   | `sturm-u9ge.12` | v1 matcher                              |
| D1   | `sturm-u9ge.13` | `QRAM_read` runtime stub                |
| F1   | `sturm-u9ge.14` | optional warning *(post-v1)*            |
| D2   | `sturm-u9ge.15` | rewrite emitter                         |
| E1   | `sturm-u9ge.16` | OOS diagnostics                         |
| G1   | `sturm-u9ge.17` | end-to-end gate                         |

---

## §2 Dependency graph

```
                ┌──────────────────────┐
                │  A1 alias header     │  (frontend type, no transpiler)
                └─────────┬────────────┘
                          │
                ┌─────────▼────────────┐
                │  A2 alias operators  │  (stubs that route to qint_t<W>
                │                      │   measurement-then-classical)
                └─────────┬────────────┘
                          │
   ┌──────────────────────┼──────────────────────┐
   │                      │                      │
┌──▼──┐               ┌───▼───┐              ┌───▼───┐
│ B1  │ width infer.  │  D1   │ runtime stub │  E1   │ OOS diagnostics
└──┬──┘               └───┬───┘              └───────┘
   │                      │
   └──────────┬───────────┘
              │
        ┌─────▼─────┐
        │    C1     │  matcher (needs B1 for W and D1 for the call shape)
        └─────┬─────┘
              │
        ┌─────▼─────┐
        │    D2     │  emitter (needs C1's hits; emits D1's call)
        └─────┬─────┘
              │
        ┌─────▼─────┐
        │    G1     │  end-to-end fixture
        └───────────┘

F1 (optional warning) is independent and may land at any time after A1.
```

Beats A1 and A2 are independent of all transpiler work and are landed
first so library users can experiment with the source spelling even
before the transpiler matcher exists.

---

## §3 Pre-flight (bd issues filed, no code)

These are filed as bd issues with `--type=task`, no implementation,
**before** the implementation epic opens. They settle the open questions
in PRD §11 so later beats are not blocked.

- **`sturm-u9ge.1` (plan: D0a) — Decide `QRAM_read` runtime entry-point signature.**
  Resolves PRD §11.1. Output: a one-page design note appended to
  `docs/prd_qram_subscript.md` or filed as `docs/design_qram_runtime.md`,
  pinning template/overload structure for the three container forms.
  Gates beats D1 / D2.
- **`sturm-u9ge.2` (plan: D0b) — Decide QROM vs quantum-register semantics for `a`.**
  Resolves PRD §11.2. Output: same note as D0a (or sibling section).
  Two execution paths share a syntactic shape; the runtime must know
  which one to dispatch. Gates beat D1.
- **`sturm-u9ge.3` (plan: D0c) — Decide width-inference precedence rules.**
  Resolves PRD §11.3 / §6. Output: short rules table (when (b) wins
  over (a), what diagnostic fires on ambiguity). Gates beat B1.
- **`sturm-u9ge.4` (plan: D0d) — Decide `QRAM_read` adjoint registration.**
  Resolves PRD §11.4 / §10.2. Output: name + signature for
  `__QRAM_read_adj` (or named uncompute helper); registration via
  `STURM_REGISTER_ADJOINT` per P9c. Gates beat D2 (emitter must know
  what to emit at uncompute sites).

`sturm-u9ge.1`–`sturm-u9ge.4` (D0a–D0d) all close before beat A1
(`sturm-u9ge.5`) begins.

---

## §4 Beat A — Frontend alias type

PRD §4 step 1, M1.

### `sturm-u9ge.5` (plan: A1) — header `qint_alias.hpp`

**Goal.** Introduce the non-templated frontend `qint` class with the
load-bearing implicit `operator size_t() const noexcept` (PRD §4.1).
Sufficient surface to declare and default-construct `qint`, and to make
`a[qint_idx]` parse for the three container shapes in PRD §7.

**Production.** `include/sturm/qtypes/qint_alias.hpp` (≤ 300 LoC).
- `class qint` non-templated.
- Default ctor; copy/move = default; classical-int converting ctor
  (`qint(int64_t)`, **not** `explicit`, per P4a).
- **Implicit** `operator size_t() const noexcept;` — body invokes
  measurement and returns the measured value cast to `size_t`. Body is
  defined out-of-line so test code can verify it is *never* reached
  in transpiled output.
- No arithmetic / compare / bitwise operators yet — that is A2.
- Deliberately **no** `explicit operator int64_t()` here; that lives
  on the backend `qint_t<W>` (PRD §4.2) and stays the post-transpile
  safety net (PRD §5).

**Tests.** `tests/qtypes/test_qint_alias.cpp` (≤ 300 LoC).
- Compile-only: `qint a; std::array<qint, 4> arr; auto x = arr[a];`
  must build.
- Compile-only: `qint a; qint buf[4]; auto x = buf[a];` builds.
- Compile-only: `qint a; qint *p = nullptr; auto x = p[a];` builds
  (no execution).
- Runtime: implicit `size_t` conversion measures (record sink calls).
- Negative compile test (`SFINAE` / `static_assert`) ensuring backend
  `qint_t<W>` still rejects the same conversion (P2 unchanged).

### `sturm-u9ge.10` (plan: A2) — operator stubs `qint_alias_ops.hpp`

**Goal.** Mirror the public surface of `qint_t<W>` so user code that
uses `qint` outside subscript still compiles. PRD §4.1 step 2.

**Production.** `include/sturm/qtypes/qint_alias_ops.hpp` (≤ 300 LoC).
- Forwarding stubs for arithmetic (`+ - * / %`), compare (`== != < ≤ > ≥`),
  bitwise (`& | ^ ~ << >>`), compound forms (`+= ^= …`).
- Implementation strategy: each stub `measure-then-classical` —
  internally invokes the implicit `size_t` conversion and dispatches the
  classical operation. This is intentionally lossy: the matcher
  rewrites the matched shape so the stub body is never reached
  post-transpile; if it *is* reached, behaviour is well-defined
  (measurement + classical op) and observable in the test sink.
- For shapes that only the transpiler is allowed to produce
  (e.g. `qint_t<W> b = QRAM_read(...)`'s temporary), prefer
  `__builtin_unreachable()` over a measurement body.

If the stub list grows past 300 LoC, split by family:
`qint_alias_ops_arith.hpp`, `qint_alias_ops_compare.hpp`,
`qint_alias_ops_bitwise.hpp`. Each file in the split must keep ≤ 300 LoC.

**Tests.** `tests/qtypes/test_qint_alias_ops.cpp` (≤ 300 LoC).
- Each stub: invoke pre-transpile, observe measurement counter
  increments and classical result.
- Each stub: assert the post-transpile path is unreachable by
  matching the emitted source against a fixture (fixture lives in
  `transpiler/tests/fixtures/` once C1 lands; until then the test is
  marked `[[skip-until-C1]]`).

---

## §5 Beat B — Width inference

PRD §6, M2. Depends on `sturm-u9ge.3` (plan: D0c).

### `sturm-u9ge.11` (plan: B1) — `width_inference.{hpp,cpp}`

**Goal.** Given a `VarDecl` of frontend `qint`, decide the backend
width `W` for the substituted `qint_t<W>`.

**Production.** `transpiler/src/width_inference.{hpp,cpp}`
(each ≤ 300 LoC).
- Public: `unsigned infer_width(const clang::VarDecl&, const InferContext&);`
- `InferContext` carries the global default width (PRD §6a),
  diagnostics sink, and the matcher's container-element width lookup.
- Three rules implemented in the order fixed by `sturm-u9ge.3` (plan: D0c):
  1. RHS-driven: if the initializer is a subscript on a container of
     `qint_t<W>`, return `W`.
  2. Future-reserved: parse `qint<W>` annotation form (PRD §6c). v1
     emits a diagnostic noting it is reserved and falls back to the
     next rule. The parser hook lives behind a `kAllowAnnotation`
     flag set to `false` in v1.
  3. Global default: configurable, candidates 32 / 64. Default chosen
     in `sturm-u9ge.3` (plan: D0c); the value lives in a single named constant in
     `width_inference.hpp` so future changes are one-line.
- Ambiguity diagnostic per `sturm-u9ge.3` (plan: D0c).

**Tests.** `transpiler/tests/test_width_inference.cpp` (≤ 300 LoC).
- One unit test per rule, exercised on synthetic AST nodes built via
  the existing `test_matcher_harness`.
- Negative: ambiguous case fires the chosen diagnostic with the
  expected message and source-range.

---

## §6 Beat C — v1 matcher

PRD §7, M3. Depends on A1, B1.

### `sturm-u9ge.12` (plan: C1) — `matcher_qram_subscript.{hpp,cpp}`

**Goal.** Recognise exactly the v1 shape `qint b = a[i];` for the three
containers in PRD §7. Produce a hit vector consumed by the D2 emitter.

**Production.** `transpiler/src/matcher_qram_subscript.{hpp,cpp}`
(each ≤ 300 LoC).
- Pattern style mirrors `matcher_modular_op.cpp`: a single
  `register_qram_subscript_matcher(MatchFinder&, Hits&)` registers a
  callback that pushes one `QramSubscriptHit` per match.
- `QramSubscriptHit` carries: the `VarDecl` for `b`, the container
  expression `a` (with its discriminated container kind:
  `StdArray | CArray | Pointer`), the index expression `i`, the
  inferred width `W` (via B1), and source-ranges for rewrite.
- **Discriminator** (PRD §7): the index sub-expression contains an
  `ImplicitCastExpr` of `CK_UserDefinedConversion` whose conversion
  function is `qint::operator size_t`. This is the strong signal; the
  matcher rejects shapes whose index does not show this cast (those go
  to beat E1's diagnostic matcher or pass through unchanged).
- Container detection: `CXXOperatorCallExpr` on
  `std::array::operator[]` for the `std::array` arm; `ArraySubscriptExpr`
  for the C-array and pointer arms (distinguished by the base
  expression type).

**Tests.** `transpiler/tests/test_matcher_qram_subscript.cpp`
(≤ 300 LoC).
- Three positive fixtures (one per container shape), each producing
  exactly one `QramSubscriptHit` with the right kind tag.
- Negative: classical `size_t` index produces zero hits.
- Negative: index that is `qint` but where `b` is *not* a freshly
  declared `qint` (existing target) produces zero hits here — those
  are E1's job.
- Negative: subscript inside a larger expression (`c = a[i] + d;`)
  produces zero hits — also E1's job.

Fixture files live in `transpiler/tests/fixtures/qram_read_*.cpp`
(input only; no `.expected.cpp` counterpart needed for matcher tests —
those land in D2).

---

## §7 Beat D — Runtime stub + rewrite emitter

PRD §8, M4. Depends on C1, `sturm-u9ge.1` (plan: D0a), `sturm-u9ge.2` (plan: D0b), `sturm-u9ge.4` (plan: D0d).

### `sturm-u9ge.13` (plan: D1) — runtime `QRAM_read` stub

**Goal.** Stand up the runtime entry-point so emitter output compiles
and the counter-mode sink (B1a) records the operation. Semantics
defined by `sturm-u9ge.1` (plan: D0a)/`D0b`.

**Production.**
- `include/sturm/qram/qram_read.hpp` (≤ 200 LoC).
  - Templated declarations covering `std::array<qint_t<W>, N>`,
    `qint_t<W>[N]` (via decay to pointer), and `qint_t<W>*` per
    `sturm-u9ge.1` (plan: D0a).
  - Adjoint registration via `STURM_REGISTER_ADJOINT` per P9c, with
    name fixed in `sturm-u9ge.4` (plan: D0d).
- `src/qram/qram_read.cpp` (≤ 200 LoC).
  - Counter-mode body: increment a `qram_read` counter on the active
    sink; do **not** emit individual gates. Gate-level expansion is
    a separate epic and is explicitly out of scope here.

**Tests.** `tests/qram/test_qram_read_stub.cpp` (≤ 200 LoC).
- For each container shape: construct, populate (`P4a` classical init),
  call `QRAM_read(a, i, b)`, observe the counter sink incremented
  exactly once, observe `i` was *not* measured (mask of `i` unchanged).
- Adjoint round-trip: `invert(QRAM_read)(a, i, b)` registered correctly
  and callable via the existing P9 lookup machinery.

### `sturm-u9ge.15` (plan: D2) — rewrite emitter

**Goal.** Consume `QramSubscriptHit`s from C1 and emit:

```cpp
qint_t<W> b;
QRAM_read(a, i, b);
```

(PRD §8). Modify the source file in-place via the existing rewrite
infrastructure used by `lossy_rewrite_emitter.cpp` and
`modular_rewrite_emitter.cpp`.

**Production.** `transpiler/src/qram_emitter.{hpp,cpp}`
(each ≤ 300 LoC).
- Public: `void emit_qram_rewrites(Rewriter&, const Hits&);`
- Per hit: replace the original `qint b = a[i];` declaration site with
  a two-line sequence (declaration of `qint_t<W> b` + call), preserving
  trailing comments and column alignment per the existing rewriter
  conventions.
- Adjoint placement at the enclosing reversible scope's exit per
  `sturm-u9ge.4` (plan: D0d) (mirrors how `uncompute_*` is placed today by
  `adjoint_emitter.cpp`).

**Tests.** `transpiler/tests/test_qram_emitter.cpp` (≤ 300 LoC).
- Three pairs of fixture files, one per container shape, in
  `transpiler/tests/fixtures/`:
  - `qram_read_std_array.cpp` / `.expected.cpp`
  - `qram_read_c_array.cpp` / `.expected.cpp`
  - `qram_read_pointer.cpp` / `.expected.cpp`
- Each pair runs through `test_driver.cpp`-style golden comparison.
- Negative: emitter is a no-op when the hit vector is empty.

---

## §8 Beat E — Out-of-scope diagnostics

PRD §9, M5. Depends on A1, A2 (so all four shapes parse).

### `sturm-u9ge.16` (plan: E1) — `matcher_qram_oos.{hpp,cpp}`

**Goal.** Produce a compile-time error citing the PRD section for each
of the four out-of-scope shapes (PRD §9), so a missed shape never falls
through to silent measurement.

**Production.** `transpiler/src/matcher_qram_oos.{hpp,cpp}`
(each ≤ 300 LoC).
- One callback per shape, each raising a distinct diagnostic id:
  `qram-oos-existing-target`, `qram-oos-write`, `qram-oos-rmw`,
  `qram-oos-expression-position`.
- Diagnostic body cites `docs/prd_qram_subscript.md §9` and the row of
  the table that covers the shape.
- Detection uses the same `UserDefinedConversion` discriminator as C1
  but in *negative* contexts (assignment LHS, compound RHS-of-`=`,
  inside binary operators, etc.).

**Tests.** `transpiler/tests/test_matcher_qram_oos.cpp` (≤ 300 LoC).
- Four input-only fixtures (`transpiler/tests/fixtures/qram_oos_*.cpp`),
  one per shape; each must produce exactly the expected diagnostic id.
- Coexistence test: a file containing both an in-scope read and an
  out-of-scope shape produces one rewrite + one diagnostic, in either
  order.

---

## §9 Beat F — Optional warning *(post-v1)*

PRD §10.1, M7. Depends on A1.

### `sturm-u9ge.14` (plan: F1) — `matcher_qint_implicit_warn.{hpp,cpp}`

**Goal.** Warn (not error) at any `UserDefinedConversion` site for
`qint → integer` whose enclosing expression is *not* a subscript.

**Production.** `transpiler/src/matcher_qint_implicit_warn.{hpp,cpp}`
(each ≤ 200 LoC).
- Warning id `qint-implicit-measure`, suppressed by default in v1
  but available behind `-Wsturm-qint-implicit-measure`.

**Tests.** `transpiler/tests/test_qint_implicit_warn.cpp` (≤ 200 LoC).
- Positive: `int x = q;`, `std::vector<int> v(q);`,
  `for (size_t i = 0; i < q; ++i)` each fire the warning.
- Negative: `arr[q]` (subscript context) does *not* fire.

This beat may land at any time after A1 and is independent of the
critical path.

---

## §10 Beat G — End-to-end gate

### `sturm-u9ge.17` (plan: G1) — `test_qram_e2e.cpp`

**Goal.** A single test that takes a hand-written `.cpp` file using the
v1 syntax, runs the full transpile pipeline, compiles the output,
executes it under counter-mode and circuit-mode sinks, and asserts:

- The emitted file does **not** contain `operator size_t` calls on
  `qint_t<W>` (post-transpile safety net per PRD §5).
- `QRAM_read` is invoked exactly once per source-level subscript.
- Index mask is unchanged after the call (no measurement of the
  quantum index).
- `invert(routine)` of the enclosing reversible function uncomputes the
  `QRAM_read` correctly.

**Tests.** `transpiler/tests/test_qram_e2e.cpp` (≤ 300 LoC).
- One fixture per container shape, exercised through both sink modes.

---

## §11 Beat H — Follow-up bd issues filed at v1 close

PRD §12.6, M6. One bd issue per non-goal shape, filed but not
implemented:

- `sturm-u9ge.6` (plan: H1) — `b = a[i];` (existing target; needs uncompute of old `b`).
- `sturm-u9ge.7` (plan: H2) — `a[i] = b;` (QRAM-write).
- `sturm-u9ge.8` (plan: H3) — `a[i] += b;` (RMW = read + adjusted-write).
- `sturm-u9ge.9` (plan: H4) — `c = a[i] + d;` (expression-position read; ancilla
  extract + uncompute).

Each issue's description references this plan and PRD §9; bodies are
empty (`status=open`, `priority=4`/backlog) until promoted.

---

## §12 Acceptance criteria for v1 close

The epic closes when **all** of the following hold:

1. Beats A1, A2, B1, C1, D1, D2, E1, G1 land green.
2. `sturm-u9ge.1` (plan: D0a)–`sturm-u9ge.4` (plan: D0d) are closed.
3. `sturm-u9ge.6` (plan: H1)–`sturm-u9ge.9` (plan: H4) are filed (not implemented).
4. `docs/prd_qram_subscript.md` is updated to **Status: Implemented**
   with a back-link to this plan.
5. `docs/getting_started.md` (or the user-facing introduction it
   points to) carries the mandatory measurement-footgun note from
   PRD §10.1.
6. `ctest --parallel 6` is green on both `Debug` and `Release`.
7. F1 may or may not have landed; it is not a v1 gate.

---

## §13 Test-driven landing protocol per beat

For every beat in this plan:

1. Open the bd issue (`bd update <id> --claim`).
2. Write the test module first (it will not compile against a missing
   production module — that is the red phase).
3. Land the production module against the test module until
   `ctest --parallel 6 -R <beat-pattern>` is green.
4. Run the **full** suite (`ctest --parallel 6`) — beat does not close
   on a partial run.
5. Commit with the issue id in the subject line; close the bd issue.

The 6-thread cap is project-wide (`/Users/sorenwilkening/Desktop/STURM-C++/CLAUDE.md`); never relax it.

---

## §14 Risks specific to this plan (vs. PRD risks)

- **Drift between A2 stubs and `qint_t<W>` operator surface.** If
  `qint_t<W>` grows a new operator after v1, the alias stub list
  silently lags and pre-transpile code stops compiling. Mitigation:
  add a *static-assertion harness* in `test_qint_alias_ops.cpp` that
  enumerates expected operators and fails if `qint_t<W>` exposes one
  the alias does not. (PRD §5 already calls this out as the alias
  model's maintenance cost; the harness makes the cost loud.)
- **Width inference rule churn.** If `sturm-u9ge.3` (plan: D0c) is reopened
  mid-implementation, B1 and every fixture in C1/D2 may need
  regeneration. Mitigation: keep B1's rule list one function deep so a
  rule-order swap is a single-file change, and parametrize fixtures by
  width.
- **Fixture-driven tests vs golden-file fragility.** Three pairs of
  fixtures in D2 are golden-file comparisons; whitespace churn in the
  rewriter breaks them. Mitigation: reuse the existing
  `test_driver.cpp` normalisation step (already in use for the
  `return_to_out_param_*` fixtures) rather than raw `diff`.

---

## §15 Cross-references

- `docs/prd_qram_subscript.md` — the source of every requirement here.
- `docs/01_principles.md` — P2, P4a, P9, P9a–d, B5/B5a, B7.
- `docs/TODO_reversibility_deferrals.md` — adjoint registration cross-cut
  (see `sturm-u9ge.4` (plan: D0d)).
- `transpiler/src/matcher_modular_op.{hpp,cpp}` — closest existing
  analogue for the C1 / D2 split (one matcher pushing `Hit`s, one
  emitter draining them).
- `transpiler/src/lossy_rewrite_emitter.{hpp,cpp}` — closest existing
  analogue for the in-place declaration-rewrite shape D2 emits.
