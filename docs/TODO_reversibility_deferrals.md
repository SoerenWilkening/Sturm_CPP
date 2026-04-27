# TODO — Reversibility Deferrals (PRD §9)

**Status:** Backlog. No implementation planned. File child bd issues when a concrete use case motivates tackling any of these five.

**Source:** Carried forward from bd issue `sturm-spv8` (P4) and the Phase T completion blockquote in `docs/roadmap_transpiler_post_mvp.md` (2026-04-24).

**Baseline at deferral:** Phase T closed green with 314/314 CTests passing (0 failed) under `ctest --parallel 6`. Every `[[clang::annotate("sturm::reversible")]]` forward that P-C / Q-B accept and R-A / R-B / S-A can emit for now gets an auto-synthesised adjoint sibling through the transpiler's normal pipeline, with no user-side `STURM_REGISTER_ADJOINT` call required. The five items below are the remaining shape-envelope gaps.

---

## 1. Recursion

Self-calls inside a `[[clang::annotate("sturm::reversible")]]` forward.

**Current behaviour.** Q-A twin synth + R-A adjoint emission do not handle a forward calling itself; naive emission would recurse into the un-synthesised adjoint.

**Required work.** Either a fixed-point emission strategy, or an explicit base-case contract.

**References.**
- Plan §0 Q4 (follow-up epic bucket).
- Roadmap notes at lines 1485–1486, 1836–1837, 2102, 2301 in `docs/roadmap_transpiler_post_mvp.md`.

---

## 2. Cross-TU synthesis

Forward defined in one translation unit, `sturm::invert(&fn)` call site in another.

**Current behaviour.** PI-1's invert-call scanner + T-2's three-condition gate (PRD §9 Q2) operate at end-of-TU; a cross-TU invert call cannot see the forward's annotation at synth time.

**Required work.** Either a link-time synthesis pass, an export/import manifest, or forcing all reversible forwards into headers with inline semantics.

**References.**
- PRD §9 (locked decisions around reversibility scope).
- Plan §9 follow-up bucket.
- Roadmap notes at lines 1489–1492, 1838, 2103 in `docs/roadmap_transpiler_post_mvp.md`.

---

## 3. Member function synthesis

Class methods annotated reversible.

**Current behaviour.** The current matcher pipeline assumes free functions:
- Q-A twin synth constructs `__fn_out` at global scope.
- R-B emits `STURM_REGISTER_ADJOINT(fn, __fn_adj)` with fully-qualified names.

**Complications introduced by members.** Implicit `this`, access control, and class-template-specialisation interactions — none of which the current matchers handle.

**Required work.** Extend the matcher pipeline to recognise `CXXMethodDecl` / `CXXMemberCallExpr`, thread `this` through twin synth and adjoint emission, and decide how to name/register member adjoints.

**References.**
- Plan §9 follow-up bucket.
- Roadmap note at line 1492 in `docs/roadmap_transpiler_post_mvp.md`.

---

## 4. Template synthesis

Template-dependent reversible forwards (e.g., `template<int N> void fn(qint<N>&)`).

**Current behaviour.** Synth is driven at AST-parse time per concrete `FunctionDecl`; template instantiations only produce concrete FDs on-demand.

**Required work.** Either lazy instantiation-time synthesis, or eager all-instantiations synthesis (with the associated combinatorial cost).

**References.**
- Plan §9 follow-up bucket.
- Roadmap notes around line 1534 (template survival through Q-A) and line 2301 in `docs/roadmap_transpiler_post_mvp.md`.

---

## 5. Free-function predicates inside `WHEN(...)`

The WHEN-lift matcher does not recognise free-function calls as the predicate shape — e.g. `WHEN(marked(x, T)) { ... }` is not lifted, even though `marked` is a fully-supported reversible forward elsewhere in the pipeline.

**Current behaviour.** `transpiler/src/matcher_when_lift.cpp:374-380` lifts predicate expressions into a fresh `qbool __stu_tN` temp, runs the body controlled on it, and auto-uncomputes the temp at the close-brace. The matcher only handles operator shapes (comparators like `a == b`, bitwise compounds like `(b | c) & d`); on any other shape — including free-function calls and bare literals — it bails with the comment `// Not a supported shape (e.g. WHEN(foo(a)) or a literal).` Today the user must lift manually:

