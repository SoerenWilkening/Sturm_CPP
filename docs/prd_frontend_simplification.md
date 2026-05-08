# PRD: Frontend Simplification

- **Date:** 2026-05-07
- **Status:** Implemented (sturm-f8ib / Phase 9 — all seven acceptance
  gates A1–A7 wired in `tests/regressions/test_acceptance_gates.cmake`
  and green in clean `-DSTURM_FULL_TEST_SUITE=ON` build, 2026-05-08).
- **Owner:** Soren Wilkening
- **Supersedes:** none (additive — earlier work landed in `docs/archive/prd_qint_alias_completion.md`)

## 1. Motivation

The current user-facing frontend is functional but verbose. A typical demo
(`examples/qram_demo.cpp`, pre-this-PRD) requires:

- Eight `#include`s spanning `sturm/core`, `sturm/backend`, `sturm/qram`,
  `sturm/qtypes`.
- A `#define STURM_BACKEND_ENABLED 1` line before any include.
- A three-line backend setup
  (`sturm_backend_create` + `sturm_set_thread_context`).
- A two-line teardown
  (`sturm_set_thread_context(nullptr)` + `sturm_backend_destroy`).
- A user-chosen `kNumQubits` constant that they have to estimate from
  workload.
- Manual `ctx->ir` plumbing into `sturm::draw_ascii`.

For users who only want to write quantum logic, almost none of this carries
information. This PRD specifies a reduction of the user-visible surface so
that a typical demo reads as ordinary C++ with sturm types — no
infrastructure, no qubit-count guess, one (or two) header lines.

## 2. Goals

G1. **One umbrella header `sturm.h`** covering the curated public API
    (`qint`, `qbool`, gates, the qram-subscript rewrite hook).

G2. **Two opt-in feature headers** for heavier / output-only concerns:
    - `sturm/qram.h` — pulls in `qram_read.hpp` (template-heavy; not free).
    - `sturm/draw_ascii.h` — exposes the ASCII renderer entry points.

G3. **Mode as a build flag.** `STURM_MODE_APPEND` / `STURM_MODE_COUNT_ONLY`
    / `STURM_MODE_SIMULATE` is selected at configure time
    (`-DSTURM_MODE=APPEND|COUNT|SIMULATE`), default `APPEND`. Switching
    modes within one binary is *not* supported through the auto-injected
    path — users who need that fall back to the explicit lifecycle.

G4. **Transpiler-injected lifecycle.** The transpiler wraps `int main(...)`
    in any TU that includes `sturm.h` with `sturm_backend_create`,
    `sturm_set_thread_context`, and matching teardown. The user's `main`
    body is unchanged.

G5. **Qubit cap removed entirely.** `sturm_backend_create` no longer takes
    a `max_qubits` argument. `QubitPool` no longer enforces a cap. The
    `STURM_ANCILLA_CAPACITY` constant is deleted. SIMULATE-mode memory
    cost is the user's responsibility.

G6. **No-argument renderer entry points.**
    - `std::string sturm::draw_ascii()` — renders the current thread
      context's IR. (Tests still use this.)
    - `void sturm::print_ascii()` — prints `draw_ascii()` to `stdout`.
    - `std::size_t sturm::gate_count()` — current context's `ir.size()`.

## 3. Non-goals

- **Test-framework auto-injection.** GoogleTest / Catch fixtures continue
  to call `sturm_backend_create` / `sturm_set_thread_context` explicitly.
  Auto-injection only targets a top-level `int main(...)` definition.
- **Per-instance `super_mask` widening.** Each `qint`/`qbool` carries a
  `uint64_t super_mask`. This caps a single instance at 64 bits, which is
  fine — `qint_t<W>` widths in practice are ≤64.
- **Per-context `super_mask` / `promotion_mask` widening, or
  `classical_values[17]` extension.** These are SIMULATE bookkeeping; the
  user owning SIMULATE memory cost (G5) implies they are responsible for
  staying under the SIMULATE-tractable qubit count where these structures
  matter.
- **Renderer formats beyond ASCII.** Mermaid / SVG / JSON are out of
  scope; if added later, each gets its own opt-in header
  (`sturm/draw_<fmt>.h`).
- **Frontend-only / backend-disabled compilation.** With `sturm.h`
  always pulling the runtime, the option to compile without
  `STURM_BACKEND_ENABLED` goes away. This is a deliberate
  simplification, not a deferral.
