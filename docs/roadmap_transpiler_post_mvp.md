# Roadmap: After the Transpiler MVP

**Status:** Draft
**Date:** 2026-04-14
**Relates to:** `prd_transpiler_uncompute.md`

This document describes the work to do **after** the minimum viable pipeline from the PRD is green. The MVP proves the pipeline (Clang LibTooling → IR → uncompute pass → C++ emitter → CMake integration) on a single trivial case: `qbool tmp = a | b;`. Everything below widens coverage, adds optimization, and retires the legacy runtime uncompute layer.

Phases are ordered by dependency. Each phase is independently shippable and independently testable with a snapshot-fixture pair (input `.cpp` → expected output `.cpp`).

---

## Phase A — Self-inverse operations

> **2026-04-15:** Complete. All four patterns match and emit correct
> inverses under snapshot fixtures in `tests/transpiler/fixtures/`:
> `not_single` (PA-1), `xor_single` (PA-2), `xor_assign_qbool` (PA-3),
> `xor_assign_classical` (PA-4). The three patterns whose forward op is
> shipped on the runtime (MVP OR, PA-1, PA-3) are also exercised
> end-to-end in `examples/or_circuit.cpp` and verified by the
> `example_or_circuit_injected` CTest. PA-2 and PA-4 remain
> fixture-only until their forward operators (`operator^` on qbool,
> `qbool::operator^=(int)`) ship on the real runtime. Next up: Phase C.

Add match rules and inverse emission for operations that are their own inverse. No new IR concepts; the "inverse" is emitting the same forward op again.

Covered ops:
- `tmp = ~a;` → inverse: `tmp = ~tmp;`
- `a ^= b;` → inverse: `a ^= b;` (same statement, emitted in uncompute phase)
- `tmp = a ^ b;` → inverse: `tmp ^= a; tmp ^= b;` or the symmetric equivalent
- `a ^= c;` *(c classical)* → inverse: `a ^= c;`

**Deliverables:** new matchers, entries in the inverse table, new snapshot fixtures.

---

## Phase B — Constant arithmetic

> **2026-04-15:** Complete. All four compound-assign patterns
> (`a += k;`, `a -= k;`, `a *= k;`, `a /= k;` with `k` a classical
> constant lifted through the non-explicit `qint_t(int64_t)`
> converting constructor) match and emit the dual operator as their
> inverse. Snapshot fixtures in `tests/transpiler/fixtures/`:
> `add_assign_const`, `sub_assign_const`, `mul_assign_const`,
> `div_assign_const`. End-to-end behavior is pinned by
> `examples/constant_arith.cpp` and the
> `transpiler_example_constant_arith_injected` +
> `transpiler_idempotent_example_constant_arith` CTests. The
> coprime-with-modulus assumption for the PB-3 inverse (`a /= k` as
> the inverse of `a *= k`) is documented here as user responsibility
> — the transpiler does not emit a runtime check. Next up: Phase C.

Non-self-inverse operations with a classical constant operand.

Covered ops:
- `a += k;` *(k classical)* → `a -= k;`
- `a -= k;` → `a += k;`
- `a *= k;` → `a /= k;` *(guard: k != 0; k coprime with modulus if modular)*
- `a /= k;` → `a *= k;`

Touches existing `sub_const` / `add_const` on `qint_base`. Already present; no runtime addition needed.

**Deliverables:** matchers + snapshot fixtures.

---

## Phase C — qint-qint arithmetic

> **2026-04-15:** Complete. All five compound-assign patterns
> (`a += b;`, `a -= b;`, `a *= b;`, `a /= b;`, `a %= b;` with `b`
> another qint) match and emit a free-function call as inverse
> (`uncompute_add_qint`, `uncompute_sub_qint`, `uncompute_mul_qint`,
> `uncompute_div_qint`, `uncompute_mod_qint`). Snapshot fixtures in
> `tests/transpiler/fixtures/`: `add_assign_qint`, `sub_assign_qint`,
> `mul_assign_qint`, `div_assign_qint`, `mod_assign_qint`. End-to-end
> behavior is pinned by `examples/qint_arith.cpp` and the
> `transpiler_example_qint_arith_injected` +
> `transpiler_idempotent_example_qint_arith` CTests. Runtime-side,
> the tagged-union branches `ADD_QINT`, `SUB_QINT`, `MUL_INVERSE`,
> `DIV_INVERSE`, `MOD_INVERSE` in `uncompute_op.hpp` were retired in
> this phase — their stamp sites, factories, enum values, and switch
> cases are gone; the free functions in `uncompute_api.hpp` replace
> them. The mul/div inverses carry the coprime-with-`2^W` /
> no-overflow caveat documented in `uncompute_api.hpp` as user
> responsibility; `uncompute_mod_qint` ships with a TODO stub body
> (no clean dual — the forward op discards the quotient). Next up:
> Phase D.

Inverse requires access to the source `qint`, which must outlive its consumer. The transpiler enforces this by emitting the inverse in the same scope before either operand's named temporary goes out of scope.

Covered ops:
- `a += b;` *(b qint)* → `a -= b;`
- `a -= b;` → `a += b;`
- `a *= b;` → bespoke inverse via existing library adjoint
- `a /= b;`, `a %= b;` → same

**Runtime work:** complete the stub bodies currently in `uncompute_op.hpp` (`ADD_QINT`, `SUB_QINT`, `MUL_INVERSE`, `DIV_INVERSE`, `MOD_INVERSE`) **as free functions** in `uncompute_api.hpp`, not as tagged-union branches. This retires those branches permanently.

**Deliverables:** new free functions, matchers, snapshots.

---

## Phase D — Comparison operations

> **2026-04-15:** Complete. All six comparison patterns
> (`c = (a == b);`, `!=`, `<`, `<=`, `>`, `>=` with `a`, `b` qints)
> match and emit a free-function call as inverse
> (`uncompute_eq_qint`, `uncompute_ne_qint`, `uncompute_lt_qint`,
> `uncompute_le_qint`, `uncompute_gt_qint`, `uncompute_ge_qint`).
> Snapshot fixtures in `tests/transpiler/fixtures/`:
> `eq_compare_qint`, `ne_compare_qint`, `lt_compare_qint`,
> `le_compare_qint`, `gt_compare_qint`, `ge_compare_qint`. End-to-end
> behavior is pinned by `examples/comparison.cpp` and the
> `transpiler_example_comparison_injected` +
> `transpiler_idempotent_example_comparison` CTests. The replacement
> free functions live in `uncompute_api.hpp`. Runtime-side, the
> stopgap `kind::COMPARE` tagged-union branch — including its
> `make_compare` factory and the `compare_forward` / `compare_inverse`
> stubs in `uncompute_op.hpp` / `qint_base.hpp` — was retired in this
> phase; the free functions in `uncompute_api.hpp` replace it. Next
> up: Phase E.

Non-self-inverse. Today's runtime emits forward + inverse back-to-back as a stopgap (`uncompute_op.hpp:284`); this phase replaces that with a proper ancilla-qubit comparator adjoint.

Covered ops:
- `c = (a == b);` → `uncompute_eq_qint(c, a, b);` *(originally drafted as `uncompute_eq`)*
- `c = (a != b);`, `<`, `<=`, `>`, `>=` → analogous (`uncompute_ne_qint`, `uncompute_lt_qint`, `uncompute_le_qint`, `uncompute_gt_qint`, `uncompute_ge_qint`; originally drafted as `uncompute_ne`, `uncompute_lt`, etc.)

**Runtime work:** implement `uncompute_eq_qint`, `uncompute_lt_qint`, etc. (originally drafted as `uncompute_eq`, `uncompute_lt`), using the existing primitive comparator circuits' adjoints. The library routines shipped with manual adjoints per **P9**; these inverse functions are thin wrappers that invoke those adjoints with the right qubit bindings.

**Deliverables:** library adjoint wiring, free functions, matchers, snapshots.

---

## Phase E — Compound expressions with named intermediates

> **2026-04-15:** Complete. Nested qbool bitwise compounds in a single
> VarDecl initializer now decompose into a flat sequence of named
> intermediates with LIFO uncompute. Coverage includes the roadmap
> example `qbool r = (b | c) & d;` plus the two mirror shapes from
> PE-4: `qbool r = (b & c) | d;` and `qbool r = (a | b) | (c | d);`.
> Runtime-side, the new free function `uncompute_and(qbool&, const
> qbool&, const qbool&)` lives in `include/sturm/uncompute/uncompute_api.hpp`
> (impl in `src/sturm/uncompute/uncompute_api.cpp`) alongside
> `uncompute_or`. IR-side, `QOpKind::AND` was added; the emitter
> gained `Rewriter.ReplaceText` support via a new `QReplacement`
> struct returned from `synthesize()` in a `QSynthesisResult`
> wrapper (replacements applied before insertions). Transpiler-side,
> a new `FreshNameAllocator` helper (`transpiler/src/fresh_names.hpp`)
> produces `__stu_t<N>` temporaries, and the new matcher
> `transpiler/src/matcher_qbool_compound.cpp` recognises nested qbool
> bitwise compounds. Snapshot fixtures in `tests/transpiler/fixtures/`:
> `compound_or_and`, `compound_and_or`, `compound_nested_or`. End-to-end
> behavior is pinned by `examples/compound_expression.cpp` and the
> `transpiler_example_compound_expression_injected` +
> `transpiler_idempotent_example_compound_expression` CTests.
> Still deferred to later phases: XOR / NOT nesting, mixed qint/qbool
> compounds inside a single initializer, and liveness analysis for
> when a named temporary can be freed earlier than scope exit (Phase
> H). The Phase E implementation plan has been archived to
> `docs/archive/implementation_plan_transpiler_phase_e.md`. Next up:
> Phase F.

First phase that requires real data-flow work. The transpiler must decompose `qbool r = (b | c) & d;` into a sequence with named intermediates and insert uncomputation in LIFO order.

Example transformation:
```cpp
// Input
qbool r = (b | c) & d;

// Output
qbool __stu_t0 = b | c;
qbool r = __stu_t0 & d;
uncompute_and(r, __stu_t0, d);           // if r itself is intermediate
uncompute_or(__stu_t0, b, c);            // LIFO order
```

Requires:
- IR pass that walks the AST expression tree and emits a linearized sequence with fresh names.
- Uncompute pass that places the inverses in reverse order at scope exit (or before re-use, see Phase H).
- Source locations tracked so diagnostics point at the original expression.

**Deliverables:** IR expansion for binary expressions, uncompute scheduler, many snapshot fixtures exercising nested patterns.

---

## Phase F — `WHEN` scope integration

> **2026-04-15:** Complete. The `WHEN(expr) { body }` matcher
> (`transpiler/src/matcher_when_lift.cpp`) now performs the three-point
> lift: flat `qbool __stu_tN = ...;` decls injected before the WHEN,
> the macro argument rewritten to the outermost temp, and a LIFO
> `uncompute_*` pair planted directly after the WHEN body's `}`.
> Coverage spans five hermetic snapshot fixtures in
> `tests/transpiler/fixtures/`: `when_single_or` (PF-3, lift `b | c`),
> `when_named_passthrough` (PF-3, bare-DeclRefExpr short-circuit),
> `when_compound` (PF-4, the roadmap `(b | c) & d` shape),
> `when_compare` (PF-4, the Phase D comparator widening), and
> `when_nested_passthrough` (PF-5, per-WHEN post-body brace identity
> through a nested `WHEN(a) { WHEN(b | c) { ... } }`). The end-to-end
> demo `examples/when_integration.cpp` pins the full Phase F rewrite
> against a real build, backed by two CTests
> (`transpiler_example_when_integration_injected` and
> `transpiler_idempotent_example_when_integration`). Zero runtime
> changes were required — the existing `WhenGuard` / `WhenCapture` /
> `make_when_guard` machinery in `include/sturm/control/when.hpp`
> accepts the named temp unchanged; Phase F's entire contribution
> lives in the transpiler + emitter. Next up: Phase H (loops and
> classical control flow around quantum ops).

The `WHEN(expr) { ... }` macro stays in user source. The transpiler's responsibility:

1. If `expr` is already a named `qbool`, pass through.
2. If `expr` is an expression (e.g. `WHEN(b | c)`), decompose using Phase E logic, emit a named temporary, pass the name to `WHEN`.
3. Ensure uncomputation of the temporary happens **after** the `WHEN` body, at the same scope level as the `WHEN` itself.

Example:
```cpp
// Input
WHEN((b | c) & d) { a ^= true; }

// Output
qbool __stu_t0 = b | c;
qbool __stu_t1 = __stu_t0 & d;
WHEN(__stu_t1) { a ^= true; }
uncompute_and(__stu_t1, __stu_t0, d);
uncompute_or(__stu_t0, b, c);
```

The runtime's existing `WhenGuard` continues to handle push/pop of the active control qubit. No runtime change here.

**Deliverables:** transpiler special case for `WHEN(...)` macro invocations, snapshots.

---

## Phase G — Nested `WHEN` (AND-fold in the transpiler)

> **2026-04-16:** Complete. The new matcher
> `transpiler/src/matcher_when_nested.cpp` fires on named-named nested
> `WHEN`s and lowers them to `qbool __stu_ctrl<N> = outer & inner;` +
> `WHEN(__stu_ctrl<N>) { body; }` + a trailing
> `uncompute_and(__stu_ctrl<N>, outer, inner);` planted inside the
> outer body. Depth ≥ 3 uses a pairwise cascade: one
> `__stu_ctrl<N>` temp and one `uncompute_and` per nesting level,
> LIFO. The runtime AND-fold was retired from
> `include/sturm/control/when.hpp` (~68 LOC removed; members
> `and_folded_` / `ancilla_` / `inner_expr_qubit_` gone), with the
> depth-1 invariant preserved by a `control_stack` swap instead of a
> CCX. Three snapshot fixtures in `tests/transpiler/fixtures/` pin
> the rewrite: `when_nested_named`, `when_nested_cascade`,
> `when_nested_siblings`. The M12 harness grew a
> `nested_when_runtime` / `nested_when_reference` pair, and
> `examples/nested_when.cpp` plus its
> `transpiler_example_nested_when_injected` +
> `transpiler_idempotent_example_nested_when` CTests pin end-to-end
> builds. Tracked as bd issues sturm-d2sl (PG-0), sturm-s73p (PG-1),
> sturm-295e (PG-2), sturm-j0un (PG-3), sturm-ewto (PG-4), sturm-yqr4
> (PG-5), sturm-5t9i (PG-6), sturm-bvbd (PG-7), sturm-b3oi (PG-8),
> sturm-oizk (PG-9). Next up: Phase H (loops and classical control
> flow around quantum ops).

Today, the runtime's `WhenGuard` AND-fold allocates an ancilla and emits `CCX(outer, inner, anc)` at construction (`include/sturm/control/when.hpp:83-204`). This is the single biggest source of runtime complexity.

The transpiler can replace this entirely by lowering nested `WHEN` into an explicit `WHEN` over a named AND temporary:

```cpp
// User writes
WHEN(outer) {
    WHEN(inner) { body; }
}

// Transpiler emits
WHEN(outer) {
    qbool __stu_ctrl = outer & inner;      // one CCX
    WHEN(__stu_ctrl) { body; }
    uncompute_and(__stu_ctrl, outer, inner);
}
```

The inner `WhenGuard` now sees a single ordinary `qbool`, not a magic nested control. No runtime AND-fold code path is exercised.

