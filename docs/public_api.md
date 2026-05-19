# STURM Public API

**Status:** v2 (2026-05-07). Updated for the
[`archive/prd_frontend_simplification.md`](archive/prd_frontend_simplification.md)
umbrella + opt-in feature headers.
**Scope tag:** `packaging-export`.
**Companion doc:** [`transpiler_emit_targets.md`](transpiler_emit_targets.md).
**Machine-readable snapshot:** [`public_api.txt`](public_api.txt).

This document is the authoritative list of public symbols exposed by
the STURM C++ language front-end. A symbol is **public** iff it is
both:

1. **reachable** from a translation unit that includes only the
   curated umbrella header `sturm.h` (PRD §5.1) — optionally with the
   opt-in feature headers `sturm/qram.h` and `sturm/draw_ascii.h`
   (PRD §5.2) — AND
2. **classified `public`** in
   [`docs/transpiler_emit_targets.md`](transpiler_emit_targets.md)
   (E1.M2) — or, when not present in the emit-targets table, lives at
   namespace `sturm::` scope (not under any inner `detail*` /
   `_detail*` namespace) and ships as part of the user-facing surface
   listed in PRD §3.3.

Anything **not** on this list — including everything reachable only
via `<sturm/detail/...>` — is internal and may change without notice.

## Stability promise

**Stability is deferred to a later issue.** The library version macros
`STURM_VERSION_*` are sourced from the top-level CMake `project(...)`
declaration (E4.M1). Once the project ships a 1.0 release, this list
becomes subject to versioning per E4 (the `packaging-export` PRD
§3.8). Until then, any symbol on this list may change between commits;
the drift check exists so that *every* such change is a deliberate,
reviewer-visible edit to this document.

## How to regenerate

The companion machine-readable snapshot
[`public_api.txt`](public_api.txt) is the source of truth that CI
diffs against. Regenerate both files together when the public surface
changes:

```bash
python3 tools/extract_public_api.py > docs/public_api.txt
# then update the symbol table below to match docs/public_api.txt
bash tools/check_public_api_drift.sh   # confirms drift == 0
```

The drift check fails CI on any mismatch between the umbrella header's
reachable surface and the committed snapshot.

## Header layout (PRD §5.1, §5.2)

The public surface is partitioned across one umbrella + two opt-in
feature headers:

| Header                  | Brings in                                                                       |
|-------------------------|---------------------------------------------------------------------------------|
| `sturm.h`               | `qint`, `qbool`, the C-ABI lifecycle, the `using sturm::qint/qbool` aliases.    |
| `sturm/qram.h`          | The `QRAM_read` overload set (required for the `qint b = a[i];` rewrite hook). |
| `sturm/draw_ascii.h`    | The IR-taking renderer overload + the no-arg entry points (PRD §5.6).          |

The umbrella sets `STURM_BACKEND_ENABLED 1` (default-on) and the
sentinel macro `STURM_UMBRELLA_INCLUDED 1` that the transpiler's
`matcher_main_lifecycle` (PRD §5.4) checks for to decide whether to
auto-inject the `sturm_backend_create` / `destroy` pair around `int
main(...)`. Users can opt out with `#define STURM_NO_AUTO_LIFECYCLE`
before including the umbrella; see `examples/explicit_lifecycle.cpp`.

## Symbol list

Total: **32 symbols** (4 macros from `WHEN` + `STURM_VERSION_*`,
1 macro `STURM_REGISTER_ADJOINT`, 2 classes `qbool` / `qint_t`,
1 RAII guard `WhenGuard`, 4 user primitives via free functions
`add_mod`/`mul_mod`/`pow_mod`/`invert`, plus `pow`, the
`uncompute_*` API, and the lifted-primitive helpers).

### qint — quantum W-bit integer

Defining headers: `qtypes/qint_fwd.hpp` → `qtypes/qint_core.hpp` →
`qtypes/qint.hpp` (transitively pulled in by the umbrella `sturm.h`).

| Symbol | Defining header | Description |
|---|---|---|
| `qint_t` | `qtypes/qint_core.hpp` | Headline class template `qint_t<Width = 64>` (PRD §3.3). The W-bit quantum integer; supports arithmetic, bitwise, comparison, and shift operators (each defined in its own `qtypes/qint_*` header, all transitively included). |

The umbrella `sturm.h` brings the user-facing alias `using sturm::qint;`
into the including TU's namespace; `qint` resolves to
`sturm::frontend::qint` (the width-agnostic frontend wrapper).

### qbool — quantum bool

