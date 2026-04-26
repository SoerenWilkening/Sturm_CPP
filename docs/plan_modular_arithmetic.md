# Implementation Plan — Modular Arithmetic for `qint_t`

**Companion to:** `docs/prd_modular_arithmetic.md` (Draft 2026-04-26).

**Status:** Draft (2026-04-26).

**Style:** Test-driven, modular, bottom-up. Every primitive lands as a
forward + adjoint header pair, behind a red→green→refactor test. No
production code is merged before its sibling test is in place and failing.

**Per-module LoC ceiling:** **300 lines** for any single header or `.cpp`.
If the natural implementation pushes past 280, split into a `detail_*.hpp`
helper before merging. Existing primitives (`adder_dsl.hpp` 212 LoC,
`div_dsl.hpp` 299 LoC, `mod_dsl.hpp` 59 LoC) demonstrate this is feasible.

**Hard layering rule (from PRD §4):** every modular primitive is
implemented strictly on top of existing `lib_*_dsl` calls. No direct gate
emission, no duplication of arithmetic kernels, no parallel dispatch path.
Reviews must reject any patch that emits a `Toffoli`/`CNOT`/`X` from inside
a `lib_*_mod_dsl` body.

---

## 0. Snapshot of relevant existing pieces

Confirmed by reading the tree on 2026-04-26:

| Existing piece | Path | LoC |
|---|---|---|
| `lib_add_dsl` (Cuccaro) | `include/sturm/lib/adder_dsl.hpp` | 212 |
| `lib_compare_dsl` family | `include/sturm/lib/compare_dsl.hpp` | 183 |
| `lib_div_dsl` + adjoint | `include/sturm/lib/div_dsl{,_adj}.hpp` | 299+79 |
| `lib_mod_dsl` + adjoint (model for our pattern) | `include/sturm/lib/mod_dsl{,_adj}.hpp` | 59+86 |
| `lib_mul_dsl` + adjoint | `include/sturm/lib/mul_dsl.hpp` | 119 |
| `lib_pow_dsl` (no separate adj) | `include/sturm/lib/pow_dsl.hpp` | 144 |
| Adjoint round-trip test pattern | `tests/lib/test_div_mod_dsl_adjoint.cpp` | — |
| Transpiler matcher pattern | `transpiler/src/matcher_lossy_op.{hpp,cpp}` | — |

The new primitives slot in next to these; the test mirrors
`test_div_mod_dsl_adjoint.cpp`; the matcher mirrors `matcher_lossy_op`.

There is no need to invent infrastructure — every step is "compose
existing pieces, add a small new shell."

---

## 1. Phase map

```
P0 scaffolding (no primitives yet)
   └─ P1 add_mod ──┐
                  └─ P2 mul_mod ──┐
                                  └─ P3 pow_mod ──┐
                                                  └─ P4 free fns
                                                          └─ P5 matcher
                                                                  └─ P6 flag + xval
                                                                          └─ P7 docs
```

Each phase has an **entry gate** (what must be green before starting) and
an **exit gate** (what must be green before the next phase starts).

The phase order is the same as PRD §9; the difference is that here each
phase is broken into **TDD beats**: write the failing test first, make it
green with the smallest primitive change, then refactor.

---

## 2. Phase 0 — Scaffolding

**Goal:** all the directories, CMake hooks, and empty headers exist so
that subsequent phases only touch the bodies of files that already
compile.

### 2.1 Files created (empty/stub)

