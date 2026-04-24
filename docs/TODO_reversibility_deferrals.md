# TODO — Reversibility Deferrals (PRD §9)

**Status:** Backlog. No implementation planned. File child bd issues when a concrete use case motivates tackling any of these four.

**Source:** Carried forward from bd issue `sturm-spv8` (P4) and the Phase T completion blockquote in `docs/roadmap_transpiler_post_mvp.md` (2026-04-24).

**Baseline at deferral:** Phase T closed green with 314/314 CTests passing (0 failed) under `ctest --parallel 6`. Every `[[clang::annotate("sturm::reversible")]]` forward that P-C / Q-B accept and R-A / R-B / S-A can emit for now gets an auto-synthesised adjoint sibling through the transpiler's normal pipeline, with no user-side `STURM_REGISTER_ADJOINT` call required. The four items below are the remaining shape-envelope gaps.

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

## Cross-references

- **PRD:** `docs/prd_automatic_adjoint_synthesis.md` §9 (locked reversibility-scope decisions).
- **Roadmap:** `docs/roadmap_transpiler_post_mvp.md` — Phase T completion blockquote (2026-04-24 entry) is the authoritative deferral list.
- **Closed epic:** bd `sturm-xrob` (Phase T — Transpiler integration of automatic adjoint synthesis).
- **Tracker issue:** bd `sturm-spv8` (this document is its materialised form).

---

## Filing new work

When a concrete use case lands:

1. Open a new bd issue for the specific deferral (recursion / cross-TU / member / template).
2. Link it to the roadmap entry and to this document.
3. Update this file's entry for that deferral once work is scoped (move status from **Backlog** → **Scoped (bd issue <id>)** → remove once closed).
