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

Non-self-inverse. Today's runtime emits forward + inverse back-to-back as a stopgap (`uncompute_op.hpp:284`); this phase replaces that with a proper ancilla-qubit comparator adjoint.

Covered ops:
- `c = (a == b);` → `uncompute_eq(c, a, b);`
- `c = (a != b);`, `<`, `<=`, `>`, `>=` → analogous

**Runtime work:** implement `uncompute_eq`, `uncompute_lt`, etc., using the existing primitive comparator circuits' adjoints. The library routines shipped with manual adjoints per **P9**; these inverse functions are thin wrappers that invoke those adjoints with the right qubit bindings.

**Deliverables:** library adjoint wiring, free functions, matchers, snapshots.

---

## Phase E — Compound expressions with named intermediates

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

Only after coverage is complete. Deferring these avoids premature optimization and lets us validate correctness before speed.

1. **Zero-ancilla fusion.** The pattern `qbool __t = a & b; x ^= __t; /* __t used once here */` fuses into `x ^= (a & b)` at the IR level, emitting a single CCX with no intermediate qubit. This is the optimization today's `lazy_expr` materialization tries to approximate at runtime — moving it to the IR is cleaner and composes with other rewrites.
2. **Peephole gate reordering.** Commute commuting gates to expose fusion opportunities and cancellations.
3. **Uncompute hoisting.** When a `qbool` is produced inside a loop and uncomputed at the end of the loop body, but the forward computation is loop-invariant, the transpiler may hoist both compute and uncompute out of the loop. This is an optimization; correctness is only affected by getting the hoist conditions right.
4. **Dead-ancilla elimination.** When an intermediate is produced but never consumed (e.g. dead after a classical branch is eliminated), skip the allocation entirely.

Each optimization is a separate IR pass, gated by a CLI flag (`--O1`, `--O2`) for easy bisection.

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
