# Implementation Plan — `qint` alias completion + transpiler-driven type substitution

**Status.**
- Wave 1 (§§0–18): shipped 2026-05-04 under bd epic `sturm-65rs`
  (issues `.1`–`.14` closed). The plan documents historical mnemonic
  `sturm-qac.*` for these beats; the bd ids of record are
  `sturm-65rs.*`.
- Wave 2 (§§19–22): drafted 2026-05-05 under bd epic `sturm-qaca`
  (`sturm-qaca.1` .. `sturm-qaca.5`). Tracks PRD §9 / G5–G6 / A7–A10.

**Tracks.** `docs/prd_qint_alias_completion.md` G1–G6 / A1–A10.
**Predecessors.** `docs/archive/plan_qram_subscript.md` (alias-class
introduction, beats A1/A2/B1/C1), `docs/archive/plan_qram_backend.md`
(QROM emission, `render_qint_typename`).
**Scope tag (wave 1).** `sturm-qac` epic (mnemonic; bd ids
`sturm-65rs.1` .. `sturm-65rs.18`). Beat → bd-id map at §1a.
**Scope tag (wave 2).** `sturm-qaca` epic; child ids
`sturm-qaca.1` .. `sturm-qaca.5`. Beat → bd-id map at §19a.

---

## §0 Reading guide

This plan decomposes the PRD into **bd issues**, grouped into **beats**.
Each beat is a self-contained, test-driven landing:

- One *production* module per beat. Hard cap **≤ 300 LoC** including
  comments and blank lines.
- One *test* module per beat. Same 300-LoC cap unless explicitly noted.
- Beats land in dependency order. After every beat the full test suite
  (`CTEST_PARALLEL_LEVEL=6 ctest --output-on-failure`) is green before
  the next beat is opened.
- Tests are written **before** the production code they exercise (RED
  → GREEN → REFACTOR). Each beat below names its red-phase fixture
  first.

LoC budgets are advisory ceilings. If a module would exceed 300 LoC,
split it along the seam noted in its description; do not relax the cap.

The plan was filed as bd epic `sturm-qac`; mnemonic beat names (O0,
A1, A2, A3, B0, B1, C0, C1, C2, C3, D0, D1, E1, F1) survive only as
documentation. The bd ids are the authoritative work tracker.

---

## §1 Module map (bird's-eye view)

| Layer       | File                                                           | Cap | Beat   | New? |
|-------------|----------------------------------------------------------------|-----|--------|------|
| Header      | `include/sturm/qtypes/qint_alias.hpp`                          | 300 | A1     | edit |
| Header      | `include/sturm/qtypes/qint_alias_ops.hpp`                      | 300 | A2     | edit |
| Header      | `include/sturm/qtypes/qint_alias_ops_mixed.hpp` *(if needed)*  | 200 | A2-X   | new  |
| Header      | `include/sturm/qtypes/qint_fwd.hpp`                            | 100 | B1     | edit |
| Transpiler  | `transpiler/src/render_qint_typename.hpp`                      | 100 | C0     | new  |
| Transpiler  | `transpiler/src/qram_emitter.cpp` (delete local copy)          | n/a | C0     | edit |
| Transpiler  | `transpiler/src/matcher_qint_alias_subst.{hpp,cpp}`            | 300 | C1     | new  |
| Transpiler  | `transpiler/src/qint_alias_subst_emitter.{hpp,cpp}`            | 300 | C2     | new  |
| Transpiler  | `transpiler/src/transpile_consumer.cpp`                        | n/a | C3     | edit |
| Tests       | `tests/qtypes/test_qint_alias_ops.cpp`                         | 300 | A2     | edit |
| Tests       | `tests/qtypes/test_qint_alias_member_ops.cpp`                  | 300 | A1     | new  |
| Tests       | `tests/packaging/test_qint_resolution.cpp`                     | 200 | B1     | new  |
| Tests       | `transpiler/tests/test_render_qint_typename.cpp`               | 100 | C0     | new  |
| Tests       | `transpiler/tests/test_matcher_qint_alias_subst.cpp`           | 300 | C1     | new  |
| Tests       | `transpiler/tests/test_qint_alias_subst_emitter.cpp`           | 300 | C2     | new  |
| Tests       | `transpiler/tests/test_consumer_qint_alias_subst.cpp`          | 200 | C3     | new  |
| Tests       | `transpiler/tests/test_qint_alias_subst_e2e.cpp`               | 200 | E1     | new  |
| Tests       | `tests/regressions/test_qint_callsite_respelling.cpp`          | 200 | D1     | new  |
| Fixtures    | `transpiler/tests/fixtures/qint_alias_subst_var.{cpp,exp.cpp}` | n/a | C1, C2 | new  |
| Fixtures    | `transpiler/tests/fixtures/qint_alias_subst_parm.{cpp,exp.cpp}`| n/a | C1, C2 | new  |
| Fixtures    | `transpiler/tests/fixtures/qint_alias_subst_field.{cpp,exp.cpp}`|n/a | C1, C2 | new  |
| Fixtures    | `transpiler/tests/fixtures/qint_alias_subst_ret.{cpp,exp.cpp}` | n/a | C1, C2 | new  |
| Fixtures    | `transpiler/tests/fixtures/qint_alias_subst_cast.{cpp,exp.cpp}`| n/a | C1, C2 | new  |
| Fixtures    | `transpiler/tests/fixtures/qint_alias_subst_overlap.{cpp,exp}` | n/a | C3     | new  |
| Examples    | `examples/qram_demo.cpp`                                       | n/a | F1     | edit |
| Doc         | `docs/qram_user_intro.md` (migration note)                     | n/a | F1     | edit |