- **Control-flow gaps in auto-injected lifecycle (R7).** The
  transpiler-injected lifecycle (G4) wraps `int main(...)` with an IIFE
  followed by `sturm_set_thread_context(nullptr)` + `sturm_backend_destroy`.
  Non-local control flow that bypasses the IIFE return path will skip
  that teardown: `setjmp` / `longjmp`, `std::exit` / `std::_Exit` /
  `std::quick_exit`, and uncaught exceptions that escape the lambda all
  leak the heap-allocated `sturm_backend_context_t`. Userland code that
  uses any of these from inside `main` must either call
  `sturm_backend_destroy` explicitly before the bypass, or opt into the
  manual lifecycle via `STURM_NO_AUTO_LIFECYCLE`. The same caveats are
  documented in `include/sturm.h`'s comment block.

## 4. User-visible shape (target)

**Before** (current `examples/qram_demo.cpp`):

```cpp
#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qint_alias.hpp"

using sturm::qint;
using sturm::qbool;

int main() {
    constexpr uint32_t kNumQubits = 8;
    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    qint a[4];
    for (int i = 0; i < 4; ++i) a[i] = i + 5;
    qint i = 2;
    qint b = a[i];

    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);
    std::fprintf(stdout, "\n[gate count = %zu]\n", ctx->ir.size());

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
```

**After** (the file committed at `examples/qram_demo.cpp`; sturm-f8ib /
P9 / A1 reconciliation — the listing was updated from the original
two-include schematic to match the actual demo so the byte-identity
test gate `test_qram_demo_byte_identical` and the textual-diff sub-gate
of A1 both observe the same source of truth. The opt-in `sturm/qram.h`
include is required for the `qint b = a[i];` subscript-rewrite hook to
reach the `QRAM_read` overload set; the per-bit `i[0].phi() += 3;`
rotations encode the index `i = 2` into a non-trivial gate stream so
the rendered ASCII diagram has substance):

```cpp
// QRAM demo — natural-syntax read `qint b = a[i];`, with the resulting
// gate stream printed as an ASCII circuit diagram.
//
// `add_quantum_executable()` routes this file through `sturm-transpile`,
// whose C1 matcher (`matcher_qram_subscript`) rewrites the read line into
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` before codegen, and
// whose `matcher_main_lifecycle` (Phase 7 / sturm-e3ru) wraps `main` with
// the auto-injected `sturm_backend_create` / `sturm_backend_destroy`
// lifecycle now that this TU includes the umbrella `sturm.h`.
//
// Frontend simplification (PRD §4 "After" listing — sturm-yggr / Phase 8):
// the umbrella `sturm.h` brings in the curated public API
// (`qint`, `qbool`, the C ABI lifecycle), the opt-in `sturm/qram.h` makes
// the QRAM_read overloads reachable, and the opt-in `sturm/draw_ascii.h`
// exposes the no-arg renderer entry points (PRD §5.6).
#include "sturm.h"
#include "sturm/draw_ascii.h"
#include "sturm/qram.h"

int main() {
    qint a[4];
    for (int i = 0; i < 4; ++i) {
        a[i] = i;
    }

    qint i = 2;
    i[0].phi() += 3;
    i[1].phi() += 3;
    qint b = a[i];

    std::fputs("\n--- QRAM read circuit (APPEND-mode IR) ---\n", stdout);
    sturm::print_ascii();
    std::fprintf(stdout, "\n[gate count = %zu]\n", sturm::gate_count());
    return 0;
}
```

Build: `cmake -S . -B build` (mode defaults to `APPEND`); override with
`-DSTURM_MODE=SIMULATE` etc.

## 5. Specifications

### 5.1 — `sturm.h` umbrella

`include/sturm.h` (new):

- Forwards to: `sturm/qtypes/qint.hpp`, `sturm/qtypes/qint_alias.hpp`,
  `sturm/qtypes/qbool.hpp`, `sturm/core/core.h`,
  `sturm/core/context.hpp`.
- Provides `using sturm::qint; using sturm::qbool;` at namespace scope so
  user code works without an explicit `using` line.
- Sets `#define STURM_BACKEND_ENABLED 1` if not already defined.
- Does **not** include `sturm/qram/qram_read.hpp`,
  `sturm/backend/draw_ascii.hpp`, `sturm/backend/exec_append.hpp`, or
  any backend-internal `.hpp`.

### 5.2 — Opt-in feature headers

- `include/sturm/qram.h` — wraps `sturm/qram/qram_read.hpp`. Required for
  the `qint b = a[i];` subscript rewrite to compile.
- `include/sturm/draw_ascii.h` — declares the no-arg renderer entry
  points (§5.6) and pulls `sturm/backend/draw_ascii.hpp` for the
  IR-taking overload that tests still call directly.

### 5.3 — Mode build flag