**Deliverables:** matcher for nested `WHEN` macro expansions, IR lowering, snapshots. **Retires** the AND-fold code path in `WhenGuard` (can be `#if 0`'d out in parallel with a regression test that the transpiler-emitted path produces the same gate stream as the legacy AND-fold).

---

## Phase H — Loops and classical control flow around quantum ops

> **2026-04-16:** Complete. PH-1 refactored the scope-finder in
> `transpiler/src/matcher_common.hpp` from
> `enclosing_compound_stmt(...)` to `enclosing_scope(node, ctx)`
> returning `{scope_anchor_stmt, kind ∈ {CompoundStmt,
> BracelessBody}}`; every Phase A–G matcher switched to the new API
> with all pre-existing braced fixtures staying byte-identical. Two
> new matcher modules landed: `transpiler/src/matcher_brace_wrap.cpp`
> (PH-2) auto-wraps braceless `for` / `while` / `if` / `else` bodies
> containing quantum ops by scheduling `{` / `}` insertions through
> `raw_insertions`, and `transpiler/src/matcher_outer_var_guard.cpp`
> (PH-3) walks the parent chain to detect outer-scoped `qbool` /
> `qint` mutations inside loop/branch/WHEN bodies, emits a
> `STURM: qbool/qint '<name>' ... reverse-loop synthesis`
> diagnostic on stderr, and flags the `QOperation` with a new
> `skip_uncompute=true` bit that `uncompute_pass.cpp` honors per-op.
> PH-4 added nine snapshot fixtures in `tests/transpiler/fixtures/`:
> `for_intermediate_or`, `for_intermediate_or_braceless`,
> `while_intermediate_or`, `while_intermediate_or_braceless`,
> `if_then_intermediate_or`, `if_then_intermediate_or_braceless`,
> `if_else_intermediate_or`, `if_else_intermediate_or_braceless`,
> `for_outer_xor_reject` (plus `for_mixed_xor_reject` exercising
> per-op flag granularity). PH-5 pinned `examples/control_flow.cpp`
> with `transpiler_example_control_flow_injected` +
> `transpiler_idempotent_example_control_flow` CTests via
> `tests/transpiler/check_example_control_flow.cmake`. PH-6a wired
> `for_loop_runtime` / `for_loop_reference` and PH-6b wired
> `if_branches_runtime` / `if_branches_reference` into
> `test_gate_equivalence.cpp` for byte-identical `GateRecord`
> streams. Tracked as bd issues sturm-kc2d (PH-1), sturm-ygmu (PH-2),
> sturm-ebcv (PH-3), sturm-2fwn (PH-4), sturm-oxok (PH-5), sturm-tf3g
> (PH-6a), sturm-9d2y (PH-6b), sturm-uwa1 (PH-7). Next up: Phase I
> (user-defined routines and `invert()`).

Classical `for` / `while` / `if` containing quantum operations. The transpiler leaves the classical control flow alone but must place uncomputation **inside** the loop body (so each iteration is balanced), not after the loop.

```cpp
// Input
for (int i = 0; i < n; i++) {
    qbool r = (b | c) & d;
    WHEN(r) { a ^= true; }
}

// Output — unchanged: uncomputation already lives inside the scope that ends
// at the closing `}` of the loop body.
```

For `if` / `else`, each branch is a separate scope; uncomputation lives per-branch. This is already correct behavior for the scope-based scheduler from Phase E.

**Edge case:** a quantum variable declared before a loop and modified in the body — uncomputation must happen after the loop, not per-iteration. This is where the liveness analysis starts earning its keep.

**Deliverables:** liveness analysis for quantum locals, snapshots.

---

## Phase I — User-defined routines and `invert()`

> **2026-04-16:** Complete. The `invert()` free-function template plus
> the `STURM_REGISTER_ADJOINT(fn, adj)` macro now ship from
> `include/sturm/routines/invert.hpp` (PI-0): a trait-based,
> compile-time specialization of `sturm::_detail::adjoint_of<FnPtr>`
> yields zero runtime overhead, and unregistered `invert(bar)` surfaces
> a readable missing-specialization diagnostic. Transpiler-side, PI-1
> added `transpiler/src/routine_registry.hpp/.cpp` owning a
> TU-wide `std::unordered_map<const clang::FunctionDecl*, std::string>`
> populated by an AST matcher on the `adjoint_of<...>`
> `ClassTemplateSpecializationDecl` shape; `transpiler/src/main.cpp`
> wires the registry-build hook before the Phase I routine-call
> matcher runs. PI-2 added `transpiler/src/matcher_user_routine.cpp`,
> which fires on any `callExpr` of a registered routine, classifies
> each argument by its parameter's declared type (non-const
> `qbool&`/`qint&` -> output, `const qbool&`/`qint&` -> input,
> classical scalar -> input), and builds a `QOperation{kind =
> USER_ROUTINE, routine_name, operands, outputs_mask}` rooted at the
> enclosing scope via `detail::enclosing_scope()`. PI-3 introduced the
> shared `classify_output(vd, call, call_scope, ctx, sm, lang) ->
> OutputClass` helper in `transpiler/src/matcher_common.hpp`, giving
> four verdicts — `Intermediate` / `IntermediateOuter` / `Final` /
> `SkipWithDiagnostic` — with the multi-output conservative-skip rule
> (any `Final` or `SkipWithDiagnostic` disables uncompute for the
> whole call); PH-3's `matcher_outer_var_guard.cpp` was refactored
> onto the same helper with its existing snapshots staying
> byte-identical. PI-4 extended the IR: `QOpKind::USER_ROUTINE` sits
> after the Phase D comparator kinds, `QOperation` grew
> `std::string routine_name` + `uint32_t outputs_mask`, `qir.cpp`
> `dump()` renders the new kind, and `uncompute_pass.cpp` emits
> `invert(<routine_name>)(<comma-joined operands>);` (with a
> defensive empty-name guard). PI-5 pinned seven snapshot fixtures in
> `tests/transpiler/fixtures/`: `routine_single_out_local`,
> `routine_single_out_escapes`, `routine_inside_when`,
> `routine_inside_loop`, `routine_outer_var_reject`,
> `routine_self_adjoint`, `routine_multiple_lifo`. PI-6 added
> `examples/user_routine.cpp` plus
> `tests/transpiler/check_example_user_routine.cmake` and its
> `transpiler_example_user_routine_injected` +
> `transpiler_idempotent_example_user_routine` CTests. PI-7 closed
> the loop with `user_routine_runtime` / `user_routine_reference`
> pair in `test_gate_equivalence.cpp`, proving the transpiler-injected
> `invert(...)` produces a byte-identical `GateRecord` stream to a
> hand-written LIFO adjoint. Tracked as bd issues sturm-hi66 (PI-0),
> sturm-mmsa (PI-1), sturm-11wh (PI-2), sturm-jisy (PI-3),
> sturm-kdg8 (PI-4), sturm-7mle (PI-5), sturm-lkov (PI-6),
> sturm-90fq (PI-7), sturm-6s7l (PI-8). Next up: Phase J (IR-level
> optimizations).

A user-written quantum routine:
```cpp
void my_routine(qint& a, const qint& b) { /* ... */ }
```
ships with a manual adjoint, per **P9**. The `invert(my_routine)` free function already exists and returns the registered adjoint.

Transpiler responsibility:
- When `my_routine(x, y)` is a compute step (its result contributes to an intermediate that must be uncomputed later), emit `invert(my_routine)(x, y)` at the appropriate uncompute point.
- When `my_routine(x, y)` is the user's final computation (not an intermediate), no uncomputation is emitted.

Distinguishing the two requires **ownership / liveness analysis**: is the result consumed by a later op and then become dead within this scope, or does it escape?

**Deliverables:** routine-call matcher, adjoint-dispatch emission, fixtures exercising user-defined routines.

---

## Phase J — IR-Level Optimizations

