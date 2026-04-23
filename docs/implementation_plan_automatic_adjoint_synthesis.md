## Implementation Plan — Automatic Adjoint Synthesis for User Routines

**Status:** Draft
**Owner:** Soren Wilkening
**Date:** 2026-04-23
**Parent PRD:** `docs/prd_automatic_adjoint_synthesis.md`
**Principles touched:** P4, P9/P9a-d, B10, B11 (already merged into `docs/01_principles.md`)
**Phases:** P (prelude) · Q (signature normalization) · R (straight-line emission) · S (loop reversal)

---

### 0. Locked decisions (resolving PRD §9)

These answer the four open questions before implementation starts. They are the contract; any drift requires a PRD amendment.

| # | Question | Decision |
|---|---|---|
| Q1 | Opt-in vs opt-out | **Opt-in via attribute** `[[sturm::reversible]]` on the forward routine. Later phase may flip default after two consecutive roadmap phases ship without a regression. Attribute is parsed by the transpiler *only* — runtime headers ignore it. |
| Q2 | Partial synthesis fallback | **Hand-registration remains the escape hatch.** If the validation pass (P-2) rejects a routine but the user has a `STURM_REGISTER_ADJOINT` binding, the registry entry from PI-1 wins and no diagnostic is emitted. Rejection is a hard error **only** if (a) the routine carries `[[sturm::reversible]]` AND (b) no manual registration exists AND (c) `invert(fn)` is called somewhere in the TU. |
| Q3 | Mutation tracking granularity | **Pointwise.** Each loop iteration un-mutates exactly; the adjoint is a reversed-iteration mirror of the forward. LIFO + loop reversal (B11) delivers this for free. The m12 byte-compare gate (§5) pins the contract. |
| Q4 | Recursion | **Out of scope for P-S.** A recursive reversible routine carrying `[[sturm::reversible]]` triggers a P9d diagnostic pointing at the recursive call site. Follow-up bd epic `sturm-TBD` captures the call-stack design. |

---

### 1. Scope and LOC budget

All new source files stay **≤ 400 LOC** (CLAUDE.md rule). Where a module would exceed, split along responsibility boundaries listed below.

| Phase | New source modules | New test modules | New fixtures | Tests added | Cumulative LOC ceiling |
|---|---|---|---|---|---|
| P | 3 | 2 | 6 | 8 | 1050 |
| Q | 2 | 1 | 4 | 6 | 700 |
| R | 3 | 2 | 6 | 10 | 1100 |
| S | 2 | 2 | 5 | 9 | 750 |
| **Total** | **10** | **7** | **21** | **33** | **3600** |

No single new module exceeds 400 LOC; a module approaching 350 LOC is a refactor signal.

---

### 2. Module inventory

#### 2.1 Phase P — synthesis prelude

| Module | Path | Role | LOC budget |
|---|---|---|---|
| **P-A** `reversible_attribute` | `transpiler/src/reversible_attribute.{hpp,cpp}` | Parse `[[sturm::reversible]]` via Clang `AnnotateAttr`; expose `bool is_reversible(const FunctionDecl*)`. | 120 impl / 80 hdr |
| **P-B** `synthesis_registry` | `transpiler/src/synthesis_registry.{hpp,cpp}` | Track `{forward FD*, out-param twin FD* or null, emitted adjoint name, status enum}`. Extension of `RoutineRegistry`, not a replacement — `routine_registry.hpp` already holds the hand-registered pairs; the synthesis registry layers on top and defers to it for conflict resolution (Q2). | 280 impl / 150 hdr |
| **P-C** `matcher_reversible_validate` | `transpiler/src/matcher_reversible_validate.cpp` | P9d validation pass: walks every `[[sturm::reversible]]` routine body, rejects measurement, classical I/O, unregistered callee, `while`-loop, quantum-dependent condition. Emits through `DiagContext` with new `report_reversible_*` family. | 360 impl |
| **P-D** `DiagContext` extension | `transpiler/src/diag_context.{hpp,cpp}` (edit) | Add `report_reversible_measurement`, `report_reversible_io`, `report_reversible_unregistered_callee`, `report_reversible_while_loop`, `report_reversible_classical_cond`. Mirrors the five existing `report_*` methods. | +90 LOC combined |

