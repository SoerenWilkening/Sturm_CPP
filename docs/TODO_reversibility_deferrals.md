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

## 5. LO-2 OOP wrapper backends (`mul_oop` / `and_oop` / `or_oop`)

**Category note.** Items 1–4 above are *synthesis-pipeline* deferrals (recursion / cross-TU / member / template). This item is a different category: a *runtime-target* deferral on the LO-2 transpiler's emitted output. It is grouped here because both deferral kinds gate the same end-to-end reversibility story.

**Status:** Scoped (bd epic `sturm-ph6f`).

**Source.** Surfaced during `sturm-czfi` (LO-2 emitter fix that made the example consumers compile). The classical / TODO(backend) posture is documented in-source at `include/sturm/qtypes/lossy_oop.hpp:14-23` and on the `mul_oop` body at `lossy_oop.hpp:66`.

**Current behaviour.** The LO-2 transpiler emits calls to `mul_oop` / `and_oop` / `or_oop` / `divide_oop` (and `*_oop_adj` counterparts) for desugared compound-assigns on `qint_t<W>`. `divide_oop` already has a real reversible implementation in `include/sturm/qtypes/divide_oop.hpp`. The other three forwards in `lossy_oop.hpp` classically update `tmp.value` / `tmp.super_mask` and leave `tmp.qubits` at the default-constructed `-1` sentinel. All four `*_oop_adj` helpers (mul / and / or / divide) just zero `tmp` rather than running a structural inverse. This is sufficient for the example targets that drove `sturm-czfi` (`example_qint_arith`, `example_phase_abc_demo`) because both keep their operands on the classical short-circuit path (`qubits[0] < 0` throughout `main`); on that path the simulator never enters the gate-emitting fast path, and the wrappers only need to honour the classical bookkeeping invariants.

**Required work.** For each of `mul_oop` / `and_oop` / `or_oop`:

1. Allocate `W` qubits for `tmp` (replace the `qubits left at -1` shortcut) when at least one operand is on the gate-emitting path.
2. Dispatch into the matching `lib_*_dsl` to emit the reversible network (controlled-add ladder / Toffoli per bit / De Morgan ladder respectively). The DSLs already exist in tree (`include/sturm/lib/{mul,c_and,or}_dsl.hpp`) with adjoint registrations extracted into `*_dsl_adj.hpp` siblings via `sturm-nmf1`.
3. Register `*_oop_adj` as the structural inverse via `STURM_REGISTER_ADJOINT`, mirroring `divide_oop`'s pattern, so `sturm::invert<&mul_oop<W>>()` resolves to the proper adjoint instead of the classical zeroer.

`include/sturm/qtypes/divide_oop.hpp` is the worked reference for what each wrapper should look like (fast-path / classical-path split, qubit allocation, DSL dispatch, adjoint registration).

**Tracking.**
- **Epic:** `sturm-ph6f` — *LO-2 backend: replace classical-only `*_oop` wrappers with reversible gate networks.*
- **Children:** `sturm-ph6f.2` (mul / `lib_mul_dsl`), `sturm-ph6f.3` (and / `c_and_dsl`), `sturm-ph6f.1` (or / `or_dsl`).

**References.**
- `include/sturm/qtypes/lossy_oop.hpp:14-23` — preamble explaining the classical / TODO(backend) posture.
- `include/sturm/qtypes/lossy_oop.hpp:66` — `TODO(backend)` marker on `mul_oop`.
- `include/sturm/qtypes/divide_oop.hpp` — reference implementation.
- bd `sturm-czfi` — LO-2 emitter fix that made this scope visible.
- bd `sturm-nmf1` — extracted `*_dsl_adj.hpp` siblings, prerequisite for clean dispatch.

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