The `_mixed.hpp` split is conditional: file A2 first; only split if it
breaches 300 LoC after the new operator pairs land (PRD R4).

---

## §1a Beat → bd-issue id map

| Beat | bd id          | What it is                                          |
|------|----------------|-----------------------------------------------------|
| —    | `sturm-qac`    | epic                                                |
| O0   | `sturm-qac.1`  | scoping: audit alias callsites & lock §3 inventory  |
| A1   | `sturm-qac.2`  | member ops on `frontend::qint`                      |
| A2   | `sturm-qac.3`  | mixed-type free ops (`-*/%&|^`) + drift-gate        |
| A2-X | `sturm-qac.4`  | (conditional) split `qint_alias_ops_mixed.hpp`      |
| B0   | `sturm-qac.5`  | header-cycle audit between `qint_fwd` ↔ `qint_alias`|
| B1   | `sturm-qac.6`  | repoint `sturm::qint` to `frontend::qint`           |
| C0   | `sturm-qac.7`  | extract shared `render_qint_typename.hpp`           |
| C1   | `sturm-qac.8`  | matcher `matcher_qint_alias_subst`                  |
| C2   | `sturm-qac.9`  | emitter `qint_alias_subst_emitter`                  |
| C3   | `sturm-qac.10` | consumer registration + claimed-decls overlap guard |
| D0   | `sturm-qac.11` | re-spell internal callsites needing `qint_t<64>`    |
| D1   | `sturm-qac.12` | regression test pinning re-spelled callsites        |
| E1   | `sturm-qac.13` | end-to-end: `qram_demo.cpp` zero `frontend::qint`   |
| F1   | `sturm-qac.14` | example + docs migration note                       |
| —    | `sturm-qac.15` | follow-up: `qbool` compares on alias (out of scope) |
| —    | `sturm-qac.16` | follow-up: write-side `q[k] = …` (out of scope)     |
| —    | `sturm-qac.17` | follow-up: per-Parm/Field width inference           |
| —    | `sturm-qac.18` | follow-up: bump `kDefaultWidth` to 64               |

`sturm-qac.15`–`.18` are filed but not landed by this plan (PRD §7).

---

## §2 Dependency graph

```
        ┌─────┐
        │ O0  │ scope/audit (no code)
        └──┬──┘
           │
   ┌───────┼─────────┐
   ▼                 ▼
┌────┐            ┌────┐
│ A1 │            │ C0 │  shared render header (independent)
└─┬──┘            └─┬──┘
  ▼                 │
┌────┐              │
│ A2 │              │
└─┬──┘              │
  ▼                 │
┌────┐              │
│A2-X│ (cond.)      │
└─┬──┘              │
  │   ┌────┐        │
  └──▶│ B0 │        │
      └─┬──┘        │
        ▼           │
      ┌────┐        │
      │ B1 │        │
      └─┬──┘        │
        ▼           ▼
      ┌────┐     ┌────┐
      │ D0 │     │ C1 │
      └─┬──┘     └─┬──┘
        ▼          ▼
      ┌────┐     ┌────┐
      │ D1 │     │ C2 │
      └────┘     └─┬──┘
                   ▼
                 ┌────┐
                 │ C3 │
                 └─┬──┘
                   ▼
                 ┌────┐
                 │ E1 │  (gates on B1 + C3 + D0)
                 └─┬──┘
                   ▼
                 ┌────┐
                 │ F1 │
                 └────┘
```

A and C tracks are independently testable. They merge at E1.

---

## §3 Beat O0 — Scoping & callsite audit (`sturm-qac.1`)

**No code.** Output: a checked-in markdown table of every `sturm::qint`
callsite in `tests/`, `examples/`, and `src/` that touches a member
absent from `frontend::qint` (after A1+A2 land). PRD §6 R1 names five
files; O0 confirms or extends that list and pins the inventory used by
D0.

### Deliverables
1. `bd remember` entry tagged `qac-callsite-audit` listing every file +
   line that uses one of: `super_mask`, `qubits[]` direct access, `phi()`,
   `theta()`, or any non-const `BitProxy` write (`q[k] = …`).
2. Confirmation that the alias header dependency graph
   (`qint_alias.hpp` ↔ `qint_fwd.hpp`) is acyclic when flipped — see B0.

### Acceptance
- Audit list ≥ 5 files (PRD R1 callsites + any new ones).
- Inventory is the input to D0.

---

## §4 Beat A1 — Member ops on `frontend::qint` (`sturm-qac.2`)

PRD §4.1 member-ops bullet.

### Red-phase tests (write FIRST)
`tests/qtypes/test_qint_alias_member_ops.cpp` (new, ≤ 300 LoC):