- New CMake cache variable `STURM_MODE`, type `STRING`, values
  `APPEND` (default) | `COUNT` | `SIMULATE`. Validated at configure
  time; invalid values fail with an actionable diagnostic.
- Top-level CMake adds `-DSTURM_MODE_DEFAULT=STURM_MODE_<X>` to the
  global compile definitions.
- The auto-injected lifecycle (§5.4) reads `STURM_MODE_DEFAULT`.
- No runtime mode switching through the auto-injected path. A user who
  needs runtime selection writes the explicit `sturm_backend_create`
  call themselves and adds `#define STURM_NO_AUTO_LIFECYCLE` before
  including `sturm.h`.

### 5.4 — Transpiler auto-injection

A new matcher `matcher_main_lifecycle` (in `transpiler/src/`) fires on
the unique `int main(...)` `FunctionDecl` of any TU whose preprocessor
state shows `sturm.h` was included (probe: `__has_include("sturm.h")`
plus a sentinel macro the umbrella sets). It rewrites:

```cpp
int main(/* user-args */) {
    /* user-body */
}
```

into:

```cpp
int main(/* user-args */) {
    sturm_backend_context_t* __sturm_ctx =
        sturm_backend_create(STURM_MODE_DEFAULT);
    sturm_set_thread_context(__sturm_ctx);
    int __sturm_rc = ([&]() -> int {
        /* user-body */
    })();
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(__sturm_ctx);
    return __sturm_rc;
}
```

Skip rewrite when:
- The TU defines `STURM_NO_AUTO_LIFECYCLE` (escape hatch for tests,
  libraries, demos that want the explicit form).
- The TU does not transitively include `sturm.h`.
- No `main` is defined in this TU.

### 5.5 — Qubit cap removal

Files touched:

- `include/sturm/core/qubit_pool.hpp`
  - Delete `kCapacity` constant.
  - Delete `max_qubits` field.
  - Delete the per-context `QubitPool(uint32_t cap)` constructor.
  - Delete the `acquire()` cap check and the abort path
    (`kCapExceededMsg`).
  - `acquire()` and `allocate()` collapse into one method that always
    grows the high-water mark on demand (vector grows naturally).
- `include/sturm/core/core.h`
  - `sturm_backend_create` signature drops the `uint32_t max_qubits`
    parameter.
- `include/sturm/core/context.hpp`
  - `BackendContext::BackendContext` drops the `max_q` parameter.
  - `kMaxClassicalQubits = 17u`, `classical_values[17]`, per-context
    `super_mask`, `promotion_mask` — **kept as-is**, SIMULATE-only
    bookkeeping (see Non-goals §3).
- `src/sturm/core/context.cpp`
  - `sturm_backend_create` signature update.
- `STURM_ANCILLA_CAPACITY` preprocessor macro: deleted from CMake and
  any `target_compile_definitions`.
- All 126 caller files of `sturm_backend_create(mode, N)` (189 call
  sites in total) updated to `sturm_backend_create(mode)` —
  mechanical, mostly tests. (sturm-5jta confirmed the actual count
  via `grep -rln "sturm_backend_create" --include="*.cpp"
  --include="*.hpp" --include="*.h"`; the original "~80" estimate was
  conservative.)

### 5.6 — No-argument renderer entry points

In `include/sturm/draw_ascii.h`:

```cpp
namespace sturm {

// String-returning form. Queries the thread-local context, derives
// canvas width from the IR's max qubit index + 1.
std::string draw_ascii();

// Convenience: writes draw_ascii() to stdout (no trailing newline beyond
// what the renderer emits).
void print_ascii();

// Current context's ir.size().
std::size_t gate_count();

} // namespace sturm
```

Implementation notes:
- Both `draw_ascii()` and `print_ascii()` `assert` on a non-null thread
  context. Calling them outside an auto-injected `main` (or an explicit
  lifecycle block) is a programming error, not a recoverable condition.
- Canvas width derivation: scan the IR for the maximum referenced qubit
  index; use `max + 1`. If the IR is empty, return an empty diagram.
  This avoids storing a separate "n_qubits" on the context now that the
  cap is gone.

### 5.7 — SIMULATE-mode OOM clarity

When `STURM_MODE=SIMULATE`, the statevector allocation in
`OrkanBridge` is wrapped in `try { ... } catch (std::bad_alloc&) { ... }`
that writes a stable message to **`stderr`** and calls `std::abort`.
The message format (locked, see `tests/backend/test_simulate_oom.cpp`):
`"STURM: SIMULATE mode out of memory at N qubits — reduce qubit count"`
where `N` is the requested qubit count. The destination is `stderr`
(not `stdout`) so the diagnostic is preserved when stdout is captured
or piped. This is the only safety guard provided for SIMULATE;
finer-grained limits remain the user's responsibility (G5).