```cpp
qbool m = marked(x, T);   // user-named, NOT auto-uncomputed
WHEN(m) { ... }           // bare named qbool — supported shape
```

**Required work.** Extend the WHEN-lift matcher to recognise `CallExpr` predicates whose callee is a reversible forward returning `qbool` (or, more generally, anything the auto-adjoint pipeline already handles), synthesise a lifted temp, and emit the structural inverse of the call at the close-brace via the existing scope-exit framework.

**Scope boundary.** This deferral covers only the *predicate position* of `WHEN(...)`, where the lifted result is a compiler-introduced temporary. It does NOT extend auto-uncompute to standalone user-named results (see "Out of scope" note below).

**References.**
- `transpiler/src/matcher_when_lift.cpp:374-380` — current bail-out on unsupported predicate shapes.
- Existing supported shapes: `examples/when_integration.cpp`, `examples/nested_when.cpp`, `examples/rotations.cpp`.
- `marked` as a reversible forward: `tests/transpiler/fixtures/reversible_return_style_qbool.expected.cpp` (return-style) and `reversible_out_param_canonical.expected.cpp` (out-param style).

---

## Out of scope: user-named results (e.g. `pow(qint, int)`)

The auto-uncompute / scope-exit reversibility machinery exists to clean up **compiler-introduced temporaries** that the user never named — ancillae born inside compound ops (`mul`, `divide_oop`, etc.) that would otherwise leak. It is **not** a general-purpose "delete the user's data at scope exit" mechanism.

A free function like `pow(qint_t<W> base, int64_t exp)` returns a first-class, user-named `qint_t<W>` result. Auto-reversing it at scope exit would mean the transpiler erasing the user's data behind their back, which is not the contract anywhere else in the language (a local `int x` doesn't get auto-zeroed at scope exit either).

If a user wants to uncompute such a result, they invert the call themselves via `sturm::invert<&pow<W>>()` (or the equivalent for whichever forward they used). For functions more exotic than the in-tree primitives, the user is expected to provide their own inverse.

This is a deliberate boundary, not a deferral — it does not belong on the list above.

---

## Cross-references

- **Roadmap:** `docs/roadmap_transpiler_post_mvp.md` — Phase T completion blockquote (2026-04-24 entry) is the authoritative deferral list.
- **Closed epic:** bd `sturm-xrob` (Phase T — Transpiler integration of automatic adjoint synthesis).
- **Tracker issue:** bd `sturm-spv8` (this document is its materialised form).

---

## Cross-check: modular-arithmetic PRD §7 (sturm-6ov3.4 / 2026-04-26)

Plan `docs/plan_modular_arithmetic.md` §9.1 #6 instructs us to cross-
check `docs/prd_modular_arithmetic.md` PRD §7 (non-goals / explicitly
deferred) against this list and surface any deferred item that
interacts with reversibility.

**Result of the review.** None of the five PRD §7 deferrals introduces
a reversibility deferral. For the record:

| PRD §7 item | Reversibility impact |
|---|---|
| `qint_mod<N>` type wrapper | Pure type-system sugar over `qint_t<W>`; the underlying primitives (`lib_*_mod_dsl`) and their adjoints (`__lib_*_mod_dsl_adj`) are unchanged. |
| Per-region modular flag scope | Build-time vs. region-time toggle of the same `pow %` rewrite; the rewrite target (`lib_pow_mod_dsl`) is reversible by construction (Phase 3 / sturm-pp7m). |
| Compound modular assigns | Sugar over the existing free functions; lowering goes through the same reversible primitives. |
| Modular subtraction / negation | Lowers to `add_mod(a, n - b, n)`, which is the same reversible primitive. |
| Precondition-checking debug mode | Inserts reversible `compare(a, n)` guards (already a reversible primitive); does not change the modular ops themselves. |

No items interact with the five reversibility deferrals listed above
(recursion, cross-TU synthesis, member functions, templates, free-
function predicates inside `WHEN(...)`). Re-run this cross-check if a
new entry lands in PRD §7 of `prd_modular_arithmetic`.

---

## Filing new work

When a concrete use case lands:

1. Open a new bd issue for the specific deferral (recursion / cross-TU / member / template / WHEN free-call predicate).
2. Link it to the roadmap entry and to this document.
3. Update this file's entry for that deferral once work is scoped (move status from **Backlog** → **Scoped (bd issue <id>)** → remove once closed).