**Test surface (Phase P):**

| Test | Path | Asserts |
|---|---|---|
| `test_reversible_attribute.cpp` | `transpiler/tests/` | Parser recognizes attribute, rejects typo `[[sturm::reversable]]`, survives template instantiation. |
| `test_synthesis_registry.cpp` | `transpiler/tests/` | Insert / lookup / conflict with `RoutineRegistry`, deterministic iteration, null-FD guard. |
| Positive fixture `tests/transpiler/fixtures/reversible_oracle.cpp` | XOR oracle body, must pass validation. | |
| Negative fixtures (5, one per reject reason) in `tests/transpiler/fixtures/reversible_reject_*.cpp` | Each paired with `.expected.diag` golden file. | |
| Test harness `check_reversible_diagnostic.cmake` | `tests/transpiler/` | Cloned from `check_qbool_prep_diagnostic.cmake`; compares diagnostic stream byte-for-byte. |

#### 2.2 Phase Q — signature normalization

| Module | Path | Role | LOC budget |
|---|---|---|---|
| **Q-A** `return_to_out_param` | `transpiler/src/return_to_out_param.{hpp,cpp}` | Given a reversible routine FD whose return type is a quantum type and whose body is `return <expr>;` (single return), synthesize the out-param twin's source text. Pure string production — no IR mutation, no uncompute integration. Output is attached to the synthesis registry entry for R to consume. | 260 impl / 110 hdr |
| **Q-B** `constness_enforcement` | `transpiler/src/matcher_reversible_signature.cpp` | AST matcher walking the parameter list of every `[[sturm::reversible]]` routine. Rejects: non-const pass-by-value of quantum types that the body mutates, `const` reference whose body mutates, pass-by-pointer (must be ref). Emits through P-D's diagnostic family. | 240 impl |

**Test surface (Phase Q):**

| Test | Path | Asserts |
|---|---|---|
| `test_return_to_out_param.cpp` | `transpiler/tests/` | Golden-file comparison of generated twin source against `fixtures/return_to_out_param_{1..4}.expected.cpp`. |
| Positive fixture pairs `tests/transpiler/fixtures/reversible_return_style_{qbool,qint}.cpp` → `.expected.cpp` | | |
| Negative fixture `tests/transpiler/fixtures/reversible_sig_const_ref_mutated.cpp` | | |
| Negative fixture `tests/transpiler/fixtures/reversible_sig_multi_return.cpp` | Body has two `return` statements → reject. | |
| Positive fixture `tests/transpiler/fixtures/reversible_out_param_canonical.cpp` | Already canonical shape — twin step is a no-op. | |

#### 2.3 Phase R — straight-line adjoint emission

| Module | Path | Role | LOC budget |
|---|---|---|---|
| **R-A** `adjoint_emitter` | `transpiler/src/adjoint_emitter.{hpp,cpp}` | Walks a validated, normalized routine body in reverse statement order; for each statement, calls the existing `render_uncompute` from `uncompute_pass.cpp` to produce adjoint source. Emits as a sibling function `__<fn>_adj` into the rewriter's buffer, not inline. Self-contained — no global state beyond the synthesis registry. | 370 impl / 130 hdr |
| **R-B** `auto_register_emitter` | `transpiler/src/auto_register_emitter.{hpp,cpp}` | Appends `STURM_REGISTER_ADJOINT(fn, __<fn>_adj)` after each synthesized adjoint. Must produce the same AST shape PI-1 already consumes, so PI-1's matcher picks up the registration on the next pass (two-pass transpile already required by PM3). | 120 impl / 60 hdr |
| **R-C** `matcher_reversible_drive` | `transpiler/src/matcher_reversible_drive.cpp` | Top-level driver matcher that orchestrates R-A + R-B for each `[[sturm::reversible]]` FD that passed P-C validation and Q-B constness. Single entry point callable from `transpile_consumer.cpp`. | 180 impl |

