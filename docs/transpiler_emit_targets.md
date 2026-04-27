# Transpiler Emit-Target Classification (E1.M2)

**Status:** Draft (2026-04-27).
**Scope tag:** `packaging-export`.
**Companion docs:** [`prd_packaging_export.md`](prd_packaging_export.md),
[`plan_packaging_export.md`](plan_packaging_export.md).

This document is the human-classified output of E1.M1's enumeration script
(`tools/audit_emit_targets.py`). It enumerates every C++ identifier that the
transpiler splices into rewritten user code, locates the header that defines
the identifier today, and assigns each row a **classification** that gates
downstream packaging work:

- **E2 (header split, `include/sturm/detail/`)** — every row classified
  `internal-public-template-dependency` is `git mv`d under
  `include/sturm/detail/` in E2.M1.
- **E8.M1 (`docs/public_api.md`)** — every row classified `public` becomes a
  bullet in the authoritative public-API list.

## How this doc was generated

```
python3 tools/audit_emit_targets.py > /tmp/emit_targets.tsv
```

> **Post-E2.M1 path note (sturm-nalq.1).** The `defining_header` column of
> the table below preserves the original *pre-E2.M1* paths so the
> `internal-public-template-dependency` reasoning stays readable. The
> on-disk paths after the header split live under `include/sturm/detail/`
> for every internal row (`qtypes/lossy_oop.hpp` →
> `detail/qtypes/lossy_oop.hpp`, every `lib/*_dsl*.hpp` →
> `detail/lib/*_dsl*.hpp`, plus `qtypes/divide_oop.hpp` /
> `qtypes/bit_proxy.hpp` → `detail/qtypes/...`). The companion TSV at
> `docs/transpiler_emit_targets.tsv` is regenerated from the script post-
> move and IS the byte-equal source-of-truth that
> `tools/check_emit_targets_drift.sh` enforces in CI.

Each row of the resulting TSV (`matcher_file`, `emitted_symbol`,
`defining_header`) is reproduced below verbatim, plus a fourth
**classification** column filled in by the reviewer and a **notes** column
with a one-line rationale. The audit script is rerun in CI (E1.M3); any new
row that does not appear here fails the build, forcing a re-classification
pass before merge.

## Classification taxonomy

Per `prd_packaging_export.md` §3.2 / `plan_packaging_export.md` §E1.M2 the
classification column takes exactly one of three values:

- **`public`** — the symbol appears in the user-visible API surface listed in
  PRD §3.3 (the four primitives, `qint`/`qbool`/`WHEN`, `add_mod` /
  `mul_mod` / `pow_mod`, `invert`). Its defining header lives directly under
  `include/sturm/` and ships in the umbrella `<sturm/sturm.hpp>` /
  `<sturm/prelude.hpp>` exports. Users may name the symbol in source code.
- **`internal-public-template-dependency`** — the symbol IS a transpiler
  emit target (it appears verbatim in rewritten user TUs) and therefore its
  defining header MUST stay installed, but users do not write the symbol
  directly. Per PRD §3.2 these headers move to `include/sturm/detail/` in
  E2.M1; the symbol is reachable only because a public template
  transitively `#include`s the detail header at consumer compile time.
- **`move-to-algorithms`** — the symbol belongs in the downstream
  algorithms repository (PRD §2 D1) and should be removed from this
  repository as part of E2 or later. None of the rows in the current TSV
  fall into this bucket; any that appear in future audit runs must be
  reviewed against PRD D1's "language vs. algorithm boundary" rule.
- **`?`** — genuinely ambiguous; the notes column states what the reviewer
  must decide before E2 begins. Never merge a `?` row into E2.

## Emit-target table

