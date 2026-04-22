# Implementation Plan: Transpiler Phase M — PM5 Peephole Gate Reordering (with alias-analysis prerequisite)

**Status:** Draft (implementation plan skeleton, PM5-0)
**Date:** 2026-04-22
**Relates to:**
- `docs/roadmap_transpiler_post_mvp.md:630-632` — the PJ-2 deferral blockquote from the Phase J completion note that names this work.
- `docs/roadmap_transpiler_post_mvp.md:984` — the Phase M bullet "Peephole gate reordering" that this plan will strike through at PM5-11.
- `docs/01_principles.md` (B1b, B6, B9, B10, P9 — the PM5 surface must not violate any of them; B9's enumeration gains a line at PM5-10).
- bd epic `sturm-u655` (this file is the PM5-0 deliverable, tracked as `sturm-u655.1`).
- `/home/agent/.claude/plans/yes-do-that-ask-linked-sunbeam.md` (operational breakdown that produced `sturm-u655.1..12`; Part B is the scoping substance mirrored here).

## Context

Phase J (landed 2026-04-17) shipped four IR-level optimizations: PJ-1 zero-ancilla fusion, PJ-3 uncompute hoisting, PJ-4 dead-ancilla elimination, and a deferral note on PJ-2. That deferral reads (`docs/roadmap_transpiler_post_mvp.md:630-632`): *"PJ-2 (peephole gate reordering) was deferred to Phase M on the value-gain analysis — it would require alias analysis to pay for itself, and PJ-1's adjacent-statement peephole already captures the motivating fusion case."*

Phase M has since drained four of its five stretch items — **PM1** in-memory transpile (v0.1.2, 2026-04-19), **PM2** `#line`-based source maps (2026-04-20), **PM3** quantum-specific diagnostics (epic `sturm-5btt`, 2026-04-21), **PM4** pluginization (epic `sturm-4oyr`, 2026-04-21). PJ-2 peephole gate reordering is the last Phase M item still open, now renamed **PM5** to mark its move from Phase J to Phase M.

PM5 is that deferred work. It has two halves, shipped as one epic under `sturm-u655`:

1. **Alias analysis prerequisite.** A minimal QIR-level alias-footprint API — no interprocedural analysis, no pointer chasing, no cross-scope reasoning — just enough to answer "do these two `QValueRef`s reference bit-disjoint qubit slices?" with a conservative false-positive on any case the extractor cannot resolve. This is the machinery the PJ-2 deferral note predicted the work would need.

2. **Peephole reorder consumer.** One new matcher that walks `QScope.ops` in source order, finds adjacent triples `(A, B, C)` where A and C would fuse under PJ-1 if B weren't between them, tests whether B's footprint overlaps A's result or C's reads/writes, and emits a `QReplacement` that moves B past C if the footprints are disjoint. The matcher registers **last** in `transpile_consumer.cpp` — after `register_hoist_invariant_matcher` — so it observes the final op list after fusion, hoisting, and dead-ancilla elimination have already run.

The value-gain concern from the PJ-2 deferral note is addressed by scope: PM5 fires only on the narrow triple-commutation pattern, not on arbitrary gate-level reorderings. This keeps the optimizer's footprint tiny, the test surface finite (six fixtures), and the analysis conservative (footprint-only — no value-range inference, no loop-carried dependence).

---

## 1. Scope

PM5 ships **one analysis header + one peephole matcher**. Deliberate minimalism.

**In scope:**
- `QubitFootprint` data model + two free functions in `sturm::transpile::detail` namespace: `footprint(const QValueRef&, const clang::ASTContext&) -> QubitFootprint` and `may_overlap(const QubitFootprint&, const QubitFootprint&) -> bool`.
- One new matcher source `matcher_peephole_reorder.cpp` exporting `register_peephole_reorder_matcher` via the shared `transpiler/include/sturm/transpile/matcher.hpp` header.
- Registration in `transpile_consumer.cpp` as the **last** matcher in the registration block, amending the existing "LAST: hoist" ordering comment at lines 329-351.
- A one-line edit to `docs/01_principles.md` B9 prose enumerating PM5 alongside the existing PJ-1 / PJ-3 / PJ-4 list.
- Six new snapshot fixtures + one gate-equivalence namespace pair + one idempotency-verified example.

**Explicitly out of scope for PM5:**
- **Cross-scope aliasing.** Two `QValueRef`s that live in different `QScope`s are never compared. PM5's matcher walks one scope at a time; the alias extractor does not attempt to resolve declarations across function / routine / loop / branch boundaries.
- **Plugin-op footprint hooks.** `QOpKind::PLUGIN` ops are treated as **fully opaque** — every operand collapses to the universal footprint sentinel, so the matcher refuses to commute through any plugin op. A future `Registry::register_footprint_fn` v2 API would unlock this; it is out of scope for PM5 for the same reason PM4 punted priority APIs (ABI stability of the Registry surface is a v2 concern).
- **Reorders crossing hoist/fuse boundaries.** If any op in the `(A, B, C)` triple has `hoist_to_override` set, or if any of the three statements is covered by `unit.fused_stmt_ranges` / `unit.eliminated_stmt_ranges`, the matcher bails. PJ-1/PJ-3/PJ-4 anchor ops to specific pre-loop/post-loop locations; commuting past them breaks invariants that Phase J already verified.
- **Non-constant BitProxy indices.** `q[i]` with runtime `i` collapses to the full-width footprint. Optimistic value-range inference is a v2 target (see §12 Sharp edges).
- **Loop-carried dependence.** PM5 does not reason about whether a reorder is safe *across* loop iterations — it only reasons about ordering *within* a single `QScope`. The hoist-invariant matcher (PJ-3) already handles loop-invariance; PM5 leaves iteration-level dataflow alone.
- **Interprocedural alias.** Calls to routines other than `USER_ROUTINE`-tagged ops are treated as opaque. USER_ROUTINE ops use their operand mask to decide which of the routine's operands are written (already computed in Phase I PI-2); no routine body is re-analyzed.

Out-of-scope items may land later as separate phases. The v1 surface is deliberately small so the test matrix (six fixtures + one m12 pair) stays finite and the footprint of the patch stays auditable.

---

## 2. What "aliasing" means in this domain

The core data model is `QubitFootprint` — a range into the bit-space of a single named qubit container:

```cpp
namespace sturm::transpile::detail {

/// A bit-range footprint on a named qubit container.
///
/// `name` + `decl_loc` together identify the qubit container (qbool or qint_t<W>)
/// this footprint points at. `bit_range` is the half-open interval [lo, hi) of
/// bits within that container that this footprint covers.
///
/// A zero-initialized `QubitFootprint{}` (empty name, invalid decl_loc, lo==hi==0)
/// is the "universal" sentinel — it may_overlap() with every other footprint,
/// including other universal sentinels. Callers emit this sentinel whenever
/// extraction fails (dependent type, plugin-op operand, unresolvable expression).
struct QubitFootprint {
    std::string name;                // empty == universal sentinel
    clang::SourceLocation decl_loc;  // invalid iff universal
    struct BitRange { int lo; int hi; };
    BitRange bit_range{0, 0};        // half-open interval; lo==hi==0 iff universal
};

} // namespace sturm::transpile::detail
```

**Extraction rules** (from the `footprint()` free function):

- **`qbool` operand.** A bare `DeclRefExpr` to a `qbool` yields `{name=<decl spelling>, decl_loc=<VarDecl::getLocation()>, bit_range={0,1}}`. `qbool` is always width-1; no template peeling needed.
- **`qint_t<W>` operand.** A bare `DeclRefExpr` to a `qint_t<W>` yields `{name=<decl spelling>, decl_loc=<VarDecl::getLocation()>, bit_range={0,W}}`. Width extraction resolves the `ClassTemplateSpecializationDecl::getTemplateArgs()` and reads the first argument as an integral constant.
- **`q[k]` BitProxy with integer-literal `k`.** A subscript call `operator[](k)` on a `qint_t<W>` variable, where `k` is a compile-time integer (via `Expr::EvaluateAsInt` returning true and a non-negative value `< W`), yields `{name=<decl spelling>, decl_loc=<VarDecl::getLocation()>, bit_range={k, k+1}}`.
- **`q[k]` BitProxy with non-constant `k`.** `Expr::EvaluateAsInt` fails, or returns a value outside `[0, W)`. Falls back to `{name=<decl spelling>, decl_loc=<VarDecl::getLocation()>, bit_range={0,W}}` — the full-width footprint. This is the conservative optimism-rejection case.
- **Dependent type / unresolvable expression.** Any `QualType::isDependentType()` call chain, any sugar the resolver cannot peel, any expression shape the extractor does not recognize → `QubitFootprint{}` universal sentinel.
- **Plugin-op operand.** `QOpKind::PLUGIN` ops' operands collapse to the universal sentinel unconditionally. The Registry has no footprint-hook API in v1 (see §12 Sharp edges, item 3).

**Overlap rules** (the `may_overlap()` free function):

```cpp
/// Returns false IFF the two footprints provably refer to disjoint bits.
/// Returns true in all other cases (including both universal sentinels).
///
/// Disjointness criteria (short-circuited in order):
///   1. Either footprint is the universal sentinel (empty name) → true.
///   2. Different names or different decl_locs → false (different decls
///      cannot alias in single-TU mode; qubits are indexed-backed).
///   3. Same name + same decl_loc → compare bit ranges; disjoint iff
///      a.hi <= b.lo OR b.hi <= a.lo.
bool may_overlap(const QubitFootprint& a, const QubitFootprint& b);
```

The "different decls cannot alias" short-circuit leans on a property of the runtime: each `qbool` / `qint_t<W>` construction allocates a fresh index range from the pool, and the pool does not hand the same index to two distinct live declarations. This is already true today (B7: qubit indices are backend-managed) and is not changed by PM5.

**What this buys us.** A triple `(A, B, C)` where A is `qbool __t = a & b;`, B is `x ^= y;`, and C is `z ^= __t;` is safe to reorder to `(A, C, B)` iff:
- `footprint(__t)` is disjoint from `footprint(x)` and `footprint(y)` (B's operands do not touch A's result).
- `footprint(__t)` is disjoint from `footprint(x)` and `footprint(y)` at C's call site (C's operand-`__t` is the same `__t` as A's result, covered by the same footprint test).
- A, B, C's statements are not already fused / eliminated / hoisted (those matchers anchor to source locations PM5 must not perturb).

---

## 3. API surface

New header `transpiler/include/sturm/transpile/alias.hpp`, namespace `sturm::transpile::detail` (matches `matcher_common.hpp` convention — this is an internal matcher helper, not public plugin API). Two free functions, no class, no state:

```cpp
#pragma once

#include "sturm/transpile/qir.hpp"
#include <clang/AST/ASTContext.h>

namespace sturm::transpile::detail {

struct QubitFootprint {
    std::string name;
    clang::SourceLocation decl_loc;
    struct BitRange { int lo; int hi; } bit_range{0, 0};
};

/// Extract a QubitFootprint from a QValueRef by peeling the underlying
/// expression via `ctx`. Returns a universal sentinel on any extraction
/// failure (see §2 and alias.cpp for the exact fallback ladder).
QubitFootprint footprint(const QValueRef& op, const clang::ASTContext& ctx);

/// Returns true iff `a` and `b` may refer to overlapping bits. Designed
/// to be cheap (O(1) after extraction); see §2 for the overlap rules.
bool may_overlap(const QubitFootprint& a, const QubitFootprint& b);

} // namespace sturm::transpile::detail
```

**Why a free function + namespace, not a class.** The extractor is stateless. A class would imply there is per-instance state to keep (a cache, a visitor, a parent pointer) — there is not. The free-function shape composes the same way `expr_is_loop_invariant` composes in `matcher_common.hpp`: callers pass the AST and get a value.

**Why `detail` namespace.** This helper is not part of the plugin ABI. Plugin authors have no business calling `footprint()` — the Registry does not ship a footprint-hook in v1 (§12 Sharp edges). `detail` signals "internal to the matcher ordering machinery, not user-facing."

**Why return-by-value.** `QubitFootprint` is three `int`-sized fields + a `std::string` + a `SourceLocation`. Pessimistically ~80 bytes. Return-by-value is strictly less painful than output-parameter-by-reference for callers that want to compare two extractions back-to-back.

---

## 4. Footprint extraction

The extractor peels three operand shapes. Each ladder step is a separate function in `alias.cpp`, called from `footprint()` in order:

1. **Bare qbool DRE.** `QValueRef::decl_loc` points at a `VarDecl`; `VarDecl::getType()->isRecordType()` and the record's qualified name matches `sturm::qbool`. Emits `bit_range={0,1}`.

2. **Bare qint_t<W> DRE.** `VarDecl::getType()` yields a `ClassTemplateSpecializationDecl` whose qualified name matches `sturm::qint_t`. Read `CTSD::getTemplateArgs()`; the first arg is an `TemplateArgument::Integral` APSInt `W`. Emits `bit_range={0, W}`. If `W` is non-positive or the template arg is type-only (pre-instantiation), emit the universal sentinel.

3. **BitProxy subscript.** The `QValueRef` carries the full `Expr*` the matcher anchored on. If the expression is a `CXXOperatorCallExpr` with operator `OO_Subscript` whose `getArg(0)` is a DRE matching case 2, peel the call's `getArg(1)` and call `Expr::EvaluateAsInt(ctx, value)`. On success with a non-negative value `k < W`, emit `{name=<arg0 spelling>, decl_loc=<arg0 VarDecl::getLocation()>, bit_range={k, k+1}}`. On failure, fall back to case 2's full-width footprint.

4. **`USER_ROUTINE` operand.** The operand-mask bit at the operand's index tells us whether this operand is written by the routine. Extraction itself is case 1 or 2 — the operand is a bare DRE. The caller consults the mask before asking whether a reorder is safe (a write-operand has both read + write footprints, conservatively treated as both). See §5 for how the matcher consumes this.

5. **Fallback.** Any expression shape the ladder does not recognize → universal sentinel. This includes dependent-type operands, pointer / reference surgery, implicit conversions to the wrong type, etc.

Failure is cheap: the universal sentinel is three default-initialized fields. It may_overlap against everything, so the matcher conservatively refuses to commute.

**`alias.cpp` layout.**
- `namespace sturm::transpile::detail { … }` wrapper.
- Anonymous-ns helpers: `extract_qint_width(const clang::ClassTemplateSpecializationDecl&) -> std::optional<int>`, `is_qbool_record(const clang::RecordDecl&) -> bool`, `peel_bitproxy_constant(const clang::Expr&, const clang::ASTContext&) -> std::optional<int>`.
- Two public free-function implementations: `footprint()` and `may_overlap()`.
- No static state. No globals. No singletons. No cache.

---

## 5. Peephole reorder consumer

New matcher `transpiler/src/matcher_peephole_reorder.cpp`. Export one symbol:

```cpp
void register_peephole_reorder_matcher(clang::ast_matchers::MatchFinder& finder,
                                       QUnit& unit);
```

**Trigger shape.** The matcher binds on a TranslationUnit and walks `unit.scopes` *after* all other matchers have completed their callbacks — it does not use a MatchFinder binding. This mirrors `matcher_hoist_invariant.cpp`'s pattern: the registered callback runs in MatchFinder's terminal phase, observing the fully-populated `QUnit`.

**Per-scope pass.** For each `QScope` in `unit.scopes`:
- Skip non-`LoopBody` / non-`Function` scopes (branch bodies, routine bodies under special control regimes). Use `detail::classify_scope_kind` from `matcher_common.hpp` to classify.
- Walk `QScope.ops` looking at adjacent triples `(A, B, C)` by index. For each triple:
  - **Gate 1 — kind shape.** A must be `QOpKind::AND` with a synthetic-name result (`__stu_tN` prefix). C must be `QOpKind::XOR_ASSIGN` whose first operand name matches A's result name. B may be any kind **except** `QOpKind::PLUGIN` or `QOpKind::USER_ROUTINE` with ambiguous mask (gated by §4 rule 4 / §12 Sharp edges).
  - **Gate 2 — hoist / fuse / eliminated guards.** `A.hoist_to_override.isInvalid() && B.hoist_to_override.isInvalid() && C.hoist_to_override.isInvalid()` (no op in the triple is a hoisted head/tail). No statement in `unit.fused_stmt_ranges` covers any of A.stmt_range / B.stmt_range / C.stmt_range (use PJ-1e's `is_range_covered_by_fused` probe). Same for `unit.eliminated_stmt_ranges` via PJ-4a's parallel probe.
  - **Gate 3 — footprint disjointness.** Extract footprints for A.result, all of B's operands, and C's operands (minus the `__t` read which is already covered). Call `may_overlap()` pairwise. If any B operand's footprint may overlap A's result OR any of C's operands, bail — the reorder is unsafe.
  - **Gate 4 — fuse precondition check.** Once B is moved past C, the resulting `(A, C)` pair must match the PJ-1 `ccnot_fuse` trigger shape. Seed the PJ-1d / PJ-1f fuse condition inline: same synthetic `__t`, single-reader-in-scope (`detail::count_readers_in_scope` from `matcher_common.hpp`), same scope, adjacent after reorder. If the fuse precondition fails, bail — the reorder has no payoff.
- On all four gates passing, emit a `QReplacement` that moves B's statement text from between A and C to after C. The QReplacement uses `format_line_directive()` (PM2-1) to preserve `#line` attribution for B — B's source position changes, but its `#line` points back at the user's original line.
- **Single-pass design.** Do not iterate to fixed point. One pass over each scope's op list. If a reorder exposes a new triple, it will land in the next transpile invocation (PM1-4's nested invocation is not re-entered by PM5; the user's build runs sturm-transpile once per TU).

**Output mask integration.** A `USER_ROUTINE` op with a non-zero output-mask bit on an operand means that operand is **written** — it must be treated as both read + written for footprint disjointness. PM5-4's alias unit test pins the `f(q, q)` aliasing case where two call-by-reference operands to the same routine correctly blocks commutation (see §12 Sharp edge 6).

---

## 6. Ordering vs Phase J — LAST after hoist

PM5's matcher registers in `transpile_consumer.cpp` **after** `register_hoist_invariant_matcher` (line 352). It becomes the new last entry in the registration block. The existing ordering comment at lines 329-351 gets amended from "LAST: hoist" to "LAST: hoist, then reorder" with a sub-bullet explaining why.

**Why last.**
- **After PJ-1 fuse.** The reorder matcher's Gate 4 (fuse precondition) checks whether `(A, C)` would fuse under PJ-1. If PJ-1 has already fused `(A, C)` when B wasn't between them — say, because A and C were already adjacent in a different scope, or because an earlier pass removed B — then there is no reorder to do. Running after PJ-1 ensures PM5 sees the post-fusion op list.
- **After PJ-4 dead-ancilla.** PJ-4 erases ops whose result has zero readers. If A's result has zero readers (because the consumer C was deleted), A is gone by the time PM5 runs, and the triple doesn't exist. Running after PJ-4 ensures PM5 never tries to reorder a triple around a doomed op.
- **After PJ-3 hoist.** If A or C has been hoisted out of the current scope, the triple is no longer adjacent in any meaningful sense, and the reorder boundary crosses a hoist anchor — forbidden by Gate 2. Running after PJ-3 ensures PM5 sees the post-hoist op list and the `hoist_to_override` guards are definitive.
- **After all Phase A..I matchers.** Same reason Phase J's hoist matcher runs last: the op list must be fully populated before a reorder pass can make decisions about the final ordering.

**Backstop discipline.** `HandleTranslationUnit` already sequences `apply_fused_stmt_guards` → `apply_eliminated_stmt_guards` → `synthesize`. PM5's callback fires from MatchFinder's terminal phase, which runs **before** `synthesize`, so the guards are already applied by the time PM5 reads `unit.scopes`. No new backstop is needed.

**Why not "before hoist."** If PM5 reorders `(A, B, C) → (A, C, B)` and then PJ-3 hoists A out of the loop, B is now dangling past C with A gone. The hoist invariant ("forward/uncompute pair stays paired") breaks. Running PM5 after PJ-3 means A is either not hoisted (safe to reorder) or hoisted (triple bails at Gate 2). Either way, no cross-pass interaction bug.

**Documentation delta.** The ordering comment at `transpile_consumer.cpp:329-351` is extended with:

```
//   3. LAST — after register_hoist_invariant_matcher. The reorder
//      matcher observes the post-fuse / post-hoist / post-dead-ancilla
//      op list and reorders adjacent triples only when all three
//      of (A, B, C) survive the prior passes' guards. Bails on any
//      hoisted op, any fused range, any eliminated range, any
//      plugin-op boundary.
```

---

## 7. Sub-tasks (mirrors bd children under `sturm-u655`)

| bd ID | Title | Depends | Parallelizable with |
|---|---|---|---|
| PM5-0 (`sturm-u655.1`) | **This plan doc** | — | — |
| PM5-1 (`sturm-u655.2`) | Public `alias.hpp` header (`QubitFootprint` + `footprint` + `may_overlap` decls) | PM5-0 | — |
| PM5-2 (`sturm-u655.3`) | Alias impl: qbool + qint_t<W> width extraction in `alias.cpp` | PM5-1 | PM5-3 |
| PM5-3 (`sturm-u655.4`) | Alias impl: BitProxy constant-index peel + conservative fallback in `alias.cpp` | PM5-1 | PM5-2 |
| PM5-4 (`sturm-u655.5`) | Alias unit tests `test_alias_footprint.cpp` — 8–12 cases across shapes + cross-decl + cross-name + `f(q, q)` aliasing | PM5-2, PM5-3 | PM5-5 |
| PM5-5 (`sturm-u655.6`) | `matcher_peephole_reorder.cpp` + `matcher.hpp` export + anonymous-ns helpers | PM5-2, PM5-3 | PM5-4 |
| PM5-6 (`sturm-u655.7`) | Register in `transpile_consumer.cpp` LAST after hoist + ordering-invariant comment update | PM5-5 | — |
| PM5-7 (`sturm-u655.8`) | Matcher unit tests `test_matcher_peephole_reorder.cpp` — 6 fixtures (disjoint-qbool, disjoint-qint-bits, overlap-rejected, BitProxy-const, BitProxy-nonconst, plugin-op-refused) | PM5-6 | PM5-8 |
| PM5-8 (`sturm-u655.9`) | End-to-end snapshot `pm5_reorder_snapshot` + gate-equivalence `m12_reorder_{transpiled,reference}` pair | PM5-6 | PM5-7 |
| PM5-9 (`sturm-u655.10`) | Example `examples/peephole_reorder.cpp` + idempotency CTest | PM5-6 | — |
| PM5-10 (`sturm-u655.11`) | B9 prose edit in `docs/01_principles.md` enumerating PM5 alongside PJ-1/PJ-3/PJ-4; principle re-check | PM5-7, PM5-8 | — |
| PM5-11 (`sturm-u655.12`) | Roadmap completion blockquote (under Phase M) + bullet update at `roadmap_transpiler_post_mvp.md:984` + CHANGELOG | PM5-8, PM5-9, PM5-10 | — |

**Parallel pairs** (safe to spawn two bd-workers concurrently): PM5-2 ∥ PM5-3, PM5-4 ∥ PM5-5, PM5-7 ∥ PM5-8.

---

## 8. Dependency graph

```
PM5-0 ──> PM5-1 ──┬──> PM5-2 ──┬──> PM5-4 ──┐
                  │            │            │
                  │            ├──> PM5-5 ──┼──> PM5-6 ──┬──> PM5-7 ──┐
                  │            │            │            │            │
                  └──> PM5-3 ──┘            │            ├──> PM5-8 ──┼──> PM5-10 ──┐
                                            │            │            │            │
                                            │            └──> PM5-9 ──┘            ├──> PM5-11
                                            │                                      │
                                            └──────────────────────────────────────┘
```

---

## 9. Critical files

### New
- `transpiler/include/sturm/transpile/alias.hpp` — public `QubitFootprint` struct + `footprint()` + `may_overlap()` decls.
- `transpiler/src/alias.cpp` — implementations; anonymous-ns helpers `extract_qint_width` / `is_qbool_record` / `peel_bitproxy_constant`.
- `transpiler/src/matcher_peephole_reorder.cpp` — the peephole matcher; consumes `alias.hpp` + `matcher_common.hpp` helpers.
- `transpiler/tests/test_alias_footprint.cpp` — unit tests for the alias extractor (PM5-4). Expected 8–12 test cases.
- `transpiler/tests/test_matcher_peephole_reorder.cpp` — unit tests for the matcher (PM5-7). Expected 6 fixtures.
- `tests/transpiler/fixtures/reorder_disjoint_qbool.expected.cpp` — snapshot fixture (disjoint-qbool triple).
- `tests/transpiler/fixtures/reorder_disjoint_qint_bits.expected.cpp` — snapshot fixture (disjoint qint bit-slices).
- `tests/transpiler/fixtures/reorder_overlap_rejected.expected.cpp` — snapshot fixture (B overlaps A.result — reorder refused, emission unchanged).
- `tests/transpiler/fixtures/reorder_bitproxy_const.expected.cpp` — snapshot fixture (BitProxy with literal index).
- `tests/transpiler/fixtures/reorder_bitproxy_nonconst.expected.cpp` — snapshot fixture (BitProxy with runtime index — reorder refused).
- `tests/transpiler/fixtures/reorder_plugin_op_refused.expected.cpp` — snapshot fixture (B is a plugin op — reorder refused).
- `tests/transpiler/fixtures/pm5_reorder_runtime.cpp` — runtime fixture for `m12_reorder_transpiled`.
- `tests/transpiler/fixtures/pm5_reorder_reference.cpp` — manually-written reference matching the post-reorder emission byte-for-byte, for `m12_reorder_reference`.
- `examples/peephole_reorder.cpp` — dogfood example showing the reorder in action. Paired with `tests/transpiler/check_example_peephole_reorder.cmake`.

### Modified
- `transpiler/include/sturm/transpile/matcher.hpp` — new `register_peephole_reorder_matcher` declaration, alongside the other `register_*_matcher` decls.
- `transpiler/src/transpile_consumer.cpp` — register LAST after `register_hoist_invariant_matcher` at line 352; amend ordering comment at lines 329-351.
- `transpiler/CMakeLists.txt` — add `alias.cpp` + `matcher_peephole_reorder.cpp` as new sources to both `sturm-transpile` and `sturm-transpile-plugin` targets. Mirror the PM4-6 dogfood migration pattern — the new sources link into both.
- `transpiler/tests/test_gate_equivalence.cpp` — add `m12_reorder_transpiled` + `m12_reorder_reference` namespace pair consuming `pm5_reorder_runtime.cpp` / `pm5_reorder_reference.cpp`.
- `examples/CMakeLists.txt` — wire `examples/peephole_reorder.cpp` as a quantum executable + add `check_example_peephole_reorder.cmake` as a CTest.
- `docs/01_principles.md` — one-line edit to B9 prose enumerating PM5 alongside PJ-1/PJ-3/PJ-4.
- `docs/roadmap_transpiler_post_mvp.md` — completion note (PM5-11 blockquote) + bullet update at line 984.

### Reused (do not reinvent)
- `QValueRef` + `QOperation.operands` + `QOperation.outputs_mask` + `QUnit.fused_stmt_ranges` + `QUnit.eliminated_stmt_ranges` in `transpiler/include/sturm/transpile/qir.hpp`.
- `detail::count_readers_in_scope` (PJ-1a) in `transpiler/src/matcher_common.hpp` — used by Gate 4 fuse precondition.
- `detail::classify_scope_kind` (PJ-3a) in `matcher_common.hpp` — used at the per-scope gate.
- `detail::expr_is_loop_invariant` (PJ-3b) in `matcher_common.hpp` — not directly consumed, but the mental model of "scope-kind + loop-body-only" is the same.
- PJ-1e's `is_range_covered_by_fused` probe + PJ-4a's `is_range_covered_by_eliminated` probe — consumed in Gate 2.
- PM2's `format_line_directive()` in `transpiler/src/emitter.cpp` — for `#line` attribution of the moved B statement.
- `QReplacement` text-rewrite channel — same emission pipeline Phase E / F / I matchers use; no new rewrite channel.
- `render_uncompute` in `transpiler/src/uncompute_pass.cpp` — unchanged by PM5; reorders do not change adjoint emission (uncompute order follows the re-ordered forward order, naturally).

### Not touched
- The `QOpKind` enum. No new kind.
- The `QOperation` struct. No new field.
- The plugin ABI (`plugin_api.hpp`). PM5 is transpile-internal.
- The diagnostics surface (`diag_context.hpp` / PM3 matchers). PM5 emits no diagnostics.
- The runtime (`include/sturm/runtime/*`). PM5 is transpile-time only.

---

## 10. Verification

**Build-command hard limit.** Every `cmake`, `cmake --build`, `ctest`, `make`, `ninja` invocation MUST cap at 6 threads — `--parallel 6` / `-j6` / `CTEST_PARALLEL_LEVEL=6`. This is a project-wide rule (`CLAUDE.md`).

### Per-bd-item (inside bd-worker)

```bash
cmake --build build --parallel 6 --target <target>
CTEST_PARALLEL_LEVEL=6 ctest --test-dir build -R '<item-regex>' --output-on-failure
```

### End-to-end (after PM5 epic closes)

```bash
cmake -S . -B build
cmake --build build --parallel 6
CTEST_PARALLEL_LEVEL=6 ctest --test-dir build --output-on-failure
```

**Expected.** All existing tests pass byte-identical (confirming PM5's reorder fires only on the new fixtures and does not perturb any prior snapshot), plus the new fixtures pass:

- **Unit — alias (`test_alias_footprint`, PM5-4).** 8–12 cases, organized into groups:
  - `qbool` bare footprint: 1 case.
  - `qint_t<W>` bare footprint for W ∈ {1, 4, 8}: 3 cases.
  - BitProxy constant index: 1 case (in-range), 1 case (out-of-range fallback).
  - BitProxy non-constant index: 1 case (falls back to full-width).
  - Cross-decl disjointness: 2 cases (two `qbool` with different names, two `qint_t<4>` with different names).
  - Same-decl overlap: 2 cases (full overlap, partial overlap).
  - `f(q, q)` USER_ROUTINE aliasing: 1 case pinning §12 Sharp edge 6.

- **Unit — matcher (`test_matcher_peephole_reorder`, PM5-7).** 6 fixtures matching the §9 snapshot list:
  - `reorder_disjoint_qbool` — reorder accepts.
  - `reorder_disjoint_qint_bits` — reorder accepts (qint bit-slice disjointness).
  - `reorder_overlap_rejected` — reorder refuses (B overlaps A's result).
  - `reorder_bitproxy_const` — reorder accepts (BitProxy literal index is safe).
  - `reorder_bitproxy_nonconst` — reorder refuses (BitProxy runtime index is conservatively unsafe).
  - `reorder_plugin_op_refused` — reorder refuses (plugin op is opaque).

- **Snapshot — end-to-end (`pm5_reorder_snapshot`, PM5-8).** A user-facing snapshot fixture exercising the reorder pass through the full transpile pipeline; the rewritten buffer matches the expected `.expected.cpp` byte-for-byte.

- **Gate equivalence (`m12_reorder_transpiled` / `m12_reorder_reference`, PM5-8).** Transpile-path emission vs a manually-written reference emitting the post-reorder gate stream. Counter-mode `GateRecord` stream byte-identical across N iterations.

- **Example (`peephole_reorder_injected` / `peephole_reorder_idempotent`, PM5-9).** The example transpiles cleanly, its emission matches the expected; re-transpiling the emitted buffer is a no-op (idempotency).

### Principle check (before PM5-10 closes)

Reread `docs/01_principles.md`. The PM5 extension surface does not touch any B-principle (B1b, B6, B9, B10) or P9 — all remain valid. Reasoning:

- **B1b** stays intact: PM5 reorders existing forward rewrites and their adjoints. No runtime auto-inversion is introduced. No new adjoint lookup path.
- **B6** stays intact: ancilla lifetimes are unchanged. PM5 never moves a gate **past** an ancilla's RAII scope boundary because reorders are gated on `classify_scope_kind` staying constant across the triple — the triple must live in a single `LoopBody` / `Function` scope.
- **B9** gains a line: the prose body that enumerates "PJ-1 fusion, PJ-3 hoisting, PJ-4 dead-ancilla elimination" becomes "PJ-1 fusion, PJ-3 hoisting, PJ-4 dead-ancilla elimination, PM5 peephole reordering." The count ("one global optimization pass at transpile time") stays one — PM5 runs in the same MatchFinder terminal phase as PJ-3, not as a second pass. **This edit is the PM5-10 deliverable.**
- **B10** stays intact: inverses are still emitted by the transpiler. PM5 moves forward gates around; their adjoints move with them automatically (adjoint emission is scheduled relative to the forward gate's final position, not its original position). Destructors still release qubit indices without emitting gates.
- **P9** stays intact: the transpiler remains a consumer of manual adjoints. PM5 does not synthesize or infer any adjoint.

If an unexpected principle revision emerges during implementation, file it as a sub-bullet under PM5-10. Otherwise no `01_principles.md` edit beyond the B9 enumeration line.

### Session close (mandatory per `CLAUDE.md`)

```bash
bd close <PM5-epic-id>
git pull --rebase
bd dolt push || true   # local-only; no remote configured for bd
git push
git status  # MUST report "up to date with origin/main"
```

---

## 11. Principle check (summary for § 10 cross-reference)

| Principle | Status | Reasoning |
|---|---|---|
| **B1b** (AST-based auto-generation default; runtime auto-inversion retired) | Untouched | PM5 adds no runtime path and no new auto-inversion. |
| **B6** (Ancillas scope-bound via RAII) | Untouched | Reorders never cross scope boundaries; ancilla lifetime is unchanged. |
| **B9** (Two runtime opt layers + one transpile-time global pass) | Enumeration line extended | "PJ-1 / PJ-3 / PJ-4" becomes "PJ-1 / PJ-3 / PJ-4 / PM5." Count stays at one pass. |
| **B10** (Uncomputation is compile-time; destructors do not emit gates) | Untouched | Adjoints move with their forwards; destructor semantics unchanged. |
| **P9** (Routines invertible by explicit adjoint) | Untouched | PM5 does not synthesize adjoints; it reorders emissions of already-registered adjoints. |

---

## 12. Sharp edges / risks

Six risks called out for PM5 reviewers. Each is pinned by a test or a documented decision.

1. **BitProxy non-constant index.** The dominant real-world shape inside loops: `for (int i = 0; i < W; ++i) q[i] ^= other[i];`. `Expr::EvaluateAsInt` fails; the extractor falls back to full-width. `may_overlap` then returns true against every other operand, and the matcher refuses most reorders. **This is deliberate.** Optimistic analysis needs value-range inference (recognize that the loop bounds constrain `i` to a disjoint bit position per iteration), which is a v2 target. PM5-4 pins the conservative behavior with `test_alias_footprint.bitproxy_nonconst_fallback`. If the user's hot loop loses a fusion opportunity, the v1 workaround is to unroll the loop or to use bare qbool operands; the v2 fix is value-range inference.

2. **Control-flow-sensitive aliasing.** WHEN guards / branch bodies create cases where two operands' footprints overlap in AST shape but are runtime-disjoint (one path writes one half, the other path writes the other half). PM5 is footprint-only — it cannot see control flow. **Mitigation:** the matcher gates on `classify_scope_kind ∈ {LoopBody, Function}`, refusing to reorder across WHEN boundaries entirely. If a user's triple spans a WHEN scope exit, the triple is not adjacent in PM5's view and bails on Gate 1 (adjacency). PM5-7's `reorder_overlap_rejected` fixture includes a WHEN-guarded sibling to pin this behavior.

3. **`QOpKind::PLUGIN` operand opacity.** PM4's Registry does not expose a `footprint_for(plugin_kind_id)` hook in v1 (punted for ABI-stability reasons; see `docs/implementation_plan_transpiler_phase_m_pm4.md` §1 Scope). PM5 treats every plugin-op operand as the universal footprint sentinel, so `may_overlap` returns true, so the matcher refuses to commute through any plugin op. **This is deliberate.** A v2 `Registry::register_footprint_fn` API would unlock plugin-op participation; it is out of scope for PM5 for the same reason PM4 punted priority APIs (unstable ABI discipline applies to the footprint hook, too). PM5-7's `reorder_plugin_op_refused` fixture pins the opacity.

4. **Hoisted ops (`hoist_to_override` valid).** PJ-3 anchors forward/uncompute halves to specific pre-loop / post-loop locations. Commuting past them breaks the pairing: the adjoint's anchor assumes the forward op lives at a specific scope position, and moving it invalidates the assumption. **Mitigation:** Gate 2 in the matcher explicitly checks `A.hoist_to_override.isInvalid() && B.hoist_to_override.isInvalid() && C.hoist_to_override.isInvalid()`. One false → bail. PM5-7 does not include a dedicated hoist-crossing fixture because the gate is documented and unit-testable via `test_matcher_peephole_reorder.hoist_boundary_bails`; if the implementation diverges from the docs, that unit test will fire.

5. **Fused-pair transitivity.** If `(A, C)` have already been fused into a `CCNOT_INPLACE` op by PJ-1f, the triple `(A, B, C)` doesn't exist — the op list has `(AC_fused, B)` or similar. `unit.fused_stmt_ranges` carries the source ranges of the fused-into-one pair. **Mitigation:** Gate 2 consults `is_range_covered_by_fused` on A.stmt_range / B.stmt_range / C.stmt_range. Any coverage → bail. Parallel mitigation for PJ-4's eliminated ranges. PM5-4 unit tests consume `fused_stmt_ranges` indirectly (via the extractor's awareness of covered ranges) but do not need a dedicated fixture — PJ-1's own fixtures already verify the fused-range semantics; PM5 inherits.

6. **`USER_ROUTINE` output-mask aliasing.** Two call-by-reference operands to the same routine (`f(q, q)`) correctly blocks commutation — both operands are written, so their footprints cover the entire routine's write set. Even if the two `q` operands are distinct variables, the routine's body might mutate shared state. PM5's alias extractor treats a `USER_ROUTINE` op's written-operand footprints as both read + written. **Pin:** PM5-4 adds `test_alias_footprint.user_routine_double_ref_aliases` as a dedicated test case. The expected outcome: `may_overlap` returns true when either of the two `q` operands is compared with any write-target footprint, and the reorder matcher bails at Gate 3.

---

## 13. Forward compatibility

Two v2 concerns are documented here rather than filed as separate issues; they would graduate to epics if demand emerges:

- **v2 alias analysis — value-range inference.** The BitProxy non-constant case (risk 1) is the most-requested reachable optimization. A v2 pass that tracks `i`'s value range across loop iterations would let the extractor return `{k,k+1}` for `q[i]` when `i`'s range is a compile-time constant range. This is a real optimizer task (not just extraction), so it shipped as PM5-follow-up, not PM5-v1.

- **v2 Registry footprint hook.** `Registry::register_footprint_fn(kind_id, fn)` would let a plugin op declare its footprint without PM5 needing to reason about the plugin body. The ABI for `fn` is a closure over `const QValueRef&, const ASTContext&) -> QubitFootprint` — same signature as `detail::footprint`. Punted on ABI-stability grounds (see §12 risk 3).

Neither v2 concern is blocking for PM5-v1. The v1 surface is conservatively safe (refuses rather than miscommutes), and the test matrix stays finite.

---

## 14. Interaction with PM1-4 / PM2 / PM3 / PM4

For reviewers loading this doc in a future session, a compact cross-reference:

- **PM1 (in-memory transpile).** PM5 does not touch the in-memory pipeline. The reorder pass runs inside the same `TranspileConsumer` the in-memory path invokes; reorders emit through the same `QReplacement` channel PM1-4's nested invocation consumes.
- **PM2 (source maps).** Every moved statement carries a `#line` directive via `format_line_directive()`. The user's compile errors on the moved statement still point at the original source line. PM5-7's fixtures verify `#line` preservation.
- **PM3 (quantum-specific diagnostics).** PM5 emits no diagnostics. The Reviewer's note: if a reorder's Gate 3 fails with an actionable pattern (e.g. "these two operands appear to alias; consider decomposing"), a v2 diagnostic could fire, but v1 stays silent.
- **PM4 (plugin ABI).** PM5 refuses to commute through plugin ops (§12 risk 3). The plugin ABI surface is unchanged; no new `_v2` entry point required.

---

## 15. Definition of done

PM5 is done when all twelve sub-tasks close green, specifically:

- [ ] PM5-0 shipped: this file exists and is >400 lines.
- [ ] PM5-1 shipped: `alias.hpp` compiles, declares two free functions in `sturm::transpile::detail`.
- [ ] PM5-2 shipped: `footprint()` extracts qbool + qint_t<W> footprints; unit-test stub passes.
- [ ] PM5-3 shipped: `footprint()` extracts BitProxy footprints; full unit-test battery passes.
- [ ] PM5-4 shipped: `test_alias_footprint.cpp` contains 8–12 cases; all pass.
- [ ] PM5-5 shipped: `matcher_peephole_reorder.cpp` compiles; `register_peephole_reorder_matcher` is exported.
- [ ] PM5-6 shipped: registration in `transpile_consumer.cpp` is LAST; ordering comment updated.
- [ ] PM5-7 shipped: `test_matcher_peephole_reorder.cpp` contains 6 fixtures; all pass.
- [ ] PM5-8 shipped: `pm5_reorder_snapshot` + `m12_reorder_*` pair green in CTest.
- [ ] PM5-9 shipped: `examples/peephole_reorder.cpp` + idempotency CTest green.
- [ ] PM5-10 shipped: `docs/01_principles.md` B9 prose enumerates PM5; principle re-check documented as a PR comment.
- [ ] PM5-11 shipped: roadmap completion blockquote under Phase M; line 984 bullet updated; CHANGELOG entry for `v0.1.X`.

Each sub-task has a single bd id; bd-worker drains them in the dependency-graph order (§8). A future `bd prime` session begins with `bd ready` and picks PM5-1 on the first call.