**Test surface (Phase R):**

| Test | Path | Asserts |
|---|---|---|
| `test_adjoint_emitter.cpp` | `transpiler/tests/` | Unit tests: reverse-statement-order walk on hand-built IR; each primitive kind's adjoint render exercised. Uses `test_matcher_harness.hpp`. |
| `test_auto_register_emitter.cpp` | `transpiler/tests/` | Generated `STURM_REGISTER_ADJOINT(fn, __fn_adj)` text equals a golden. |
| Snapshot fixtures (6) `tests/transpiler/fixtures/reversible_body_{xor,and,compound,rotation_theta,rotation_phi,mixed}.cpp` → `.expected.cpp` | Each: forward routine + synthesized adjoint, compile-ready. | |
| **m12 pair** `tests/transpiler/fixtures/reversible_synth_{transpiled,reference}.cpp` | Transpiled side uses `[[sturm::reversible]]`; reference side has hand-written adjoint. Added as `m12_reversible_synth_{transpiled,reference}` namespaces in `tests/transpiler/test_gate_equivalence.cpp`. | Byte-identical `GateRecord` streams. |
| Roundtrip test in `tests/test_invert.cpp` | Forward + synthesized adjoint = identity; gate counter = 0 at scope exit. | |

#### 2.4 Phase S — loop reversal

| Module | Path | Role | LOC budget |
|---|---|---|---|
| **S-A** `loop_reversal` | `transpiler/src/loop_reversal.{hpp,cpp}` | Consumes a `ForStmt` AST node belonging to a reversible routine body; produces the reversed-iteration adjoint loop header + recursively descends into the body via `adjoint_emitter`. Handles stride-aware bound computation for `i += s`. Rejects non-canonical for-shape (non-trivial init, compound condition, side-effecting increment). | 320 impl / 80 hdr |
| **S-B** `matcher_outer_var_guard` (**edit**) | `transpiler/src/matcher_outer_var_guard.cpp` | Suppress `skip_uncompute=true` when the enclosing FD is `[[sturm::reversible]]` — instead mark the op for loop-reversal handling by S-A. Outside synthesis context, current PH-3 behavior preserved. Edit only; LOC delta ≤ +40. | +40 LOC |

**Test surface (Phase S):**

| Test | Path | Asserts |
|---|---|---|
| `test_loop_reversal.cpp` | `transpiler/tests/` | Unit table: forward loop header shape → reversed header; stride 1, 2, negative, zero. |
| Fixture `reversible_loop_ripple.cpp` → `.expected.cpp` | Forward ripple + synthesized reverse-order adjoint. | |
| Fixture `reversible_loop_bit_reversal.cpp` → `.expected.cpp` | | |
| Fixture `reversible_loop_adder_carry.cpp` → `.expected.cpp` | | |
| Negative fixture `reversible_while_loop.cpp` | Must emit `report_reversible_while_loop`. | |
| Negative fixture `reversible_qdep_trip_count.cpp` | Trip count reads a qint; diagnostic via P-D. | |
| **m12 pairs (3)** in `test_gate_equivalence.cpp` | `m12_reversible_loop_{ripple,bit_reversal,adder}_{transpiled,reference}` namespaces. | Byte-identical `GateRecord` streams on 3 independent payloads. |
| Roundtrip tests (3) in `tests/test_invert.cpp` | Forward then synthesized adjoint on prepared state = identity. | |

---

### 3. Dependencies and ordering

```
          P-A  P-D
            \  /
             P-B ── P-C ──────┐
                             Q-A ── Q-B ─┐
                                        R-A ── R-C ── R-B
                                                 \
                                                  S-A ── S-B (edit)
```