| matcher_file | emitted_symbol | defining_header | classification | notes |
|---|---|---|---|---|
| `transpiler/src/lossy_rewrite_emitter.cpp` | `and_oop` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 forward wrapper for `&`; emitted into user TUs by the lossy emitter, not user-callable. The header comment already says it lives behind `sturm::detail::`. |
| `transpiler/src/lossy_rewrite_emitter.cpp` | `mul_oop` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 forward wrapper for `*`; same reasoning as `and_oop`. |
| `transpiler/src/lossy_rewrite_emitter.cpp` | `or_oop` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 forward wrapper for `\|`; same reasoning. |
| `transpiler/src/lossy_rewrite_emitter.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | Headline user-facing class template (PRD §3.3). The audit picks `qint_modular.hpp` as the shortest defining header; the canonical declaration tree is rooted at `qtypes/qint_fwd.hpp` → `qtypes/qint_core.hpp` → `qtypes/qint.hpp`. E2 must keep all of those public. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `and_oop_adj` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 scope-exit adjoint for `and_oop`; only resolved through `STURM_REGISTER_ADJOINT`, never written by users. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `divide_oop_adj` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 scope-exit adjoint for `divide_oop`. The forward `divide_oop` lives in `qtypes/divide_oop.hpp`; both are emit-only helpers. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `invert` | `include/sturm/qtypes/lossy_oop.hpp` | `public` | The audit's shortest-path tie-break picked `lossy_oop.hpp` because it has a `using sturm::invert;` mention; the *real* defining header is `include/sturm/routines/invert.hpp` (PRD §3.3 lists `invert<>` as public). E1.M3 drift check should treat `routines/invert.hpp` as the canonical home; do not move `invert` to detail. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `lib_c_AND_dsl` | `include/sturm/lib/c_and_dsl.hpp` | `internal-public-template-dependency` | DSL building block invoked through the `*_oop` wrappers; emitted into LO-2 cleanup lines but never named by users. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `lib_div_dsl` | `include/sturm/lib/div_dsl.hpp` | `internal-public-template-dependency` | Same pattern: gate-level n-bit division kernel, emit-only. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `lib_mul_dsl` | `include/sturm/lib/mul_dsl.hpp` | `internal-public-template-dependency` | Same pattern: gate-level multiplier kernel, emit-only. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `lib_or_dsl` | `include/sturm/lib/compare_dsl.hpp` | `internal-public-template-dependency` | Audit located `lib_or_dsl` in `compare_dsl.hpp` (it is reused there); the canonical home is `include/sturm/lib/logic_dsl.hpp`. Either way, emit-only — not user-callable. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `mul_oop_adj` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 scope-exit adjoint for `mul_oop`. |
| `transpiler/src/lossy_scope_exit_emitter.cpp` | `or_oop_adj` | `include/sturm/qtypes/lossy_oop.hpp` | `internal-public-template-dependency` | LO-2 scope-exit adjoint for `or_oop`. |
| `transpiler/src/matcher_dropped_quantum_return.cpp` | `qbool` | `include/sturm/control/when_fwd.hpp` | `public` | Headline user-facing type (PRD §3.3, D7). The forward declaration in `when_fwd.hpp` is what the matcher's diagnostic text references; the full definition lives in `qtypes/qbool.hpp`. Both must remain public. |
| `transpiler/src/matcher_dropped_quantum_return.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | See above; canonical tree rooted at `qtypes/qint_fwd.hpp`. |
| `transpiler/src/matcher_lossy_op.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | Same. |
| `transpiler/src/matcher_modular_compound_collapse.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | Same. |
| `transpiler/src/matcher_modular_op.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | Same. |
| `transpiler/src/matcher_modular_pow_mod.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | Same. |
| `transpiler/src/matcher_reversible_drive.cpp` | `invert` | `include/sturm/qtypes/lossy_oop.hpp` | `public` | Canonical home is `include/sturm/routines/invert.hpp` (PRD §3.3). |
| `transpiler/src/matcher_reversible_validate.cpp` | `measure_qubit` | `include/sturm/core/measure.hpp` | `?` | The audit treats this as an emit target, but the matcher only **recognizes** the name (in `name_is_measurement`) — it does not splice `measure_qubit` into rewritten code. Reviewer to decide: (a) make `measure_qubit` a documented public primitive (then keep `core/measure.hpp` public), (b) keep it backend-internal and tighten the audit script's regex to not pick up validator name lists, or (c) move it to the downstream algorithms repository. |
| `transpiler/src/matcher_reversible_validate.cpp` | `sturm_measure` | `<unknown>` | `?` | Same posture as `measure_qubit`. The C-ABI declaration lives in `include/sturm/core/core.h` (a `.h` file, not picked up by the audit's `*.hpp` glob). Reviewer to decide whether to (a) widen the audit to `.h` files and classify `sturm_measure` as public C-ABI, (b) tighten the audit to skip validator name-set literals, or (c) excise the C-ABI surface from the public install set. |
| `transpiler/src/modular_rewrite_emitter.cpp` | `add_mod` | `include/sturm/ops/qint_modular.hpp` | `public` | PRD D2 + §3.3: modular arithmetic free functions are part of the language ABI. |
| `transpiler/src/modular_rewrite_emitter.cpp` | `mul_mod` | `include/sturm/ops/qint_modular.hpp` | `public` | Same. |
| `transpiler/src/modular_rewrite_emitter.cpp` | `pow_mod` | `include/sturm/ops/qint_modular.hpp` | `public` | Same. |
| `transpiler/src/modular_rewrite_emitter.cpp` | `qint_t` | `include/sturm/ops/qint_modular.hpp` | `public` | Same as the other `qint_t` rows. |

## Summary by classification

Total rows in TSV: 26.

- `public`: 13 rows (`qint_t` recurs in seven emitting matchers / emitters,
  `qbool`×1, `add_mod`/`mul_mod`/`pow_mod`×3, `invert`×2).
- `internal-public-template-dependency`: 11 rows (the `*_oop` /
  `*_oop_adj` wrappers — `and_oop`, `mul_oop`, `or_oop`, `and_oop_adj`,
  `divide_oop_adj`, `mul_oop_adj`, `or_oop_adj` — plus the four
  `lib_*_dsl` helpers `lib_c_AND_dsl`, `lib_div_dsl`, `lib_mul_dsl`,
  `lib_or_dsl`).
- `move-to-algorithms`: 0 rows.
- `?`: 2 rows (`measure_qubit`, `sturm_measure` — see notes; both are
  pattern-matched validator names, not splice targets, and the audit
  script's regex does not currently distinguish).

## Action items for downstream epics

- **E1.M3 drift check.** When run in CI, the diff against the committed TSV
  must be zero. If a new emit target appears, this doc must be updated in
  the same PR with a classification (or `?` plus a note).
- **E2.M1 header split.** Move every `internal-public-template-dependency`
  row's defining header under `include/sturm/detail/`. Specifically:
  `include/sturm/qtypes/lossy_oop.hpp`,
  `include/sturm/qtypes/divide_oop.hpp` (transitively, since
  `lossy_oop.hpp` `#include`s it),
  `include/sturm/qtypes/bit_proxy.hpp` (template dependency),
  every `include/sturm/lib/*_dsl*.hpp`, and the dispatch / backend headers
  reachable only through them. The umbrella `sturm.hpp` must continue to
  compile because public headers transitively `#include` the detail tree.
- **E2.M2 lint.** The public-header lint must allow public headers to
  `#include "sturm/detail/..."` (template dependencies need it) but reject
  every other path outside `include/sturm/`.
- **E8.M1 public_api.md.** Generated from the `public` rows above plus
  `WHEN` (a preprocessor macro, not an emit target, so it does not appear
  in the TSV) and the `STURM_VERSION_*` macros from E4.M1.
- **`?` rows.** Resolve before E2 starts. The two open questions are both
  about the validator name-list — preferred resolution is to tighten
  `tools/audit_emit_targets.py` to skip string literals consumed by
  `name_is_*` predicates rather than spliced into emitted text, then re-run
  the audit and confirm both rows disappear.
