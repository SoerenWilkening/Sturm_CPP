# PRD: Transpiler-Based Uncomputation (MVP)

**Status:** Not Started
**Date:** 2026-04-14
**Predecessors:** `archive/prd_mixed_quantum_classical_fastpath.md`, `archive/prd_bitproxy_when_promotion.md`
**Supersedes:** The runtime RAII auto-uncomputation layer (`WhenCapture`, `uncompute_op`, `uncompute_run`, `lazy_expr` materialization).

## Context

Automatic uncomputation currently runs at C++ runtime, driven by RAII destructors (`qbool::~qbool()`, `~qint_t<W>`, `WhenCapture`) and a thread-local callback trampoline. It works for simple expressions but becomes fragile as soon as compound expressions, nested `WHEN`s, or non-self-inverse operations enter the picture.

Concrete symptoms in the current code:

- `include/sturm/uncompute/uncompute_op.hpp` has stub bodies for `ADD_QINT`, `SUB_QINT`, `MUL_INVERSE`, `DIV_INVERSE`, `MOD_INVERSE`. Five of ten tagged-union variants are TODO.
- `include/sturm/uncompute/uncompute_op.hpp:284-285` — the `COMPARE` case emits both the forward and the inverse circuit back-to-back. This doubles gate count because the runtime does not carry enough data-flow information to derive the inverse on its own.
- `include/sturm/control/when_capture.hpp` installs a thread-local trampoline (`active_instance_`, `when_capture_defer_fn`) to intercept destructors mid-full-expression because RAII destruction order does not match the order required for reverse uncomputation.
- `include/sturm/control/when.hpp:83-204` — `WhenGuard`'s AND-fold path juggles TLS, `control_stack`, an inline ancilla `qbool`, a `pushed_to_ctx_stack_` flag, and three separate cleanup paths, all for one language construct.
- Lifetime invariants ("pointed-to object must outlive this `uncompute_op`") are comments, not checked by any tool.

The root cause is that uncomputation is fundamentally a static-analysis problem — data-flow, liveness, and control-flow. RAII can only simulate it by making runtime state stand in for compile-time knowledge.

Principle **B1b** (`docs/01_principles.md`) already anticipates this direction: *"Macro-based or AST-based auto-generation may be added later as opt-in mechanisms."* This PRD is the execution of B1b.

## Goals (MVP)

1. Replace runtime auto-uncomputation with a compile-time transpiler.
2. Preserve the user-facing C++ syntax. Users keep writing `qbool r = (b | c) & d;` and `WHEN(expr) { ... }`. No custom file extension, no DSL.
3. Keep the backend oblivious: the transpiler emits explicit `uncompute_*` calls. The runtime only knows "emit these gates," never "undo this later."
4. Output of the transpiler is **primitive-level C++** (operations on `qint` / `qbool`), not gate-level. The existing dispatch/sink/classicality-mask runtime keeps its job.
5. Provide a compile-time flag that disables the legacy runtime auto-uncomputation so the transpiler's output can be validated in isolation.

## Non-Goals (MVP)

- Not a new language. Input is a documented subset of standard C++.
- Not gate-level output. The transpiler does not emit `execute_gate` calls.
- Not an LLVM IR. The internal IR is project-local.
- Not yet optimizing — no zero-ancilla fusion, no gate-count minimization, no cross-operation peephole rewrites in the MVP.
- Not yet a deletion of the RAII layer. The legacy code stays in-tree but is gated off during validation.

## Architecture

```
user.cpp                             ← plain C++, qint/qbool, unchanged user syntax
    │
    ▼  Clang LibTooling (AST matchers + rewriter)
Quantum IR                           ← SSA-ish, operation-level, with data-flow edges
    │
    ▼  Uncompute synthesis pass      ← reverse-order uncomputation, per scope
Quantum IR (+ inverse emissions)
    │
    ▼  C++ emitter (source rewriter)
build/sturm_gen/<relpath>/user.cpp   ← primitive-level C++, explicit uncompute_* calls
    │
    ▼  clang++ / g++ (standard C++ compile)
binary
```