### 5.8 — Migration

- All 189 `sturm_backend_create(mode, N)` call sites across 126 files
  updated (sturm-5jta). Mostly `tests/`. Mechanical.
- `examples/`: each example either (a) deletes the explicit lifecycle
  and relies on auto-injection, or (b) keeps the explicit form with
  `#define STURM_NO_AUTO_LIFECYCLE`. Default to (a) unless the example
  is *about* the lifecycle.
- `docs/qram_user_intro.md`, `docs/getting_started.md`,
  `docs/public_api.md`, `docs/public_api.txt` — rewritten for the new
  shape. The umbrella header replaces the multi-include block; the
  `kNumQubits` discussion is removed; the renderer section uses
  `sturm::print_ascii()`.

## 6. Open questions

OQ1. **Auto-injection trigger.** Is `__has_include("sturm.h")` plus a
     sentinel macro robust enough, or do we need a Clang
     `PreprocessorCallback` to detect the `#include` directly? The
     latter is more reliable but heavier.

OQ2. **Lifecycle escape hatch naming.** `STURM_NO_AUTO_LIFECYCLE` is
     descriptive but long. Alternatives: `STURM_MANUAL_LIFECYCLE`,
     `STURM_EXPLICIT_CTX`. Pick before implementation lands.

OQ3. **Examples migration policy.** Migrate all examples to
     auto-injection, or keep one or two (`in_memory_transpile.cpp`,
     `nested_when.cpp`?) on the explicit form for didactic purposes?

OQ4. **Renderer naming collision.** Does `sturm::print_ascii()` collide
     with any existing symbol? Spot-check before locking the name.

OQ5. **Build flag → file rebuild semantics.** Changing
     `-DSTURM_MODE=...` should force rebuild of TUs that use the
     auto-injected lifecycle. CMake `target_compile_definitions` on the
     interface library handles this if the umbrella is consumed via
     that target — confirm in the implementation plan.

## 7. Out-of-scope follow-ups

To be filed as separate beads issues, not blocking this PRD:

- Per-context `super_mask` / `promotion_mask` widening (currently
  `uint32_t` — silently truncates above qubit 32 in SIMULATE
  bookkeeping paths).
- `classical_values[17]` removal or dynamic widening.
- Misleading abort string `"max 17"` cleanup (the string is deleted
  along with the cap in §5.5, so this lands as part of this PRD; noted
  here for traceability with prior discussion).
- Renderer formats beyond ASCII (`sturm/draw_mermaid.h`,
  `sturm/draw_svg.h`, `sturm/draw_json.h`).
- Auto-injection for non-`main` entry points
  (`[[sturm::entry_point]]` attribute for libraries / test fixtures).

## 8. Acceptance criteria

A1. `examples/qram_demo.cpp` matches the §4 "After" listing exactly
    (modulo comments) and produces byte-identical output to the
    pre-PRD version.

A2. `cmake -S . -B build && cmake --build build --parallel 6 &&
    ctest --test-dir build --parallel 6` passes with no
    `STURM_MODE` flag set (i.e. `APPEND` default works).

A3. `cmake -S . -B build -DSTURM_MODE=SIMULATE && ctest ...` passes the
    SIMULATE-mode test subset.

A4. No surviving cap-enforcement code: no production file in
    `include/`, `src/`, or `transpiler/` references
    `STURM_ANCILLA_CAPACITY`, `kMaxClassicalQubits`-as-a-cap, the
    `max_qubits` parameter on `sturm_backend_create`, or
    `kCapExceededMsg`. Tests / docs / examples may legitimately mention
    these tokens for historical or migration prose; the audit is
    restricted to production code (P9 / sturm-f8ib reword: the original
    "no file in `include/`, `src/`, `tests/`, `examples/`, or
    `transpiler/` references …" wording is functionally impossible
    because the regression gate `test_no_cap_artifacts.cpp` itself must
    name the forbidden tokens to scan for them).

A5. A test in `tests/packaging/` verifies that
    `#include "sturm.h"` alone (no other sturm includes) compiles a
    minimal qint program. A second test verifies the same for
    `#include "sturm.h"` + `#include "sturm/qram.h"`.

A6. A transpiler test verifies that `STURM_NO_AUTO_LIFECYCLE` correctly
    suppresses the rewrite (round-trip: rewritten source equals input
    source modulo whitespace).

A7. `docs/qram_user_intro.md` and `docs/getting_started.md` reflect the
    new shape; pre-PRD examples are not present in those files.
