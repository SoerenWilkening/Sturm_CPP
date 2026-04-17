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

Only after coverage is complete. Deferring these avoids premature optimization and lets us validate correctness before speed.

1. **Zero-ancilla fusion.** The pattern `qbool __t = a & b; x ^= __t; /* __t used once here */` fuses into a single `ccnot_inplace(x, a, b);` call at the IR level, emitting a single CCX with no intermediate qubit. This is the optimization today's `lazy_expr` materialization tries to approximate at runtime — moving it to the IR is cleaner and composes with other rewrites. Implementation lives in the pre-lowering peephole `matcher_ccnot_fuse.cpp`, which fires **before** the Phase E compound-flatten matcher so the `__stu_tN` temporary is never allocated.
2. **Peephole gate reordering.** *Deferred to Phase M stretch.* Commute commuting gates to expose fusion opportunities and cancellations.
3. **Uncompute hoisting.** When a `qbool` is produced inside a loop and uncomputed at the end of the loop body, but the forward computation is loop-invariant, the transpiler may hoist both compute and uncompute out of the loop. This is an optimization; correctness is only affected by getting the hoist conditions right.
4. **Dead-ancilla elimination.** When an intermediate is produced but never consumed (e.g. dead after a classical branch is eliminated), skip the allocation entirely.

---

## Phase K — Cleanup

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

Lower priority, tracked for visibility.

- **In-memory transpile.** Skip the filesystem round-trip: transpile and feed directly to Clang's codegen. Keep the sibling-file emit as a `--dump-transpiled` option for debugging.
- **Alternative backends.** The IR is backend-agnostic. A second emitter could target OpenQASM 3, a simulator-specific IR, or a custom hardware-aware representation.
- **Diagnostics.** Quantum-specific compile errors ("operand modified inside its own WHEN", "qbool escapes its scope without explicit measurement or uncompute"). Requires the liveness analysis from Phase H to be mature.
- **Source maps.** Preserve `#line` directives in the generated file so that compiler and debugger diagnostics point at the user's source, not the generated temporary names.
- **Transpiler pluginization.** User-defined rewrite rules registered with the transpiler, for ecosystem libraries that introduce new quantum operations. Only after the core is stable.
- **Peephole gate reordering** *(deferred from Phase J PJ-2, 2026-04-16).* Commute commuting gates across unrelated ops to expose fusion opportunities and cancellations beyond what PJ-1's adjacent-statement peephole captures. Requires alias analysis for the value-gain ratio to pay its way; dropped from Phase J on that basis.

---

## Principle Check

The current principles in `docs/01_principles.md` align with this plan with one required revision:

- **B1b** already anticipates the transpiler ("*AST-based auto-generation may be added later as opt-in mechanisms*"). After Phase K, "opt-in" becomes "default."
- **P9** ("Routines are invertible by explicit adjoint") remains load-bearing. The transpiler is a **consumer** of manual adjoints, not a replacement for them.
- **B9** is the one that needs rewording. "No global pass" becomes "the transpiler *is* a global pass, by design."
- **B6** ("Ancillas and control temporaries are scope-bound via C++ RAII") stays true — RAII still owns qubit lifetime; it just no longer owns *uncomputation*.

---

## Ordering Rationale

Phases A–D widen the simple cases (single intermediate, named temporaries). Phase E introduces the first real analysis (expression decomposition). Phases F–H integrate control flow. Phase I closes the loop on user-defined routines. Phase J adds optimization. Phase K retires the old system. Phase L (distribution) runs in parallel with B–J once the MVP is usable. Phase M is stretch.

Each phase is a checkpoint: the transpiler's coverage strictly grows, snapshot tests strictly accumulate, and the legacy system remains as a reference implementation until Phase K.