Key properties:
- Clang LibTooling provides the parsed, type-resolved AST. The transpiler implements only the rules.
- The transpiler never emits gates. It emits calls to the same `qint`/`qbool` primitive operations the user writes.
- Purely classical sub-expressions are copied through verbatim.

## Input Contract

Users write ordinary C++ with the quantum types:

```cpp
#include <sturm/sturm.hpp>
using namespace sturm;

void routine(qbool a, qbool b, qbool c, qbool d) {
    qbool r = (b | c) & d;      // quantum expression
    WHEN(r) { a ^= true; }      // quantum control
    int n = get_shot_count();   // classical — passes through
    for (int i = 0; i < n; i++) {
        a ^= b;                 // quantum inside classical loop
    }
}
```

- Full C++ is accepted. Templates, concepts, standard library, classical functions all work.
- Quantum types are `qint`, `qint_t<W>`, `qbool`. The transpiler recognizes them by type.
- Overloaded operators on quantum types are the transpiler's primary match targets.
- `WHEN(expr) { ... }` remains a macro. The transpiler ensures `expr` is a named `qbool` by the time `WHEN` sees it.

## Output Contract

The transpiler emits standard C++ with:

- Quantum intermediates named as `qbool __stu_tN` / `qint __stu_tN` local variables.
- Explicit uncompute calls inserted in LIFO order before scope exit.
- Classical code copied unchanged.
- A generated-file header comment that serves as both a signature and an idempotency marker.

Forward / inverse pairs:

| Forward (transpiler emits same as user writes) | Inverse (transpiler emits) |
|---|---|
| `tmp = a \| b;` | `uncompute_or(tmp, a, b);` |
| `tmp = a & b;` | `uncompute_and(tmp, a, b);` |
| `tmp = ~a;` | `tmp = ~tmp;` *(self-inverse)* |
| `a ^= b;` | `a ^= b;` *(self-inverse)* |
| `a += c;` *(classical c)* | `a -= c;` |
| `a += q;` *(qint q)* | `a -= q;` |
| `c = (a == b);` | `uncompute_eq(c, a, b);` |
| `foo(x, y);` *(user routine)* | `invert(foo)(x, y);` |

Inverses are **free functions** in namespace `sturm`. They do not clutter the `qint` / `qbool` public API and can be extended without touching the core types.

## Runtime Changes

1. **Compile-time flag `STURM_AUTO_UNCOMPUTE`** (default: undefined/OFF for transpiler mode, defined for legacy mode during transition).
   - When OFF: `~qbool()` and `~qint_t<W>()` only release qubit indices back to the pool. No inverse gates are emitted by destructors.
   - When ON: preserves the current RAII behavior for validation and bisection against the transpiler.
2. **Free uncompute functions** in `include/sturm/uncompute/uncompute_api.hpp` (new file). MVP needs only `uncompute_or(qbool& r, const qbool& a, const qbool& b)`. More added per phase in the post-MVP roadmap.
3. **Retain** `WhenGuard`. It handles the push/pop of the active control qubit, which the transpiler still relies on.
4. **Retain** `QubitPool`, sinks, the 18-gate `execute_gate` ABI, classicality-mask tracking, primitive-op files (`qint_arith_v3`, `qint_bitwise_v3`, etc.) with their RAII uncompute calls disabled when the flag is OFF.
5. **Remove from the hot path when `STURM_AUTO_UNCOMPUTE` is OFF:** `WhenCapture`, `uncompute_op::apply`, `run_uncompute`, `lazy_expr` materialization with ancilla allocation. The code remains in the tree for the duration of the transition.

## Build Integration

- Top-level CMake option `STURM_TRANSPILE` (default: ON once MVP is green).
- CMake function `add_quantum_executable(<target> <sources>)` that:
  1. Declares a custom command: for each source `src/foo.cpp`, run `sturm-transpile` with input `src/foo.cpp` and output `build/sturm_gen/src/foo.cpp`.
  2. Declares the generated file as the actual compilation unit.
  3. Tracks `sturm-transpile` binary as a dependency so a transpiler change triggers regeneration.