Defining header: `qtypes/qbool.hpp` (forward decl in
`control/when_fwd.hpp`).

| Symbol | Defining header | Description |
|---|---|---|
| `qbool` | `qtypes/qbool.hpp` | Headline class for a boolean that may be in a quantum superposition (PRD §3.3, D7). Inherits from `qint_t<1>`. The forward declaration in `control/when_fwd.hpp` is what the transpiler matchers cite in their diagnostic text. |

The umbrella `sturm.h` brings `using sturm::qbool;` into the
including TU's namespace.

### control — `WHEN` and lift utilities

Defining headers: `control/when.hpp`, `control/lift.hpp`.

| Symbol | Defining header | Description |
|---|---|---|
| `WHEN` | `control/when.hpp` | Preprocessor macro `WHEN(expr) { body }` — the language's quantum-control primitive (PRD §3.3, D5). Expands to a `WhenGuard` RAII scope that pushes `expr.qubits[0]` onto the active control stack. |
| `WhenGuard` | `control/when.hpp` | RAII type behind the `WHEN` macro. Public so the macro can name it and so users that need bespoke control-stack manipulation in adjoint hand-rolls can construct one directly. |
| `lift_under` | `control/lift.hpp` | `lift_under(flag, body)` — depth-1 lift helper for adjoint emission. Runs `body` with `flag` as the single active control. Header-only; used by routine-inversion plumbing. |

### ops — modular arithmetic and lifted primitives

Defining headers: `ops/qint_modular.hpp`, `ops/lifted_primitives.hpp`.

| Symbol | Defining header | Description |
|---|---|---|
| `add_mod` | `ops/qint_modular.hpp` | `add_mod(a, b, n)` returns `(a + b) mod n`. Transpiler-emit target for `(a + b) % n` (PRD §3.3, D2). |
| `mul_mod` | `ops/qint_modular.hpp` | `mul_mod(a, b, n)` returns `(a * b) mod n`. Transpiler-emit target for `(a * b) % n`. |
| `pow_mod` | `ops/qint_modular.hpp` | `pow_mod(base, exp, n)` returns `(base ^ exp) mod n`. Transpiler-emit target for `pow(a, x) % n`. |
| `pow` | `qtypes/qint_arith.hpp` | `pow(base, exp)` free function, classical/quantum dispatched. Companion to `pow_mod`. |
| `emit_X_lifted` | `qtypes/qbool_ops.hpp` | Helper that emits an `X` gate respecting the active WHEN control stack. Reachable through the umbrella; called from `qbool` / `qint_t` operator bodies. |
| `emit_CX_lifted` | `qtypes/qbool_ops.hpp` | Helper that emits a CNOT (`CX`) gate respecting the active WHEN control stack. |
| `emit_CCX_lifted` | `qtypes/qbool_ops.hpp` | Helper that emits a Toffoli (`CCX`) gate respecting the active WHEN control stack. |
| `emit_RY_lifted` | `ops/lifted_primitives.hpp` | Helper that emits a `RY(theta)` rotation respecting the active WHEN control stack. |
| `emit_RZ_lifted` | `ops/lifted_primitives.hpp` | Helper that emits a `RZ(theta)` rotation respecting the active WHEN control stack. |
| `get_ctx` | `qtypes/qbool_ops.hpp` | Accessor that returns the thread-local `BackendContext&`. Reachable through the umbrella; used by the `emit_*_lifted` helpers above. |

> **Note.** The `emit_*_lifted` helpers and `get_ctx` are presently at
> namespace `sturm::` scope and therefore reachable through the
> umbrella; they are documented here for transparency. A future
> cleanup may relocate them under `sturm::detail::` (and thus drop
> them from this table) — the drift check will catch the move and
> force this doc to be updated in lockstep.

### routines — adjoint registration

Defining header: `routines/invert.hpp`.

| Symbol | Defining header | Description |
|---|---|---|
| `invert` | `routines/invert.hpp` | `sturm::invert<&fn>()` — compile-time adjoint lookup keyed on a non-type template parameter (function-pointer value). Returns the adjoint function pointer registered for the forward `fn`. |
| `STURM_REGISTER_ADJOINT` | `routines/invert.hpp` | Macro `STURM_REGISTER_ADJOINT(fn, adj)` — registers `adj` as the adjoint of `fn` for `invert<>` lookup. Expands to a full specialisation of `sturm::_detail::adjoint_of<&::fn>` at namespace scope. |

### uncompute — free-function uncompute API

Defining header: `uncompute/uncompute_api.hpp`.

