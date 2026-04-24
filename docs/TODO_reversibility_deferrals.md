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

## 5. Controlled-lossy compound assignments leak garbage registers inside WHEN

Source epic: bd `sturm-h5it` (Correct lossy compound assignments `&=`, `|=`, `*=`, `/=`, `%=` inside WHEN). Children 1–5 have landed; Child 6 is this documentation entry.

**Current behaviour.** Inside a `WHEN(ctrl) { ... }` scope, the compound assignments

- `qint_t::operator&=` and `qint_t::operator|=` (see `include/sturm/qtypes/qint_bitwise_v3.hpp`),
- `qint_t::operator*=`, `qint_t::operator/=`, and `qint_t::operator%=` (see `include/sturm/qtypes/qint_arith_v3.hpp`),

each allocate a fresh W-qubit register to hold the computed result, then use a controlled-SWAP (CSWAP on `ctrl`) between the old `A` register and the new result register so that:

- When `ctrl = |1>`, the new result is swapped into `A`'s storage slot and the old `A` contents end up in the freshly-allocated register.
- When `ctrl = |0>`, the old `A` is preserved in place and the freshly-allocated register holds the (now-unwanted) computed result instead.

Because both branches must coexist coherently, neither register can be released at the end of the op: the old-`A` register is retained as the live `A`, and the new result register is **leaked** as garbage — one W-qubit leak per invocation. `*=` additionally leaks the upper W bits of the Cuccaro 2W product, and `/=` leaks the remainder register; `%=` leaks the quotient register. These are the irreducible cost of making a lossy op in-place reversible without an explicit uncomputation pass.

**Why it is deferred.** Lossy ops cannot be made in-place reversible without storing extra state somewhere. A `ctrl = |0>` branch must preserve the old `A` contents bit-for-bit (to honour the controlled semantics), which forces us to retain the pre-op value; the only way to do that without destroying the `ctrl = |1>` result is to keep both registers live and swap rather than overwrite. Correctly *releasing* the garbage register back to `|0>` requires an op-specific uncomputation sequence (see sturm-njul below) — AND/OR admit a cheap XOR-based undo, but MUL/DIV/MOD need either a dedicated reverse DSL or Bennett-style copy-and-uncompute. That work is out of scope for the in-place-under-WHEN fix.

**Discoverability.** The leaked registers are recorded at emit time in a thread-local registry so that a future transpiler pass (or tooling / audit) can enumerate them:

- Header: `include/sturm/control/garbage_registry.hpp`
- Namespace: `sturm::detail::garbage_registry`
- Record fields: `op_id`, `ctrl_qubit`, `W`, `qubit_indices`, `source_op_tag`.

Every CSWAP-and-leak site in `qint_bitwise_v3.hpp` and `qint_arith_v3.hpp` pushes a record (with an `op_id` scoped to the current WHEN scope) before returning. Consumers can iterate the registry at WHEN scope exit to discover exactly which qubits leaked and which op produced them.

**Required work.** Tracked by the two deferred bd issues below.

**Follow-up issues.**

- bd `sturm-njul` (P3) — **Transpiler pass to consume `garbage_registry` and emit uncomputation at WHEN scope exit.** Walks the per-scope records registered during the WHEN body and emits an appropriate uncomputation sequence so the garbage qubits return to `|0>` and can be released cleanly. Needs per-op-tag uncomputation schemes (AND/OR self-inverse XOR via retained inputs; MUL/DIV/MOD via reverse DSLs or Bennett-style saved copies).
- bd `sturm-pqs0` (P3) — **Correct handling of `*=` upper-W bits and `/=` discarded remainder under superposition inputs.** In `qint_arith_v3.hpp` the upper W bits of the 2W Cuccaro product (around line 113) and the `/=` remainder register (around line 141) are released unconditionally. That is safe only when `A` and `B` are in computational-basis states; with superposition inputs those registers are entangled with `A`/`B` and releasing them silently discards quantum information. Fix is either Bennett-style uncomputation or explicit documentation that `*=` / `/=` require classical operands.

**References.**

- Epic: bd `sturm-h5it` (CLOSED children 1–5, Child 6 is this entry).
- Headers: `include/sturm/qtypes/qint_bitwise_v3.hpp`, `include/sturm/qtypes/qint_arith_v3.hpp`, `include/sturm/control/garbage_registry.hpp`.

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
