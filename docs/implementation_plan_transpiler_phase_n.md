# Implementation Plan: Transpiler Phase N — Rotation & Preparation Primitives

**Status:** Draft (implementation plan skeleton, PN-0)
**Date:** 2026-04-22
**Relates to:**
- `docs/01_principles.md` P5 items 2 and 3 — `q.theta += d` (amplitude rotation) and `q.phi += d` (phase rotation). These two forward operations need adjoint emission through the transpiler; Phase N is where that wiring lands.
- `docs/01_principles.md` P5 item 1 — `qbool(p)` preparation. Phase N does not add a `PREP` op kind; instead, preparation inside an uncompute-eligible scope raises a quantum-specific diagnostic through the existing `DiagContext` (PM3-pattern).
- `docs/01_principles.md` B5 — "Primitives have uncontrolled and singly-controlled forms only." Phase G AND-fold collapses nested WHEN controls to a single ancilla, so rotations under `WHEN` are depth ≤ 1 by construction. Multi-control rotations are **out of scope**; the fixture & example pin this invariant.
- `docs/01_principles.md` P9 — "Routines are invertible by explicit adjoint." Amplitude / phase rotations follow the dual operator rule `+= ↔ -=`; the transpiler's uncompute pass emits the sign-flipped inverse inline (no `uncompute_api.hpp` hook needed because `ThetaProxy::operator-=` / `PhiProxy::operator-=` already exist at `include/sturm/qtypes/qint_core.hpp:305,372`).
- `docs/roadmap_transpiler_post_mvp.md` (the "Phase N — Rotation & preparation primitives" section that PN-9 adds after Phase M).
- bd epic `sturm-z2a0` (this file is the PN-0 deliverable).

## Context

Phases A–D (self-inverse, constant arithmetic, qint-qint arithmetic, qint comparisons) widened coverage to **integer-typed** quantum operations. Phase J added optimization. Phase M landed the long-term stretches (in-memory transpile, source maps, diagnostics, pluginization, peephole reorder). With PM5 green (2026-04-22), Phase M is complete and the transpiler covers the **XOR family** + **integer compound-assigns** + **qint-qint arithmetic / comparison** + **user routines** + **loop hoisting** + **peephole reorder**.

What Phase N adds: **P5 primitives 2 and 3**, i.e. `q.theta += d` / `q.theta -= d` and `q.phi += d` / `q.phi -= d`. These are the **continuous-parameter rotations** that, until Phase N, the transpiler passed through untouched because no matcher anchored on `ThetaProxy::operator+=` / `PhiProxy::operator+=`. User programs that use these rotations currently ship un-uncomputed adjoints: the runtime emits a forward `Ry(d)` / `Rz(d)` into the sink, and because no QOperation is created, the M8 uncompute pass sees nothing to invert.

Phase N also handles the diagnostic side of **P5 primitive 1** (`qbool(p)` preparation). Preparation **has no adjoint** — it is a CP map, not a unitary — so it must not appear inside a scope whose exit the transpiler will uncompute. PN-5 wires a dedicated matcher that detects `qbool x(p);` decls inside uncompute-eligible scopes (WHEN body, compound-expression intermediate) and emits a Warning-severity diagnostic through `DiagContext` (6th `report_*` method). This closes the P5-item-1 hole without introducing a new `QOpKind::PREP` whose adjoint would violate P9.

**User-locked decisions (from the bd issue and `docs/01_principles.md` review):**

1. **Multi-control rotations are out of scope.** Phase G AND-fold collapses nested WHEN chains to depth ≤ 1 before the rotation matcher ever sees them, so `register_{theta,phi}_{add,sub}_matcher` only ever needs to generate an uncontrolled inverse `q.theta -= d` or a depth-1 WHEN-guarded inverse `WHEN(c) { q.theta -= d; }`. Depth ≥ 2 is rejected by construction in `examples/rotations.cpp` (PN-7). A future Phase N.5 could lift this if demand emerges; it is not blocking for v1.