- When `STURM_TRANSPILE` is OFF, `add_quantum_executable` compiles the original source directly. Useful for CI sanity, debugging the runtime, and the bootstrap phase.

## Opt-Out Mechanism

File-level marker (magic comment at the top of the file):

```cpp
// sturm-transpile: skip
```

On seeing this, the transpiler copies the file through verbatim. The same mechanism provides **idempotency**: the generated output carries its own header, so running the transpiler on already-generated output is a no-op.

Legitimate reasons to skip:
- Library / runtime internal files (the implementation of the primitives).
- Hand-written uncomputation in a performance-critical routine.
- Debugging the runtime itself.
- Quick IDE / clang syntax check with no transpile overhead.

## MVP Scope — Minimum Viable Pipeline

The first milestone handles **one** quantum expression pattern end-to-end. Everything beyond this ships in the post-MVP roadmap (`docs/roadmap_transpiler_post_mvp.md`).

**Pattern:** a single `qbool` intermediate from `operator|`, with nothing else in scope.

Input:
```cpp
#include <sturm/sturm.hpp>
using namespace sturm;

void demo(qbool a, qbool b) {
    qbool tmp = a | b;
}
```

Expected output (sibling file, at `build/sturm_gen/<relpath>/demo.cpp`):
```cpp
// AUTO-GENERATED by sturm-transpile — do not edit
// Source: src/demo.cpp
#include <sturm/sturm.hpp>
using namespace sturm;

void demo(qbool a, qbool b) {
    qbool tmp = a | b;
    uncompute_or(tmp, a, b);
}
```

This case is deliberately tiny. It proves every layer of the pipeline:

- Clang LibTooling parses the input and resolves types.
- AST matcher fires on a `VarDecl` initialized from `operator|(const qbool&, const qbool&)`.
- IR captures "`tmp` is alive until scope exit, produced by `|`, operands `a` and `b`."
- Uncompute pass inserts a matching `uncompute_or` before the closing `}`.
- Rewriter emits the modified C++.
- CMake wiring compiles the generated file.
- `STURM_AUTO_UNCOMPUTE` is OFF, so `~tmp` only releases the qubit; the explicit `uncompute_or` does all the gate emission.
- Gate-stream output matches the hand-written equivalent byte-for-byte.

## Acceptance Criteria

1. `sturm-transpile` binary builds from `transpiler/` sources via CMake.
2. Running `sturm-transpile src/demo.cpp --output-dir build/sturm_gen/` produces `build/sturm_gen/src/demo.cpp` containing the expected output above.
3. The generated file compiles with `STURM_AUTO_UNCOMPUTE` OFF.
4. The compiled binary's gate stream (in circuit-mode sink) matches a hand-written reference that uses explicit `uncompute_or`.
5. The transpiler is **idempotent**: running it on its own output produces a byte-identical file.
6. The magic comment `// sturm-transpile: skip` causes the file to be copied through verbatim.
7. `STURM_TRANSPILE=OFF` disables the step globally; `add_quantum_executable` compiles the original source directly.
8. Legacy RAII auto-uncompute still works with `STURM_AUTO_UNCOMPUTE` ON — no regression in existing tests under the legacy flag.

## File Layout

```
sturm/
  runtime/                             ← existing lib, reorganized under a subdirectory
    include/sturm/…                    ← current headers (unchanged paths for now)
    src/sturm/…
    tests/
  transpiler/                          ← new
    include/sturm/transpile/…
    src/…                              ← main.cpp, matchers, IR, uncompute pass, emitter
    tests/                             ← input-output fixture pairs
  examples/                            ← end-to-end programs
  docs/
    01_principles.md
    prd_transpiler_uncompute.md        ← this file
    roadmap_transpiler_post_mvp.md
    archive/
```