- `qint& operator=(int64_t)` round-trip:
  - `qint q; q = 7; EXPECT_EQ(q.classical_value(), 7);`
  - measurement counter unchanged (P4a — classical assign is free).
- `bool operator[](size_t k) const`:
  - For `qint q = 0b10110;`, expect bits 1, 2, 4 == true, others false.
  - `k == 64` and `k == 999` return false (defensive bound).
  - Each call bumps the measurement counter by exactly 1.
- `explicit operator int64_t() const`:
  - Positive: `int64_t v = static_cast<int64_t>(q);` compiles and equals
    `q.classical_value()`.
  - Negative SFINAE: `static_assert(!std::is_convertible_v<qint,
    int64_t>);` (the conversion must be `explicit`).
  - Bumps the counter by exactly 1.
- All three ops appear in the SFINAE drift-gate harness in
  `test_qint_alias_ops.cpp` (positive: alias has it; negative for
  `operator=` / `operator[]`: backend `qint_t<W>` has the same shape).

### Production code
Edit `include/sturm/qtypes/qint_alias.hpp` (currently 215 LoC). Add the
three members in the existing class body. Counter bumps go through
`qint_alias_detail::bump_measurement_count()` (already declared).

### Constraints
- File stays ≤ 300 LoC. Current head is 215; budget 85 LoC.
- No new includes beyond what already gets pulled in transitively
  (`<cstdint>` is already there).
- Operator bodies are inline in the class body — no out-of-line
  templates needed (none of the three are templated).

### Acceptance (PRD A1, A2 partial)
- New test binary `test_qint_alias_member_ops` is RED before edits,
  GREEN after.
- Existing `test_qint_alias_ops` stays GREEN.
- File LoC count ≤ 300.

---

## §5 Beat A2 — Mixed-type free ops (`sturm-qac.3`)

PRD §4.1 free-ops bullet.

### Red-phase tests
Edit `tests/qtypes/test_qint_alias_ops.cpp`. Per PRD §4.1 closing
paragraph, add for each new operator (`-`, `*`, `/`, `%`, `&`, `|`,
`^`):
- One positive `static_assert` for `qint OP int` (must compile).
- One positive `static_assert` for `int OP qint` (must compile).
- One negative `static_assert` for `qint OP bool` (must NOT compile).
- One runtime case verifying value semantics + counter bump count
  (each operand counts once).
- The `/` and `%` overloads exercise the zero-divisor guard in the same
  way as the existing `qint × qint` versions.

### Production code
Edit `include/sturm/qtypes/qint_alias_ops.hpp` (currently 237 LoC). Add
14 free functions: 7 ops × 2 directions. To keep marginal LoC small
and uniform per PRD R4, factor a single inline helper template:

```cpp
namespace qint_alias_detail {
template <class Int, class F>
inline qint mixed_arith(const qint& a, Int c, F op) noexcept;
}  // (rendered once, called from each op)
```