2. **`qbool(p)` behavior = diagnostic.** Rather than add a `QOpKind::PREP` op whose adjoint would have to be a measurement / discard primitive (breaking P9's "routines are invertible by explicit adjoint" contract), Phase N routes the case through the PM3 diagnostics surface. A Warning is emitted when `qbool x(p);` is detected inside an uncompute-eligible scope (inside a WHEN body, or inside a compound-expression intermediate whose VarDecl `unit.scopes[i].ops` will own). At top-level scope (function body directly), prep is silent — the caller is responsible for ensuring the prep's forward emission is not paired with a synthesized inverse. The fixture pair `qbool_prep_in_when.cpp` (must emit) / `qbool_prep_top_level_clean.cpp` (must NOT emit) pins the expected behavior.

Phase N is sized similarly to Phase B (constant compound-assigns): **two new AST anchors** (`ThetaProxy::operator+=` / `PhiProxy::operator+=`), **four new QOpKinds** (`THETA_ADD_ASSIGN_CONST`, `THETA_SUB_ASSIGN_CONST`, `PHI_ADD_ASSIGN_CONST`, `PHI_SUB_ASSIGN_CONST`), **one new matcher source** (`matcher_rotation.cpp`), **one new diagnostic matcher** (`matcher_qbool_prep.cpp`), **eight snapshot fixtures** (theta add/sub × const × qint, phi add/sub × const × qint), **one m12 pair** (`m12_rotations_{transpiled,reference}`), **two prep-diagnostic fixtures**, **one example** (`examples/rotations.cpp`). The epic ships as `sturm-z2a0` with sub-items PN-1..PN-9.

---

## 1. Scope

Phase N ships **four new QOpKinds + one rotation matcher + one prep-diagnostic matcher + one example**. Deliberate minimalism to match Phase B's shape.

**In scope:**
- `QOpKind::THETA_ADD_ASSIGN_CONST`, `QOpKind::THETA_SUB_ASSIGN_CONST`, `QOpKind::PHI_ADD_ASSIGN_CONST`, `QOpKind::PHI_SUB_ASSIGN_CONST` in `transpiler/include/sturm/transpile/qir.hpp` (PN-1), each carrying one operand whose `.name` is the verbatim RHS source text captured via `Lexer::getSourceText`, and whose `result` is the qint LHS.
- Extension of `dump()` switch in `transpiler/src/qir.cpp` with four new arms mirroring `ADD_ASSIGN_CONST`'s format.
- New matcher `transpiler/src/matcher_rotation.cpp` exporting `register_theta_add_matcher` / `register_theta_sub_matcher` / `register_phi_add_matcher` / `register_phi_sub_matcher` via `transpiler/include/sturm/transpile/matcher.hpp` (PN-2). Modeled on `matcher_qint_const.cpp`'s `QIntAssignConstCallback<Kind>` template, with an AST anchor one level deeper because `theta()` / `phi()` return proxy objects.
- Uncompute pass arms (PN-4) in `transpiler/src/uncompute_pass.cpp` for the four new kinds, each emitting the sign-flipped inline inverse (`q.theta()+=d` forward → `q.theta()-=d` inverse and symmetric for theta sub / phi add / phi sub). No `uncompute_api.hpp` free function is added; the runtime's `ThetaProxy::operator-=` / `PhiProxy::operator-=` are already self-dual at `include/sturm/qtypes/qint_core.hpp:305,372`.
- Snapshot fixtures (PN-3): eight files in `tests/transpiler/fixtures/` (theta_add_const, theta_sub_const, phi_add_const, phi_sub_const, each with `.cpp` input + `.expected.cpp` golden output). Wired into `tests/transpiler/CMakeLists.txt` via the existing `run_snapshot.cmake` + `check_idempotent.cmake` harness.
- Prep-diagnostic matcher `transpiler/src/matcher_qbool_prep.cpp` (PN-5), anchoring on `varDecl` with qbool type + `cxxConstructExpr` single-arg initializer. Guarded against classical-bool init (when arg-0 is a `CXXBoolLiteralExpr` or the expression's type is `isBooleanType()` → classical init, NOT probabilistic prep). Scope check walks outward from the VarDecl's enclosing scope checking whether any enclosing scope is a WHEN macro expansion or owns the VarDecl as a compound-expression intermediate. On hit, calls a new `diag.report_prep_in_uncompute_scope(loc, name)` — the 6th `report_*` method on `DiagContext`.
- `DiagContext` extension (PN-5 cohort): new method `report_prep_in_uncompute_scope(clang::SourceLocation loc, std::string_view name)`, Warning severity, format `"qbool %0 preparation in uncompute-eligible scope has no adjoint (P9)"`. Implementation mirrors the existing five PM3 `report_*` methods in `transpiler/src/diag_context.{hpp,cpp}`.
- Prep-diagnostic fixtures (PN-6): `tests/transpiler/fixtures/qbool_prep_in_when.cpp` (must emit warning) + `tests/transpiler/fixtures/qbool_prep_top_level_clean.cpp` (must NOT emit, identical input/output). New `tests/transpiler/check_qbool_prep_diagnostic.cmake` cloned from `check_outer_var_guard_diagnostic.cmake`. Two new CTests: `plugin_diagnostic_qbool_prep_fires` and `plugin_diagnostic_qbool_prep_clean`.
- Example `examples/rotations.cpp` (PN-7) exercising all four rotation directions plus one depth-1 WHEN-guarded rotation to pin B5 (depth ≥ 2 disallowed). Paired with `tests/transpiler/check_example_rotations.cmake` + `transpiler_example_rotations_injected` + `transpiler_idempotent_example_rotations` CTests.
- m12 gate-equivalence pair (PN-8): `tests/transpiler/fixtures/rotations_runtime.cpp` (transpile input) + `tests/transpiler/fixtures/rotations_reference.cpp` (hand-written reference with explicit inverses). Namespace pair `m12_rotations_transpiled` / `m12_rotations_reference` added to `tests/transpiler/test_gate_equivalence.cpp`. Asserts byte-identical counter-mode `GateRecord` streams (Ry, Rz, CRy, CRz).
- Roadmap update (PN-9): new `## Phase N — Rotation & preparation primitives` section after Phase M in `docs/roadmap_transpiler_post_mvp.md`, with a completion blockquote enumerating PN-1..PN-8 and citing file + test evidence. The existing B9 enumeration does **not** change — Phase N adds new matchers to the one global pass, but does not introduce a new optimization layer.

**Explicitly out of scope for Phase N:**
- **Multi-control rotations.** Depth ≥ 2 nested WHEN-guarded rotations. Phase G AND-fold already collapses nested WHEN chains to depth 1; any user program whose runtime control chain depth exceeds 1 triggers B5's "uncontrolled and singly-controlled forms only" invariant at runtime. Phase N does not add the multi-control matcher variant. A future Phase N.5 could add `CCRy(θ)` / `CCRz(θ)` if a hardware backend ever supports them.
- **`QOpKind::PREP`.** No new op kind for preparation. Preparation is a CP map whose adjoint would be a discard / measurement, which violates P9. PN-5's diagnostic path is the designed answer: raise the warning at compile time and let the caller restructure.
- **Runtime-valued rotation angles via qint.** `q.theta += other.theta()` where `other` is a `qint_t<W>` and we want to sum the angle from another rotation's parameter. This is a continuous-control form; no backend currently supports it as a primitive, and expressing it as a sequence requires value-range inference (same v2 target noted in PM5 §12 risk 1). Phase N only handles the bare `double` RHS shape, matched by `matcher_qint_const.cpp`'s `QIntAssignConstCallback<Kind>` template.
- **`theta()` / `phi()` inside BitProxy expressions.** `q[i].theta += d` where `q[i]` is a `BitProxy` does not compile under the current runtime (BitProxy is a qbool view, not a qint); any future extension would live under PN+follow-up.
- **`qbool(p)` preparation outside uncompute-eligible scopes** — silent by design. PN-5 only warns when the VarDecl is inside a WHEN body or compound-expression intermediate. Top-level function-body preparation is still a valid P5 use; the warning would produce too much noise.
- **Fine-grained prep-failure diagnostics** (e.g. distinguishing the two uncompute-eligible scope types). Phase N emits a single Warning; the matcher callback does **not** emit a subnote pinning which of "WHEN body" / "compound intermediate" triggered. A v2 diagnostic could extend with a subnote; v1 stays with the one-liner.

Out-of-scope items may graduate to follow-up phases. The v1 surface stays small: 4 QOpKinds, 1 matcher + 1 diagnostic matcher, 8 + 2 fixtures, 1 m12 pair, 1 example.

---

## 2. QIR additions

Four entries land in `transpiler/include/sturm/transpile/qir.hpp` after the Phase C block and before the Phase D comparison kinds, so the "qint compound-assign" family stays contiguous:

```cpp
// Phase N — amplitude / phase rotation compound-assigns. Each op carries one
// result QValueRef (the qint LHS that theta()/phi() dispatched from) plus
// one operand QValueRef whose .name is the verbatim RHS source text
// captured via Lexer::getSourceText (same PN pattern used by Phase B for
// integer compound-assigns; the RHS is a double-valued expression at the
// source level, treated opaquely by the matcher/emitter). Inverses emit
// inline in uncompute_pass.cpp (Phase B style, no free-function helper)
// because runtime ThetaProxy/PhiProxy operator-= already exists at
// include/sturm/qtypes/qint_core.hpp:305,372 and is self-dual.
//
// See docs/implementation_plan_transpiler_phase_n.md §2-4.
THETA_ADD_ASSIGN_CONST,
THETA_SUB_ASSIGN_CONST,
PHI_ADD_ASSIGN_CONST,
PHI_SUB_ASSIGN_CONST,
```

**Positioning.** After the Phase C `ADD_ASSIGN_QINT` / `SUB_ASSIGN_QINT` / `MUL_ASSIGN_QINT` / `DIV_ASSIGN_QINT` / `MOD_ASSIGN_QINT` block, before Phase D's `EQ_QINT`. This keeps the enum ordering consistent with the chronological phase order.

**Dump extension.** The `dump()` switch in `transpiler/src/qir.cpp` gets four new arms, each mirroring `ADD_ASSIGN_CONST`'s format — `<lhs>.theta() += <verbatim RHS>` with `+=` / `-=` / `.phi()` variations. The format is human-readable so `test_qir_dump.cpp` snapshots stay diagnosable.

**No new `QOpKind::PREP`.** PN-5 routes through `DiagContext`, not through the IR.

**Invariant preservation.** Every existing switch in the uncompute pass and emitter gets exactly four new arms (PN-4). There is no `default:` fallthrough — missing arms are a build failure by design (same contract as Phase B).

**Four golden cases.** `tests/transpiler/test_qir_dump.cpp` gains four new fixtures (one per new kind) that pin the exact `dump()` output.

---

## 3. Rotation matcher

New file `transpiler/src/matcher_rotation.cpp`. Modeled on `transpiler/src/matcher_qint_const.cpp`'s `QIntAssignConstCallback<Kind>` template. Key differences from Phase B:

1. **AST anchor is one level deeper.** `q.theta() += 0.5` desugars to `q.theta().operator+=(0.5)`, i.e. the operator-call's `.getArg(0)` is a `cxxMemberCallExpr` whose callee is a `cxxMethodDecl` named `"theta"` (or `"phi"`) that was dispatched from a `declRefExpr` of qint type. The matcher DSL reads:

```cpp
cxxOperatorCallExpr(
    hasOverloadedOperatorName("+="),  // or "-="
    argumentCountIs(2),
    hasArgument(0, cxxMemberCallExpr(
        on(declRefExpr(hasType(qint_guard())).bind("lhs")),
        callee(cxxMethodDecl(hasName("theta"))))),   // or "phi"
    hasArgument(1, expr().bind("rhs")))
    .bind("call")
```

The `qint_guard()` helper (defined in anon ns) restricts the DRE type to a `ClassTemplateSpecializationDecl` whose qualified name matches `sturm::qint_t`. This mirrors the guard pattern in `matcher_qint_const.cpp`.

2. **RHS extraction is unwrapped.** Phase B's matcher peels a `CXXConstructExpr` wrapper because the Phase B RHS is implicitly constructed as a `qint_t` via the converting constructor. Phase N's RHS is already a plain `double`-valued `Expr`; no peel is needed. The callback reads the RHS verbatim via `Lexer::getSourceText(call.getArg(1)->getSourceRange(), sm, lang)` (same helper Phase B uses).

3. **Four exported registrars.**

```cpp
void register_theta_add_matcher(MatchFinder& finder, QUnit& unit);  // THETA_ADD_ASSIGN_CONST
void register_theta_sub_matcher(MatchFinder& finder, QUnit& unit);  // THETA_SUB_ASSIGN_CONST
void register_phi_add_matcher  (MatchFinder& finder, QUnit& unit);  // PHI_ADD_ASSIGN_CONST
void register_phi_sub_matcher  (MatchFinder& finder, QUnit& unit);  // PHI_SUB_ASSIGN_CONST
```

All four are declared in `transpiler/include/sturm/transpile/matcher.hpp` alongside the existing Phase A/B/C register_* decls.

4. **Dogfood through the plugin registry.** Following the PM4-6 pattern established by `matcher_qint_const.cpp`'s `PBDogfoodPlugin`, Phase N's four registrars are bundled into a `PNRotationPlugin` class in an anonymous namespace at the bottom of `matcher_rotation.cpp` and announced via `STURM_REGISTER_PLUGIN(PNRotationPlugin);` at file scope. This exercises the PM4 Registry API and keeps the matcher list in `transpile_consumer.cpp` focused on phases that predate pluginization.

5. **Reused infrastructure.** `detail::enclosing_scope` + `detail::find_or_create_scope` + `detail::make_ref` (all in `matcher_common.hpp`) + `Lexer::getSourceText` (Clang API). No new helpers.

6. **LOC budget.** `matcher_qint_const.cpp` is ~280 lines; `matcher_rotation.cpp` is a hair smaller (no CXXConstructExpr peel), expected ~220 lines. Well under the 400-line cap per `CLAUDE.md`.

---

## 4. Uncompute render

Four new case arms in `transpiler/src/uncompute_pass.cpp`, landing after the `DIV_ASSIGN_CONST` arm (currently ~line 139). Each arm:

- Emits a single line `    <lhs_spelling>.theta() -= <rhs_text>;` for `THETA_ADD_ASSIGN_CONST`, and symmetrically `+=` for `THETA_SUB_ASSIGN_CONST`. Same for `.phi()` on the phi variants.
- Prefixes the emission with a `format_line_directive()` call (PM2-1) so the user sees source-file attribution when tooling walks the inverse.
- Uses the same indentation (`"    "`) and trailing-newline convention as the Phase B arms, so `check_idempotent.cmake` byte-comparisons pass.

Pattern-match for the four arms (abridged):

```cpp
case QOpKind::THETA_ADD_ASSIGN_CONST: {
    os << format_line_directive(op.stmt_range.getBegin(), sm);
    os << "    " << op.result.name << ".theta() -= " << op.operands[0].name << ";\n";
    break;
}
case QOpKind::THETA_SUB_ASSIGN_CONST: {
    os << format_line_directive(op.stmt_range.getBegin(), sm);
    os << "    " << op.result.name << ".theta() += " << op.operands[0].name << ";\n";
    break;
}
case QOpKind::PHI_ADD_ASSIGN_CONST: {
    os << format_line_directive(op.stmt_range.getBegin(), sm);
    os << "    " << op.result.name << ".phi() -= " << op.operands[0].name << ";\n";
    break;
}
case QOpKind::PHI_SUB_ASSIGN_CONST: {
    os << format_line_directive(op.stmt_range.getBegin(), sm);
    os << "    " << op.result.name << ".phi() += " << op.operands[0].name << ";\n";
    break;
}
```

**Why inline, no free-function helper.** The two-character sign flip (`+=` ↔ `-=`) is the full inverse — there is no non-trivial arithmetic to encapsulate. A free function `uncompute_theta_add(qint_t& q, double d)` would be a one-line wrapper around `q.theta() -= d` that the transpiler would have to declare in `uncompute_api.hpp`; that adds header surface for zero payoff. The Phase B precedent (inline constant compound-assigns) established this pattern, and Phase N inherits it.

**Why `ThetaProxy::operator-=` / `PhiProxy::operator-=` are sufficient.** The runtime already ships these at `include/sturm/qtypes/qint_core.hpp:305` (`ThetaProxy::operator-=(double delta) { operator+=(-delta); }`) and `include/sturm/qtypes/qint_core.hpp:372` (same for `PhiProxy`). They dispatch `-delta` into the existing `Ry(θ)` / `Rz(θ)` emission path, so the counter-mode GateRecord sign agreement is automatic.

**No `uncompute_api.hpp` additions.** The API header stays as-is. PN-4 touches only `uncompute_pass.cpp`.

---

## 5. qbool(p) prep diagnostic matcher

New file `transpiler/src/matcher_qbool_prep.cpp`. The closest cousin is `transpiler/src/matcher_outer_var_guard.cpp` (PM3-2 pathway) — both walk the parent chain to discover a scope property, both emit a `DiagContext` call on match, neither writes into `unit.scopes.ops`.

**AST anchor.**

```cpp
varDecl(
    hasType(qbool_guard()),
    hasInitializer(cxxConstructExpr(
        argumentCountIs(1),
        hasArgument(0, expr().bind("init_arg")))))
    .bind("prep_decl")
```

The `qbool_guard()` helper (defined in anon ns, mirroring the pattern in `matcher_qint_const.cpp`) restricts to `RecordDecl`s whose qualified name matches `sturm::qbool`.

**Classical-init guard.** Skip when the `init_arg` is a `CXXBoolLiteralExpr` (e.g. `qbool x(true);` is classical init — no preparation) or when the init expression's static type satisfies `p->getType()->isBooleanType()` (same — classical bool passed through the converting constructor). Under either guard, the callback early-returns silently.

**Scope check.** Walk outward from the VarDecl via `detail::enclosing_scope`:

1. Classify the immediately enclosing scope via `detail::classify_scope_kind`. If it is `Function` (top-level function body), early-return silently — top-level preparation is valid and outside the uncompute-eligible window.
2. Otherwise, walk the parent chain looking for two signals:
   - **WHEN macro expansion.** Any enclosing `IfStmt` whose begin loc is inside a `WHEN` macro expansion (via `detail::is_expansion_of_macro(loc, sm, lang, "WHEN")`).
   - **Compound-expression intermediate.** The VarDecl appears in a scope whose `unit.scopes[i].ops` owns an op that lists this VarDecl as a result — i.e. the Phase E compound-expression matcher has already claimed this decl as a synthesized intermediate. (In PN-5's concrete implementation the check is linear over `unit.scopes[i].ops`; scope counts are small, so no indexing is needed.)
3. If either signal fires, call `diag.report_prep_in_uncompute_scope(loc, name)` with the VarDecl's file loc and identifier spelling. Silent otherwise.

**DiagContext extension.** The 6th `report_*` method:

```cpp
/// PN-5: qbool(p) preparation inside an uncompute-eligible scope. Warning
/// severity. Format: "qbool %0 preparation in uncompute-eligible scope has
/// no adjoint (P9)". Fires when a `qbool x(p);` VarDecl whose init is a
/// probabilistic `double` (not a `bool` literal / `bool`-typed expression)
/// appears inside a WHEN body or a compound-expression intermediate scope.
void report_prep_in_uncompute_scope(clang::SourceLocation loc,
                                    std::string_view name);
```

Declared in `transpiler/src/diag_context.hpp` after `report_outer_var_mutation`. Implementation in `transpiler/src/diag_context.cpp` mirrors the existing five methods — `getOrRegister(level=Warning, fmt="qbool %0 preparation in uncompute-eligible scope has no adjoint (P9)")` then `diag_.Report(loc, id) << name`.

**Registration order.** In `transpile_consumer.cpp`, `register_qbool_prep_matcher` goes **AFTER** `register_outer_var_guard_matcher` (currently line 289). This ordering matches the PM3-2 pattern — `outer_var_guard` flags PH-3 cases; `qbool_prep` picks up the orthogonal P5-1 case. Neither depends on the other's state; registering them in a contiguous block keeps the "quantum-specific diagnostic matchers" in one visual block in the consumer source.

**Silent top-level.** Prep at function-body top-level scope is valid P5; no diagnostic is emitted and no IR op is produced. A future Phase N.5 could add a `QOpKind::PREP` with a no-op inverse for completeness, but v1 punts.

---

## 6. Fixtures

### 6.1 Rotation snapshot fixtures (PN-3)

Eight files land in `tests/transpiler/fixtures/`:

| Input | Expected | Kind |
|---|---|---|
| `theta_add_const.cpp` | `theta_add_const.expected.cpp` | `THETA_ADD_ASSIGN_CONST` forward → `q.theta() -= d;` inverse |
| `theta_sub_const.cpp` | `theta_sub_const.expected.cpp` | `THETA_SUB_ASSIGN_CONST` forward → `q.theta() += d;` inverse |
| `phi_add_const.cpp`   | `phi_add_const.expected.cpp`   | `PHI_ADD_ASSIGN_CONST` forward → `q.phi() -= d;` inverse |
| `phi_sub_const.cpp`   | `phi_sub_const.expected.cpp`   | `PHI_SUB_ASSIGN_CONST` forward → `q.phi() += d;` inverse |

Each input fixture is a minimal self-contained C++ file (matching the Phase B `add_assign_const.cpp` structure) that:

- Declares a namespace `sturm` with a template `qint_t<W>` carrying nested `ThetaProxy` / `PhiProxy` types, each with `operator+=(double)` / `operator-=(double)` (self-dual in the proxy's implementation).
- Defines a single `void demo(qint q) { q.theta() += 0.5; }` function that the transpiler rewrites.

Each `.expected.cpp` pins the expected post-transpile output byte-for-byte, including the injected `#line` directive from `format_line_directive()`.

**CTest wiring.** `tests/transpiler/CMakeLists.txt` gains four `run_snapshot.cmake` invocations (`transpiler_snapshot_theta_add_const`, `transpiler_snapshot_theta_sub_const`, `transpiler_snapshot_phi_add_const`, `transpiler_snapshot_phi_sub_const`) + four `check_idempotent.cmake` invocations that verify re-transpiling the `.expected.cpp` yields the same bytes. Pattern: `ctest --parallel 6 -R 'transpiler_snapshot_(theta|phi)_(add|sub)_const'`.

### 6.2 Prep-diagnostic fixtures (PN-6)

Two files in `tests/transpiler/fixtures/`:

- `qbool_prep_in_when.cpp` — must emit the PN-5 warning. Contains a `WHEN(c) { qbool x(0.5); x ^= y; }` block; the `qbool x(0.5)` is the probabilistic prep inside the WHEN body.
- `qbool_prep_top_level_clean.cpp` — must NOT emit. Contains `void demo() { qbool x(0.5); /* use x */ }` at function-body top-level. Input equals output (no diagnostic, no rewrite).

New `tests/transpiler/check_qbool_prep_diagnostic.cmake` cloned from `check_outer_var_guard_diagnostic.cmake`. Two new CTests:
- `plugin_diagnostic_qbool_prep_fires` — verifies stderr contains the warning text for `qbool_prep_in_when.cpp`.
- `plugin_diagnostic_qbool_prep_clean` — verifies stderr is warning-free for `qbool_prep_top_level_clean.cpp`.

Pattern: `ctest --parallel 6 -R 'plugin_diagnostic_qbool_prep'`.

---

## 7. m12 gate-equivalence pair (PN-8)

Two files in `tests/transpiler/fixtures/`:

- `rotations_runtime.cpp` — the transpile input. Contains a function that mixes all four rotation kinds (theta add, theta sub, phi add, phi sub) plus one depth-1 WHEN-guarded rotation. The transpiler rewrites this with injected inverses.
- `rotations_reference.cpp` — a hand-written reference emitting the same logical sequence with explicit inverses **in place** (byte-identical to what the transpiler will produce for `rotations_runtime.cpp` after transpile, modulo `#line` directives which the test harness strips before compare).

The existing multi-namespace `tests/transpiler/test_gate_equivalence.cpp` gains a new namespace pair:

```cpp
namespace m12_rotations_transpiled {
#include "rotations_runtime.expected.cpp"  // the transpiled output
}
namespace m12_rotations_reference {
#include "rotations_reference.cpp"         // hand-written equivalent
}

TEST(GateEquivalence, Rotations) {
    auto transpiled = record_gates([](){ m12_rotations_transpiled::demo(); });
    auto reference  = record_gates([](){ m12_rotations_reference::demo(); });
    EXPECT_EQ(transpiled, reference);  // byte-identical GateRecord streams
}
```

**Expected gate stream.** The counter-mode sink records Ry, Rz, CRy, CRz primitives. Sign agreement follows from the runtime's `ThetaProxy::operator-=(double d) { operator+=(-d); }` — inverse emission produces `Ry(-d)` / `Rz(-d)` against the forward `Ry(d)` / `Rz(d)`, cancelling to the identity as expected.

**Pattern.** `ctest --parallel 6 -R 'gate_equivalence.*rotations'`.

---

## 8. Example + idempotency (PN-7)

`examples/rotations.cpp` — dogfood example exercising all four rotation directions plus one depth-1 WHEN-guarded rotation:

```cpp
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"

using qint = sturm::qint_t<4>;

void rotations_demo(qint& a, qint& b, sturm::qbool c) {
    a.theta() += 0.3;       // THETA_ADD_ASSIGN_CONST
    a.theta() -= 0.1;       // THETA_SUB_ASSIGN_CONST
    b.phi()   += 0.7;       // PHI_ADD_ASSIGN_CONST
    b.phi()   -= 0.2;       // PHI_SUB_ASSIGN_CONST

    WHEN(c) {
        a.theta() += 0.5;   // depth-1 controlled rotation — OK per B5
    }
    // Depth ≥ 2 is disallowed: a nested WHEN inside another WHEN holding
    // a rotation would violate B5 (Phase G AND-fold produces depth-1 by
    // construction). This example pins the valid depth-1 shape; adding
    // a nested WHEN here is a test the example is wrong.
}
```

New `tests/transpiler/check_example_rotations.cmake` cloned from `check_example_peephole_reorder.cmake`. Edits:
- `examples/CMakeLists.txt` — build `rotations` as a quantum executable (reuses `add_quantum_executable`).
- `tests/transpiler/CMakeLists.txt` — add `transpiler_example_rotations_injected` + `transpiler_idempotent_example_rotations` CTests.

Pattern: `ctest --parallel 6 -R 'transpiler_example_rotations'`.

**Idempotency.** Re-transpiling the already-transpiled output is a no-op. The `check_example_rotations.cmake` harness transpiles twice and diffs; byte-identical means idempotent.

---

## 9. Sub-tasks (mirrors bd children under `sturm-z2a0`)

| bd ID | Title | Depends | Parallelizable with |
|---|---|---|---|
| PN-0 (`sturm-z2a0`) | **This plan doc** | — | — |
| PN-1 (`sturm-f8jt`) | QIR additions: four new QOpKinds + `dump()` arms + four `test_qir_dump.cpp` golden cases | PN-0 | PN-5 |
| PN-2 (`sturm-f5cz`) | Rotation matcher: `matcher_rotation.cpp` + four `register_*_matcher` decls in `matcher.hpp` + `STURM_REGISTER_PLUGIN(PNRotationPlugin)` | PN-1 | PN-5 |
| PN-3 (`sturm-ft5t`) | Rotation snapshot fixtures: 8 files in `tests/transpiler/fixtures/` + CMakeLists wiring | PN-2, PN-4 | PN-5, PN-6 |
| PN-4 (`sturm-9c2e`) | Uncompute pass dispatch: four new arms in `uncompute_pass.cpp` after `DIV_ASSIGN_CONST` | PN-1 | PN-2, PN-5 |
| PN-5 (`sturm-8h3r`) | qbool(p) prep diagnostic matcher: `matcher_qbool_prep.cpp` + `report_prep_in_uncompute_scope` extension to `DiagContext` + register in `transpile_consumer.cpp` after PH-3 | PN-0 | PN-1, PN-2, PN-4 |
| PN-6 (`sturm-k08h`) | Prep diagnostic fixtures: 2 files + `check_qbool_prep_diagnostic.cmake` + 2 new CTests | PN-5 | PN-3 |
| PN-7 (`sturm-uw33`) | Example: `examples/rotations.cpp` + `check_example_rotations.cmake` + 2 new CTests (injected + idempotent) | PN-3, PN-6 | — |
| PN-8 (`sturm-dy1f`) | m12 gate-equivalence pair: `rotations_runtime.cpp` + `rotations_reference.cpp` + namespace pair in `test_gate_equivalence.cpp` | PN-7 | — |
| PN-9 (`sturm-qy12`) | Roadmap update: new "## Phase N" section + completion blockquote + line evidence | PN-8 | — |

**Parallel pairs** (safe to spawn two bd-workers concurrently): PN-1 ∥ PN-5, PN-2 ∥ PN-5, PN-3 ∥ PN-6.

**Critical path.** PN-0 → PN-1 → PN-2 → PN-3 → PN-7 → PN-8 → PN-9. PN-4 fits alongside PN-2 (both depend only on PN-1). PN-5 runs in parallel with the full rotation chain once PN-0 lands. PN-6 runs immediately after PN-5.

---

## 10. Dependency graph

```
PN-0 ──┬──> PN-1 ──┬──> PN-2 ──┐
       │           │           │
       │           └──> PN-4 ──┼──> PN-3 ──┐
       │                       │           │
       │                       └───────────┼──> PN-7 ──> PN-8 ──> PN-9
       │                                   │
       └──> PN-5 ──> PN-6 ─────────────────┘
```

PN-5 + PN-6 (the prep diagnostic subsystem) is a sibling sub-tree that joins the critical path at PN-7 via the example's depth-1 WHEN-guarded rotation — the example exercises both the rotation emission path (PN-2..4) and, implicitly, the absence of a prep diagnostic on a WHEN body (PN-5..6 pin the positive/negative cases separately).

---

## 11. Critical files

### New
- `transpiler/src/matcher_rotation.cpp` — the rotation matcher (PN-2). Expected ~220 LOC.
- `transpiler/src/matcher_qbool_prep.cpp` — the prep diagnostic matcher (PN-5). Expected ~180 LOC.
- `tests/transpiler/fixtures/theta_add_const.cpp` + `.expected.cpp` — rotation snapshot fixture (PN-3).
- `tests/transpiler/fixtures/theta_sub_const.cpp` + `.expected.cpp` — rotation snapshot fixture (PN-3).
- `tests/transpiler/fixtures/phi_add_const.cpp` + `.expected.cpp` — rotation snapshot fixture (PN-3).
- `tests/transpiler/fixtures/phi_sub_const.cpp` + `.expected.cpp` — rotation snapshot fixture (PN-3).
- `tests/transpiler/fixtures/qbool_prep_in_when.cpp` — prep-diagnostic fixture, must emit (PN-6).
- `tests/transpiler/fixtures/qbool_prep_top_level_clean.cpp` — prep-diagnostic fixture, must not emit (PN-6).
- `tests/transpiler/fixtures/rotations_runtime.cpp` — m12 transpile input (PN-8).
- `tests/transpiler/fixtures/rotations_reference.cpp` — m12 reference (PN-8).
- `tests/transpiler/check_qbool_prep_diagnostic.cmake` — CTest harness for PN-6.
- `tests/transpiler/check_example_rotations.cmake` — CTest harness for PN-7.
- `examples/rotations.cpp` — dogfood example (PN-7).

### Modified
- `transpiler/include/sturm/transpile/qir.hpp` — four new `QOpKind` entries (PN-1).
- `transpiler/src/qir.cpp` — four new `dump()` switch arms (PN-1).
- `transpiler/include/sturm/transpile/matcher.hpp` — four new `register_*_matcher` decls (PN-2).
- `transpiler/src/transpile_consumer.cpp` — register `register_qbool_prep_matcher` AFTER `register_outer_var_guard_matcher` (PN-5). Rotation matchers are dogfooded through `STURM_REGISTER_PLUGIN` and do NOT need a consumer edit (PM4-6 pattern).
- `transpiler/src/uncompute_pass.cpp` — four new arms after `DIV_ASSIGN_CONST` (PN-4).
- `transpiler/src/diag_context.hpp` — `report_prep_in_uncompute_scope` decl (PN-5).
- `transpiler/src/diag_context.cpp` — `report_prep_in_uncompute_scope` impl (PN-5).
- `transpiler/tests/test_qir_dump.cpp` — four new golden cases (PN-1).
- `tests/transpiler/test_gate_equivalence.cpp` — new `m12_rotations_transpiled` / `m12_rotations_reference` namespace pair (PN-8).
- `tests/transpiler/CMakeLists.txt` — snapshot + idempotent + diagnostic + example CTest wiring (PN-3, PN-6, PN-7).
- `examples/CMakeLists.txt` — build `rotations` executable (PN-7).
- `transpiler/CMakeLists.txt` — add `matcher_rotation.cpp` + `matcher_qbool_prep.cpp` to both `sturm-transpile` and `sturm-transpile-plugin` targets (PM4-6 dogfood pattern).
- `docs/roadmap_transpiler_post_mvp.md` — new `## Phase N` section + completion blockquote (PN-9).

### Reused (do not reinvent)
- `QValueRef` + `QOperation` + `QUnit` + `QScope` in `transpiler/include/sturm/transpile/qir.hpp`. No new field on any of these.
- `detail::enclosing_scope` + `detail::find_or_create_scope` + `detail::make_ref` + `detail::classify_scope_kind` + `detail::is_expansion_of_macro` — all in `transpiler/src/matcher_common.hpp`.
- `Lexer::getSourceText` — the Clang API used by every phase to capture verbatim RHS text.
- `format_line_directive()` in `transpiler/src/emitter.cpp` (PM2-1) — for `#line` attribution on injected inverses.
- `DiagContext::getOrRegister` in `transpiler/src/diag_context.cpp` — the lazy-ID cache that PM3 established; PN-5 reuses verbatim.
- `QIntAssignConstCallback<Kind>` template pattern — visible in `matcher_qint_const.cpp`. PN-2 adapts it with a deeper anchor (no re-use of the template itself because the anchor shape differs).
- `run_snapshot.cmake` + `check_idempotent.cmake` — CTest harness files in `tests/transpiler/`. PN-3 wires in, adds no new harness.
- `ThetaProxy::operator-=` / `PhiProxy::operator-=` in `include/sturm/qtypes/qint_core.hpp:305,372` — the runtime's self-dual rotation operators that make inline `+=`/`-=` flipping sufficient.

### Not touched
- `include/sturm/uncompute/uncompute_api.hpp` — no new free-function helpers. The inline sign-flip in `uncompute_pass.cpp` is complete.
- The plugin ABI (`plugin_api.hpp`). Phase N's plugin is internal (the PNRotationPlugin dogfood).
- The `QOpKind::PLUGIN` dispatch (PM4-3). Phase N's new kinds are first-class.
- The runtime (except that we rely on existing ThetaProxy / PhiProxy operators; no edits).
- Peephole reorder (PM5) — rotations are not in the PJ-1 fuse family, so the reorder matcher's Gate 1 never matches a rotation triple. Phase N's ops pass through PM5 untouched.

---

## 12. Verification

**Build-command hard limit.** Every `cmake`, `cmake --build`, `ctest`, `make`, `ninja` invocation MUST cap at 6 threads — `--parallel 6` / `-j6` / `CTEST_PARALLEL_LEVEL=6`. This is a project-wide rule (`CLAUDE.md`).

### Per-bd-item (inside bd-worker)

```bash
cmake --build build --parallel 6 --target <target>
CTEST_PARALLEL_LEVEL=6 ctest --test-dir build -R '<item-regex>' --output-on-failure
```

### End-to-end (after PN-9 closes)

```bash
cmake -S . -B build
cmake --build build --parallel 6
CTEST_PARALLEL_LEVEL=6 ctest --test-dir build --output-on-failure
```

**Expected.** All existing tests pass byte-identical (confirming Phase N's new matchers fire only on rotation shapes and do not perturb any prior snapshot), plus the new fixtures pass:

- **QIR dump (PN-1).** Four new `test_qir_dump` golden cases assert the `dump()` output for each new `QOpKind`.

- **Snapshot fixtures (PN-3).** Four new `transpiler_snapshot_{theta,phi}_{add,sub}_const` CTests + four idempotency variants. Pattern: `ctest --parallel 6 -R 'transpiler_snapshot_(theta|phi)_(add|sub)_const'`.

- **Prep diagnostic (PN-6).** Two new CTests: `plugin_diagnostic_qbool_prep_fires` (positive — stderr contains the warning) and `plugin_diagnostic_qbool_prep_clean` (negative — stderr warning-free).

- **Example (PN-7).** `transpiler_example_rotations_injected` (transpile produces the expected output) + `transpiler_idempotent_example_rotations` (re-transpiling is a no-op).

- **m12 gate equivalence (PN-8).** `GateEquivalence.Rotations` asserts byte-identical counter-mode `GateRecord` streams between the transpiled output and the hand-written reference. Pattern: `ctest --parallel 6 -R 'gate_equivalence.*rotations'`.

### Principle check (before PN-9 closes)

Reread `docs/01_principles.md`. The Phase N extension surface does not touch any B-principle or P-principle materially:

- **P5** (item 1 `qbool(p)`, item 2 `q.theta += d`, item 3 `q.phi += d`) — **enforced**. Items 2 and 3 now have first-class transpiler support with explicit adjoint emission. Item 1 gains a compile-time diagnostic when used inside an uncompute-eligible scope, preventing silent P9 violations.
- **P9** (Routines invertible by explicit adjoint) — **untouched**. Rotations follow the dual-operator rule (`+=` ↔ `-=`). Prep emits a diagnostic rather than a synthesized inverse, preserving P9's "explicit adjoint" contract.
- **B5** (uncontrolled + singly-controlled primitives only) — **untouched**. Multi-control rotations are out of scope; the example pins depth-1 by construction.
- **B9** (one global optimization pass at transpile time) — **untouched**. Phase N adds matchers to the one global pass; the pass count stays at one. No enumeration-line edit is needed (B9 enumerates PJ-1 / PJ-3 / PJ-4 / PM5 as the **optimization** passes; Phase N matchers are **coverage** additions, not optimizations).
- **B10** (Uncomputation is compile-time) — **enforced**. Phase N's inverses are emitted by the transpiler's uncompute pass, not by runtime destructors.

If an unexpected principle revision emerges during implementation, file it as a sub-bullet under PN-9. Otherwise no `01_principles.md` edit is needed.

### Session close (mandatory per `CLAUDE.md`)

```bash
bd close <PN-epic-id>
git pull --rebase
bd dolt push || true   # local-only; no remote configured for bd
git push
git status  # MUST report "up to date with origin/main"
```

---

## 13. Principle check (summary for § 12 cross-reference)

| Principle | Status | Reasoning |
|---|---|---|
| **P5** items 2 & 3 (theta / phi rotations) | **Enforced** | First-class transpiler support; adjoint emitted inline via uncompute_pass. |
| **P5** item 1 (`qbool(p)` prep) | **Diagnosed** | Compile-time Warning fires inside uncompute-eligible scope; silent at top level. |
| **P9** (explicit adjoint) | Untouched | Dual operator rule (`+=` ↔ `-=`) already in the runtime proxy; transpiler emits the sign-flipped inverse. |
| **B5** (uncontrolled + singly-controlled only) | Untouched | Depth-1 by construction via Phase G AND-fold; example pins the invariant. |
| **B9** (one global optimization pass) | Untouched | Phase N adds coverage matchers, not optimization passes; B9 enumeration unchanged. |
| **B10** (Uncomputation is compile-time) | Enforced | Inverses emitted by the transpiler's uncompute pass, never by destructors. |

---

## 14. Sharp edges / risks

Six risks called out for Phase N reviewers. Each is pinned by a test or a documented decision.

1. **Multi-control depth ≥ 2 rejection.** If a user writes `WHEN(c1) WHEN(c2) { a.theta() += 0.5; }`, Phase G AND-fold collapses `c1 & c2` to a single ancilla at the inner WHEN's entry, so the rotation matcher sees a depth-1 guard. **If** a future Phase G regression re-exposes depth-2 control, the rotation matcher would emit a CCRy — which is not in the B4 eighteen-gate set. **Mitigation:** PN-7's example constructs only depth-1 WHEN-guarded rotations. The example + its idempotency CTest are the regression tripwire; a depth-2 user program today would fail at runtime dispatch rather than at transpile time. A v2 could add an explicit transpile-time check ("if your rotation is inside nested WHENs and Phase G produced depth ≥ 2, hard-error"); v1 relies on Phase G's invariant.

2. **Runtime-valued rotation angle.** `q.theta() += other_rotation_angle` where the RHS is a `double`-valued variable rather than a compile-time literal. `Lexer::getSourceText` captures the verbatim token `"other_rotation_angle"`; the inverse emits `q.theta() -= other_rotation_angle`, which compiles and runs. **This works for any pure-value RHS.** The risk surface is side-effecting RHS — e.g. `q.theta() += compute_d_with_side_effect()` — where the inverse would re-invoke the side effect. **Mitigation:** Phase B has the same concern for `a += compute_value()` and has landed without explicit mitigation. Phase N inherits the policy: if the RHS has side effects, the user is responsible for hoisting to a local. No matcher-level enforcement.

3. **`qbool(p)` with `bool`-typed variable.** `qbool x(some_bool_var);` is classical init — the guard at PN-5 catches `CXXBoolLiteralExpr` AND any `isBooleanType()` expression, so this silent-passes. The risk is an intermediate `bool` that was actually a probabilistic value cast to `bool` — e.g. `bool p_as_bool = rnd() > 0.5; qbool x(p_as_bool);`. The matcher treats this as classical. **Mitigation:** the cast to `bool` is a conscious user action; the matcher's conservative behavior (silence on all `bool`-typed inputs) is the designed policy. If a user wants the diagnostic, they should write `qbool x(0.5);` directly.

4. **`qbool(0.0)` or `qbool(1.0)`.** These are **probabilistically** classical — the double RHS is literal, but the Bernoulli variable it parametrizes has zero variance. `CXXBoolLiteralExpr` does not match (it's a `FloatingLiteral`, not a `CXXBoolLiteralExpr`), and `isBooleanType()` is false. **The diagnostic fires** on both — same warning text. **Mitigation:** this is the designed behavior — `qbool(0.0)` is still a prep operation from the runtime's POV, and it has no adjoint. If a user wants a classical false `qbool`, they should write `qbool x(false);`. PN-6's `qbool_prep_in_when.cpp` fixture uses `0.5` to pin the warning; a future Phase N.5 could add value-range inference to classify `0.0` / `1.0` as classical and suppress the warning.

5. **Theta + phi interaction.** A user program that does `q.theta() += 0.3; q.phi() += 0.4;` emits two independent QOperations. The uncompute pass emits the inverses in LIFO order (`q.phi() -= 0.4;` then `q.theta() -= 0.3;`), which is the correct adjoint order under the product decomposition of `Ry ∘ Rz`. **Verified by:** the m12 pair (PN-8) — if the LIFO order were wrong, the GateRecord stream would not byte-compare against the hand-written reference.

6. **Rotation inside WHEN body, depth 1.** Forward: the WHEN guard produces a controlled `CRy(0.5)`; the uncompute pass emits the controlled `CRy(-0.5)` inside the same WHEN body, which again AND-folds into the enclosing control chain (depth 1). **Verified by:** `examples/rotations.cpp` (PN-7) explicitly constructs this case; the idempotency CTest transpiles twice and diffs. The m12 pair (PN-8) adds gate-record equivalence on top.

---

## 15. Interaction with Phases A..M

For reviewers loading this doc in a future session, a compact cross-reference:

- **Phase A** (self-inverse XOR family). Orthogonal. Phase N's anchors are `operator+=` / `operator-=`; Phase A's are `operator^=`. No matcher collides.
- **Phase B** (constant compound-assigns `+=`/`-=`/`*=`/`/=` on qint). Closest ancestor. Phase N reuses the verbatim-RHS capture pattern but anchors one level deeper (on `theta()`/`phi()` member calls). Phase B's `matcher_qint_const.cpp` is the template.
- **Phase C** (qint-qint arithmetic). Orthogonal. Phase N's RHS is `double`; Phase C's is `qint_t`. No anchor overlap.
- **Phase D** (comparisons). Orthogonal. No anchor overlap.
- **Phase E** (compound expressions). Interaction: if a user writes `qbool r = (q.theta() += 0.3) & d;`, the expression would mix a rotation with a bitwise op — which does not compile under the current runtime because `theta()` returns `void`. Phase N does not extend the compound-expression matcher; this remains a compile-time error.
- **Phase F** (WHEN scope). Phase N respects WHEN: a rotation inside a WHEN body is lifted and anchored the same way as an XOR. The uncompute pass emits the controlled inverse inside the same WHEN body. PN-7's example pins this.
- **Phase G** (nested WHEN AND-fold). Foundational. Phase N relies on depth ≤ 1 post-AND-fold (see §14 risk 1).
- **Phase H** (loops and classical control). PH-3's `outer_var_guard` matcher registered at `transpile_consumer.cpp:289`; PN-5's prep matcher registers **after** PH-3 in the same block. No ordering dependency between the two — they flag orthogonal error classes.
- **Phase I** (user routines). Orthogonal. A user routine that does rotations internally gets its adjoint registered via the PI-2 `STURM_INVERTS(fn)` macro; the routine's body is not re-analyzed.
- **Phase J** (optimizations). PJ-1 (zero-ancilla fusion), PJ-3 (uncompute hoisting), PJ-4 (dead-ancilla elimination) — none match rotation shapes. Phase N's ops pass through unchanged.
- **Phase K** (cleanup / runtime uncompute retirement). No interaction.
- **Phase L** (distribution). No interaction.
- **Phase M**:
  - **PM1** (in-memory transpile). Phase N's new matchers run inside the same `TranspileConsumer`; in-memory transpile of a program containing rotations produces the same emitted source as the sibling-file path.
  - **PM2** (source maps). Every rotation inverse carries a `#line` directive via `format_line_directive()` — the user's compile errors on the inverse point back at the original forward rotation's line.
  - **PM3** (diagnostics). PN-5 adds a 6th diagnostic class. The `DiagContext` surface grows from 5 to 6 `report_*` methods; the `standalone_diag_surfaces` CTest gains one line.
  - **PM4** (pluginization). PN-2's rotation matcher uses the PM4-6 dogfood pattern (`STURM_REGISTER_PLUGIN(PNRotationPlugin)`). No consumer-side edit for the four rotation matchers.
  - **PM5** (peephole reorder). Orthogonal. Rotation ops are not in the PJ-1 fuse family, so PM5's triple matcher never anchors on them. PM5's footprint extractor treats rotation operands the same as any other qint DRE (bit_range = `{0, W}`).

---

## 16. Forward compatibility

Two v2 concerns are documented here rather than filed as separate issues; they would graduate to epics if demand emerges:

- **v2 multi-control rotations.** Phase G AND-fold collapses to depth 1 today, but a hardware backend that supports native `CCRy` / `CCRz` (beyond the B4 eighteen-gate set) would motivate a Phase N.5 that widens the rotation matcher to accept depth-2 guards. The scoping is small — one new op kind per direction + one new uncompute-pass arm — and the risk surface is B5 (principle revision).

- **v2 qbool(p) classification.** Value-range inference to suppress the diagnostic on `qbool(0.0)` / `qbool(1.0)` (which are probabilistically classical). This is the same v2 target PM5 §12 risk 1 called out; shipping it as one epic that also suppresses PM5's BitProxy-nonconst pessimism would consolidate the value-range work.

Neither v2 concern is blocking for Phase N v1. The v1 surface is conservatively safe (warns rather than miscompiles) and the test matrix stays finite.

---

## 17. Definition of done

Phase N is done when all nine sub-tasks close green, specifically:

- [ ] PN-0 shipped: this file exists and is ≥ 400 lines.
- [ ] PN-1 shipped: four new `QOpKind`s in `qir.hpp`; `dump()` arms extended; four `test_qir_dump` golden cases pass.
- [ ] PN-2 shipped: `matcher_rotation.cpp` compiles; four `register_*_matcher` registrars exported; `PNRotationPlugin` announces via `STURM_REGISTER_PLUGIN`.
- [ ] PN-3 shipped: eight snapshot fixtures + four `transpiler_snapshot_{theta,phi}_{add,sub}_const` CTests + four idempotent CTests pass.
- [ ] PN-4 shipped: four new arms in `uncompute_pass.cpp` emit the sign-flipped inline inverse with `#line` directives.
- [ ] PN-5 shipped: `matcher_qbool_prep.cpp` compiles; `report_prep_in_uncompute_scope` method added to `DiagContext`; registered after `outer_var_guard` in `transpile_consumer.cpp`.
- [ ] PN-6 shipped: two prep-diagnostic fixtures + `check_qbool_prep_diagnostic.cmake` + two CTests (`plugin_diagnostic_qbool_prep_fires`, `plugin_diagnostic_qbool_prep_clean`) pass.
- [ ] PN-7 shipped: `examples/rotations.cpp` transpiles cleanly; `transpiler_example_rotations_injected` + `transpiler_idempotent_example_rotations` CTests pass.
- [ ] PN-8 shipped: m12 fixtures + `GateEquivalence.Rotations` (or `gate_equivalence.*rotations`) CTest passes — byte-identical counter-mode `GateRecord` streams.
- [ ] PN-9 shipped: `docs/roadmap_transpiler_post_mvp.md` has a new `## Phase N` section with a completion blockquote enumerating PN-1..PN-8 and their file + test evidence.

Each sub-task has a single bd id; bd-worker drains them in the dependency-graph order (§10). A future `bd prime` session begins with `bd ready` and picks PN-1 on the first call.