The `runtime/` and `transpiler/` split stays in a monorepo. The split by directory keeps the boundary explicit without incurring the cross-repo versioning cost.

## Design Decisions (Rationale)

- **LibTooling over a hand-written parser.** Full C++20/23 parsing, template instantiation, and overload resolution are free. We write only the rules.
- **Sibling file over in-place rewrite or in-memory compile.** Human-inspectable output is invaluable for a quantum compiler; diffing the generated source against a snapshot is the primary correctness tool. In-memory compilation is a stretch optimization for later.
- **Primitive-level output, not gate-level.** Preserves the existing runtime's role. The transpiler only schedules uncomputation; gate emission stays with the runtime.
- **Free functions for inverses, not methods.** Keeps the `qint`/`qbool` public API minimal. Inverses are internal (transpiler ↔ runtime). Extending the inverse set does not touch the core types.
- **Legacy RAII layer stays during MVP.** Deleting it while building the transpiler would leave the main branch broken for a long stretch. Gating via `STURM_AUTO_UNCOMPUTE` lets both systems coexist briefly and allows gate-stream comparison for correctness validation.
- **Opt-out via magic comment, not file extension.** Keeps source files as plain `.cpp` — IDE, clang, reviewers all treat them normally.
- **Input language is full C++, not a subset.** The transpiler ignores classical code; the user's experience is "write normal C++." A subset would be documentable but would constantly chafe against the real language.

## Critical Files (MVP Touch List)

**New:**
- `transpiler/` subtree (CMake, main.cpp, matchers, IR, emitter).
- `include/sturm/uncompute/uncompute_api.hpp` — free function declarations (`uncompute_or` first).
- `src/sturm/uncompute/uncompute_api.cpp` — their implementations (one function for MVP).
- `cmake/SturmTranspile.cmake` — the `add_quantum_executable` helper.
- `tests/transpiler/fixtures/or_single.cpp` + `or_single.expected.cpp` — first snapshot pair.

**Modified:**
- `include/sturm/qtypes/qbool.hpp` — gate destructor-driven uncompute behind `STURM_AUTO_UNCOMPUTE`.
- `include/sturm/qtypes/qint_core.hpp` (or wherever `~qint_t` lives) — same.
- Root `CMakeLists.txt` — add `STURM_TRANSPILE` option, include `cmake/SturmTranspile.cmake`.

**Unchanged (deliberately):**
- `include/sturm/control/when.hpp`, `when_capture.hpp` — remain functional under legacy flag; transpiler-mode tests simply do not exercise them.
- `include/sturm/uncompute/uncompute_op.hpp`, `uncompute_run.hpp` — same.
- Backend ABI (`execute_gate`, 18-gate set).

## Verification

1. **Snapshot test**: `tests/transpiler/fixtures/or_single.cpp` → generated file is diffed against `or_single.expected.cpp`. Any byte-level change fails the test.
2. **Gate-stream equivalence**: the compiled binary's circuit-mode output on `or_single.cpp` matches a hand-written reference using explicit `uncompute_or`.
3. **Idempotency**: re-running the transpiler on the generated file produces a byte-identical result.
4. **Legacy regression**: existing tests under `tests/` pass with `STURM_AUTO_UNCOMPUTE=ON` and `STURM_TRANSPILE=OFF`. No behavioral change in the legacy path during MVP.
5. **Skip marker**: a file beginning with `// sturm-transpile: skip` is passed through verbatim.

## Open Questions (tracked, not blocking)

- Exact shape of the IR (SSA form? flat list with scope markers? tree?). Default: flat list with scope markers and explicit def/use edges. Revisit when a compound expression (`(b | c) & d`) enters scope in the post-MVP roadmap.
- Error-reporting format for quantum-specific violations (e.g. "operand modified inside its own `WHEN`"). MVP does not need to diagnose these yet.
- Whether to preserve `#line` directives in the generated file. Recommended yes, once the transpiler is past the trivial MVP case, so compiler errors point at the user's source line.
