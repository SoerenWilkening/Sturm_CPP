# TODO — Reversibility Deferrals (PRD §9)

**Status:** Backlog. No implementation planned. File child bd issues when a concrete use case motivates tackling any of these five.

**Source:** Carried forward from bd issue `sturm-spv8` (P4, closed 2026-04-24) when Phase T (epic `sturm-xrob`) wrapped.

**Baseline at deferral:** Phase T closed green with 314/314 CTests passing (0 failed) under `ctest --parallel 6`. Every `[[clang::annotate("sturm::reversible")]]` forward that P-C / Q-B accept and R-A / R-B / S-A can emit for now gets an auto-synthesised adjoint sibling through the transpiler's normal pipeline, with no user-side `STURM_REGISTER_ADJOINT` call required. The five items below are the remaining shape-envelope gaps.

---

## 1. Recursion

Self-calls inside a `[[clang::annotate("sturm::reversible")]]` forward.

**Current behaviour.** Q-A twin synth + R-A adjoint emission do not handle a forward calling itself; naive emission would recurse into the un-synthesised adjoint.

**Required work.** Either a fixed-point emission strategy, or an explicit base-case contract.

**References.**
- `transpiler/src/matcher_reversible_validate.hpp` — recursive self-calls are short-circuited as "out of scope" (search for "recursive call to the forward being validated").
- Phase T epic: bd `sturm-xrob` (CLOSED 2026-04-24).

---

## 2. Cross-TU synthesis

Forward defined in one translation unit, `sturm::invert(&fn)` call site in another.

**Current behaviour.** The PI-1 invert-call scanner and the three-condition synth gate both operate at end-of-TU; a cross-TU invert call cannot see the forward's annotation at synth time.

**Required work.** Either a link-time synthesis pass, an export/import manifest, or forcing all reversible forwards into headers with inline semantics.

**References.**
- Phase T epic: bd `sturm-xrob` (CLOSED 2026-04-24).

---

## 3. Member function synthesis

Class methods annotated reversible.

**Current behaviour.** The current matcher pipeline assumes free functions:
- Q-A twin synth constructs `__fn_out` at global scope.
- R-B emits `STURM_REGISTER_ADJOINT(fn, __fn_adj)` with fully-qualified names.

**Complications introduced by members.** Implicit `this`, access control, and class-template-specialisation interactions — none of which the current matchers handle.

**Required work.** Extend the matcher pipeline to recognise `CXXMethodDecl` / `CXXMemberCallExpr`, thread `this` through twin synth and adjoint emission, and decide how to name/register member adjoints.

**References.**
- Phase T epic: bd `sturm-xrob` (CLOSED 2026-04-24).

---

## 4. Template synthesis

Template-dependent reversible forwards (e.g., `template<int N> void fn(qint<N>&)`).

**Current behaviour.** Synth is driven at AST-parse time per concrete `FunctionDecl`; template instantiations only produce concrete FDs on-demand.

**Required work.** Either lazy instantiation-time synthesis, or eager all-instantiations synthesis (with the associated combinatorial cost).

**References.**
- Phase T epic: bd `sturm-xrob` (CLOSED 2026-04-24).

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

- **Closed epic:** bd `sturm-xrob` (Phase T — Transpiler integration of automatic adjoint synthesis, closed 2026-04-24).
- **Tracker issue:** bd `sturm-spv8` (this document is its materialised form).

> **Historical note (2026-04-26):** A prior cross-check against the modular-arithmetic PRD §7 (non-goals) confirmed that none of its five entries — `qint_mod<N>` wrapper, per-region modular flag scope, compound modular assigns, modular subtraction/negation, precondition-checking debug mode — interact with the five reversibility deferrals above. The modular-arith PRD/plan have since been pruned (commit `7a86606`, 2026-04-27); the conclusion stands as documented in bd `sturm-6ov3.4`.

---

## Filing new work

When a concrete use case lands:

1. Open a new bd issue for the specific deferral (recursion / cross-TU / member / template / WHEN free-call predicate).
2. Link it to this document (and to the Phase T epic `sturm-xrob` for historical context).
3. Update this file's entry for that deferral once work is scoped (move status from **Backlog** → **Scoped (bd issue <id>)** → remove once closed).