| Symbol | Defining header | Description |
|---|---|---|
| `uncompute_or` | `uncompute/uncompute_api.hpp` | `uncompute_or(r, a, b)` — adjoint of an in-place OR write into `r`. Implementation in `src/sturm/uncompute/uncompute_api.cpp` (consumers must link `libsturm`). |
| `uncompute_and` | `uncompute/uncompute_api.hpp` | `uncompute_and(r, a, b)` — adjoint of an in-place AND write into `r`. |
| `ccnot_inplace` | `uncompute/uncompute_api.hpp` | `ccnot_inplace(x, a, b)` — emits `x ^= a & b` directly as a Toffoli. Self-inverse primitive used by the uncompute family above. |
| `uncompute_add_qint` | `uncompute/uncompute_api.hpp` | `uncompute_add_qint(a, b)` — adjoint of `a += b` on `qint_t<W>`; defined as `a -= b` (header-only). |
| `uncompute_sub_qint` | `uncompute/uncompute_api.hpp` | `uncompute_sub_qint(a, b)` — adjoint of `a -= b`; defined as `a += b`. |
| `uncompute_eq_qint` | `uncompute/uncompute_api.hpp` | `uncompute_eq_qint(r, a, b)` — adjoint of an `==` comparison write. |
| `uncompute_ne_qint` | `uncompute/uncompute_api.hpp` | `uncompute_ne_qint(r, a, b)` — adjoint of a `!=` comparison write. |
| `uncompute_lt_qint` | `uncompute/uncompute_api.hpp` | `uncompute_lt_qint(r, a, b)` — adjoint of a `<` comparison write. |
| `uncompute_le_qint` | `uncompute/uncompute_api.hpp` | `uncompute_le_qint(r, a, b)` — adjoint of a `<=` comparison write. |
| `uncompute_gt_qint` | `uncompute/uncompute_api.hpp` | `uncompute_gt_qint(r, a, b)` — adjoint of a `>` comparison write. |
| `uncompute_ge_qint` | `uncompute/uncompute_api.hpp` | `uncompute_ge_qint(r, a, b)` — adjoint of a `>=` comparison write. |

### draw_ascii — the no-arg renderer entry points (opt-in)

Defining header: `sturm/draw_ascii.h` (PRD §5.6, opt-in).

| Symbol | Defining header | Description |
|---|---|---|
| `sturm::draw_ascii()` | `sturm/draw_ascii.h` | No-arg overload — queries the thread-local context, derives canvas width from the IR's max qubit index + 1, and returns the diagram as a `std::string`. |
| `sturm::print_ascii()` | `sturm/draw_ascii.h` | Convenience: writes `draw_ascii()` to `stdout`. |
| `sturm::gate_count()` | `sturm/draw_ascii.h` | Returns `ir.size()` of the current thread-local context. |

The IR-taking overload `sturm::draw_ascii(const GateIR&, std::size_t)`
remains reachable through the same header for tests that prefer it.

### versioning

Defining header: `version.hpp` (generated from
`include/sturm/version.hpp.in` via `configure_file`; sourced from the
top-level `project(... VERSION x.y.z ...)` declaration per E4.M1).

| Symbol | Defining header | Description |
|---|---|---|
| `STURM_VERSION_MAJOR` | `version.hpp.in` | Integer literal — the package major version. Usable in `#if STURM_VERSION_MAJOR >= n` guards. |
| `STURM_VERSION_MINOR` | `version.hpp.in` | Integer literal — the package minor version. |
| `STURM_VERSION_PATCH` | `version.hpp.in` | Integer literal — the package patch version. |
| `STURM_VERSION_STRING` | `version.hpp.in` | String literal — the full `"x.y.z"` version string. |

## Operator overloads

`qint_t<W>` and `qbool` overload the standard C++ operators (`+`, `-`,
`*`, `/`, `%`, `&`, `|`, `^`, `~`, `<<`, `>>`, `==`, `!=`, `<`, `<=`,
`>`, `>=`, plus their compound-assignment variants). These operators
are part of the public surface but are not enumerated as separate
symbols in `docs/public_api.txt` because the drift check matches names
(`operator+` is filtered explicitly). Their definitions live in
`qtypes/qint_arith*.hpp`, `qtypes/qint_bitwise*.hpp`,
`qtypes/qint_compare*.hpp`, `qtypes/qint_shift_backend.hpp`,
`qtypes/qbool_logic.hpp`, and `qtypes/qbool_ops.hpp`, all transitively
included from the umbrella `sturm.h`.