> **2026-04-16:** Phase J epic scoped. Items 1, 3, 4 below are committed
> and tracked as bd issues `sturm-2hnb`..`sturm-9bs4` (labels PJ-1a..PJ-1j,
> PJ-4a..PJ-4f, PJ-3a..PJ-3i). Item 2 (peephole gate reordering) has been
> **deferred to Phase M** — speculative value on top of PJ-1's
> two-statement peephole, would require alias analysis for marginal gain.
> All Phase J optimizations ship **on by default**: the roadmap's
> speculative `--O1`/`--O2` flag machinery is dropped; future more
> aggressive passes are free to introduce opt-in flags. The forward
> helper emitted by PJ-1 is named `sturm::ccnot_inplace(qbool&, const
> qbool&, const qbool&)`, self-adjoint, living in
> `include/sturm/uncompute/uncompute_api.hpp` alongside `uncompute_and`.
> Phase K-3 explicitly depends on PJ-1 ("the zero-ancilla optimization
> now lives in the IR pass") and must land after Phase J.
>
> Sub-phase ordering: PJ-1 first (also ships the shared
> `count_readers_in_scope` helper), PJ-4 second (reuses the helper),
> PJ-3 last (largest design surface — new `classify_scope_kind` +
> `expr_is_loop_invariant` helpers and the `hoist_to_override`
> field on `QOperation`).

> **2026-04-17:** PJ-1 (zero-ancilla fusion) complete. The pattern
> `qbool __t = a & b; x ^= __t;` with `__t` read exactly once now
> rewrites as a single self-adjoint `ccnot_inplace(x, a, b);` call,
> emitting one CCX with no intermediate qubit allocation. PJ-1a added
> the shared `detail::count_readers_in_scope(name, decl_loc,
> scope_anchor, ctx)` helper in `transpiler/src/matcher_common.hpp`
> (also consumed by PJ-4a's dead-ancilla gate). PJ-1b declared
> `sturm::ccnot_inplace(qbool&, const qbool&, const qbool&)` in
> `include/sturm/uncompute/uncompute_api.hpp` (impl in
> `src/sturm/uncompute/uncompute_api.cpp` delegating to
> `primitive_AND(ctx, a.qubit(), b.qubit(), x.qubit())`). PJ-1c added
> `QOpKind::CCNOT_INPLACE` after `USER_ROUTINE` in
> `transpiler/include/sturm/transpile/qir.hpp` and the matching
> render case in `transpiler/src/uncompute_pass.cpp` (emits
> `ccnot_inplace(x, a, b);` — same text forward and inverse, guarded
> against `operands.size() != 2`). PJ-1d added the new module
> `transpiler/src/matcher_ccnot_fuse.cpp` exporting
> `register_ccnot_fuse_matcher(finder, unit)`; it anchors on
> `VarDecl` of `qbool __t = a & b;` with bare `DeclRefExpr` operands
> (nested RHS rejected), walks the adjacent next statement via
> `CompoundStmt` iteration, confirms `x ^= __t` with reader-count
> `== 1`, and emits one `QReplacement` spanning both statements plus
> one `QOperation{kind=CCNOT_INPLACE}`. PJ-1e added
> `std::vector<clang::SourceRange> fused_stmt_ranges` to `QUnit` in
> `transpiler/include/sturm/transpile/qir.hpp`;
> `transpiler/src/matcher_qbool_assign.cpp` and
> `transpiler/src/matcher_qbool_compound.cpp` early-return on any
> match covered by a fused range, preventing double-emission. PJ-1f
> wired `register_ccnot_fuse_matcher` into
> `transpiler/src/main.cpp` **before** `register_xor_assign_matcher`,
> `register_compound_qbool_matcher`, and PH-3's
> `register_outer_var_guard_matcher`, with an ordering-invariant
> comment block. PJ-1g pinned five snapshot fixtures in
> `tests/transpiler/fixtures/`: `fuse_xor_and` (happy path),
> `fuse_xor_and_reject_extra_reader`,
> `fuse_xor_and_reject_nested_rhs`,
> `fuse_xor_and_reject_intervening_stmt`, and
> `fuse_xor_and_reject_classical_rhs`. PJ-1h added
> `examples/zero_ancilla_fusion.cpp` plus
> `tests/transpiler/check_example_zero_ancilla_fusion.cmake` and its
> `transpiler_example_zero_ancilla_fusion_injected` +
> `transpiler_idempotent_example_zero_ancilla_fusion` CTests (wired
> via `examples/CMakeLists.txt`). PJ-1i closed the loop with the
> `m12_fused_transpiled` / `m12_fused_reference` namespace pair in
> `tests/transpiler/test_gate_equivalence.cpp` plus
> `tests/transpiler/fixtures/fuse_xor_and_runtime.cpp` and
> `tests/transpiler/fixtures/fuse_xor_and_reference.cpp`; the
> reference path spells out `primitive_AND(ctx, a_idx, b_idx, x_idx)`
> directly (not via `lazy_expr`, which would be trivially equal) and
> the transpiled path goes through the matcher — byte-identical
> `GateRecord` stream verified (2 CCX records on both sides, no
> ancilla allocation). Tracked as bd issues sturm-2hnb (PJ-1a),
> sturm-8cxd (PJ-1b), sturm-1bd9 (PJ-1c), sturm-rnsi (PJ-1d),
> sturm-udui (PJ-1e), sturm-bhby (PJ-1f), sturm-6a2z (PJ-1g),
> sturm-6et2 (PJ-1h), sturm-wjhc (PJ-1i), sturm-s29b (PJ-1j). Next
> up: PJ-4 (dead-ancilla elimination), then PJ-3 (uncompute
> hoisting).

> **2026-04-17:** PJ-4 (dead-ancilla elimination) complete. A `qbool`
> VarDecl whose initializer is a qbool-valued expression (`a | b`,
> `~a`, `a ^ b`, or a nested bitwise combination) and whose reader
> count inside the enclosing scope is zero is now deleted verbatim
> from the output — no decl, no forward gate, no uncompute. PJ-4a
> added `QUnit::eliminated_stmt_ranges` (same
> `std::vector<clang::SourceRange>` shape as PJ-1e's
> `fused_stmt_ranges`) in `transpiler/include/sturm/transpile/qir.hpp`
> and the new matcher module `transpiler/src/matcher_dead_ancilla.cpp`
> exporting `register_dead_ancilla_matcher(finder, unit)`; it anchors
> on `VarDecl` of a `qbool` with a qbool-operand initializer, reuses
> PJ-1a's `detail::count_readers_in_scope(name, decl_loc,
> scope_anchor, ctx)` helper, and on zero readers emits one
> `QReplacement{range=<full decl stmt>, replacement=""}` covering the
> whole statement through its trailing `;` plus pushes that same
> range into `unit_.eliminated_stmt_ranges`. The cleanup pass
> `apply_eliminated_stmt_guards(unit, sm)` (declared in
> `transpiler/include/sturm/transpile/matcher.hpp`, implemented in
> `matcher_dead_ancilla.cpp`) runs after `matchAST` as the
> authoritative backstop against MatchFinder's Decl/Stmt interleaving:
> it walks every scope and removes any `QOperation` whose
> `stmt_range` lies inside an eliminated entry, so the M8 synthesis
> pass never renders an uncompute for a decl that no longer exists.
> PJ-4b wired `register_dead_ancilla_matcher` into
> `transpiler/src/main.cpp` **before** `register_or_matcher`,
> `register_not_matcher`, `register_xor_matcher`,
> `register_xor_assign_matcher`, `register_compound_qbool_matcher`,
> PH-3's `register_outer_var_guard_matcher`, and PJ-1f's
> `register_ccnot_fuse_matcher`, with an ordering-invariant comment
> block documenting the rationale; per-callback early-returns against
> `eliminated_stmt_ranges` (via PJ-1e's `is_range_covered_by_fused`
> probe) were added in `transpiler/src/matcher_qbool_bitwise.cpp`,
> `transpiler/src/matcher_qbool_compound.cpp`, and
> `transpiler/src/matcher_qbool_assign.cpp` so downstream Phase A/E
> callbacks bail on covered ranges even when the Decl/Stmt visit
> order lets them fire before the eliminator. PJ-4c pinned four
> snapshot fixtures in `tests/transpiler/fixtures/`:
> `dead_ancilla_single` (happy path — `qbool t = a | b;` with zero
> readers vanishes), `dead_ancilla_keep_read` (reject — reader
> present), `dead_ancilla_keep_chain` (reject — `qbool r = t & d;`
> consumes `t`), `dead_ancilla_inside_when` (elimination inside a
> WHEN body). PJ-4d added `examples/dead_ancilla.cpp` plus
> `tests/transpiler/check_example_dead_ancilla.cmake` and its
> `transpiler_example_dead_ancilla_injected` +
> `transpiler_idempotent_example_dead_ancilla` CTests (wired via
> `examples/CMakeLists.txt`). PJ-4e closed the loop with the
> `m12_dead_ancilla_transpiled` / `m12_dead_ancilla_reference`
> namespace pair in `tests/transpiler/test_gate_equivalence.cpp`
> plus `tests/transpiler/fixtures/dead_ancilla_runtime.cpp` and
> `tests/transpiler/fixtures/dead_ancilla_reference.cpp`; the
> reference spells out the body without the dead decl and the
> transpiled path goes through the matcher — byte-identical
> `GateRecord` stream verified (no ancilla allocated on either
> side, proving the elimination is gate-semantics-preserving). A
> regression (tracked as sturm-3dpf) that the PJ-4a zero-reader
> gate fires on the legacy MVP OR gate-equivalence fixtures
> `or_single_runtime.cpp` / `demo_or_circuit()` — which carry
> `qbool tmp = a | b;` without a reader — has been filed for
> follow-up; the canonical fix is to add a `(void)tmp;` reader,
> matching the pattern PJ-3f / PJ-4c fixtures already use.
> Tracked as bd issues sturm-ubfr (PJ-4a), sturm-kih8 (PJ-4b),
> sturm-br65 (PJ-4c), sturm-66a8 (PJ-4d), sturm-347u (PJ-4e),
> sturm-436e (PJ-4f). Next up: PJ-3 (uncompute hoisting).

> **2026-04-17:** PJ-3 (uncompute hoisting) complete. A `qbool`
> whose forward computation is loop-invariant and whose decl sits
> inside a `for` / `while` / `do` body now has its compute hoisted
> above the loop and its uncompute placed after the loop's closing
> brace — emitting one forward + one uncompute per loop, not N.
> PJ-3a added `detail::classify_scope_kind(scope_anchor, ctx)` to
> `transpiler/src/matcher_common.hpp`, walking the parent chain and
> returning `{Function, LoopBody, BranchBody, WhenBody, Other}`
> (WhenBody reuses `is_expansion_of_macro(..., "WHEN")`); no
> `QScope::kind` field was added (per plan decision #7 the walk is
> invoked only from PJ-3 callbacks, keeping every prior snapshot
> byte-identical). PJ-3b added
> `detail::expr_is_loop_invariant(operand_ref, loop_body_anchor,
> ctx)` to the same header: returns true iff `operand_ref.decl_loc`
> sits outside the loop body AND no assignment-shape
> `CXXOperatorCallExpr` / `DeclRefExpr` write to the same name is
> reachable inside the loop body. PJ-3c added
> `clang::SourceLocation hoist_to_override{}` to `QOperation` in
> `transpiler/include/sturm/transpile/qir.hpp` (parallel to
> `insert_before_override`); when valid, `synthesize()` in
> `transpiler/src/uncompute_pass.cpp` moves the forward
> `QReplacement` anchor to BEFORE the loop begin and the uncompute's
> `insert_before` to AFTER the loop end (default-invalid path
> preserves prior snapshots byte-identical). PJ-3d added the new
> module `transpiler/src/matcher_hoist_invariant.cpp` exporting
> `register_hoist_invariant_matcher(finder, unit)`; it iterates
> `unit.scopes`, selects every scope whose
> `detail::classify_scope_kind` is `LoopBody`, scans the decl-
> producing ops (OR / AND / NOT / XOR / compare kinds — *not*
> compound-assign-on-outer-var, which PH-3 already skips) whose
> operands are all loop-invariant (PJ-3b), and rewrites
> `hoist_to_override` to the loop-enclosing scope's close_brace
> with `insert_before_override` set to the loop begin location; a
> defensive `if (op.skip_uncompute) continue;` preserves PH-3
> disjointness. PJ-3e wired `register_hoist_invariant_matcher` into
> `transpiler/src/main.cpp` **after** every Phase A–I matcher,
> after `register_ccnot_fuse_matcher` (PJ-1f), and after
> `register_dead_ancilla_matcher` (PJ-4b), with an ordering-
> invariant comment block documenting why hoisting must run last
> (the scope iteration depends on every other matcher having
> already populated `unit.scopes`). PJ-3f pinned six snapshot
> fixtures in `tests/transpiler/fixtures/`: `hoist_or_out_of_for`
> (happy path, `for` loop), `hoist_and_out_of_while` (happy path,
> `while` loop), `hoist_reject_operand_written_in_loop` (reject —
> loop body writes one of the operands),
> `hoist_reject_branch_not_loop` (reject — `if`-body, not a loop),
> `hoist_nested_loops_inner_only` (only the innermost loop hoists
> to between the two loops), and `hoist_multi_op_same_loop` (pins
> LIFO ordering for multiple hoistable ops — forwards in source
> order pre-loop, uncomputes in reverse source order post-loop).
> PJ-3g added `examples/uncompute_hoisting.cpp` plus
> `tests/transpiler/check_example_uncompute_hoisting.cmake` and its
> `transpiler_example_uncompute_hoisting_injected` +
> `transpiler_idempotent_example_uncompute_hoisting` CTests (wired
> via `examples/CMakeLists.txt`). PJ-3h closed the loop with the
> `m12_hoist_transpiled` / `m12_hoist_reference` namespace pair in
> `tests/transpiler/test_gate_equivalence.cpp` plus
> `tests/transpiler/fixtures/hoist_runtime.cpp` and
> `tests/transpiler/fixtures/hoist_reference.cpp`; the reference
> spells out the hoisted compute + post-loop uncompute manually and
> the transpiled path goes through the matcher — byte-identical
> `GateRecord` stream verified across N loop iterations (both
> emitting ONE forward + ONE uncompute in total, not N). Tracked
> as bd issues sturm-tlx3 (PJ-3a), sturm-grhy (PJ-3b), sturm-9cmt
> (PJ-3c), sturm-5a4a (PJ-3d), sturm-0v9i (PJ-3e), sturm-8cwe
> (PJ-3f), sturm-zso9 (PJ-3g), sturm-djqg (PJ-3h), sturm-9bs4
> (PJ-3i). Next up: Phase K (cleanup — retire legacy runtime
> auto-uncompute now that the transpiler owns every construct).

> **2026-04-17:** Phase J complete. Three IR-level optimizations
> ship on by default: PJ-1 (zero-ancilla fusion — adjacent
> `qbool __t = a & b; x ^= __t;` with a single reader becomes one
> self-adjoint `ccnot_inplace(x, a, b);`, no ancilla), PJ-4
> (dead-ancilla elimination — a qbool-valued decl with zero
> readers is deleted verbatim, no forward gate and no uncompute),
> and PJ-3 (uncompute hoisting — a loop-invariant forward
> computation hoists above the loop with its uncompute placed
> after the loop close, converting N forward/N uncompute into
> 1/1). PJ-2 (peephole gate reordering) was deferred to Phase M on
> the value-gain analysis — it would require alias analysis to
> pay for itself, and PJ-1's adjacent-statement peephole already
> captures the motivating fusion case. Three new shared helpers
> ship in `transpiler/src/matcher_common.hpp`:
> `detail::count_readers_in_scope` (PJ-1a, also consumed by
> PJ-4a), `detail::classify_scope_kind` (PJ-3a), and
> `detail::expr_is_loop_invariant` (PJ-3b). QIR grew two new
> fields on `QOperation` (`hoist_to_override` from PJ-3c, parallel
> to PH-3's `insert_before_override`) and two new
> `std::vector<clang::SourceRange>` members on `QUnit`
> (`fused_stmt_ranges` from PJ-1e, `eliminated_stmt_ranges` from
> PJ-4a) — both probed via PJ-1e's `is_range_covered_by_fused`
> helper so downstream Phase A/E callbacks bail on covered
> ranges. Three new matcher modules ship (registered in the
> documented ordering in `transpiler/src/main.cpp`):
> `matcher_dead_ancilla.cpp` and `matcher_ccnot_fuse.cpp` run
> **before** the Phase A/E bitwise/assign/compound matchers, and
> `matcher_hoist_invariant.cpp` runs **last**, after every Phase
> A–I + PJ-1/PJ-4 matcher. One runtime entry-point added:
> `sturm::ccnot_inplace(qbool&, const qbool&, const qbool&)` in
> `include/sturm/uncompute/uncompute_api.hpp` (impl delegates to
> `primitive_AND` with the carry in-place). Fifteen new snapshot
> fixtures land under `tests/transpiler/fixtures/` (five for
> PJ-1, four for PJ-4, six for PJ-3 covering hoist accept +
> reject + nested + LIFO), three new `examples/*.cpp` with paired
> `check_example_*.cmake` + `_injected` / `_idempotent` CTests,
> and three new `m12_*_transpiled` / `m12_*_reference` namespace
> pairs in `tests/transpiler/test_gate_equivalence.cpp`. One
> regression filed for follow-up: sturm-3dpf (PJ-4a fires on the
> legacy MVP `or_single_runtime.cpp` gate-equivalence fixture —
> the canonical fix is a `(void)tmp;` reader, matching the
> pattern PJ-3f / PJ-4c fixtures already use). Tracked as bd
> issues sturm-2hnb..sturm-s29b (PJ-1a..PJ-1j), sturm-ubfr..
> sturm-436e (PJ-4a..PJ-4f), sturm-tlx3..sturm-9bs4 (PJ-3a..
> PJ-3i). Phase K-3's explicit dependency ("the zero-ancilla
> optimization now lives in the IR pass") is now satisfied —
> the cleanup phase can retire `lazy_expr`'s runtime
> materialization and collapse the destructor gate paths. Next
> up: Phase K (cleanup).

Only after coverage is complete. Deferring these avoids premature optimization and lets us validate correctness before speed.

1. **Zero-ancilla fusion.** The pattern `qbool __t = a & b; x ^= __t; /* __t used once here */` fuses into a single `ccnot_inplace(x, a, b);` call at the IR level, emitting a single CCX with no intermediate qubit. This is the optimization today's `lazy_expr` materialization tries to approximate at runtime — moving it to the IR is cleaner and composes with other rewrites. Implementation lives in the pre-lowering peephole `matcher_ccnot_fuse.cpp`, which fires **before** the Phase E compound-flatten matcher so the `__stu_tN` temporary is never allocated.
2. **Peephole gate reordering.** *Deferred to Phase M stretch.* Commute commuting gates to expose fusion opportunities and cancellations.
3. **Uncompute hoisting.** When a `qbool` is produced inside a loop and uncomputed at the end of the loop body, but the forward computation is loop-invariant, the transpiler may hoist both compute and uncompute out of the loop. This is an optimization; correctness is only affected by getting the hoist conditions right.
4. **Dead-ancilla elimination.** When an intermediate is produced but never consumed (e.g. dead after a classical branch is eliminated), skip the allocation entirely.

---

## Phase K — Cleanup

> **2026-04-19:** Complete. All seven Phase K deliverables are verified landed by PK-audit; the legacy runtime auto-uncompute layer is gone and the transpile path is the only path.
>
> - PK-1 (`when_capture.hpp` / `when_capture_fwd.hpp`): deleted. In-tree citation in `include/sturm/control/when.hpp:32-34` documents the retirement — the intermediate uncompute deferral relied on the retired `uncompute_op` / `qint_base` runtime, and compound WHEN expressions' inverse emission is now the transpiler's responsibility (Phase G `matcher_when_nested` + uncompute free functions).
> - PK-2 (`lazy_expr.hpp` `AndExpr` / `OrExpr`): deleted. Overloaded `operator|` / `operator&` now return an owning `qbool` directly — the zero-ancilla optimization lives in the IR pass (PJ-1 `ccnot_inplace`). In-tree citation in `include/sturm/control/when.hpp:42-43` and in `include/sturm/qtypes/qbool_logic.hpp`.
> - PK-2 (`uncompute_op.hpp` / `uncompute_run.hpp` / `qint_base.hpp`): deleted. The tagged-union branches were already retired incrementally through Phases C/D; Phase K removed the residual headers.
> - PK-4 (`WhenGuard` AND-fold): retired in Phase G (sturm-ewto). The runtime `WhenGuard` in `include/sturm/control/when.hpp` is clean — only a `control_stack` swap remains; the ancilla allocation / CCX construction path is gone.
> - PK-5 (`STURM_AUTO_UNCOMPUTE`): flag gone. The transpile path is the only path; the `#if`-guarded destructor branches have been removed.
> - PK-6 (`STURM_TRANSPILE=OFF` escape hatch): gone. No developer-diagnostic mode was re-added (2026-04-19 user decision) — keeping a qubit-leaking fallback alive would preserve the exact failure mode Phase K was meant to retire.
> - PK-7 (`docs/01_principles.md` revisions): landed. **B1b** (line 47) now reads "AST-based auto-generation is the default"; **B9** (line 68) now reads "Two optimization layers in the default runtime path ... and one global optimization pass at transpile time"; new **B10** (line 70) codifies that uncomputation is a compile-time concern, not a runtime concern — destructors release qubit indices to the pool; they do not emit gates.
>
> Stale-comment sweep landed under PK-stale-comments; harness WHEN-stub sync under PK-harness-whencapture. Next up: Phase M (long-term stretches). Phase L shipped `v0.1.1` in parallel.

> **2026-04-15:** LP1–LP8 landed — lazy OR initializers now rewrite; PRD + plan archived under `docs/archive/`.

Once the transpiler covers every construct the runtime auto-uncompute handled, retire the legacy layer.

1. **Delete** `include/sturm/control/when_capture.hpp` and `when_capture_fwd.hpp`.
2. **Delete** `include/sturm/uncompute/uncompute_op.hpp`, `uncompute_run.hpp`, `qint_base.hpp` (the uncompute-specific parts).
3. **Delete** the `lazy_expr` materialization path (AndExpr / OrExpr). Overloaded `operator|` / `operator&` return owning `qbool` directly, since the zero-ancilla optimization now lives in the IR pass.
4. **Delete** the AND-fold path in `WhenGuard` (Phase G already retired it).
5. **Delete** `STURM_AUTO_UNCOMPUTE`. The transpile path is the only path. Remove the flag and the `#if`-guarded destructor branches.
6. **Update** `docs/01_principles.md`:
   - **B1b** no longer says "may be added later" — AST-based auto-generation *is* the default path.
   - **B9** is revised: "Two optimization layers in the default runtime path; a global optimization pass runs at transpile time."
   - A new principle may be warranted: "**B10.** Uncomputation is a compile-time concern, not a runtime concern. Destructors release qubit indices; they do not emit gates."
7. **Remove** the `STURM_TRANSPILE=OFF` escape hatch, or keep it only as a developer-diagnostic mode with clear warnings that outputs will leak qubits.

Cleanup lands as one or a few focused commits once every construct has a green snapshot test under the transpiler path and a comparison run against the legacy RAII path shows equivalent gate streams.

---

## Phase L — Distribution and one-line install

> **2026-04-19:** Complete. PL-1..PL-7 landed end-to-end, first green release is **`v0.1.1`**, Homebrew tap is populated:
>
> - PL-1 (sturm-flli): `install()` rules + AGPL-3.0 LICENSE + `sturmConfig.cmake` — `cmake --install` exports `bin/sturm-transpile`, `include/sturm/`, and `lib/cmake/sturm/`.
> - PL-2 (sturm-mzfa): `.github/workflows/release.yml` tag-triggered build produces 4 prebuilt tarballs (`x86_64`/`aarch64` × `linux`/`macos`).
> - PL-3 (sturm-bn4y): smoke-test matrix runs each prebuilt tarball on a clean runner without system LLVM and transpiles `tests/smoke/minimal_or.cpp`, asserting `uncompute_or(...)` in the output.
> - PL-4 (sturm-cynu): `packaging/homebrew/sturm-transpile.rb` formula checked into the repo.
> - PL-5 (sturm-e0v3): top-level `README.md` install section — one `brew install` line, one curl fallback.
> - PL-7 (sturm-k5hu): Docker image built from `packaging/docker/Dockerfile` and published to GHCR on each tagged release.
> - PL-6 (sturm-pumi): Source tarball sha256 `b1a6023711beecf29fd67af6b517fffa5b3806c7eaf3ae72aecb4f232ad870e5` committed into `github.com/SoerenWilkening/homebrew-sturm` at `Formula/sturm-transpile.rb` (v0.1.1). Release page: `https://github.com/SoerenWilkening/Sturm_CPP/releases/tag/v0.1.1` — 4 tarballs + SHA256SUMS, all 4 smoke-test matrix legs green, Docker image published to GHCR.
> - Bonus (sturm-8gep): silent macOS transpile breakage fixed — LibTooling could not locate its builtin-headers resource directory, so the parser aborted before main-file translation and every Phase A-I matcher saw zero ops from user code. Now the Clang resource directory is queried via `clang -print-resource-dir` at configure time, baked into the binary, shipped alongside `bin/sturm-transpile` under `lib/clang/<N>/include/`, and injected via an `ArgumentsAdjuster`. Unblocks the Phase B/C/D example-observability ctests and the PL-3 smoke-test matrix on clean Linux/macOS runners with LLVM purged.
>
> **`v0.1.0` is retained as a historical marker only — no release artifacts.** Initial `v0.1.0` push (`d971184`) and four subsequent retags (`99d96ad`, `9dffc7d`, `704f30c`, `0de6a1b`, `4578d29`) each fixed a real but orthogonal workflow issue (`-j` OOM on 2-core runners, retired `macos-13` runner, system-LLVM assert vs. purge, shipped Clang builtin headers, distro-aware resource-dir query). The residual blocker was discovered only after reproducing locally: release.yml's smoke-test invoked `sturm-transpile tests/smoke/minimal_or.cpp` (relative input → `resolve_output_path` in `transpiler/src/io.cpp:16-29` mirrors the subtree into `/tmp/out/tests/smoke/`), but the verify step grepped the flat path `/tmp/out/minimal_or.cpp`. Fix (`8ea4019`, sturm-pumi): pass `${PWD}/tests/smoke/minimal_or.cpp` so the absolute-path branch of `resolve_output_path` collapses to basename. Rather than force-move `v0.1.0` for a sixth time, we cut a clean `v0.1.1` and treat `v0.1.0` as the ledger of what got us here.

The CMake invocation developers use today (`cmake -DSTURM_TRANSPILE=ON -DLLVM_DIR=... -DClang_DIR=...`) is a contributor workflow, not an end-user experience. For adoption the transpiler must install in one or two commands on macOS and Linux.

LLVM is structurally required — any Clang-based source-to-source tool depends on `libclangTooling`, `libclangAST`, and `libLLVMSupport` for semantic C++ parsing — but the dependency can be hidden from end users by moving it into a package manager or a prebuilt binary. The complexity doesn't shrink; it moves off the critical path of a first-time user.

Options, not mutually exclusive:

| Channel | End-user command | How LLVM is hidden |
|---|---|---|
| Homebrew formula (macOS + Linux) | `brew install sturm/tap/sturm-transpile` | Formula declares `depends_on "llvm@17"`; Homebrew resolves it |
| Debian package | `apt install sturm-transpile` | `Depends: libclang-17-dev, llvm-17` |
| Prebuilt static binary (GitHub Releases) | `curl -L .../sturm-transpile-$(uname -sm) -o sturm-transpile && chmod +x` | LLVM statically linked into a ~150–300 MB binary |
| Docker image | `docker run -v $PWD:/w sturm/transpile /w/src.cpp` | LLVM lives inside the image |

**Deliverables:**
- `sturm-transpile.rb` Homebrew formula hosted in a `homebrew-sturm` tap.
- GitHub Actions release workflow producing prebuilt binaries for `x86_64-linux`, `aarch64-linux`, `x86_64-macos`, `aarch64-macos` on each tagged release.
- CI smoke test that runs the prebuilt binary on a clean VM with no system LLVM installed (regression guard against "works on my machine" static-linking bugs and `cl::opt` double-registration, see `transpiler/CMakeLists.txt:77`).
- README install section: one `brew install` line, one `apt install` line, and a curl fallback. Nothing about `LLVM_DIR`.

**Ordering:** this phase runs **in parallel** with Phases B–J and only blocks on the MVP being feature-complete enough to be useful (roughly after Phase E). Packaging infrastructure built early is cheap to maintain; retrofitted late, it delays every release.

---

## Phase M — Long-term stretches

> **2026-04-19:** Phase M item 1 (in-memory transpile) complete; shipped
> as **`v0.1.2`**. The filesystem round-trip has been retired from the
> default build — `add_quantum_executable()` (PM1-5) invokes
> `clang++ -fplugin=$<TARGET_FILE:sturm-transpile-plugin>` and the
> plugin runs the shared `TranspileConsumer` against the AST in memory,
> driving a nested `CompilerInvocation` + `EmitObjAction` over the
> rewritten buffer without ever writing a sibling `.cpp` to disk. The
> legacy dump path is preserved behind `set(STURM_TRANSPILE_MODE "dump")`
> and via `--dump-transpiled=<path>` on the standalone binary (PM1-6);
> both paths converge on `transpiler/src/io.cpp`, so the dump output is
> byte-identical to the plugin's in-memory buffer. Sub-items:
>
> - PM1-0 (sturm-nimr): de-risking spike proving a nested
>   `CompilerInvocation` survives inside a Clang plugin without
>   re-entering `cl::opt` registration.
> - PM1-1..PM1-4: shared `TranspileConsumer` / `plugin.cpp` /
>   `transpile_consumer.cpp` / nested codegen wiring.
> - PM1-5: `cmake/SturmTranspile.cmake` rewrite — plugin mode is the
>   default (`STURM_TRANSPILE_MODE=plugin`), `dump` is opt-in.
> - PM1-6 (sturm-cpzx): `--dump-transpiled=<path>` flag on the
>   standalone `sturm-transpile` binary; mirrors the plugin's `dump-to=`
>   arg into the same `transpiler/src/io.cpp` write path.
> - PM1-7 (sturm-03ou): CTest harnesses audited; all
>   `check_example_*.cmake` fixtures keep using the standalone binary
>   for snapshot generation (decoupled from how examples are compiled);
>   `test_gate_equivalence.cpp` (M12) compiles the `*_transpiled`
>   namespaces via `-fplugin=` in plugin mode. New `plugin_smoke_test`
>   CTest compiles `tests/smoke/minimal_or.cpp` via `clang++ -fplugin=...
>   -c` and asserts the object defines the expected symbols with an
>   `uncompute_or` gate sequence.
> - PM1-8 (sturm-0sc0): `examples/in_memory_transpile.cpp` tutorial
>   exercising Phase E compound + Phase F WHEN-lift + Phase J PJ-1
>   fusion in a single TU, with the paired
>   `tests/transpiler/check_example_in_memory_transpile.cmake`
>   `_injected` / `_idempotent` CTests.
> - PM1-9 (sturm-fu75): README "Getting started" rewritten to the
>   one-line `add_quantum_executable(...)` with `-fplugin` under the
>   hood (contrasted with the two-step legacy build); Homebrew formula
>   bumped to `v0.1.2` with a new `test do` assertion that the plugin
>   `.so` / `.dylib` lands under `Cellar/sturm-transpile/v0.1.2/lib/`;
>   `transpiler/CMakeLists.txt` install rule extended to ship
>   `libsturm-transpile-plugin.{so,dylib}` alongside the driver so the
>   four `release.yml` prebuilt tarballs (x86_64/aarch64 × linux/macos)
>   carry both `bin/sturm-transpile` and
>   `lib/libsturm-transpile-plugin.{so,dylib}`. `docs/01_principles.md`
>   unchanged — B10 ("Inverses are emitted by the transpiler as
>   explicit uncompute_* / ccnot_inplace / invert(routine)(...) calls
>   in the generated source file") survives the shift to in-memory
>   because the "generated source file" now lives in a `MemoryBuffer`;
>   no other principle needed touching. Tag `v0.1.2` triggers the
>   Phase L release workflow; Homebrew bottle installs cleanly on
>   macOS + Linux runners; GHCR Docker image builds.
>
> Remaining items below (diagnostics, transpiler pluginization,
> peephole gate reordering deferred from PJ-2) remain open.

> **2026-04-20:** Phase M item 2 (source maps via `#line` directives)
> complete. Every transpiler-emitted insertion and replacement now
> carries a `#line <N> "<user-file>"` directive so that compiler and
> debugger diagnostics inside transpiled regions cite the user's
> source file and original line number — not a synthesized `__stu_tN`
> temporary line, not `<memory-buffer>`. The shared helper
> `format_line_directive(SourceManager&, SourceLocation) -> std::string`
> (PM2-1) in `transpiler/src/emitter.cpp` returns
> `#line <N> "<file>"\n` via `getPresumedLoc()` (honouring user
> `#line` pragmas) and empty string for invalid / out-of-main-file
> locations. The helper is threaded through every emission site:
> Phase E compound synthesized decls (PM2-2), `uncompute_pass.cpp`
> `render_uncompute` (PM2-3), `QReplacement` strings including PJ-1
> fuse + Phase F WHEN-lift (PM2-4), and PJ-3 hoist attribution using
> the decl's own source location rather than the loop header (PM2-5).
> Idempotency is preserved end-to-end (PM2-6): a transpiled file fed
> back through the transpiler produces a byte-identical buffer
> because the `#line` directives already resolve to the same
> presumed locations on the second pass. Sub-items:
>
> - PM2-1 (sturm-c9qo): `format_line_directive()` helper landed in
>   `transpiler/src/emitter.cpp`.
> - PM2-2 (sturm-kdsv): Phase E compound matcher
>   (`transpiler/src/matcher_qbool_compound.cpp`) prefixes every
>   synthesized `qbool __stu_tN = ...;` decl with the `#line`
>   directive pointing at the original initializer expression.
> - PM2-3 (sturm-tvkv): `uncompute_pass.cpp` `render_uncompute`
>   prefixes every synthesized `uncompute_*(...)` / `invert(...)(...)`
>   / `ccnot_inplace(...)` call with the `#line` directive pointing
>   at the forward op's source location.
> - PM2-4 (sturm-j1by): `QReplacement` string emission carries a
>   `#line` prefix for PJ-1 fuse (`matcher_ccnot_fuse.cpp`) and
>   Phase F WHEN-lift (`matcher_when_lift.cpp`), covering the
>   in-place rewrites that do not go through the uncompute pass.
> - PM2-5 (sturm-19qx): PJ-3 hoist attribution policy — the
>   hoisted forward compute's `#line` cites the original decl's
>   location, not the loop header; the post-loop uncompute cites
>   the same decl location. Pins user-facing "click to jump to
>   source" correctness for the hoist optimization.
> - PM2-6 (sturm-727c): Idempotency verification — the
>   `transpiler_idempotent_example_*` CTests already exercise the
>   two-pass byte-identity invariant, and every Phase E/F/H/I/J
>   idempotency fixture stays green after `#line` emission
>   because the presumed-location lookup is stable across passes.
> - PM2-7 (sturm-bvjm): Snapshot fixtures under
>   `tests/transpiler/fixtures/*.expected.cpp` regenerated with
>   the new `#line` directives. 64 snapshot + 46 idempotency
>   fixtures total; 138/138 transpiler tests green.
> - PM2-8 (sturm-y4kd): New `source_map_diagnostic` CTest pins
>   the user-facing guarantee. `tests/transpiler/fixtures/source_map_diagnostic_input.cpp`
>   carries a deliberate type error inside a transpiled region;
>   `tests/transpiler/check_source_map_diagnostic.cmake` compiles
>   via `clang++ -fplugin=$<TARGET_FILE:sturm-transpile-plugin> -c`
>   and asserts stderr cites `source_map_diagnostic_input.cpp:<user_line>:`
>   — not a synthesized `__stu_tN` line, not `<memory-buffer>`.
>
> Alternative backends (OpenQASM 3 / simulator IRs / custom
> hardware-aware representation) are off the roadmap permanently
> per the 2026-04-20 user decision — the corresponding bullet has
> been removed below.

> **2026-04-21:** Phase M item "Diagnostics" (PM3) complete. Five
> quantum-specific compile-time diagnostic classes now flow through
> `clang::DiagnosticsEngine` instead of raw `fprintf` / silent
> early-returns: WHEN operand mutation (error), quantum→classical in
> branch condition (error), missing adjoint registration (error),
> caller drops returned qbool (warning), and PH-3 outer-var reverse-
> loop upgrade (warning). Shared `DiagContext` scaffold in
> `transpiler/src/diag_context.{hpp,cpp}` owns a reference to the
> active `DiagnosticsEngine` and lazy-caches `getCustomDiagID`
> handles; one per-consumer instance is threaded into each matcher
> registration that emits diagnostics. The standalone
> `bin/sturm-transpile` driver now wires a
> `TextDiagnosticPrinter(llvm::errs(), &DiagOpts)` where it
> previously swallowed diagnostics via `IgnoringDiagConsumer`, so
> the five classes surface identically whether the user is building
> through the Clang plugin (`plugin.cpp`) or the standalone driver.
> `docs/01_principles.md` unchanged — PM3 reports on detection paths
> that were already present at transpile time (outer-var guard) or
> newly added as advisory AST walkers (classes 1–4); B1b, B6, B9,
> B10, and P9 are untouched because no runtime path, no ancilla
> lifetime, no IR pass, and no adjoint registration surface moves.
> Sub-items:
>
> - PM3-0 (sturm-5btt.1): `DiagContext` scaffold at
>   `transpiler/src/diag_context.{hpp,cpp}` — struct holds
>   `clang::DiagnosticsEngine&`; empty `report_*` stubs for all
>   five classes; lazy `getOrRegister(level, fmt)` helper; wired
>   into `TranspileConsumer` via `ci.getDiagnostics()` and added to
>   both the `sturm-transpile` executable and
>   `sturm-transpile-plugin` library source lists.
> - PM3-1 (sturm-5btt.2): `transpiler/src/main.cpp` swaps
>   `IgnoringDiagConsumer` for `TextDiagnosticPrinter(llvm::errs(),
>   &DiagOpts)` so the standalone driver surfaces the same stderr
>   Clang-formatted lines the plugin already produces; pinned by
>   new `transpile_diagnostic_surfaces` CTest against
>   `tests/transpiler/fixtures/standalone_diag_surfaces_input.cpp`.
> - PM3-2 (sturm-5btt.3): PH-3 outer-var warning routed through
>   `DiagContext`. `transpiler/src/matcher_outer_var_guard.cpp`
>   drops its raw `fprintf` in favour of
>   `diag.report_outer_var_mutation(...)` with
>   `DiagnosticIDs::Warning`; pinned by
>   `plugin_diagnostic_outer_var_guard` CTest plus
>   `tests/transpiler/check_outer_var_guard_diagnostic.cmake`.
> - PM3-3 (sturm-5btt.4): Class 3 — missing adjoint registration
>   (error). `transpiler/src/matcher_user_routine.cpp` computes
>   `outputs_mask` from parameter types before the registry check;
>   on miss with non-zero mask it calls
>   `diag.report_missing_adjoint(...)` instead of silently skipping.
>   Positive/negative fixtures
>   `tests/transpiler/fixtures/missing_adjoint_diagnostic_input.cpp`
>   + `missing_adjoint_clean.cpp` exercised by
>   `plugin_diagnostic_missing_adjoint_fires` /
>   `plugin_diagnostic_missing_adjoint_clean` CTests via
>   `tests/transpiler/check_missing_adjoint_diagnostic.cmake`.
> - PM3-4 (sturm-5btt.5): Class 1 — WHEN operand mutation (error).
>   New matcher
>   `transpiler/src/matcher_when_operand_mutation.cpp` anchors on
>   the WHEN middle `IfStmt` (same predicate as
>   `matcher_when_lift.cpp`), collects every DRE to a qbool/qint
>   VarDecl in the `materialize_when` argument into a
>   `SmallPtrSet`, then walks the body for
>   `CXXOperatorCallExpr` (`=`, `^=`, `+=`, …) / `UnaryOperator`
>   (`++`, `--`) hits whose LHS resolves into the set. Pinned by
>   `plugin_diagnostic_when_operand_mutation_fires` /
>   `plugin_diagnostic_when_operand_mutation_clean` CTests via
>   `tests/transpiler/check_when_operand_mutation_diagnostic.cmake`.
> - PM3-5 (sturm-5btt.6): Class 2 — quantum→classical in branch
>   condition (error). New matcher
>   `transpiler/src/matcher_quantum_to_classical_cond.cpp` binds
>   explicit `cxxStaticCastExpr` / `cStyleCastExpr` /
>   `cxxFunctionalCastExpr` from qbool/qint_t to a bool or
>   integral target; callback walks `ASTContext::getParents`
>   (skipping `ImplicitCastExpr` / `ParenExpr` /
>   `ExprWithCleanups`) to confirm the cast sits in the `getCond()`
>   slot of an `IfStmt` / `WhileStmt` / `DoStmt` /
>   `ConditionalOperator` and is NOT itself inside a WHEN
>   expansion (via `detail::is_expansion_of_macro`). Pinned by
>   `plugin_diagnostic_quantum_to_classical_cond_fires` /
>   `plugin_diagnostic_quantum_to_classical_cond_clean` CTests via
>   `tests/transpiler/check_quantum_to_classical_cond_diagnostic.cmake`.
> - PM3-6 (sturm-5btt.7): Class 4 — caller drops returned qbool
>   (warning). New matcher
>   `transpiler/src/matcher_dropped_quantum_return.cpp` binds
>   `callExpr` returning qbool / qint_t whose parent is a
>   `compoundStmt` (or `exprWithCleanups` whose parent is a
>   `compoundStmt`), reporting with `DiagnosticIDs::Warning` so
>   compilation continues. `(void)call()` survives as an explicit
>   discard because the `CStyleCastExpr` to `void` breaks the
>   parent chain. Pinned by
>   `dropped_quantum_return_diagnostic` (positive) plus
>   `dropped_quantum_return_diagnostic_bound` and
>   `dropped_quantum_return_diagnostic_void_cast` (negatives) via
>   `tests/transpiler/check_dropped_quantum_return_diagnostic.cmake`.
>
> Remaining Phase M items below (transpiler pluginization, peephole
> gate reordering deferred from PJ-2) remain open.

> **2026-04-21:** Phase M item "Transpiler pluginization" (PM4)
> complete. Third-party shared libraries can now register additional
> AST rewrite matchers + uncompute rules alongside the in-tree ones
> via two paths: runtime `dlopen` driven by a CMake `PLUGINS <abs.so>`
> argument (primary), and link-time bake-in through the
> `STURM_REGISTER_PLUGIN(TypeName)` macro (fallback). ABI is
> versioned by symbol name (`sturm_register_plugin_v1`) and gated on
> matching `CLANG_VERSION_STRING` at load time; plugins are leaked to
> process exit (no `dlclose` in v1). Scope is matchers + uncompute
> rules only — diagnostics (PM3) and IR passes stay host-private.
> Dogfooded by migrating Phase B PB-1..PB-4 (`matcher_qint_const.cpp`)
> through the Registry API with byte-identical snapshot output;
> `docs/01_principles.md` unchanged — the PM4 surface does not touch
> B1b (transpile-time inverse registration remains the default), B6
> (ancilla RAII is unaffected), B9 (the one-global-transpile-time pass
> is unchanged — plugins contribute matchers, not passes), or B10
> (`uncompute_*` emission discipline is preserved by the new
> `case QOpKind::PLUGIN:` renderer arm). Sub-items:
>
> - PM4-0 (sturm-4oyr.1): implementation plan skeleton at
>   `docs/implementation_plan_transpiler_phase_m_pm4.md` — scope, ABI
>   discipline, `RTLD_LOCAL | RTLD_NOW` rationale, Clang-version gate,
>   Meyer's-singleton ordering, in-tree → runtime → link-time drain.
> - PM4-1 (sturm-4oyr.2): public plugin header
>   `transpiler/include/sturm/transpile/plugin_api.hpp` exposes
>   `Registry::register_matcher` / `register_op`, `StaticRegistrar`,
>   the `STURM_REGISTER_PLUGIN(TypeName)` macro, and the
>   `extern "C" void sturm_register_plugin_v1(Registry&)` entry-point
>   declaration plus the `STURM_PLUGIN_DEFINE_CLANG_VERSION()` helper.
> - PM4-2 (sturm-4oyr.3): Registry implementation in
>   `transpiler/src/plugin_registry.cpp`. Owned by `TranspileConsumer`
>   (one per translation unit, not global), with collision detection
>   on both `register_matcher` names and `register_op` kind_ids, plus
>   Meyer's-singleton `registrars()` / `runtime_registrars()` vectors
>   so link-time and runtime paths share a single drain order.
> - PM4-3 (sturm-4oyr.4): `QOpKind::PLUGIN` variant and
>   `plugin_kind_id` field added to
>   `transpiler/include/sturm/transpile/qir.hpp`; new
>   `case QOpKind::PLUGIN:` in `transpiler/src/uncompute_pass.cpp`'s
>   `render_uncompute` consults `Registry::find_render_fn(kind_id)`.
>   Registry threaded through `synthesize(unit, sm, registry)` as a
>   non-owning reference — NOT a global singleton (conflicts with
>   PM1-4's nested `CompilerInvocation`). Empty `plugin_kind_id` on
>   every non-plugin op keeps 138 existing snapshot fixtures
>   byte-identical.
> - PM4-4 (sturm-4oyr.5): `transpiler/src/plugin.cpp` `ParseArgs`
>   accepts `load=<path.so>` plugin-arg; on parse it
>   `dlopen(RTLD_LOCAL | RTLD_NOW)`, reads the plugin's
>   `sturm_plugin_clang_version_v1()` and refuses to register on
>   mismatch with the host's `CLANG_VERSION_STRING`, resolves
>   `sturm_register_plugin_v1`, and queues a wrapper into
>   `runtime_registrars()`. No `dlclose` — handle leaks to process
>   exit per plan §7.
> - PM4-5 (sturm-4oyr.6): `cmake/SturmTranspile.cmake`
>   `add_quantum_executable` gains a `PLUGINS <path> [<path> ...]`
>   multi-value keyword. For each path it appends
>   `-Xclang -plugin-arg-sturm-transpile -Xclang load=<abs-path>` per
>   source; the `cc1` spelling is mandatory because the driver splits
>   `-fplugin-arg-` at the first hyphen. `PLUGINS` is plugin-mode
>   only — passing it under `-DSTURM_TRANSPILE_MODE=dump` is a
>   configuration error.
> - PM4-6 (sturm-4oyr.7): dogfood migration — all four Phase B
>   matchers in `transpiler/src/matcher_qint_const.cpp` now register
>   through `Registry::register_matcher("sturm.pb.add_assign_const",
>   ...)` (and sub/mul/div variants) via a link-time `StaticRegistrar`
>   instead of direct `TranspileConsumer` constructor calls in
>   `transpile_consumer.cpp`. QOpKinds `ADD_ASSIGN_CONST`/`SUB`/`MUL`/
>   `DIV` unchanged; snapshots stay byte-identical.
> - PM4-7 (sturm-4oyr.8): demo plugin at
>   `examples/plugin_demo/plugin_demo.cpp` + `CMakeLists.txt` builds
>   as MODULE library `sturm-pm4-demo-plugin`. Implements a novel
>   self-inverse tag op via `register_op` with kind_id
>   `pm4.demo.tag`; render function emits
>   `pm4_demo_tag_inverse(q)`. Exercises
>   `register_op` + new-kind + render-fn end-to-end without touching
>   any production matcher.
> - PM4-8 (sturm-4oyr.9): `transpiler/tests/pm4_smoke_dlopen.{cpp,cmake}`
>   CTest — user code compiled via
>   `add_quantum_executable(... PLUGINS $<TARGET_FILE:sturm-pm4-demo-plugin>)`;
>   asserts the rewritten output contains the
>   `pm4_demo_tag_inverse(` sentinel. Pins the full
>   dlopen → `register_op` → `render_uncompute` pipeline.
> - PM4-9 (sturm-4oyr.10): three CTests under `transpiler/tests/`:
>   `pm4_dogfood_snapshot.{cpp,cmake}` asserts Phase B
>   `a += k` / `-=` / `*=` / `/=` fixtures stay byte-identical
>   post-migration; `pm4_missing_plugin_errors.{cpp,cmake}` asserts
>   `PLUGINS /nonexistent.so` fails with the exact
>   `sturm-transpile plugin: failed to dlopen` stderr line;
>   `pm4_two_plugins_independent.{cpp,cmake}` asserts that two
>   plugins registering the same `kind_id` hard-error at the second
>   registration.
> - PM4-10 (sturm-4oyr.11): link-time path —
>   `STURM_PM4_LINK_DEMO` CMake option (default OFF) in
>   `transpiler/CMakeLists.txt` statically links the demo plugin's
>   registrar into `sturm-transpile-plugin` via
>   `transpiler/src/pm4_link_demo_registrar.cpp`'s
>   `STURM_REGISTER_PLUGIN` invocation.
>   `transpiler/tests/pm4_smoke_linktime.{cpp,cmake}` compiles the
>   same source as smoke 1 *without* a `PLUGINS` argument against a
>   `-DSTURM_PM4_LINK_DEMO=ON` configuration and checks the same
>   rewritten-output sentinel, proving the Meyer's-singleton
>   link-time path is independent of dlopen.
> - PM4-11 (sturm-4oyr.12): this roadmap update.
>
> Remaining Phase M item below (peephole gate reordering, deferred
> from PJ-2) stays open; its prerequisite alias analysis does not
> exist yet.

> **2026-04-22:** Phase M item "Peephole gate reordering" (PM5)
> complete. The last Phase M stretch item has landed, closing the
> PJ-2 deferral from Phase J (2026-04-16): the transpiler now commutes
> commuting gates across unrelated ops to expose additional PJ-1
> fusion opportunities beyond what the adjacent-statement peephole
> caught. The value-gain question is resolved by a QIR-level alias
> footprint API (`sturm::transpile::detail::footprint()` +
> `may_overlap()`) that gives `qbool`, `qint_t<W>`, and constant-index
> `BitProxy` references a `{name, decl_loc, bit_range}` summary
> precise enough to prove two ops touch disjoint qubits. A single
> new matcher `register_peephole_reorder_matcher` walks each
> `QScope.ops` in source order and, for every adjacent triple
> `(A: qbool __t = a & b; B; C: x ^= __t;)` where `B` has a disjoint
> footprint from both `A`'s result and `C`'s reads/writes, emits one
> `QReplacement` that moves `B` past `C` — seeding PJ-1's fuse
> condition in the same pass (no iteration-to-fixed-point). The
> matcher is registered LAST in `transpile_consumer.cpp`, after
> `register_hoist_invariant_matcher`, so reorder never crosses a
> hoist boundary; it also refuses to commute across `QOpKind::PLUGIN`
> ops (operand-opacity sharp edge) and across
> `classify_scope_kind != {LoopBody, Function}` (WHEN/branch
> control-flow-sensitive). `docs/01_principles.md` B9 prose
> (PM5-10) extends the enumeration to
> `(PJ-1 fusion, PJ-3 hoisting, PJ-4 dead-ancilla elimination, PM5
> peephole reordering)` — count remains "one global optimization
> pass at transpile time" because PM5 adds a fourth matcher within
> the existing pass, not a new pass. B1b, B6, B10, and P9 unchanged:
> reorder only shuffles existing forward rewrites and their adjoints,
> introducing no new runtime path, no new auto-inversion, no ancilla
> lifetime change, and no implicit adjoints. Sub-items:
>
> - PM5-0 (sturm-u655.1): implementation plan skeleton at
>   `docs/implementation_plan_transpiler_phase_m_pm5.md` — scope,
>   QubitFootprint model, API surface in `sturm::transpile::detail`,
>   footprint extraction rules (qbool / qint_t<W> / BitProxy const
>   peel / conservative fallback), ordering vs Phase J, sub-task
>   table, sharp-edge enumeration.
> - PM5-1 (sturm-u655.2): header
>   `transpiler/include/sturm/transpile/alias.hpp` declares
>   `struct QubitFootprint{std::string name; clang::SourceLocation
>   decl_loc; struct{unsigned lo, hi;} bit_range;}` plus free
>   functions `footprint(const QOperandRef&, const ASTContext&)` and
>   `may_overlap(const QubitFootprint&, const QubitFootprint&)` in
>   the `sturm::transpile::detail` namespace (internal matcher
>   helper, not public plugin API).
> - PM5-2 (sturm-u655.3): `transpiler/src/alias.cpp` implements
>   `footprint()` for bare `qbool` DREs (width 1, `bit_range={0,1}`)
>   and bare `qint_t<W>` DREs (W extracted via
>   `Type::getAsCXXRecordDecl()` →
>   `ClassTemplateSpecializationDecl::getTemplateArgs()`). Dependent
>   types return a universal sentinel footprint (`name=""`) so
>   `may_overlap` reports true against everything. `may_overlap()`
>   returns false iff names differ OR decl locations differ OR
>   bit ranges are disjoint.
> - PM5-3 (sturm-u655.4): `transpiler/src/alias.cpp` extends
>   `footprint()` with the `BitProxy` subscript shape: peels
>   `operator[](k)`, calls `Expr::EvaluateAsInt(Result, ASTContext)`
>   on the index, and on success sets
>   `bit_range={k, k+1}`. On failure (non-constant index) falls
>   back conservatively to `bit_range={0, W}` using the parent
>   `qint_t<W>`'s width.
> - PM5-4 (sturm-u655.5):
>   `transpiler/tests/test_alias_footprint.cpp` — ten unit cases
>   pin the API (two qbools disjoint, qbool self-overlap, `qint_t<4>`
>   full-width vs bit-ranges, `a[0]` vs `a[1]` disjoint, dynamic
>   `a[i]` conservative, same-const-index same-qint overlap,
>   cross-name no overlap, `f(q, q)` same-decl overlap,
>   dependent-type universal-footprint overlap).
> - PM5-5 (sturm-u655.6):
>   `transpiler/src/matcher_peephole_reorder.cpp` exports
>   `register_peephole_reorder_matcher(MatchFinder&, QUnit&, const
>   ASTContext&)`. Walks each `QScope.ops`, detects the
>   `(qbool __t = a & b;) B (x ^= __t;)` triple, tests disjointness
>   via `detail::may_overlap()`, and emits one `QReplacement` per
>   firing. Declaration added to
>   `transpiler/include/sturm/transpile/matcher.hpp`; source added
>   to both `sturm-transpile` and `sturm-transpile-plugin` targets
>   in `transpiler/CMakeLists.txt`.
> - PM5-6 (sturm-u655.7): wired into
>   `transpiler/src/transpile_consumer.cpp` AFTER
>   `register_hoist_invariant_matcher`; ordering-invariant comment
>   block amended from "LAST: hoist" to "LAST: hoist, then reorder"
>   documenting the single-pass seed-the-fuse-condition design.
>   Gated on `A.hoist_to_override.isInvalid() && B.* && C.*` (no
>   commuting across hoist boundary); consults
>   `unit.fused_stmt_ranges` / `unit.eliminated_stmt_ranges` via
>   PJ-1e's `is_range_covered_by_fused` probe; refuses to reorder
>   across `classify_scope_kind != {LoopBody, Function}` and across
>   `QOpKind::PLUGIN` ops.
> - PM5-7 (sturm-u655.8):
>   `transpiler/tests/test_matcher_peephole_reorder.cpp` — six unit
>   fixtures (`disjoint-qbool` fires, `disjoint-qint-bits` fires,
>   `overlap-rejected` refused, `BitProxy-const` fires,
>   `BitProxy-nonconst` refused, `plugin-op-refused` refused).
> - PM5-8 (sturm-u655.9): snapshot fixtures
>   `tests/transpiler/fixtures/reorder_*.{cpp,expected.cpp}` (six
>   cases mirroring PM5-7) pinned by a new `pm5_reorder_snapshot`
>   CTest; gate-equivalence `m12_reorder_transpiled` /
>   `m12_reorder_reference` namespace pair in
>   `tests/transpiler/test_gate_equivalence.cpp` against
>   `tests/transpiler/fixtures/reorder_runtime.cpp` +
>   `reorder_reference.cpp` asserts byte-identical `GateRecord`
>   streams between matcher-driven reorder and manual reference.
> - PM5-9 (sturm-u655.10): `examples/peephole_reorder.cpp`
>   demonstrates the before/after — a disjoint-footprint `B`
>   separating `qbool __t = a & b;` and `x ^= __t;` collapses to
>   `ccnot_inplace(x, a, b); B;` under reorder+fuse. Paired CTests
>   `transpiler_example_peephole_reorder_injected` +
>   `transpiler_idempotent_example_peephole_reorder` via
>   `tests/transpiler/check_example_peephole_reorder.cmake`, wired
>   through `examples/CMakeLists.txt` mirroring the PJ-1h
>   (`zero_ancilla_fusion.cpp`) and PJ-3g
>   (`uncompute_hoisting.cpp`) examples.
> - PM5-10 (sturm-u655.11): `docs/01_principles.md` B9 prose
>   extended to enumerate `PM5 peephole reordering` alongside
>   `PJ-1 fusion`, `PJ-3 hoisting`, and `PJ-4 dead-ancilla
>   elimination`. B1b, B6, B10, and P9 re-read and confirmed
>   untouched.
> - PM5-11 (sturm-u655.12): this roadmap update.
>
> Phase M is now complete — no remaining stretch items below.

Lower priority, tracked for visibility.

- **In-memory transpile.** *Complete — shipped in v0.1.2 (2026-04-19).* Skip the filesystem round-trip: transpile and feed directly to Clang's codegen. Keep the sibling-file emit as a `--dump-transpiled` option for debugging.
- **Diagnostics.** *Complete — shipped 2026-04-21.* Five quantum-specific compile-time diagnostic classes routed through `clang::DiagnosticsEngine`: WHEN operand mutation (error), quantum→classical in branch condition (error), missing adjoint registration (error), caller drops returned qbool (warning), and PH-3 outer-var reverse-loop upgrade (warning). Shared `DiagContext` scaffold in `transpiler/src/diag_context.{hpp,cpp}` (PM3-0) threaded into each matcher that emits diagnostics; standalone driver upgraded from `IgnoringDiagConsumer` to `TextDiagnosticPrinter` in `transpiler/src/main.cpp` (PM3-1). Detection paths: `transpiler/src/matcher_outer_var_guard.cpp` (PM3-2), `transpiler/src/matcher_user_routine.cpp` (PM3-3), `transpiler/src/matcher_when_operand_mutation.cpp` (PM3-4), `transpiler/src/matcher_quantum_to_classical_cond.cpp` (PM3-5), and `transpiler/src/matcher_dropped_quantum_return.cpp` (PM3-6). Pinned by `transpile_diagnostic_surfaces`, `plugin_diagnostic_outer_var_guard`, `plugin_diagnostic_missing_adjoint_{fires,clean}`, `plugin_diagnostic_when_operand_mutation_{fires,clean}`, `plugin_diagnostic_quantum_to_classical_cond_{fires,clean}`, and `dropped_quantum_return_diagnostic{,_bound,_void_cast}` CTests via `tests/transpiler/check_{standalone_diag_surfaces,outer_var_guard_diagnostic,missing_adjoint_diagnostic,when_operand_mutation_diagnostic,quantum_to_classical_cond_diagnostic,dropped_quantum_return_diagnostic}.cmake`.
- **Source maps.** *Complete — shipped 2026-04-20.* Transpiler-emitted insertions and replacements carry `#line <N> "<user-file>"` directives via the shared `format_line_directive()` helper in `transpiler/src/emitter.cpp` (PM2-1), threaded through Phase E compound decls (PM2-2), `render_uncompute` (PM2-3), `QReplacement` strings for PJ-1 fuse + Phase F WHEN-lift (PM2-4), and PJ-3 hoist attribution (PM2-5). Idempotency preserved end-to-end (PM2-6); 64 snapshot + 46 idempotency fixtures regenerated under `tests/transpiler/fixtures/*.expected.cpp` (PM2-7); user-facing guarantee pinned by the new `source_map_diagnostic` CTest with `tests/transpiler/fixtures/source_map_diagnostic_input.cpp` + `tests/transpiler/check_source_map_diagnostic.cmake` (PM2-8).
- **Transpiler pluginization.** *Complete — shipped 2026-04-21.* User-defined rewrite rules registered with the transpiler, for ecosystem libraries that introduce new quantum operations. Public header `transpiler/include/sturm/transpile/plugin_api.hpp` (PM4-1) exposes `Registry::register_matcher` / `register_op`, the `STURM_REGISTER_PLUGIN(TypeName)` link-time macro, and the `extern "C" void sturm_register_plugin_v1(Registry&)` runtime-dlopen entry point; Registry implementation in `transpiler/src/plugin_registry.cpp` (PM4-2) owns per-consumer state plus Meyer's-singleton `registrars()` / `runtime_registrars()` vectors. Core wiring: `QOpKind::PLUGIN` + `plugin_kind_id` field in `transpiler/include/sturm/transpile/qir.hpp` with dispatch in `transpiler/src/uncompute_pass.cpp` (PM4-3); `load=<path>` ParseArgs + `dlopen(RTLD_LOCAL | RTLD_NOW)` + Clang-version gate in `transpiler/src/plugin.cpp` (PM4-4); `PLUGINS` argument in `cmake/SturmTranspile.cmake`'s `add_quantum_executable` (PM4-5). Dogfooded by migrating Phase B PB-1..PB-4 matchers (`transpiler/src/matcher_qint_const.cpp`) through the Registry API (PM4-6); demo plugin at `examples/plugin_demo/plugin_demo.cpp` + CMakeLists (PM4-7); five CTests pin the surface end-to-end (`pm4_smoke_dlopen`, `pm4_dogfood_snapshot`, `pm4_missing_plugin_errors`, `pm4_two_plugins_independent`, `pm4_smoke_linktime` — PM4-8..PM4-10). ABI is unstable across sturm minor versions; rebuild plugins per release.
- **Peephole gate reordering** *Complete — shipped 2026-04-22.* Commute commuting gates across unrelated ops to expose PJ-1 fusion opportunities beyond what the adjacent-statement peephole captures. The PJ-2 deferral (2026-04-16) is closed: the prerequisite alias analysis landed as `sturm::transpile::detail::footprint()` / `may_overlap()` in `transpiler/src/alias.cpp` (PM5-1..PM5-3), exercised by `tests/transpiler/test_alias_footprint.cpp` (PM5-4), and consumed by the new `register_peephole_reorder_matcher` in `transpiler/src/matcher_peephole_reorder.cpp` (PM5-5). The matcher is registered LAST in `transpile_consumer.cpp` (after hoist) and gated on disjoint-footprint + hoist-boundary + PLUGIN-opacity checks (PM5-6). Pinned by `tests/transpiler/test_matcher_peephole_reorder.cpp` (PM5-7), six snapshot fixtures under `tests/transpiler/fixtures/reorder_*.cpp` + the `m12_reorder_transpiled` / `m12_reorder_reference` gate-equivalence pair (PM5-8), and `examples/peephole_reorder.cpp` (PM5-9).

---

## Phase N — Rotation & preparation primitives

> **2026-04-22:** Phase N complete. PN-1..PN-8 landed end-to-end,
> closing the P5-primitives-2-and-3 gap (`q.theta += d` / `q.theta -= d`
> and `q.phi += d` / `q.phi -= d` amplitude and phase rotations) and
> the P5-primitive-1 diagnostic hole (`qbool(p)` preparation inside
> uncompute-eligible scopes). Until Phase N the transpiler passed
> `ThetaProxy::operator+=` / `PhiProxy::operator+=` through untouched
> because no matcher anchored on them — user programs that called
> `q.theta += 0.5` shipped un-uncomputed adjoints because no
> QOperation was created for the M8 pass to invert. Phase N wires the
> four new rotation QOpKinds through a matcher modeled on Phase B's
> `QIntAssignConstCallback<Kind>` template (one AST level deeper
> because `theta()` / `phi()` return proxy objects), with inline
> inverse emission in `uncompute_pass.cpp` (Phase B pattern — no
> `uncompute_api.hpp` helper needed because runtime
> `ThetaProxy::operator-=` / `PhiProxy::operator-=` are self-dual at
> `include/sturm/qtypes/qint_core.hpp:305,372`). The `qbool(p)` prep
> case is handled diagnostically rather than via a new `QOpKind::PREP`:
> preparation is a CP map whose adjoint would be a discard /
> measurement, violating P9 ("Routines are invertible by explicit
> adjoint"), so PN-5 raises a Warning through `DiagContext` when the
> VarDecl sits inside a WHEN body or compound-expression intermediate.
> Multi-control rotations stay out of scope — Phase G AND-fold
> collapses nested WHEN chains to depth ≤ 1 before the rotation
> matcher ever sees them, per B5's "uncontrolled and singly-controlled
> forms only". `docs/01_principles.md` unchanged: Phase N adds
> matchers to the one global pass (B9) but introduces no new
> optimization layer; B1b, B6, B10, and P9 are untouched because no
> runtime path, no ancilla lifetime, no IR pass, and no new adjoint
> registration surface moves. Sub-items:
>
> - PN-1 (sturm-f8jt): four new `QOpKind` entries in
>   `transpiler/include/sturm/transpile/qir.hpp` after the Phase C
>   compound-assign block —
>   `THETA_ADD_ASSIGN_CONST`, `THETA_SUB_ASSIGN_CONST`,
>   `PHI_ADD_ASSIGN_CONST`, `PHI_SUB_ASSIGN_CONST`. Each carries one
>   operand whose `.name` is the verbatim RHS source text captured
>   via `Lexer::getSourceText`; `op.result` is the qint LHS.
>   `dump()` switch in `transpiler/src/qir.cpp` extended with four
>   new arms mirroring the `ADD_ASSIGN_CONST` format. Four golden
>   cases in `tests/transpiler/test_qir_dump.cpp`.
> - PN-2 (sturm-f5cz): rotation matcher
>   `transpiler/src/matcher_rotation.cpp` exporting
>   `register_theta_add_matcher` / `register_theta_sub_matcher` /
>   `register_phi_add_matcher` / `register_phi_sub_matcher` via
>   `transpiler/include/sturm/transpile/matcher.hpp`. AST anchor is
>   one level deeper than Phase B because `theta()` / `phi()` return
>   proxies —
>   `cxxMemberCallExpr(on(declRefExpr(<qint_t>).bind("lhs")),
>   callee(cxxMethodDecl(hasName("theta"))))` — and arg-1 is a plain
>   `Expr` (no `CXXConstructExpr` peel because the RHS is `double`).
>   Dogfooded through the PM4 Registry API via
>   `STURM_REGISTER_PLUGIN(PNRotationPlugin)` in an anonymous
>   namespace (PM4-6 pattern).
> - PN-3 (sturm-ft5t): eight snapshot fixtures under
>   `tests/transpiler/fixtures/` —
>   `theta_add_const.{cpp,expected.cpp}`,
>   `theta_sub_const.{cpp,expected.cpp}`,
>   `phi_add_const.{cpp,expected.cpp}`,
>   `phi_sub_const.{cpp,expected.cpp}`. Each pins the sign-flipped
>   inverse (`+=` forward → `-=` inverse, symmetric for theta-sub /
>   phi-add / phi-sub). Wired into `tests/transpiler/CMakeLists.txt`
>   via the existing `run_snapshot.cmake` + `check_idempotent.cmake`
>   harness. Green under
>   `ctest -R 'transpiler_snapshot_(theta|phi)_(add|sub)_const'`.
> - PN-4 (sturm-9c2e): four new `case` arms in
>   `transpiler/src/uncompute_pass.cpp` after `DIV_ASSIGN_CONST`
>   emitting the inline sign-flipped inverse (Phase B style — no
>   `uncompute_api.hpp` free function): `THETA_ADD_ASSIGN_CONST` →
>   `    <lhs>.theta() -= <rhs>;` and symmetric for the three
>   siblings. Each carries a `format_line_directive()` prefix so the
>   PM2 source-map guarantee is preserved.
> - PN-5 (sturm-8h3r): prep-diagnostic matcher
>   `transpiler/src/matcher_qbool_prep.cpp` anchoring on
>   `varDecl` with qbool type + single-arg `cxxConstructExpr`
>   initializer. Guards against classical-bool init (arg-0 is
>   `CXXBoolLiteralExpr` or RHS type is `isBooleanType()`). Walks
>   outward from the enclosing scope checking WHEN-macro-expansion
>   (`detail::is_expansion_of_macro`) or
>   compound-expression-intermediate (`unit.scopes[i].ops` owns the
>   VarDecl); on hit calls
>   `diag.report_prep_in_uncompute_scope(loc, name)`. Silent at
>   top-level function body. `DiagContext` extended with a 6th
>   `report_*` method in `transpiler/src/diag_context.{hpp,cpp}`:
>   Warning severity, format
>   `"qbool %0 preparation in uncompute-eligible scope has no adjoint (P9)"`.
>   Registered in `transpile_consumer.cpp` AFTER PH-3
>   `matcher_outer_var_guard`.
> - PN-6 (sturm-k08h): prep-diagnostic fixtures
>   `tests/transpiler/fixtures/qbool_prep_in_when.cpp` (must emit
>   warning) and
>   `tests/transpiler/fixtures/qbool_prep_top_level_clean.cpp` (must
>   NOT emit, identical input/output). New
>   `tests/transpiler/check_qbool_prep_diagnostic.cmake` cloned from
>   `check_outer_var_guard_diagnostic.cmake`. Two new CTests —
>   `plugin_diagnostic_qbool_prep_fires` and
>   `plugin_diagnostic_qbool_prep_clean` — wired in
>   `tests/transpiler/CMakeLists.txt`.
> - PN-7 (sturm-uw33): `examples/rotations.cpp` exercising all four
>   rotation directions plus one depth-1 WHEN-guarded rotation (B5
>   multi-control guardrail — depth ≥ 2 forbidden by construction).
>   Paired CTests `transpiler_example_rotations_injected` +
>   `transpiler_idempotent_example_rotations` via
>   `tests/transpiler/check_example_rotations.cmake` (cloned from
>   `check_example_peephole_reorder.cmake`), wired through
>   `examples/CMakeLists.txt` mirroring the PJ-1h / PJ-3g / PM5-9
>   example pattern.
> - PN-8 (sturm-dy1f): m12 gate-equivalence pair —
>   `tests/transpiler/fixtures/rotations_runtime.cpp` (transpile
>   input) + `tests/transpiler/fixtures/rotations_reference.cpp`
>   (hand-written reference with explicit inverses in place).
>   Namespace pair `m12_rotations_transpiled` /
>   `m12_rotations_reference` added to
>   `tests/transpiler/test_gate_equivalence.cpp`. Asserts
>   byte-identical counter-mode `GateRecord` streams (Ry, Rz, CRy,
>   CRz). Green under `ctest -R 'gate_equivalence.*rotations'`.
> - PN-9 (sturm-qy12): this roadmap update.
>
> Phase N is now complete — rotation and preparation primitives land
> the P5 items 1–3 surface end-to-end.

---

## Phase Q — Signature normalization (automatic adjoint synthesis, P9a + P9b)

> **2026-04-23:** Phase Q scoped. Tracked as bd epic `sturm-5kgu`
> with sub-items Q-0..Q-4 (`sturm-5kgu.1`..`sturm-5kgu.5`). Parent
> design in `docs/prd_automatic_adjoint_synthesis.md` §5.1 item 1
> (signature normalization) and §4.1 (writing a reversible user
> routine), and `docs/implementation_plan_automatic_adjoint_synthesis.md`
> §2.2; this stub reserves the roadmap slot ahead of implementation.
> Phase Q is the second phase of the automatic-adjoint-synthesis
> cluster (P → Q → R → S) — it consumes Phase P's validated
> `[[sturm::reversible]]` routines (`sturm-z2e8`) and produces the
> out-param-canonical shape that Phase R's `adjoint_emitter`
> (`sturm-88d7`) targets for `__<fn>_adj` placement.
>
> **Mission.** Land the two passes that resolve P9a (return-style
> vs out-param-style equivalence) and P9b (constness declares input
> mutability) before adjoint emission runs. For every
> `[[sturm::reversible]]` forward routine whose body is a single
> `return <expr>;` returning a quantum type, synthesize a sibling
> out-param twin whose source text writes into a non-const reference
> parameter via `^=` (or the appropriate dual). For every reversible
> routine — return-style or out-param-style — walk the parameter
> list and reject signatures that are ambiguous about input
> mutation (non-const by-value quantum types mutated in the body,
> `const` references mutated in the body, pass-by-pointer shapes
> that belong as references). Diagnostics reuse Phase P's `DiagContext`
> extension family (P-D), so the user-visible surface stays
> symmetric with the five `report_reversible_*` methods that
> shipped under `sturm-z2e8.4`.
>
> **Normalization shape (PRD §4.1 + §5.1 item 1).** For a forward
> routine `qbool marked(qint x, int T) { return x >= T; }`, the
> transpiler attaches to the synthesis registry entry the
> out-param twin's synthesized source:
>
> ```cpp
> void __marked_out(qbool& __stu_out, qint x, int T) {
>     __stu_out ^= (x >= T);
> }
> ```
>
> The original return-style function remains callable as an
> ordinary C++ expression (unchanged at the AST level). Phase R's
> adjoint placement targets `__marked_out` — never the return-style
> source — so the `WHEN(marked(x, 10))` sugar (PRD §4.3) expands
> via the out-param twin on the second PM3 pass and lifts cleanly
> through the existing Phase F matcher. Pure string production
> this phase: no IR mutation, no uncompute integration, no new
> matchers against primitive kinds. The registry entry is the
> only output artifact.
>
> **Constness rejects (PRD §5.3 pre-condition).** PRD §5.3's
> parameter capture contract — "computed args from mutated inputs
> are rejected unless the value is captured at dispatch time" —
> requires the transpiler to know, before R runs, which parameters
> the forward body is allowed to mutate. P9b's answer is constness:
> `qint x` is read-only, `qint&` is mutable, `const qint&` is
> read-only. Q-B's matcher enforces the three-way contract at the
> definition site via Phase P's `DiagContext`:
>
> | Reject reason | Example | Diagnostic |
> |---|---|---|
> | non-const by-value of a quantum type that the body mutates | `void f(qint x) { x ^= 1; }` | `report_reversible_sig_byval_mutated` |
> | `const` reference whose body mutates | `void f(const qint& x) { x ^= 1; }` | `report_reversible_sig_const_ref_mutated` |
> | pass-by-pointer of a quantum type | `void f(qint* x)` | `report_reversible_sig_pointer_param` |
>
> Positive signatures — `qint x` read-only, `qint&` mutated,
> `const qint&` read-only, out-param-canonical `void f(qbool& a,
> qint x)` — pass through silently and become the input shape R-C
> consumes.
>
> **Return-style restriction.** Q-A only synthesizes out-param
> twins for single-return bodies: a `[[sturm::reversible]]` routine
> whose body contains more than one `return` statement is rejected
> via `report_reversible_sig_multi_return`. PRD §9 (open question 3,
> mutation tracking granularity) locked pointwise semantics;
> multi-return bodies violate that contract because the out-param
> target depends on which return fires. Single-expression returns
> of concrete quantum types are the only shape that round-trips
> through Q-A + R-A with bit-exact adjoint byte-compare under m12
> (plan §9 "out of scope"). `std::variant` / `std::optional` of
> quantum types remain out of scope for P-S (plan §9), deferred
> to a follow-up epic.
>
> Sub-items:
>
> - Q-0 (sturm-5kgu.1): this roadmap stub.
> - Q-1 (sturm-5kgu.2): new module
>   `transpiler/src/return_to_out_param.{hpp,cpp}` (≤ 260 impl /
>   110 hdr, plan §2.2 Q-A). For reversible routines returning a
>   quantum type via single `return <expr>;`, synthesize the
>   out-param twin's source text. Pure string production — no IR
>   mutation, no uncompute integration. Output attached to the
>   synthesis registry entry (`sturm-z2e8.3`) for Phase R's
>   `adjoint_emitter` to consume. Unit-tested through
>   `transpiler/tests/test_return_to_out_param.cpp` with golden-file
>   comparison against
>   `tests/transpiler/fixtures/return_to_out_param_{1..4}.expected.cpp`.
> - Q-2 (sturm-5kgu.3): new matcher
>   `transpiler/src/matcher_reversible_signature.cpp` (≤ 240 impl,
>   plan §2.2 Q-B). AST matcher over the parameter list of every
>   `[[sturm::reversible]]` routine. Rejects non-const by-value
>   quantum types mutated in the body, `const` references mutated
>   in the body, and pass-by-pointer shapes. Emits through Phase P's
>   `DiagContext` extension family — reuses the P-D shape so no new
>   diagnostic harness is added. Dogfooded through the PM4 Registry
>   API via `STURM_REGISTER_PLUGIN` in an anonymous namespace
>   (PM4-6 pattern — same shape PN-2 and R-3 follow).
> - Q-3 (sturm-5kgu.4): four positive + two negative fixtures
>   under `tests/transpiler/fixtures/` —
>   `reversible_return_style_qbool.{cpp,expected.cpp}`,
>   `reversible_return_style_qint.{cpp,expected.cpp}`,
>   `reversible_out_param_canonical.{cpp,expected.cpp}` (already
>   canonical shape — Q-A twin step is a no-op),
>   `return_to_out_param_{1..4}.expected.cpp` (golden companions
>   for the Q-A unit test), `reversible_sig_const_ref_mutated.cpp`
>   + `.expected.diag` (Q-B reject), `reversible_sig_multi_return.cpp`
>   + `.expected.diag` (multi-return reject). Wired into
>   `tests/transpiler/CMakeLists.txt` via the existing
>   `run_snapshot.cmake` + `check_idempotent.cmake` +
>   `check_reversible_diagnostic.cmake` harness (P-5 cloned the
>   diagnostic harness from `check_qbool_prep_diagnostic.cmake`;
>   Q-3 reuses it). Green under
>   `ctest -R 'transpiler_snapshot_reversible_(return_style|out_param_canonical|sig_.*)'`
>   and `ctest -R 'reversible_sig_.*_diagnostic'`, capped at `-j6`.
> - Q-4 (sturm-5kgu.5): roadmap completion blockquote replacing
>   this stub.
>
> **Principles touched.** P9a is the load-bearing principle —
> return-style and out-param-style are two surfaces over the same
> semantic shape, and the transpiler normalizes to one before
> adjoint emission runs (out-param is canonical because R-A's
> reverse-statement-order walk needs a mutable target to write
> the adjoint into). P9b is the second load-bearer — constness
> declares input mutability, so the Q-B matcher can reject
> ambiguous signatures at the definition site rather than
> leaving the ambiguity to propagate into R's adjoint body.
> P9c / B11 are not touched this phase (they land in R / S).
> P9d is touched only insofar as Q-B extends Phase P's
> diagnostic surface with three new `report_reversible_sig_*`
> methods — the validation pass itself (P-C) stays the source
> of truth for body-level rejects. B10 unchanged (uncomputation
> stays a compile-time concern). B9 unchanged: Q adds matchers
> + a pure-string emitter to the one global pass; no new
> optimization layer. `docs/01_principles.md` unchanged by Phase Q.
>
> **Out of scope.** Multi-return bodies, `std::variant` /
> `std::optional` return types, and template reversible routines
> are deferred to follow-up epics (plan §9). Synthesis inside
> class member functions — `this`-capture introduces a fourth
> parameter-capture rule beyond PRD §5.3 — is also out of scope
> for P-S. Bodies rejected by Q-B (bad signature) never reach R
> or S; bodies rejected by Phase P's P-C validation (measurement,
> classical I/O, unregistered callee, `while`-loop,
> quantum-dependent condition) never reach Q. Recursion is
> deferred to the follow-up epic captured in plan §0 Q4.
> Cross-TU synthesis stays in the follow-up bucket (plan §9).

---

## Phase R — Straight-line adjoint emission (automatic adjoint synthesis, P9c)

> **2026-04-23:** Phase R scoped. Tracked as bd epic `sturm-88d7`
> with sub-items R-0..R-7 (`sturm-88d7.1`..`sturm-88d7.8`). Parent
> design in `docs/prd_automatic_adjoint_synthesis.md` §5.1 and
> `docs/implementation_plan_automatic_adjoint_synthesis.md` §2.3;
> this stub reserves the roadmap slot ahead of implementation.
> Phase R is the third phase of the automatic-adjoint-synthesis
> cluster (P → Q → R → S) — it consumes Phase P's validated
> `[[sturm::reversible]]` routines (`sturm-z2e8`) and Phase Q's
> normalized out-param shape (`sturm-5kgu`), and it produces the
> straight-line adjoint body that Phase S (`sturm-ha2k`) later
> wraps in reversed-iteration loop headers for `ForStmt` nodes.
>
> **Mission.** For every `[[sturm::reversible]]` forward routine
> that passes P-C validation (P9d) and Q-B constness enforcement,
> emit a sibling function `__<fn>_adj` whose body is the reverse
> statement-order adjoint of the forward body, and append a
> `STURM_REGISTER_ADJOINT(fn, __<fn>_adj)` line so the PI-1
> matcher picks up the new pair on the two-pass transpile's
> second pass (PM3 slot). Loops are out of scope — they retain
> Phase H PH-3's `skip_uncompute=true` diagnostic until Phase S
> replaces it with real reversal. Phase R is the "straight-line"
> subset: every statement that Phase B/C/D/E/N can already invert
> inline in `uncompute_pass.cpp` is now invertible at the
> routine-definition site as a named, registered adjoint.
>
> **Emission shape (PRD §5.1 item 3).** For a forward routine
> `void marked(qbool& a, qint x, int T) { a ^= (x >= T); }`, the
> transpiler appends to the same TU:
>
> ```cpp
> void __marked_adj(qbool& a, qint x, int T) {
>     a ^= (x >= T);   // self-dual; distinct symbol preserves
>                      // the audit trail per P9c.
> }
> STURM_REGISTER_ADJOINT(marked, __marked_adj);
> ```
>
> P9c guarantees the distinct `__<fn>_adj` symbol even for
> self-inverse bodies — this keeps the PI-4 audit name-match
> complete and survives future refactors that may promote a
> self-dual body to a non-self-dual one.
>
> **Parameter capture (PRD §5.3).** Literal and const args
> (`theta += 0.3`) reuse the same literal in the adjoint
> (`theta -= 0.3`) so gate-stream byte-compare holds. Computed
> args from unchanged inputs re-evaluate in the adjoint; computed
> args from mutated inputs are rejected unless Phase Q-A/Q-B has
> already hoisted the expression into a `const` local before the
> forward op. B11 is load-bearing: forward-emitted values are
> reused verbatim, so no floating-point drift appears between
> synthesized and hand-written adjoints.
>
> Sub-items:
>
> - R-0 (sturm-88d7.1): this roadmap stub.
> - R-1 (sturm-88d7.2): new module
>   `transpiler/src/adjoint_emitter.{hpp,cpp}` (≤ 370 impl / 130
>   hdr, plan §2.3 R-A). Walks a validated, normalized routine
>   body in reverse statement order; for each statement, calls
>   the existing `render_uncompute` from `uncompute_pass.cpp:429`
>   to produce adjoint source text. Emits as a sibling function
>   `__<fn>_adj` into the rewriter's buffer, not inline. Reuses
>   the Phase B/C/D/E/N inline-inverse arms — no new per-kind
>   code paths. Unit-tested through `test_matcher_harness.hpp`
>   against hand-built IR covering each primitive kind's adjoint
>   render.
> - R-2 (sturm-88d7.3): new module
>   `transpiler/src/auto_register_emitter.{hpp,cpp}` (≤ 120 impl
>   / 60 hdr, plan §2.3 R-B). Appends
>   `STURM_REGISTER_ADJOINT(fn, __<fn>_adj);` after each
>   synthesized adjoint. Must produce the exact AST shape PI-1's
>   matcher already consumes, so the second PM3 transpile pass
>   picks up the registration without a new matcher. Golden-file
>   test pins the emitted text.
> - R-3 (sturm-88d7.4): top-level driver matcher
>   `transpiler/src/matcher_reversible_drive.cpp` (≤ 180 impl,
>   plan §2.3 R-C). Orchestrates R-1 + R-2 for each
>   `[[sturm::reversible]]` FD that passed Phase P's P-C
>   validation and Phase Q's Q-B constness. Single entry point
>   callable from `transpile_consumer.cpp`. Dogfooded through
>   the PM4 Registry API via `STURM_REGISTER_PLUGIN` in an
>   anonymous namespace (PM4-6 pattern — same shape PN-2
>   followed).
> - R-4 (sturm-88d7.5): six snapshot fixtures under
>   `tests/transpiler/fixtures/` —
>   `reversible_body_xor.{cpp,expected.cpp}`,
>   `reversible_body_and.{cpp,expected.cpp}`,
>   `reversible_body_compound.{cpp,expected.cpp}`,
>   `reversible_body_rotation_theta.{cpp,expected.cpp}`,
>   `reversible_body_rotation_phi.{cpp,expected.cpp}`,
>   `reversible_body_mixed.{cpp,expected.cpp}`. Each pairs a
>   forward routine marked `[[sturm::reversible]]` with the
>   expected synthesized adjoint + `STURM_REGISTER_ADJOINT`
>   line, compile-ready. Wired into
>   `tests/transpiler/CMakeLists.txt` via the existing
>   `run_snapshot.cmake` + `check_idempotent.cmake` harness.
>   Green under
>   `ctest -R 'transpiler_snapshot_reversible_body_.*'`.
> - R-5 (sturm-88d7.6): m12 gate-equivalence pair —
>   `tests/transpiler/fixtures/reversible_synth_transpiled.cpp`
>   (uses `[[sturm::reversible]]`) and
>   `tests/transpiler/fixtures/reversible_synth_reference.cpp`
>   (hand-written adjoint via existing `STURM_REGISTER_ADJOINT`).
>   Namespace pair `m12_reversible_synth_transpiled` /
>   `m12_reversible_synth_reference` added to
>   `tests/transpiler/test_gate_equivalence.cpp`. Asserts
>   byte-identical counter-mode `GateRecord` streams between
>   synthesized and hand-written adjoints (PN-8 pattern lifted
>   to synthesized routines). Green under
>   `ctest -R 'gate_equivalence.*reversible_synth'`.
> - R-6 (sturm-88d7.7): roundtrip test in
>   `tests/test_invert.cpp`. Forward then synthesized adjoint on
>   a prepared state equals identity; gate counter equals zero
>   at scope exit (plan §5). Covers the P9c audit guarantee that
>   the generated buffer name-matches `__<fn>_adj`.
> - R-7 (sturm-88d7.8): roadmap completion blockquote replacing
>   this stub.
>
> **Principles touched.** P9c is the load-bearing principle —
> the transpiler always emits a distinct `__<fn>_adj` even for
> self-inverse bodies (preserves the PI-4 audit trail, survives
> refactors, costs nothing at runtime because the call is
> name-matched and inlined by the host compiler). B11 is
> reused at the statement level (reverse statement order,
> forward-emitted values reused) but loop-iteration reversal is
> deferred to Phase S. P9a/P9b interact through Phase Q's
> normalization — R consumes the out-param twin, not the
> return-style source. B10 unchanged (uncomputation stays a
> compile-time concern). `docs/01_principles.md` unchanged: R
> adds matchers + emitters to the one global pass (B9) but
> introduces no new optimization layer.
>
> **Out of scope.** `ForStmt` bodies retain the Phase H PH-3
> `skip_uncompute=true` diagnostic until Phase S (`sturm-ha2k`)
> ships; any `[[sturm::reversible]]` routine containing a loop
> passes P-C validation but its Phase R adjoint body carries the
> same diagnostic the inline uncompute path emits today. Bodies
> rejected by Phase P validation (measurement, classical I/O,
> unregistered callee, `while`-loop, quantum-dependent condition)
> never reach R. Recursion is deferred to a follow-up epic (plan
> §0 Q4). Cross-TU synthesis stays in the follow-up bucket
> (plan §9).

> **2026-04-23:** Phase R complete. R-0..R-7 landed end-to-end,
> delivering the straight-line half of automatic adjoint synthesis
> (P9c): the transpiler can now walk a validated, normalized
> `[[sturm::reversible]]` routine body in reverse statement order,
> call `render_uncompute` for each primitive, emit the result as a
> sibling `__<fn>_adj` function into the rewriter buffer, and append
> a `STURM_REGISTER_ADJOINT(fn, __<fn>_adj);` line in the exact AST
> shape PI-1's matcher consumes on the PM3 second pass. The
> emitter-pair is exercised against hand-built IR covering every
> primitive kind (Phase B/C/D/E/N) plus six compile-ready snapshot
> fixtures and an m12 gate-equivalence pair; a dedicated roundtrip
> test pins the forward-then-synthesized-adjoint identity + zero
> gate counter guarantee. Loops retain the Phase H PH-3
> `skip_uncompute=true` diagnostic — Phase S (`sturm-ha2k`) replaces
> that with real loop reversal. `docs/01_principles.md` unchanged:
> R adds matchers + emitters to the one global pass (B9); no new
> optimization layer, no new runtime path, no ancilla-lifetime
> change. Sub-items:
>
> - R-0 (sturm-88d7.1): Phase R roadmap stub (the scope blockquote
>   above) in `docs/roadmap_transpiler_post_mvp.md`.
> - R-1 (sturm-88d7.2): `adjoint_emitter` module —
>   `transpiler/src/adjoint_emitter.{hpp,cpp}` walks the normalized
>   routine body in reverse statement order and calls
>   `render_uncompute` (via a new 25-line declaration block added to
>   `transpiler/include/sturm/transpile/uncompute_pass.hpp`) to
>   produce adjoint source for each primitive; emits the result as a
>   sibling `__<fn>_adj` into the rewriter buffer. Output attaches
>   to a new synthesis-registry entry in
>   `transpiler/src/synthesis_registry.{hpp,cpp}` so R-3's driver can
>   drain per-routine adjoint text in one place. Unit-tested via
>   `transpiler/tests/test_adjoint_emitter.cpp` (720 LOC) against
>   hand-built IR covering each Phase B/C/D/E/N primitive kind's
>   adjoint render, wired through
>   `transpiler/tests/CMakeLists.txt`.
> - R-2 (sturm-88d7.3): `auto_register_emitter` module —
>   `transpiler/src/auto_register_emitter.{hpp,cpp}` appends
>   `STURM_REGISTER_ADJOINT(fn, __<fn>_adj);` after each synthesized
>   adjoint in the exact AST shape PI-1's matcher already consumes
>   on the PM3 second transpile pass (no new matcher needed on the
>   registration side). Golden-file tested via
>   `transpiler/tests/test_auto_register_emitter.cpp`, wired through
>   `transpiler/tests/CMakeLists.txt`.
> - R-3 (sturm-88d7.4): top-level driver matcher —
>   `transpiler/src/matcher_reversible_drive.{hpp,cpp}` orchestrates
>   R-1 + R-2 per `[[sturm::reversible]]` FD, exposing a single
>   entry point callable from `transpile_consumer.cpp`. Dogfooded
>   through the PM4 Registry API via `STURM_REGISTER_PLUGIN` in an
>   anonymous namespace (PM4-6 pattern). Unit-tested through
>   `transpiler/tests/test_matcher_reversible_drive.cpp` (634 LOC).
>   **Wiring deferred:** the matcher is implemented and unit-pinned
>   but NOT yet registered from
>   `transpiler/src/transpile_consumer.cpp` — R-3 intentionally
>   stops at the driver layer because it must gate on P-C validation
>   (Phase P `sturm-z2e8`) and Q-B constness enforcement (Phase Q
>   `sturm-5kgu`), neither of which has landed yet. Once those
>   validators ship, a follow-up task registers the driver in the
>   consumer after Phase P's `matcher_reversible_validate` and Phase
>   Q's `matcher_reversible_signature` have run — no change to the
>   R-3 module itself is required.
> - R-4 (sturm-88d7.5): six snapshot fixtures under
>   `tests/transpiler/fixtures/` —
>   `reversible_body_xor.{cpp,expected.cpp}`,
>   `reversible_body_and.{cpp,expected.cpp}`,
>   `reversible_body_compound.{cpp,expected.cpp}`,
>   `reversible_body_rotation_theta.{cpp,expected.cpp}`,
>   `reversible_body_rotation_phi.{cpp,expected.cpp}`,
>   `reversible_body_mixed.{cpp,expected.cpp}`. Each pairs a forward
>   `[[sturm::reversible]]` routine with the expected synthesized
>   adjoint + `STURM_REGISTER_ADJOINT` line, compile-ready. Wired
>   into `tests/transpiler/CMakeLists.txt` via the existing
>   `run_snapshot.cmake` + `check_idempotent.cmake` harness. Green
>   under
>   `ctest -R 'transpiler_snapshot_reversible_body_.*'`.
> - R-5 (sturm-88d7.6): m12 gate-equivalence pair —
>   `tests/transpiler/fixtures/reversible_synth_runtime.cpp`
>   (transpile input using `[[sturm::reversible]]`) +
>   `tests/transpiler/fixtures/reversible_synth_reference.cpp`
>   (hand-written reference with explicit adjoint via
>   `STURM_REGISTER_ADJOINT`). Namespace pair
>   `m12_reversible_synth_transpiled` /
>   `m12_reversible_synth_reference` added to
>   `tests/transpiler/test_gate_equivalence.cpp`, asserting
>   byte-identical counter-mode `GateRecord` streams between
>   synthesized and hand-written adjoints (PN-8 pattern lifted to
>   synthesized routines). Wired through
>   `tests/transpiler/CMakeLists.txt`. Green under
>   `ctest -R 'gate_equivalence.*reversible_synth'`.
> - R-6 (sturm-88d7.7): roundtrip test added to
>   `tests/test_invert.cpp`. Forward then synthesized adjoint on a
>   prepared state equals identity; gate counter equals zero at
>   scope exit. Covers the P9c audit guarantee that the generated
>   buffer name-matches `__<fn>_adj`.
> - R-7 (sturm-88d7.8): this roadmap update.
>
> Green-light: 294/294 CTests passing under
> `CTEST_PARALLEL_LEVEL=6 ctest --parallel 6` at closure
> (2026-04-23). Phase R is now complete — straight-line adjoint
> emission lands; loop reversal continues in Phase S
> (`sturm-ha2k`).

---

## Phase S — Loop reversal (automatic adjoint synthesis, B11)

> **2026-04-23:** Phase S scoped. Tracked as bd epic `sturm-ha2k`
> with sub-items S-0..S-7 (`sturm-ha2k.1`..`sturm-ha2k.8`). Parent
> design in `docs/prd_automatic_adjoint_synthesis.md` §5.2 and
> `docs/implementation_plan_automatic_adjoint_synthesis.md` §2.4;
> this stub reserves the roadmap slot ahead of implementation.
> Phase S is the final phase of the automatic-adjoint-synthesis
> cluster (P → Q → R → S) — it depends on Phase R's
> `adjoint_emitter` (`sturm-88d7`) because the per-iteration adjoint
> body is produced by the straight-line emitter; Phase S only
> reverses the loop header and schedules the descent.
>
> **Mission.** Replace the Phase H PH-3 `skip_uncompute=true` flag
> set by `transpiler/src/matcher_outer_var_guard.cpp` with real
> loop reversal for `ForStmt` nodes that live inside a
> `[[sturm::reversible]]` routine body. Outside synthesis context
> (ad-hoc inline uncompute), the existing PH-3 warn-and-skip
> behavior is preserved byte-for-byte — Phase S is additive, not a
> rewrite of PH-3.
>
> **Reversal table (PRD §5.2).**
>
> | Forward | Adjoint |
> |---|---|
> | `for (int i = 0; i < N; ++i) body` | `for (int i = N-1; i >= 0; --i) body_adj` |
> | `for (int i = 0; i < N; i += s) body` | `for (int i = ((N-1)/s)*s; i >= 0; i -= s) body_adj` |
> | `while (cond) body` | rejected — unbounded trip count is not invertible without a manual adjoint |
>
> Nested loops reverse innermost-first (standard LIFO lifted to
> loop structure). Pointwise mutation tracking (plan §0 Q3):
> each iteration un-mutates exactly, not bulk end-of-routine.
>
> Sub-items:
>
> - S-0 (sturm-ha2k.1): this roadmap stub.
> - S-1 (sturm-ha2k.2): new module
>   `transpiler/src/loop_reversal.{hpp,cpp}` (≤ 320 impl / 80 hdr,
>   plan §2.4 S-A). Consumes a `ForStmt` AST node belonging to a
>   reversible routine body; produces the reversed-iteration adjoint
>   loop header and recursively descends into the body via Phase R's
>   `adjoint_emitter`. Stride-aware bound computation for `i += s`.
>   Rejects non-canonical for-shape (non-trivial init, compound
>   condition, side-effecting increment).
> - S-2 (sturm-ha2k.3): edit to
>   `transpiler/src/matcher_outer_var_guard.cpp` (≤ +40 LOC, plan
>   §2.4 S-B). Suppress `skip_uncompute=true` when the enclosing FD
>   is `[[sturm::reversible]]` — instead mark the op for
>   loop-reversal handling by S-1. Outside synthesis context, the
>   current PH-3 diagnostic + skip behavior is preserved; PH-3's
>   existing fixtures (`for_outer_xor_reject`,
>   `for_mixed_xor_reject`) stay green.
> - S-3 (sturm-ha2k.4): three positive loop fixtures under
>   `tests/transpiler/fixtures/` — `reversible_loop_ripple.cpp`,
>   `reversible_loop_bit_reversal.cpp`,
>   `reversible_loop_adder_carry.cpp`, each paired with
>   `.expected.cpp` goldens and wired through
>   `run_snapshot.cmake` + `check_idempotent.cmake`.
> - S-4 (sturm-ha2k.5): two negative loop fixtures —
>   `reversible_while_loop.cpp` (must emit
>   `report_reversible_while_loop` via Phase P's DiagContext
>   extension) and `reversible_qdep_trip_count.cpp` (trip count
>   reads a qint; diagnostic via Phase P-D). Harness via a cloned
>   `check_reversible_diagnostic.cmake` from Phase P.
> - S-5 (sturm-ha2k.6): three m12 gate-equivalence pairs in
>   `tests/transpiler/test_gate_equivalence.cpp` —
>   `m12_reversible_loop_ripple_{transpiled,reference}`,
>   `m12_reversible_loop_bit_reversal_{transpiled,reference}`,
>   `m12_reversible_loop_adder_{transpiled,reference}`. Each asserts
>   byte-identical `GateRecord` streams between the synthesized
>   adjoint and a hand-written reference (PN-8 pattern lifted to
>   loops).
> - S-6 (sturm-ha2k.7): three roundtrip tests in
>   `tests/test_invert.cpp` — forward then synthesized adjoint on a
>   prepared state equals identity; gate counter equals zero at
>   scope exit (plan §5).
> - S-7 (sturm-ha2k.8): roadmap completion blockquote replacing
>   this stub.
>
> Unit-table coverage for `loop_reversal` (S-1): stride 1, stride 2,
> negative stride, zero-trip-count edge case (plan §8 risk register).
> Nested-loop innermost-first is exercised by the adder-carry
> fixture (S-3, S-5, S-6).
>
> **Principles touched.** B11 is the load-bearing principle —
> adjoint synthesis reverses statement order AND loop iteration
> order; gate parameters reuse forward-emitted values (no drift).
> B10 unchanged (uncomputation stays a compile-time concern). P4
> WHEN-immutability preserved: loop reversal does not touch WHEN
> control expressions. No new optimization layer, so B9 unchanged.
>
> **Out of scope.** `while` loops and quantum-dependent trip counts
> are diagnosed, not synthesized (PRD §5.2). Recursion is deferred
> to a follow-up epic (plan §0 Q4). Cross-TU synthesis stays in the
> follow-up bucket (plan §9).

---

## Principle Check

The predicted Phase K principle revisions have landed in `docs/01_principles.md` (PK-7, 2026-04-17). Numbering is stable — existing B1..B9 citations remain valid:

- **B1b** has landed — the "may be added later as opt-in mechanisms" clause has been replaced. The principle now reads: "Macro-based or AST-based auto-generation is the default: the transpiler pass at compile time registers inverse operations for every construct. Runtime auto-inversion has been retired."
- **P9** ("Routines are invertible by explicit adjoint") remains load-bearing. The transpiler is a **consumer** of manual adjoints, not a replacement for them.
- **B9** has landed — the "no global pass" clause has been replaced. The principle now reads: "Two optimization layers in the default runtime path (inlined dispatch, classicality specialization) and one global optimization pass at transpile time." The body enumerates the three Phase J rewrites (PJ-1 fusion, PJ-3 hoisting, PJ-4 dead-ancilla elimination).
- **B6** ("Ancillas and control temporaries are scope-bound via C++ RAII") stays true — RAII still owns qubit lifetime; it just no longer owns *uncomputation*.
- **B10** is new — added after B9 to codify that uncomputation is a compile-time concern: "Inverses are emitted by the transpiler as explicit uncompute_* / ccnot_inplace / invert(routine)(...) calls in the generated source file. Destructors release qubit indices to the pool; they do not emit gates. Ancilla scope (B6) still uses C++ RAII, but scope exit and uncomputation are now separate concerns."

---

## Ordering Rationale

Phases A–D widen the simple cases (single intermediate, named temporaries). Phase E introduces the first real analysis (expression decomposition). Phases F–H integrate control flow. Phase I closes the loop on user-defined routines. Phase J adds optimization. Phase K retires the old system. Phase L (distribution) runs in parallel with B–J once the MVP is usable. Phase M is stretch.

Each phase is a checkpoint: the transpiler's coverage strictly grows, snapshot tests strictly accumulate, and the legacy system remains as a reference implementation until Phase K.