Reverse direction (`Int OP qint`) is **only** added for `+` per
PRD §3 ("Reverse-direction `<integral> OP qint` for non-`+` arithmetic
… backend does not carry these either"). Forward direction
(`qint OP Int`) is added for all 7. So the actual count is 8 new free
functions, not 14.

### Constraints
- File stays ≤ 300 LoC. Current head is 237; budget 63 LoC.
- If after the helper still over budget → A2-X: split mixed overloads
  into `qint_alias_ops_mixed.hpp`, included from `qint_alias_ops.hpp`
  at the bottom. Test file `test_qint_alias_ops.cpp` covers the
  combined surface unchanged.

### Acceptance (PRD A2)
- Drift-gate has positive + negative `static_assert` per PRD §4.1.
- All existing tests stay GREEN.
- LoC budget honoured (or A2-X opened and closed in same beat).

---

## §6 Beat B0 — Header-cycle audit (`sturm-qac.5`)

**No production code.** Output: the paragraph below, which pins the
include order B1 (`sturm-qac.6`) implements verbatim.

### B0 audit paragraph (specification for B1)

After B1 lands, `qint_alias.hpp` keeps including `qint_fwd.hpp` for
the forward declaration of `qint_t<W>` (the templated converting
ctor `qint(const ::sturm::qint_t<W>&)` declared in the class body
needs the type name to be visible, but never the full definition —
the inline body at `qint_alias.hpp:246` does not dereference `src`).
`qint_fwd.hpp`, AFTER declaring `template <std::size_t Width = 64>
class qint_t;`, then `#include`s `sturm/qtypes/qint_alias.hpp` and
re-exports the alias class as `using qint =
::sturm::frontend::qint;` in namespace `sturm`. The cycle that
results (`qint_fwd.hpp` → `qint_alias.hpp` → `qint_fwd.hpp`) is
broken by `#pragma once` on both headers combined with the property
that `qint_alias.hpp` only ever needs the forward declaration of
`qint_t<W>` — never its definition — so re-entering `qint_fwd.hpp`
under the `#pragma once` guard is a no-op and `qint_alias.hpp`
finishes parsing against the forward declaration emitted before the
include. The include order INSIDE `qint_fwd.hpp` must therefore be,
exactly:

```cpp
// (1) header guard
#pragma once
// (2) forward declaration of the backend template — MUST precede (3)
namespace sturm {
template <std::size_t Width = 64> class qint_t;
}
// (3) pull the alias class definition (it includes qint_fwd.hpp back,
//     but #pragma once + the forward decl above keep that no-op safe)
#include "sturm/qtypes/qint_alias.hpp"
// (4) re-export the frontend alias under the bare sturm:: spelling
namespace sturm {
using qint = ::sturm::frontend::qint;
}
```

If steps (2) and (3) are inverted, `qint_alias.hpp` re-enters
`qint_fwd.hpp`'s `#pragma once` guard with no `qint_t<W>` forward
declaration in scope, and the templated converting ctor fails to
parse. The cross-reference for this analysis is the O0 audit
memory `qac-callsite-audit` (header dependency graph section), which
this paragraph does not contradict.

### Acceptance
- B0 closes when the include order paragraph above is documented in
  this section. (B1 / `sturm-qac.6` is the beat that lands the unit
  test demonstrating compile-success of `#include
  "sturm/qtypes/qint_fwd.hpp"` standalone — out of scope here.)

---

## §7 Beat B1 — Repoint `sturm::qint` (`sturm-qac.6`)

PRD §4.2.

### Red-phase tests
`tests/packaging/test_qint_resolution.cpp` (new, ≤ 200 LoC):

```cpp
#include <sturm/sturm.hpp>
static_assert(std::is_same_v<sturm::qint, sturm::frontend::qint>);

// Backwards-compat: local typedef shadows.
namespace { using qint = sturm::qint_t<32>; }
static_assert(std::is_same_v<::qint, sturm::qint_t<32>>);
```

Plus a runtime smoke test that `qint q = 5; size_t s = q;` still
compiles and bumps the counter (unchanged behaviour).

### Production code
Edit `include/sturm/qtypes/qint_fwd.hpp` per §6 above. Replace `using
qint = qint_t<64>;` with the include + re-export pattern.

### Constraints
- After B1, **all 50+ existing transpiler fixtures with local `using
  qint = sturm::qint_t<W>;` still compile unchanged** — this is the
  PRD A6 invariant. Local typedef shadows the namespace alias, so the
  fixtures keep their semantics.
- B1 will fail compile in any TU where `sturm::qint` is treated as a
  `qint_t<W>` (e.g. `q.super_mask`). Those callsites are re-spelled in
  D0 — D0 MUST land before B1 to keep the tree green.

**Reordering note:** D0 actually runs before B1 in landing order, but
both need each other's awareness. The dependency graph in §2 reflects
this: D0 → B1 (re-spell first, then flip the alias). Numbering in
§1a is documentation order, not landing order. The bd `--dependency`
edges encode the real ordering: B1 has `depends_on: D0`.

### Acceptance (PRD A6, G2)
- `test_qint_resolution` GREEN.
- Full test suite GREEN (= no fixture regression = PRD A6).

---

## §8 Beat C0 — Shared `render_qint_typename` (`sturm-qac.7`)

PRD §4.3 close.

### Red-phase tests
`transpiler/tests/test_render_qint_typename.cpp` (new, ≤ 100 LoC):
- `render_qint_typename(8)  == "sturm::qint_t<8>"`
- `render_qint_typename(32) == "sturm::qint_t<32>"`
- `render_qint_typename(64) == "sturm::qint_t<64>"`

### Production code
1. Create `transpiler/src/render_qint_typename.hpp` (≤ 100 LoC) with
   one inline function. Match the body currently at
   `qram_emitter.cpp:110`.
2. Delete the local definition from `qram_emitter.cpp`. Add `#include
   "render_qint_typename.hpp"`.
3. Verify `lossy_rewrite_emitter.cpp` (referenced by the comment at
   `qram_emitter.cpp:104`) — if it has its own copy, route it through
   the shared header too. Do this in the same commit so there is one
   definition site.

### Acceptance
- New unit test GREEN.
- Existing transpiler tests UNCHANGED behaviour.
- `grep -R "qint_t<.*>"` in `transpiler/src/` shows only one
  definition site of the rendering helper.

---

## §9 Beat C1 — Matcher `matcher_qint_alias_subst` (`sturm-qac.8`)

PRD §4.3 match anchors.

### Red-phase tests
`transpiler/tests/test_matcher_qint_alias_subst.cpp` (new, ≤ 300
LoC). Five fixtures, one per anchor class:

| Fixture                                        | Anchor                  |
|------------------------------------------------|-------------------------|
| `qint_alias_subst_var.cpp`                     | `VarDecl`               |
| `qint_alias_subst_parm.cpp`                    | `ParmVarDecl`           |
| `qint_alias_subst_field.cpp`                   | `FieldDecl`             |
| `qint_alias_subst_ret.cpp`                     | function return type    |
| `qint_alias_subst_cast.cpp`                    | `CXXFunctionalCastExpr` |

Each fixture is matcher-only at this beat — the test asserts the
matcher fires exactly once per anchor, with the correct `TypeLoc`
range captured. Emission is C2's job.

Negative tests (matcher MUST NOT fire):
- A `using qint = sturm::qint_t<8>;` followed by `qint x;` — the
  `TypeAliasDecl` must not match (PRD §4.3 close).
- `sturm::qint_t<32> x;` — backend type, must not match.
- `frontend::qint x{};` *inside* a macro expansion — out of scope per
  matcher conventions (pin behaviour either way).

### Production code
`transpiler/src/matcher_qint_alias_subst.{hpp,cpp}` (≤ 300 LoC each).
Use the discriminator from PRD §4.3:

```cpp
cxxRecordDecl(hasName("qint"),
              hasParent(namespaceDecl(hasName("frontend"))))
```

Bind each anchor to a stable name (`"vd"`, `"pmd"`, `"fd"`, `"fn"`,
`"cast"`). The matcher emits a typed `Match` struct; the emitter
consumes it.

### Acceptance (PRD A4 partial)
- Five fixtures match; negative cases don't. Counter at end of run
  matches the expected hit count per anchor.

---

## §10 Beat C2 — Emitter `qint_alias_subst_emitter` (`sturm-qac.9`)

PRD §4.3 rewrite shape.

### Red-phase tests
`transpiler/tests/test_qint_alias_subst_emitter.cpp` (new, ≤ 300 LoC).
Pair each C1 fixture with `*.expected.cpp`:

- `qint_alias_subst_var.cpp` → emits `sturm::qint_t<W>` where `W` is
  whatever `infer_width()` returns for the VarDecl. Cover one case
  with an integer-literal initializer (rule 2) and one without (rule
  3 → 32).
- For `parm`, `field`, `ret`: width = `kDefaultWidth = 32` (rule 3,
  per PRD §3 non-goal: "Width inference for ParmVarDecl / FieldDecl /
  return-type beyond the rule-3 default" is out of scope).
- For `cast`: width = 32.

### Production code
`transpiler/src/qint_alias_subst_emitter.{hpp,cpp}` (≤ 300 LoC each).
Reuses `render_qint_typename.hpp` (C0) and the existing
`infer_width(VarDecl, InferContext)`.

### Acceptance (PRD A4 full)
- All five fixtures round-trip through C1+C2 to the pinned
  `.expected.cpp`.
- Test binary GREEN.

---

## §11 Beat C3 — Consumer wiring + overlap guard (`sturm-qac.10`)

PRD §4.3 co-existence with C1 (QRAM-subscript) matcher.

### Red-phase tests
`transpiler/tests/test_consumer_qint_alias_subst.cpp` (new, ≤ 200 LoC):

- Fixture `qint_alias_subst_overlap.cpp` contains the QRAM-subscript
  shape `qint b = a[i];` AND a separate `qint x;` declaration in the
  same TU. After full transpile, `b` is rewritten by the QRAM emitter
  (its existing path) and `x` is rewritten by the new emitter — each
  exactly once. Pinned by `.expected.cpp`.
- Unit test `test_matcher_qint_alias_subst_no_overlap` (PRD R2): the
  same VarDecl is never claimed by both emitters.

### Production code
Edit `transpiler/src/transpile_consumer.cpp`:

1. Register the new matcher.
2. Drain it **after** `emit_qram_rewrites`.
3. Maintain a `std::unordered_set<const VarDecl*>` of decls already
   rewritten by the QRAM emitter; the new emitter consults it and
   skips overlapping hits.

Try to keep the consumer edit ≤ 60 net new LoC; the file is already
1244 LoC. If the change pushes it past a natural seam, factor the
claimed-decls set into a small struct in
`transpiler/src/qint_alias_subst_emitter.hpp` and pass it by ref —
**do not** rip apart `transpile_consumer.cpp` in this beat.

### Acceptance (PRD R2)
- Overlap fixture passes. Single-VarDecl-single-rewrite invariant
  pinned by unit test.

---

## §12 Beat D0 — Re-spell internal callsites (`sturm-qac.11`)

PRD R1.

### Production code (no new test file)
Re-spell, in one commit, the callsites enumerated by O0 — PRD R1's
audit pins five files:

- `tests/test_resource_lifecycle.cpp:106`
- `tests/backend/test_when_control_stack_bridge.cpp:266,297,337,383`
- `tests/packaging/test_umbrella_only.cpp:34`

In each: `sturm::qint q;` → `sturm::qint_t<64> q;`. Where the local
context already does `using qint = sturm::qint_t<W>;` no edit is
needed (B1 leaves those untouched).

If O0's audit found additional callsites, include them here.

### Acceptance
- Test suite GREEN with B1 reverted (= D0 is independently green).
- After B1 lands, suite GREEN.

---

## §13 Beat D1 — Regression test (`sturm-qac.12`)

### Red-phase test (write before any further changes)
`tests/regressions/test_qint_callsite_respelling.cpp` (new, ≤ 200
LoC). For every callsite re-spelled in D0, assert the type chosen via
`std::is_same_v<decltype(...), sturm::qint_t<64>>`. This pins the
re-spelling against future drift back to bare `qint`.

### Acceptance
- Test GREEN. If any future change drops a `qint_t<64>` back to bare
  `qint` and that callsite needs the backend surface, the regression
  test fails loudly.

---

## §14 Beat E1 — End-to-end alias erasure (`sturm-qac.13`)

PRD A5.

### Red-phase test
`transpiler/tests/test_qint_alias_subst_e2e.cpp` (new, ≤ 200 LoC):

1. Run the transpiler over `examples/qram_demo.cpp`.
2. Read `build/sturm_gen/examples/qram_demo.cpp`.
3. Assert: `EXPECT_EQ(content.find("sturm::frontend::qint"),
   std::string::npos);`
4. Assert the runtime invariant from G1 still holds (compile + run the
   generated file → `measurement_count() == 0`).

### Production code
None expected — A1+A2+B1+C1+C2+C3 should already make the assertion
hold. If E1 RED, file a follow-up bd issue per gap.

### Acceptance (PRD A5)
- Test GREEN.

---

## §15 Beat F1 — Example + docs migration note (`sturm-qac.14`)

PRD A3, R3.

### Edits
1. `examples/qram_demo.cpp`:
   - Remove `using qint = sturm::frontend::qint;` (line 37 today).
   - Fix the local-shadow bug (`qint a = 3, b = 4; … qint b = a[i];`
     redeclares `a`/`b`).
   - Verify it compiles + runs.
2. `docs/qram_user_intro.md`:
   - Add a short migration note: bare `qint` now resolves to the
     frontend alias; users who previously got `qint_t<64>` semantics
     should re-spell as `sturm::qint_t<64>` if they need bit-level
     access. Reference PRD §6 R3.

### Acceptance (PRD A3)
- Demo compiles & runs cleanly. No `using qint = ...` line in the
  example.

---

## §16 Test-pyramid summary

```
                        ┌──────────────────┐
   end-to-end (E1)      │  qram_demo e2e   │  1 test
                        ├──────────────────┤
   integration (C3, B1) │  consumer +      │  3 tests
                        │  packaging       │
                        ├──────────────────┤
   unit (A1, A2, B0,    │  member ops,     │  ~30 tests
   C0, C1, C2, D1)      │  free ops,       │
                        │  matchers,       │
                        │  emitters,       │
                        │  render helper,  │
                        │  callsites       │
                        └──────────────────┘
```

Each beat must add at least one failing test before the production
edit. Beats land green or not at all.

---

## §17 Risk register (mirrors PRD §6)

| ID | Risk                                    | Mitigation in plan        |
|----|-----------------------------------------|---------------------------|
| R1 | Repointing breaks `super_mask` callsites| D0 (re-spell) before B1   |
| R2 | Double-rewrite VarDecl                  | C3 claimed-decls set+test |
| R3 | Width default 32 surprises              | F1 docs migration note    |
| R4 | LoC over budget on `qint_alias_ops.hpp` | A2-X conditional split    |

Plus plan-level:

- **R-plan-1.** B1 lands while a fixture still uses `q.super_mask`
  → tree red. **Mitigation:** D0 strictly precedes B1 in the bd
  dependency graph.
- **R-plan-2.** C2's emitter shares `infer_width` with the QRAM
  emitter; a behaviour change in `infer_width` propagates to both.
  **Mitigation:** add a `test_width_inference` snapshot test for the
  rule-3 default (32) before C2 lands.
- **R-plan-3.** `transpile_consumer.cpp` is already 1244 LoC.
  **Mitigation:** C3 limits net edit to ≤ 60 LoC; refactor of the
  consumer is out of scope.

---

## §18 Definition of done (epic `sturm-qac`)

1. PRD A1–A6 all green:
   - A1 build + ctest green.
   - A2 drift-gate covers every new op.
   - A3 demo no using-line.
   - A4 fixtures round-trip.
   - A5 zero `sturm::frontend::qint` post-transpile.
   - A6 50+ existing fixtures unchanged.
2. All beats `sturm-qac.1`–`sturm-qac.14` closed in bd.
3. Follow-ups `sturm-qac.15`–`sturm-qac.18` filed and labeled
   `out-of-scope`.
4. `docs/prd_qint_alias_completion.md` and this plan moved to
   `docs/archive/` per project convention. *(Deferred until wave 2
   §22 also closes — the doc pair travels together.)*
5. `git push` succeeds (project session-completion rule).

---

## §19 Wave 2 — Array & pointer carrier coverage

**Tracks.** PRD §9 (G5, G6, A7–A10).
**Trigger.** Build target `example_qram_demo` fails post-wave-1
because the alias-subst matcher's anchor predicates do not traverse
`ArrayType` / `PointerType` carriers — see PRD §9.1 for the precise
failure-mode evidence.

### §19a Beat → bd-id map

| Beat | bd id           | What it is                                              |
|------|-----------------|---------------------------------------------------------|
| —    | `sturm-qaca`    | epic                                                    |
| G1   | `sturm-qaca.1`  | matcher: array-element + pointer-pointee traversal      |
| G2   | `sturm-qaca.2`  | emitter round-trip coverage on G3 fixtures              |
| G3   | `sturm-qaca.3`  | fixture pairs (carray, ptr, carray_typedef)             |
| G4   | `sturm-qaca.4`  | CI gate `test_sturm_gen_clean`                          |
| G5   | `sturm-qaca.5`  | unblock `examples/qram_demo.cpp` end-to-end + ctest run |
| —    | `sturm-qaca.6`  | follow-up: multi-dim / reference-to-array (out of scope)|

`sturm-qaca.6` is filed but not landed by this wave (PRD §9.5 R6).

### §19b Beat G1 — Matcher extension (`sturm-qaca.1`)

PRD §9.3.1.

#### Red-phase tests (write FIRST)
Edit `transpiler/tests/test_matcher_qint_alias_subst.cpp` to add:

| Anchor      | Source shape              | Expected match |
|-------------|---------------------------|----------------|
| VarDecl     | `qint a[4];`              | 1 hit, range = `qint` token only (NOT `[4]`) |
| ParmVarDecl | `void f(qint b[]);`       | 1 hit, element span only                     |
| FieldDecl   | `struct S { qint c[3]; };`| 1 hit, element span only                     |
| VarDecl     | `qint* p;`                | 1 hit, range = `qint` token only (NOT `*`)   |
| ParmVarDecl | `void g(qint* q);`        | 1 hit, pointee span only                     |
| FieldDecl   | `struct T { qint* d; };`  | 1 hit, pointee span only                     |
| (negative)  | `qint a[N][M];`           | 0 hits (multi-dim — R6, out of scope)        |
| (negative)  | `qint (&r)[N];`           | 0 hits (reference-to-array — R6)             |
| (negative)  | `using QArr = qint[4]; QArr a;` | 0 hits (sugared carrier — R5)         |

#### Production code
Edit `transpiler/src/matcher_qint_alias_subst.cpp`. Replace the
single-record gate on each declarator anchor with a disjunction
helper:

```cpp
auto frontend_qint_carrier() {
  return anyOf(
    /* direct: qint x;  */
    hasCanonicalType(hasDeclaration(frontend_qint_record())),
    /* array: qint a[N] / qint a[] */
    hasCanonicalType(arrayType(hasElementType(
      hasDeclaration(frontend_qint_record())))),
    /* pointer: qint* p */
    hasCanonicalType(pointerType(pointee(
      hasDeclaration(frontend_qint_record())))));
}
```

Update `typeloc_range_of(DeclaratorDecl*)` to:

1. Read the DeclaratorDecl's `TypeSourceInfo`.
2. Walk into `ArrayTypeLoc::getElementLoc()` /
   `PointerTypeLoc::getPointeeLoc()` once (single-level — multi-level
   is R6).
3. If the resulting TypeLoc is a `TypedefTypeLoc`, return an invalid
   `SourceRange` (R5 — preserve user typedef; matcher's invalid-range
   gate at line 116 / 134 / 152 then drops the match).
4. Otherwise return the final TypeLoc's source range.

Cap the helper at ≤ 30 net new LoC; if it grows, factor a small
recursive `element_typeloc_walk(TypeLoc)`.

#### Constraints
- File `matcher_qint_alias_subst.cpp` head 277 LoC; budget 23 net new
  before the file's 300-LoC cap. If the budget is tight, lift the
  helper into a sibling `qint_alias_carrier_walk.{hpp,cpp}` ≤ 100 LoC.
- Multi-dim, reference-to-array, member pointers stay out of scope
  in v1; the negative tests pin that boundary.

#### Acceptance (PRD A7, A8 partial — matcher half)
- All positive tests fire on the right anchor with the right
  TypeLoc range.
- All negative tests do NOT fire.
- Existing wave-1 matcher tests stay GREEN (no regression).

---

### §19c Beat G2 — Emitter round-trip coverage (`sturm-qaca.2`)

PRD §9.3.2.

#### Red-phase tests
Extend `transpiler/tests/test_qint_alias_subst_emitter.cpp` to
round-trip the G3 fixtures (forward reference; bd
`depends_on: sturm-qaca.3`).

#### Production code
None expected. The emitter substitutes whatever TypeLoc range the
matcher hands it; G1 already feeds the element/pointee span. If a
fixture round-trip fails, escalate via a new G2-prime task; do NOT
inflate G2.

#### Acceptance (PRD A7, A8)
- All carrier fixtures round-trip to pinned `.expected.cpp`.

---

### §19d Beat G3 — Fixtures (`sturm-qaca.3`)

#### Edits
Three new fixture pairs in `transpiler/tests/fixtures/`:

| Fixture                                        | Pins                          |
|------------------------------------------------|-------------------------------|
| `qint_alias_subst_carray.{cpp,expected.cpp}`   | VarDecl + ParmVarDecl + FieldDecl with C-array carrier |
| `qint_alias_subst_ptr.{cpp,expected.cpp}`      | same anchors with pointer carrier                      |
| `qint_alias_subst_carray_typedef.{cpp,expected.cpp}` | R5 pin: user typedef carrier survives unchanged |

Each fixture mirrors the wave-1 hermetic-stub pattern (matcher unit
test does not link the full sturm headers). The expected file for
`carray_typedef` is byte-identical to the input.

#### Acceptance
- Fixtures compile against the hermetic stub.
- G2's round-trip test consumes them GREEN.

---

### §19e Beat G4 — Backend-script clean CI gate (`sturm-qaca.4`)

PRD §9.3.3 / G6 / A10.

#### Red-phase test
`transpiler/tests/test_sturm_gen_clean.cpp` (new, ≤ 200 LoC):

1. Resolve the build's `sturm_gen/` directory via a CMake-generated
   header `<sturm_gen_path.hpp>` (`configure_file` at configure time).
2. Walk `**/*.cpp` and `**/*.hpp` under it.
3. Hard fail on any line matching:
   - `sturm::frontend::qint`
   - `using qint = ::sturm::frontend::qint`
   - `using qint = sturm::frontend::qint`
4. Print offending file:line on failure for direct nav.

#### Production code
1. Add `test_sturm_gen_clean` target in
   `transpiler/tests/CMakeLists.txt`.
2. `configure_file` `transpiler/src/sturm_gen_path.hpp.in` →
   `${CMAKE_BINARY_DIR}/include/sturm_gen_path.hpp` recording
   `kSturmGenDir = "${CMAKE_BINARY_DIR}/sturm_gen"`.
3. `add_dependencies(test_sturm_gen_clean
   <every transpile-build target>)` so the test fires only after
   `sturm_gen/` is materialised.

#### Negative-control verification (manual one-shot during G4)
Comment out the array-element arm of the matcher; rebuild;
`test_sturm_gen_clean` goes RED with the offending file:line.
Restore. Do NOT check in a flake-prone "deliberately break" mode.

#### Constraints
- Test file ≤ 200 LoC.
- No dependency on third-party regex; std::regex or hand-rolled
  substring scan both fine.
- Exit-code on failure must list ALL offending sites in one run
  (don't bail on first hit — debugging cycles benefit from full
  inventory).

#### Acceptance (PRD A10)
- Test GREEN under wave-2 matcher.
- Negative-control demonstration passes.

---

### §19f Beat G5 — `qram_demo.cpp` end-to-end (`sturm-qaca.5`)

PRD A9.

#### Red-phase
Failing build evidence (top of wave-2 commit):
`cmake --build build_mac --target example_qram_demo --parallel 6` ⇒
*"no matching function for call to 'QRAM_read'"*. Acceptance flips
this to GREEN.

#### Production code
None expected on `examples/qram_demo.cpp` — the file already has
the wave-2 target shape (`qint a[4]; for(...) { a[i] = i; }
qint i = 10; qint b = a[i];`). G5 only:

1. Wires a `example_qram_demo_run` ctest target that runs the binary
   and asserts exit code 0 + the printed circuit diagram is non-empty.
2. Verifies the wave-1 G3-style invariant
   (`measurement_count() == 0`) still holds — gated through the
   wave-1 E1 test (`test_qint_alias_subst_e2e.cpp`) which already
   covers this assertion shape; if `qram_demo.cpp` is not in its
   input set, add it.

If the build is still RED after G2 lands, file `sturm-qaca.6`+ with
the precise gap; do NOT inflate G5.

#### Acceptance (PRD A9)
- `cmake --build build_mac --target example_qram_demo --parallel 6`
  GREEN.
- `ctest -R example_qram_demo_run` GREEN.

---

## §20 Wave 2 dependency graph

```
       ┌────┐
       │ G1 │ matcher extension
       └─┬──┘
         ▼
       ┌────┐
       │ G3 │ fixtures
       └─┬──┘
         ▼
       ┌────┐
       │ G2 │ emitter round-trip
       └─┬──┘
         ▼
       ┌────┐
       │ G4 │ CI gate
       └─┬──┘
         ▼
       ┌────┐
       │ G5 │ qram_demo e2e
       └────┘
```

Strict serial; no parallel beats this wave. (G3 could land before
G1 to give G1 a richer red-phase, but the `frontend_qint_carrier()`
disjunction can be tested with inline source strings inside
`test_matcher_qint_alias_subst.cpp` — the on-disk fixtures are only
consumed by G2.)

---

## §21 Wave 2 risk register (mirrors PRD §9.5)

| ID | Risk                                           | Mitigation in plan                          |
|----|------------------------------------------------|---------------------------------------------|
| R5 | Element TypeLoc walks through user typedef     | G1 negative test + G3 carray_typedef pin    |
| R6 | Multi-dim arrays / reference-to-array slip in  | G1 negative tests pin v1 boundary; bd       |
|    |                                                | follow-up `sturm-qaca.6` filed              |
| R7 | Build-system path drift across `build*/` dirs  | G4 `configure_file`-generated path header   |

---

## §22 Wave 2 definition of done (epic `sturm-qaca`)

1. PRD A7–A10 all GREEN.
2. All beats `sturm-qaca.1`–`sturm-qaca.5` closed in bd.
3. Multi-dim / reference-to-array follow-up `sturm-qaca.6` filed and
   labeled `out-of-scope`.
4. `examples/qram_demo.cpp` builds + runs under
   `cmake --build build_mac --target example_qram_demo --parallel 6`
   (project CLAUDE.md hard cap on `--parallel 6`).
5. After wave-2 closes, both `prd_qint_alias_completion.md` and this
   plan move to `docs/archive/` per project convention (the move
   deferred at wave-1 §18 step 4 happens here).
6. `git push` succeeds (project session-completion rule).