- P-A and P-D are parallelizable; P-B consumes both.
- P-C blocks everything below: no synthesis runs without validation.
- Q-A can proceed once P-B is merged; Q-B extends P-C's diagnostic surface.
- R-A needs Q-A's output shape for return-style bodies; R-C wires it up.
- R-B runs after R-A produces an adjoint; order within R-C is fixed.
- S-A depends on R-A (loop body descent reuses it).
- S-B is an in-place edit to existing matcher — ships last to avoid stranding PH-3 behavior.

---

### 4. Test-driven ordering (per module)

For every module listed above, the work order is:

1. **Fixture first.** Check in the input `.cpp` + golden `.expected.cpp` (or `.expected.diag`). Test fails immediately.
2. **CTest wired.** Add to `tests/transpiler/CMakeLists.txt` — must appear in `ctest --print-labels` (capped at `-j6`).
3. **Unit-test skeleton.** `transpiler/tests/test_<module>.cpp` with failing assertions pinning the API surface.
4. **Implementation.** Minimum code to turn the tests green. LOC budget enforced.
5. **Idempotence check.** Run `check_idempotent.cmake` — transpile(transpile(X)) = transpile(X).
6. **Cross-phase integration test.** Only once the phase ships: all fixtures re-run in a single `ctest -j6`.

The repo's existing harness — `run_snapshot.cmake`, `check_idempotent.cmake`, `check_outer_var_guard_diagnostic.cmake`, `test_gate_equivalence.cpp` — is the substrate. No new harness is introduced; each new `.cmake` file is a clone + rename of the closest existing one.

---

### 5. Test strategy (PRD §8 realized)

Three gate types, all enforced via `ctest -j6`:

| Gate | What it proves | New tests count | Pre-existing infra |
|---|---|---|---|
| **Snapshot** | Generated source is byte-identical to golden. | 21 fixtures across P/Q/R/S. | `run_snapshot.cmake` |
| **Idempotence** | Transpile is a fixed point. | Applied to every positive fixture. | `check_idempotent.cmake` |
| **Diagnostic golden** | P9d + constness + while-loop diagnostics match expected text. | 7 negative fixtures. | `check_<X>_diagnostic.cmake` pattern |
| **m12 gate-equivalence** | Synthesized adjoint produces same gate stream as hand-written reference. | 4 new namespace pairs (1 straight-line + 3 loop). | `test_gate_equivalence.cpp` |
| **Roundtrip** | Forward + synthesized adjoint = identity on prepared state. | 4 new tests in `tests/test_invert.cpp`. | `tests/test_invert.cpp` |

**m12 byte-compare is the primary correctness gate.** If byte-compare passes but roundtrip fails, the test harness is wrong — file a bug. If roundtrip passes but byte-compare fails, the synthesis has diverged from the hand-written reference; accept the divergence only after updating the reference.

---

### 6. Per-phase acceptance criteria

A phase ships when **all** of these hold:

- All new modules ≤ 400 LOC (verified via `wc -l`).
- `ctest -j6` green on `tests/transpiler/` and `tests/`.
- `clang-format` clean; `clang-tidy` clean on new files.
- Transpile cycle on every positive fixture is idempotent.
- The corresponding bd issues are closed with evidence (fixture path + test name).
- `docs/roadmap_transpiler_post_mvp.md` has a new completion blockquote.

---

### 7. Beads epic layout

Mirrors the Phase N shape from `implementation_plan_transpiler_phase_n.md`. File one epic per phase with sub-items:

```
sturm-TBD-P  Phase P — synthesis prelude
    P-0  Roadmap stub + this plan reference
    P-1  reversible_attribute parser
    P-2  synthesis_registry + RoutineRegistry bridge
    P-3  DiagContext extension (5 new report_ methods)
    P-4  matcher_reversible_validate
    P-5  Positive + 5 negative fixtures, harness wiring
    P-6  Roadmap update blockquote

sturm-TBD-Q  Phase Q — signature normalization
    Q-0  Roadmap stub
    Q-1  return_to_out_param
    Q-2  constness_enforcement matcher
    Q-3  4 positive + 2 negative fixtures
    Q-4  Roadmap update

sturm-TBD-R  Phase R — straight-line emission
    R-0  Roadmap stub
    R-1  adjoint_emitter
    R-2  auto_register_emitter
    R-3  matcher_reversible_drive
    R-4  6 snapshot fixtures
    R-5  m12 pair + namespace in test_gate_equivalence.cpp
    R-6  Roundtrip test in tests/test_invert.cpp
    R-7  Roadmap update

sturm-TBD-S  Phase S — loop reversal
    S-0  Roadmap stub
    S-1  loop_reversal
    S-2  matcher_outer_var_guard edit (preserve PH-3 outside synthesis)
    S-3  3 positive loop fixtures
    S-4  2 negative loop fixtures (while, qdep trip count)
    S-5  3 m12 pairs + namespaces
    S-6  3 roundtrip tests
    S-7  Roadmap update
```

Issues are sized so each closes in one bd-worker session. None of them modifies more than three source files.

---

### 8. Risk register (plan-level)

| Risk | Mitigation |
|---|---|
| Clang attribute plugin adds link-time cost. | `reversible_attribute` consumes `AnnotateAttr` — no `PluginASTAction` registration needed; zero runtime cost. |
| Two-pass transpile (forward pass emits adjoint, second pass registers it via PI-1) doubles build time. | PM3 already runs a second pass for emission verification; R-B piggybacks on that slot — no new passes added. |
| `[[sturm::reversible]]` attribute clashes with future language-standard attribute. | Namespaced under `sturm::`; Clang's attribute namespace scoping prevents collision. |
| Loop reversal miscomputes stride bounds for edge cases (stride 1, negative stride, zero trip count). | S-A's unit table covers each; `reversible_loop_*` fixtures add integration coverage. |
| Users write `[[sturm::reversible]]` on a measurement-containing body without realizing. | P-C rejects at definition site with SourceLocation-anchored diagnostic; golden-file comparison pins the text. |
| Synthesized adjoint diverges from a hand-written one across library primitive changes. | m12 byte-compare runs on every primitive evolution; 4 namespace pairs make the breakage loud. |
| Attribute parsing regresses across Clang version bumps. | `test_reversible_attribute.cpp` exercises three LLVM versions via the existing matrix. |

---

### 9. Out of scope (deferred to follow-up epics)

Captured here so they don't creep into P-S:

- Recursion support for reversible routines (Q4 decision).
- Cross-TU synthesis — each TU synthesizes independently; a reversible routine defined in one TU cannot be `invert()`ed from another unless the adjoint is exported via PI-1 after synthesis. Follow-up.
- `std::variant` / `std::optional` of quantum types as return value — return-to-out-param (Q-A) only handles single-expression returns of concrete quantum types.
- Synthesis inside class member functions — `this`-capture introduces a fourth parameter-capture rule beyond PRD §5.3. Follow-up.
- Template reversible routines — instantiation-time synthesis changes R-C's driver shape. Follow-up.

---

### 10. References

- `docs/prd_automatic_adjoint_synthesis.md` — parent PRD.
- `docs/01_principles.md` — P4, P9/P9a-d, B10, B11.
- `docs/implementation_plan_transpiler_phase_n.md` — prior art for this plan's shape.
- `transpiler/src/uncompute_pass.cpp:429` — statement-order LIFO; R-A reuses `render_uncompute`.
- `transpiler/src/matcher_outer_var_guard.cpp` — PH-3 `skip_uncompute` flag; S-B edits.
- `transpiler/src/routine_registry.hpp` — PI-1 map; P-B extends.
- `transpiler/src/diag_context.{hpp,cpp}` — 5 existing `report_*` methods; P-D adds 5 more.
- `tests/transpiler/test_gate_equivalence.cpp` — m12 namespace-pair harness; R-5 and S-5 extend.
- `include/sturm/routines/invert.hpp:70` — `sturm::invert(fn)` free function, unchanged.