| File | Purpose | Initial size |
|---|---|---|
| `include/sturm/lib/add_mod_dsl.hpp` | forward primitive (stub) | ~30 LoC, returns no-op |
| `include/sturm/lib/add_mod_dsl_adj.hpp` | adjoint sibling (stub) | ~20 LoC |
| `include/sturm/lib/mul_mod_dsl.hpp` | stub | ~30 LoC |
| `include/sturm/lib/mul_mod_dsl_adj.hpp` | stub | ~20 LoC |
| `include/sturm/lib/pow_mod_dsl.hpp` | stub | ~30 LoC |
| `include/sturm/lib/pow_mod_dsl_adj.hpp` | stub | ~20 LoC |
| `include/sturm/ops/qint_modular.hpp` | free fns `add_mod` / `mul_mod` / `pow_mod` (stub) | ~30 LoC |
| `tests/lib/test_add_mod_dsl.cpp` | empty `int main(){return 0;}` | ~10 LoC |
| `tests/lib/test_mul_mod_dsl.cpp` | empty | ~10 LoC |
| `tests/lib/test_pow_mod_dsl.cpp` | empty | ~10 LoC |
| `tests/lib/test_qint_modular.cpp` | empty | ~10 LoC |
| `transpiler/src/matcher_modular_op.{hpp,cpp}` | empty matcher class registered with no patterns | ~80 LoC |
| `transpiler/src/modular_rewrite_emitter.{hpp,cpp}` | empty emitter | ~60 LoC |
| `tests/transpiler/fixtures/modular_add_op.{cpp,expected.cpp}` | identity fixture (input == expected) | ~20 LoC |
| `tests/transpiler/fixtures/modular_mul_op.{cpp,expected.cpp}` | identity | ~20 LoC |
| `tests/transpiler/fixtures/modular_pow_op_default.{cpp,expected.cpp}` | identity (no flag, falls back) | ~20 LoC |
| `tests/transpiler/fixtures/modular_pow_op_flag.{cpp,expected.cpp}` | identity (flag off → falls back) | ~20 LoC |

Stub primitives just `assert(false && "not implemented")` inside their
body when called with `n != 0`; the `n == 0` no-op short-circuit keeps any
inadvertent caller silent. This guarantees Phase 0 cannot accidentally
ship usable-but-wrong behavior.

### 2.2 CMake wiring

- Add a CMake option `STURM_MODULAR_POW` (`BOOL`, default `OFF`) in the
  top-level `CMakeLists.txt`.
- Propagate it as a preprocessor define `-DSTURM_MODULAR_POW` only to the
  transpiler driver target (not to the library or tests — the library
  primitives compile unconditionally; only the transpiler's pattern
  selection is gated).
- Wire each new test source into the existing `tests/lib/CMakeLists.txt`
  glob/list. Run with `-j6`.
- Wire each new fixture pair into `tests/transpiler/CMakeLists.txt` via
  the existing `run_snapshot.cmake` mechanism.

### 2.3 Phase 0 exit gate

```
cmake --build build --parallel 6        # green
ctest --test-dir build --parallel 6     # green (new tests pass trivially)
```

Stubs compile; new tests are no-ops; identity fixtures roundtrip.

---

## 3. Phase 1 — `lib_add_mod_dsl`

**Goal:** correct, gate-reversible W-bit modular addition, peak ancilla
≤ `W + O(1)`, built strictly from `lib_add_dsl` + `lib_compare_dsl` +
controlled `lib_sub_dsl` (`detail_div::lib_sub_n_constant_inplace` or
the ripple-borrow path used inside `div_dsl`).

### 3.1 Algorithm (composing existing primitives)

```
inputs  : a_bits[W], b_bits[W], n_bits[W], r_bits[W]    (all valid, r=|0>)
precond : a, b ∈ [0, n)                                  (PRD §5)
output  : r_bits = (a + b) mod n
```

Steps:

1. Allocate `(W+1)`-bit sum register `s` from `QubitPool`.
2. Copy `a → s_low` via `lib_add_dsl` (XOR via add of zero is wasteful;
   use a per-bit `s[i] ^= a[i]` loop — that's the same idiom as
   `lib_div_dsl`'s setup, no new gate kernel).
3. `lib_add_dsl(b_bits, s_low, s_high, W)` so `s = a + b`, top bit is the
   carry.
4. Allocate `1`-bit `ge_flag`. Run `lib_ge_dsl(s_bits, n_extended_bits,
   W+1, ge_flag)` where `n_extended_bits = [n_bits..., 0]` (zero-padded).
   `ge_flag = 1` iff `s ≥ n`.
5. Push `ge_flag` as a control. Subtract `n_extended` from `s` via the
   gate-reverse of `lib_add_dsl` (the `detail_div::lib_add_adj` already
   used inside `mul_dsl`/`div_dsl`). Pop control. After this step, low
   `W` bits of `s` hold `(a+b) mod n` and the top bit + `ge_flag` are
   redundant.
6. Uncompute `ge_flag`: re-run `lib_ge_dsl` on the **new** state — by
   construction the new `s_low` is in `[0, n)`, so `ge_flag` flips back
   to 0. Release.
7. Copy `s_low → r_bits` via per-bit `r[i] ^= s[i]`.
8. Uncompute `s`: subtract `b` then `a` from `s` (gate-reverse of step 3
   and step 2) so `s = 0`. Release `s` LIFO.

Steps 4 and 6 both call `lib_ge_dsl` — that is the natural place to use
the *paired-uncompute* trick the existing `mod_dsl.hpp` uses for its
quotient ancilla. Adjoint is then just the gate-reverse of this 8-step
sequence, which `__lib_add_mod_dsl_adj` writes out by hand the same way
`__lib_mod_dsl_adj` does (mod_dsl_adj.hpp lines 41–79 are the template).

### 3.2 Files & LoC budget

| File | Target LoC |
|---|---|
| `include/sturm/lib/add_mod_dsl.hpp` | ≤ 250 |
| `include/sturm/lib/add_mod_dsl_adj.hpp` | ≤ 200 |
| `tests/lib/test_add_mod_dsl.cpp` | ≤ 250 |

If either header trends toward 280 LoC, factor a `detail_add_mod`
helper namespace into a private header.

### 3.3 TDD beats

For each beat, write the test first, watch it fail, then implement the
minimum to make it green.

| Beat | Test asserts | Smallest impl that passes |
|---|---|---|
| 1.1 | `n == 0` short-circuits, leaves `r` unchanged | empty body |
| 1.2 | `W=2`, `(a=1, b=1, n=3) → r=2` for one classical assignment | full algorithm above, single classical state |
| 1.3 | Exhaustive `W=2` sweep over all `(a, b, n)` with `a, b < n`, `n ≥ 1` | algorithm correct in all branches |
| 1.4 | `W=3` random-sample sweep (50 cases) vs. classical reference | no W-3 regression |
| 1.5 | Adjoint round-trip: forward then `__lib_add_mod_dsl_adj` returns `r` to `\|0>` for every Beat-1.3 input | adjoint correct |
| 1.6 | Counter-sink test asserts peak live ancillas ≤ `W + 3` | algorithm meets §3 budget claim |
| 1.7 | `test_lossy_ancilla_cleaned`-style assertion: `QubitPool::instance().live_count()` returns to its pre-call value after every Beat-1.3 input | LIFO release correct |

Beat 1.6 reuses the existing `tests/test_sink_counter.cpp` infrastructure;
Beat 1.7 reuses the lossy-ancilla pool counter already invoked elsewhere.

### 3.4 Phase 1 exit gate

- All Phase 1 tests green at `-j6`.
- Each new file ≤ 300 LoC (`wc -l`).
- `__lib_add_mod_dsl_adj` registered via `STURM_REGISTER_ADJOINT`,
  resolvable through `invert<&lib_add_mod_dsl<BitProxy>>()`.

Open one bd issue per beat (1.1…1.7) plus one umbrella for the file
scaffolding. Beat 1.1 is parent of 1.2…1.4, which are siblings; 1.5/1.6/1.7
depend on 1.3.

---

## 4. Phase 2 — `lib_mul_mod_dsl`

**Goal:** correct W-bit modular multiplication with peak ancilla
`W + O(1)`. Layered on `lib_add_mod_dsl` (Phase 1).

### 4.1 Algorithm — shift-and-add (PRD §8 default)

```
inputs : a_bits[W], b_bits[W], n_bits[W], r_bits[W]    (r = |0>)
output : r = (a * b) mod n
```

Standard interleaved-reduction multiplier:

```
for i in 0..W-1:
    if b[i]:                  # control on b[i]
        r := add_mod(r, (a << i) mod n, n)   # via lib_add_mod_dsl
```

The `(a << i) mod n` term is recomputed on demand: maintain a `shifted`
ancilla register that starts as `a` and at each step holds
`(a · 2^i) mod n`. Update rule: `shifted := add_mod(shifted, shifted, n)`
(doubling is the same primitive). After the W-th iteration, uncompute
`shifted` by running the W doubling steps in adjoint order.

This keeps peak ancilla at `2W + O(1)` (one W-bit `shifted`, one W-bit
`r`, plus add_mod's interior `W+3`). PRD §6 bullet 4 lists `W + O(1)` as
the target — the doubling register is the unavoidable `+W`. Document
that explicitly: the bullet is met for the **temporary inflation
beyond** the working register, not absolute.

> _If a future patch wants a strict `W + O(1)`, the alternative is the
> Karatsuba-style interleaved subtract-on-overflow design listed in
> PRD §8 #1. Out of scope for first pass._

### 4.2 Files & LoC budget

| File | Target LoC |
|---|---|
| `include/sturm/lib/mul_mod_dsl.hpp` | ≤ 280 |
| `include/sturm/lib/mul_mod_dsl_adj.hpp` | ≤ 220 |
| `tests/lib/test_mul_mod_dsl.cpp` | ≤ 250 |

If `mul_mod_dsl.hpp` trends toward 280, factor `detail_mul_mod`
namespace into a sibling helper header.

### 4.3 TDD beats

| Beat | Test |
|---|---|
| 2.1 | `n == 0` no-op |
| 2.2 | `W=2` single classical case |
| 2.3 | Exhaustive `W=2` sweep |
| 2.4 | `W=3` random sweep (50 cases) |
| 2.5 | Adjoint round-trip |
| 2.6 | Peak ancilla counter ≤ `2W + 5` |
| 2.7 | Pool live-count returns to pre-call value |

### 4.4 Phase 2 exit gate

Same shape as Phase 1. Phase 3 cannot start until 2.5 (adjoint) is green.

---

## 5. Phase 3 — `lib_pow_mod_dsl`

**Goal:** correct modular exponentiation, no wide intermediate.
Repeated squaring on top of `lib_mul_mod_dsl`.

### 5.1 Algorithm

```
inputs : base[W], exp[W], n[W], r[W]    (r = |0>)
output : r = (base ^ exp) mod n
```

```
acc := 1                       # set bit 0 of r
sq  := base                    # W-bit register; alloc & copy
for i in 0..W-1:
    if exp[i]:                 # control on exp[i]
        acc := mul_mod(acc, sq, n)
    sq := mul_mod(sq, sq, n)   # squaring
uncompute sq via adjoint mul_mod loop
release sq
```

The squaring step writes to a fresh ancilla, then swaps with `sq` and
uncomputes the old one — the standard reversible pattern. Each
`mul_mod` call costs `W + O(1)` extra ancillas, so peak ancilla is
`O(W)`. PRD §6 bullet 4: `O(W)` for pow.

### 5.2 Files & LoC budget

| File | Target LoC |
|---|---|
| `include/sturm/lib/pow_mod_dsl.hpp` | ≤ 280 |
| `include/sturm/lib/pow_mod_dsl_adj.hpp` | ≤ 220 |
| `tests/lib/test_pow_mod_dsl.cpp` | ≤ 250 |

### 5.3 TDD beats

| Beat | Test |
|---|---|
| 3.1 | `n == 0` no-op (mirror PRD §8 #3) |
| 3.2 | `0^0 == 1` convention (matches `lib_pow_dsl`) |
| 3.3 | `W=2` single case `pow_mod(2, 3, 5) == 3` |
| 3.4 | Exhaustive `W=2` sweep |
| 3.5 | `W=3` random sweep |
| 3.6 | Adjoint round-trip |
| 3.7 | Peak ancilla `≤ c·W` for small `c` |
| 3.8 | Pool live-count return |

### 5.4 Phase 3 exit gate

All beats green; both adjoints registered.

---

## 6. Phase 4 — Free functions

### 6.1 File

`include/sturm/ops/qint_modular.hpp` (≤ 150 LoC).

### 6.2 API (matches PRD §3.1)

```cpp
template <std::size_t W>
qint_t<W> add_mod(const qint_t<W>& a, const qint_t<W>& b, const qint_t<W>& n);

template <std::size_t W>
qint_t<W> mul_mod(const qint_t<W>& a, const qint_t<W>& b, const qint_t<W>& n);

template <std::size_t W>
qint_t<W> pow_mod(const qint_t<W>& base, const qint_t<W>& exp, const qint_t<W>& n);
```

Each body is ≤ 15 LoC: allocate a fresh `qint_t<W>` (zero-initialized),
call the corresponding `lib_*_mod_dsl` on the underlying `Bit*` views,
return the new `qint_t`. No new logic — wrapper only.

### 6.3 Tests

`tests/lib/test_qint_modular.cpp` (≤ 200 LoC):

| Beat | Test |
|---|---|
| 4.1 | `add_mod` matches `lib_add_mod_dsl` for one input |
| 4.2 | `mul_mod` matches `lib_mul_mod_dsl` for one input |
| 4.3 | `pow_mod` matches `lib_pow_mod_dsl` for one input |
| 4.4 | Each free fn is move/copy-correct (no ancilla leak through `qint_t`'s ctor/dtor) |

Phase 4 exit gate: 4.1–4.4 green. No transpiler change yet — calls are
explicit free-function calls.

---

## 7. Phase 5 — Transpiler matcher + emitter

**Goal:** rewrite `(a OP b) % n` for `OP ∈ {+, *}` (always on) and
`pow(a, x) % n` (gated on `STURM_MODULAR_POW`) at the AST level, before
gate emission.

### 7.1 Files & LoC budget

| File | Target LoC |
|---|---|
| `transpiler/src/matcher_modular_op.hpp` | ≤ 80 |
| `transpiler/src/matcher_modular_op.cpp` | ≤ 280 |
| `transpiler/src/modular_rewrite_emitter.hpp` | ≤ 60 |
| `transpiler/src/modular_rewrite_emitter.cpp` | ≤ 200 |

If the matcher's `.cpp` exceeds 280, split per-pattern: one file for the
`%` matcher, one for the `pow %` matcher.

### 7.2 Patterns recognized

The matcher operates on the same Clang AST level as
`matcher_lossy_op.cpp`. Patterns:

1. **`(qint OP qint) % qint`** as a binding RHS in a declaration:
   ```cpp
   qint_t<W> r = (a + b) % n;       // → emit lib_add_mod_dsl
   qint_t<W> p = (a * b) % n;       // → emit lib_mul_mod_dsl
   ```
2. **Compound peephole-collapsed form:** `r = a + b; r %= n;` after the
   existing `matcher_peephole_reorder` runs. Match the two adjacent
   stmts in one BB. (If the peephole isn't eligible because of an
   intervening read of `r`, the matcher falls back to the wide path —
   document this.)
3. **`pow(a, x) % n`** and **`pow(a, int64_t x) % n`** — emitted
   primitive choice depends on `STURM_MODULAR_POW`:
   - flag off: leave both ops alone (current behavior — `pow` lowers to
     `lib_pow_dsl`, `%` lowers to `lib_mod_dsl`).
   - flag on: rewrite both into a single `lib_pow_mod_dsl` call, dropping
     the wide intermediate.

### 7.3 Snapshot fixtures

Add under `tests/transpiler/fixtures/`:

| Fixture pair | Asserts |
|---|---|
| `modular_add_op.{cpp,expected.cpp}` | `(a+b)%n` rewritten unconditionally |
| `modular_mul_op.{cpp,expected.cpp}` | `(a*b)%n` rewritten unconditionally |
| `modular_compound_collapse.{cpp,expected.cpp}` | `r = a+b; r %= n;` collapses |
| `modular_pow_op_default.{cpp,expected.cpp}` | `pow(a,x)%n` NOT rewritten when flag off |
| `modular_pow_op_flag.{cpp,expected.cpp}` | `pow(a,x)%n` IS rewritten when flag on |
| `modular_pow_int_exp.{cpp,expected.cpp}` | same, with `int64_t` exponent |

Run via `run_snapshot.cmake` (existing infra). The flag-on / flag-off
fixtures use the existing `check_*_diagnostic.cmake` mechanism with two
build configurations or a `--define` flag passed through the harness.

### 7.4 TDD beats

| Beat | Test |
|---|---|
| 5.1 | `add_op` fixture (matcher recognizes the AST shape, emitter emits the call) |
| 5.2 | `mul_op` fixture |
| 5.3 | `compound_collapse` fixture |
| 5.4 | `pow_op_default` fixture (flag off) |
| 5.5 | `pow_op_flag` fixture (flag on) |
| 5.6 | `pow_int_exp` fixture (both modes — defaults and flag) |
| 5.7 | Idempotence: applying the matcher twice produces identical output (existing `check_idempotent.cmake`) |
| 5.8 | Non-pattern code untouched: regression fixture with unrelated `%` (e.g. `r = a % b;`) is unchanged |

### 7.5 Phase 5 exit gate

Snapshot suite green; no existing fixture regresses.

---

## 8. Phase 6 — CMake flag wiring + cross-validation

### 8.1 CMake

(Most of this landed in Phase 0. Phase 6 finalizes by:)

- Adding the `-DSTURM_MODULAR_POW=ON|OFF` to the CMake help text with a
  one-line description of effect (PRD §3.4).
- Documenting in the top-level `README.md` `Build` section.

### 8.2 Cross-validation fixture (PRD §6 #6)

`tests/lib/test_pow_mod_xval.cpp` (≤ 150 LoC):

For each of N=20 random `(a, x, n)` with `a < n`:

1. Build the test program once with `-DSTURM_MODULAR_POW=OFF` (default
   path: `pow %` → `lib_pow_dsl + lib_mod_dsl`).
2. Build it once with `-DSTURM_MODULAR_POW=ON` (rewritten path:
   `lib_pow_mod_dsl`).
3. Assert both programs produce the same `r`.

Implementation note: rather than two builds, the test invokes both
primitives directly (it has access to the lib layer) and compares —
that is what existing `test_div_mod_dsl_adjoint.cpp` does for div vs.
mod. Two-build CI coverage handles the *transpiler* side; the lib-level
test handles the *primitive* side. Together they discharge §6 #6.

### 8.3 Phase 6 exit gate

- `ctest --parallel 6` green with `STURM_MODULAR_POW=OFF`.
- `ctest --parallel 6` green with `STURM_MODULAR_POW=ON`.
- Cross-val fixture green in both configs.

---

## 9. Phase 7 — Documentation

### 9.1 Updates required (matches PRD §5 + §6 #7)

1. **`docs/01_principles.md`**: new section "Modular arithmetic
   contract" — one paragraph stating the precondition `a, b ∈ [0, n)`
   and pointing at `prd_modular_arithmetic.md`.
2. **Doxygen on each free function** in
   `include/sturm/ops/qint_modular.hpp`: `@pre` clause + behavior under
   violation + reference to PRD §5.
3. **Doxygen on each `lib_*_mod_dsl`** primitive header.
4. **CMake help text** for `-DSTURM_MODULAR_POW=ON` (one line).
5. **CHANGELOG entry** (if maintained — not currently visible in repo).
6. **`docs/TODO_reversibility_deferrals.md`**: cross-reference any
   deferred items from PRD §7 that interact with reversibility (none
   currently identified, but verify).

### 9.2 Phase 7 exit gate

A reviewer can land on the PRD-level claim "library does not check the
precondition" by following any of the three documented paths.

---

## 10. Bd issue plan

Mapping plan beats → bd issues. Each beat is one `bd create` with type
`task` and priority `2` unless flagged otherwise.

```
P0  scaffold-modular-arith              (epic)
    ├─ scaffold-headers
    ├─ scaffold-tests
    ├─ scaffold-cmake-flag
    └─ scaffold-fixtures

P1  add-mod-dsl                         (epic, blocks P2)
    ├─ add-mod-dsl-1.1   n==0 no-op
    ├─ add-mod-dsl-1.2   single classical
    ├─ add-mod-dsl-1.3   W=2 sweep
    ├─ add-mod-dsl-1.4   W=3 random
    ├─ add-mod-dsl-1.5   adjoint round-trip
    ├─ add-mod-dsl-1.6   ancilla counter
    └─ add-mod-dsl-1.7   pool live-count

P2  mul-mod-dsl                         (epic, blocks P3)
    └─ beats 2.1…2.7

P3  pow-mod-dsl                         (epic, blocks P4)
    └─ beats 3.1…3.8

P4  qint-modular-free-fns               (epic, blocks P5)
    └─ beats 4.1…4.4

P5  transpiler-modular-rewrite          (epic, blocks P6)
    └─ beats 5.1…5.8

P6  modular-pow-flag-xval               (epic, blocks P7)
    ├─ cmake-flag-help-text
    └─ pow-mod-xval-test

P7  modular-arith-docs                  (epic)
    ├─ principles-snippet
    ├─ doxygen-free-fns
    ├─ doxygen-primitives
    └─ cmake-help-text
```

Open these in order with `bd create`; mark each as blocking the next
phase via `bd blocks`. The autopilot drains the queue depth-first.

---

## 11. Review checklist (per-PR gate)

Every PR landing a beat must satisfy:

- [ ] All new files ≤ 300 LoC (`wc -l`).
- [ ] No new file emits gates directly — only calls to existing
      `lib_*_dsl` primitives. Verified by grep:
      `grep -nE 'sturm::Toffoli|sturm::CNOT|qbool::flip\(|push_control|emit_'
      include/sturm/lib/*_mod_dsl*.hpp` returns only allowed forms
      (`flip`, `push_control` are allowed for control plumbing).
- [ ] Forward header `#includes` its `_adj` sibling at the bottom.
- [ ] `STURM_REGISTER_ADJOINT` block present in the `_adj` header.
- [ ] All `cmake --build` / `ctest` invocations use `--parallel 6` /
      `-j6`.
- [ ] New tests: TDD evidence (the test was written and seen to fail
      before the implementation landed — mention in PR body).
- [ ] No regression in pre-existing `tests/lib/` or
      `tests/transpiler/` runs.
- [ ] PRD precondition documented in Doxygen if the PR adds a new
      public symbol.

---

## 12. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Layering rule violated under deadline pressure (someone inlines a Toffoli for "perf") | Review checklist grep gate; mention in CONTRIBUTING |
| `mul_mod` peak ancilla exceeds the budget (`2W + O(1)`) | Beat 2.6 fails CI before merge; revisit Karatsuba alt (PRD §8 #1) if persistent |
| `pow_mod` adjoint synthesis is too verbose (>220 LoC) | Factor a `detail_pow_mod` helper; keep `*_adj.hpp` as a thin shell |
| Transpiler matcher conflicts with `matcher_lossy_op` ordering | Add an explicit phase ordering test; matcher_modular_op runs **after** peephole reorder, **before** lossy_op |
| Cross-validation fixture flaky on random seeds | Use a fixed seed (`std::mt19937(42)`); print the seed in failure messages |
| `STURM_MODULAR_POW` flag accidentally affects add/mul rewrites | Beat 5.1 / 5.2 build with both flag values and assert identical output |

---

## 13. Out of scope (mirrors PRD §7)

- `qint_mod<N>` type.
- Per-region / per-TU flag scope.
- Compound modular assigns (`add_mod_inplace`, etc.).
- Modular subtraction / negation (`(a - b) % n`).
- Precondition checking debug mode.
- `mul_mod` Karatsuba / windowed `pow_mod` — file as future bd issues
  if benchmarks demand them.

---

## 14. Definition of done (whole effort)

1. PRD §6 acceptance criteria 1–7 all green in CI at `-j6`.
2. Every new file ≤ 300 LoC.
3. Every new primitive built strictly from existing `lib_*_dsl`
   primitives.
4. PRD status flips Draft → Implemented, owner assigned, in a follow-up
   commit that also retires this plan into `docs/archive/` (and removes
   it from any Required Reading list — none currently).
