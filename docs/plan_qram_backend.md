# Implementation Plan — QRAM backend gate emission (QROM path, v1)

**Status.** Draft, 2026-05-03.
**Tracks.** `docs/prd_qram_backend.md` §9 milestones B1–B5.
**Predecessors.** `docs/archive/prd_qram_subscript.md` (frontend epic
`sturm-u9ge`, closed) and its beat plan `docs/archive/plan_qram_subscript.md`.
**Related principles.** P5 (DSL primitive set), P9 / P9c (adjoint
synthesis), B5a (depth-1 control), B7 (qubit-index ownership), B10
(uncomputation is compile-time).

---

## §0 Reading guide

The plan decomposes the PRD into **bd issues** grouped into ordered
**beats**. Each beat is a self-contained, **test-driven** landing:

1. **Test first** — every beat opens by writing the test module that
   pins the contract the production module must satisfy. The test
   compiles and *fails* (or fails to link) before any production code
   is written. Production code lands until the test goes green; only
   then is the beat closed.
2. **One production module per beat.** Hard cap **≤ 300 LoC** including
   comments and blank lines.
3. **One test module per beat.** Same 300-LoC ceiling. If a single
   matrix needs more, split along an axis (e.g. one file per `(N,W)`
   pair) — never relax the cap.
4. Beats land in dependency order. Every beat is green (full library
   + qram + transpiler test suite passing under `ctest --parallel 6`)
   before the next is opened.

LoC budgets are advisory ceilings. If a module would exceed 300 LoC,
split it along the seam noted in its description.

The plan will be filed as a bd epic (suggested mnemonic
`sturm-qram-be`); children use numeric suffixes
(`sturm-qram-be.1` … `sturm-qram-be.N`). The mnemonic beat names below
(B1–B5, plus T0/T-prep prerequisites) survive as documentation; the bd
ids are the authoritative work tracker — see §1a once filed.

**Threading limit (CLAUDE.md).** Every `cmake`, `ctest`, `make`, and
`ninja` invocation is capped at **6 parallel threads**. Restate this in
every subagent prompt.

---

## §1 Module map (bird's-eye view)

| Layer        | New / touched file                                           | Cap | Beat |
|--------------|--------------------------------------------------------------|-----|------|
| Sink iface   | `include/sturm/core/sink.hpp` (touched)                      | —   | B0   |
| Counter sink | `include/sturm/core/counter_sink.hpp` (touched)              | —   | B0   |
| Recorder     | `include/sturm/core/recording_sink.hpp` (touched)            | —   | B0   |
| Test         | `tests/qram/test_qram_split_counters.cpp` (new)              | 200 | B0   |
| Runtime hdr  | `include/sturm/qram/qram_read.hpp` (touched)                 | 250 | B1   |
| Runtime impl | `src/sturm/qram/qram_read.cpp` (touched)                     | 200 | B1   |
| Test         | `tests/qram/test_qram_read_dispatch.cpp` (new)               | 250 | B1   |
| DSL forward  | `include/sturm/detail/lib/qram_read_dsl.hpp` (new)           | 300 | B2   |
| DSL helper   | `include/sturm/detail/lib/qram_read_predicate.hpp` (new)     | 200 | B2a  |
| Test         | `tests/lib/test_qram_read_predicate.cpp` (new)               | 250 | B2a  |
| Test         | `tests/lib/test_qram_read_dsl_recording.cpp` (new)           | 300 | B2   |
| Test         | `tests/lib/test_qram_read_dsl_simulate.cpp` (new)            | 300 | B2   |
| DSL adjoint  | `include/sturm/detail/lib/qram_read_dsl_adj.hpp` (new)       | 150 | B3   |
| Test         | `tests/lib/test_qram_read_dsl_adjoint.cpp` (new)             | 250 | B3   |
| Telemetry    | `include/sturm/core/sink.hpp` (touched, split hooks)         | —   | B4   |
| Telemetry    | `include/sturm/core/counter_sink.hpp` (touched)              | —   | B4   |
| Telemetry    | `include/sturm/core/recording_sink.hpp` (touched)            | —   | B4   |
| Test         | `tests/qram/test_qram_telemetry_split.cpp` (new)             | 200 | B4   |
| E2E gate     | `tests/qram/test_qram_read_qrom_gates.cpp` (new)             | 300 | B5   |
| Build glue   | `tests/qram/CMakeLists.txt` (touched)                        | —   | B0–B5|
| Build glue   | `tests/lib/CMakeLists.txt` (touched)                         | —   | B2–B3|

Existing tests retained, must remain green:

- `tests/qram/test_qram_read_stub.cpp` — umbrella `qram_read_count` still
  bumps once per call (PRD G4).
- `transpiler/tests/test_qram_e2e.cpp` — frontend rewrite + dispatch
  end-to-end.
- `tests/backend/test_acceptance_prd.cpp` — backend acceptance pins.

---

## §1a Beat → bd-issue id map

| Beat | bd id          | What it is                                                |
|------|----------------|-----------------------------------------------------------|
| —    | sturm-2w6h     | epic                                                      |
| B0   | sturm-2w6h.1   | split telemetry counters (`qrom_read` / `qreg_read`)      |
| B1   | sturm-2w6h.2   | refactor impls to receive `(a, n, i, b)`                  |
| B2a  | sturm-2w6h.3   | predicate compute/uncompute helper                        |
| B2   | sturm-2w6h.4   | sequential XOR-fanout DSL body                            |
| B3   | sturm-2w6h.5   | adjoint sibling header + `STURM_REGISTER_ADJOINT`         |
| B4   | sturm-2w6h.6   | wire split counters through QROM/qreg dispatch            |
| B5   | sturm-2w6h.7   | end-to-end gate-stream + statevector test                 |

---

## §2 Dependency graph

```
            ┌─────────────────────────┐
            │ B0  split-counter sink  │  (telemetry surface, no QROM yet)
            └──────────┬──────────────┘
                       │
            ┌──────────▼──────────────┐
            │ B1  arg-passing impls   │  (refactor — counter-mode preserved)
            └──────────┬──────────────┘
                       │
            ┌──────────▼──────────────┐
            │ B2a predicate helper    │  (reusable eq_k compute/uncompute)
            └──────────┬──────────────┘
                       │
            ┌──────────▼──────────────┐
            │ B2  XOR-fanout body     │  (full QROM body in DSL form)
            └──────────┬──────────────┘
                       │
            ┌──────────▼──────────────┐
            │ B3  adjoint sibling     │  (P9c registration)
            └──────────┬──────────────┘
                       │
            ┌──────────▼──────────────┐
            │ B4  wire split counters │  (qrom_read fires from QROM body)
            └──────────┬──────────────┘
                       │
            ┌──────────▼──────────────┐
            │ B5  e2e gate / sim test │  (closes the v1 contract)
            └─────────────────────────┘
```

B0 is independent of the rest of the backend body and lands first so
that B2/B5 can pin the new counters from the start. Its new tests are
purely about the sink surface — they do not require the QROM body.

---

## §3 Test discipline

The PRD lists five goals (G1–G5). Each maps to a *named* test that lives
with a specific beat:

| Goal | Test                                             | Beat |
|------|--------------------------------------------------|------|
| G1   | `tests/lib/test_qram_read_dsl_simulate.cpp`      | B2   |
| G1   | `tests/qram/test_qram_read_qrom_gates.cpp`       | B5   |
| G2   | `tests/lib/test_qram_read_dsl_recording.cpp`     | B2   |
| G3   | `tests/lib/test_qram_read_dsl_adjoint.cpp`       | B3   |
| G3   | `tests/qram/test_qram_read_qrom_gates.cpp`       | B5   |
| G4   | `tests/qram/test_qram_split_counters.cpp`        | B0   |
| G4   | `tests/qram/test_qram_telemetry_split.cpp`       | B4   |
| G5   | `tests/qram/test_qram_read_qrom_gates.cpp`       | B5   |

A goal is **not** considered satisfied until its named test is green
and committed.

### §3.1 Statevector simulation policy

Where the gate budget fits the orkan 17-qubit budget, the test runs the
QROM body against a real statevector simulator (`OrkanBridge` /
`SIMULATE` mode) and asserts on basis-state probabilities — see
`tests/backend/test_bitwise_and_or_simulate.cpp` for the existing
pattern. The QROM body for `(N=4, W=4)` uses:

- `W = 4` bits for `i`, `W = 4` bits for `b`, `4 · 4 = 16` bits for the
  classical container `a` (these are *fully classical*, mask = 0, so
  they consume no simulator qubits — they live as `int64_t` values
  inside the qint),
- 1 predicate ancilla (`eq_k`),
- up to `W − 2 = 2` `c_n_AND` internal ancillas (per
  `c_and_dsl.hpp:96-114`),

for a peak of `4 + 4 + 1 + 2 = 11` simulator qubits — comfortably under
17. For larger `(N, W)` the simulate test skips with a printed
explanation; the recording-sink test still pins the gate stream.

### §3.2 Recording-sink discipline

Each gate-stream test installs a `RecordingSink` via `ScopedSink`,
clears the records, runs `QRAM_read`, then asserts:

1. **Gate-set membership.** Every record's `op` is one of `quantum_xor`
   (CX/CCX class) or `quantum_and` (CCX class via `qbool::operator&`)
   — never a rotation, prepare, or measurement op.
2. **Counter parity.** `current_sink()->get_count("qrom_read") == 1`,
   `..."qreg_read") == 0`, and umbrella
   `qram::qram_read_count() == 1`.
3. **Index immutability.** `i.super_mask` unchanged (entry vs. exit).
4. **Exact gate budget** for one representative `(N, W)` pair (B5
   asserts the value computed in §4).

---

## §4 Gate-budget cheat sheet

For `(N, W)` with `K = ⌈log₂ N⌉` (the active address bits), per call to
the QROM body:

- **Predicate compute** (per `k`): `K` X-flips on `i`'s active bits to
  re-encode `eq_k`, then `lib_c_n_AND_dsl(K controls, eq_k)` =
  `2K − 3` CCX (for `K ≥ 3`) or 1 CCX (`K = 2`) or 1 CX (`K = 1`).
  Then `K` X-flips to undo. *Reusing one shared `eq_k` across `k` is
  safe because we uncompute it before incrementing `k`.*
- **XOR-fanout payload** (per `k`): for each set bit `j` of `a[k]`,
  one CX (the `WHEN(eq_k)` lift turns `b[j].flip()` into a CX
  controlled on `eq_k`). Worst case `W` CX per `k`.
- **Predicate uncompute**: re-runs the compute step. Same cost.

Total per call (worst case, all `a[k]` filled):

```
N · ( 2 · (2K − 3 + 2K)  +  W )  CX/CCX  =  N · (8K − 6 + W)  primitives
```

For `(N, W) = (4, 4)`: `K = 2`, so `4 · (8·2 − 6 + 4) = 4 · 14 = 56`
primitives worst case (every bit of every `a[k]` set). For the test
case `a = {0xA, 0x5, 0xF, 0x0}` the per-`k` payload counts are
`popcount(a[k]) = {2, 2, 4, 0}`, giving an exact recorded count the
test pins by computation, not by hardcoding.

The B5 test computes the expected count from `(N, W, popcount(a[k]))`
and asserts equality — no magic numbers in the test.

---

## §5 Beats

### Beat B0 — Split telemetry counters land on `Sink`

**Goal.** Add `qrom_read()` / `qreg_read()` virtual hooks to `Sink`,
override on `CounterSink` (bumps `qrom_read` / `qreg_read` counters)
and on `RecordingSink` (records as ops). Keep the existing
`qram_read()` hook as a non-pure-virtual no-op for ABI continuity.
QROM/qreg helpers do **not** call the new hooks yet — that lands in
B4 — but the surface is in place.

**Tests** (`tests/qram/test_qram_split_counters.cpp`):
- `CounterSink::get_count("qrom_read") == 0` initially.
- After explicitly calling `current_sink()->qrom_read()` once, count
  is 1; `qreg_read` still 0.
- Symmetric for `qreg_read`.
- Existing umbrella `qram_read` counter still observable.

**Production:** ~30 lines across the three sink headers.

**Done when:** new test green; full suite green (umbrella tests
unchanged).

### Beat B1 — Refactor impls to receive `(a, n, i, b)`

**Goal.** Change the two TU-private helpers
`_qram_detail::qram_read_qrom_impl` and `qram_read_qreg_impl` from
no-arg shims into templates parameterised on `(W)` taking
`(const qint_t<W>* a, std::size_t n, const qint_t<W>& i, qint_t<W>& b)`.
The three public `QRAM_read` overloads in `qram_read.hpp` and the
three `__QRAM_read_adj` overloads forward `(a, i, b)` (and `n`,
inferred from `N` for the array shapes) to the helper instead of
discarding them. Bodies remain counter-mode bumps in this beat — the
arguments are forwarded but unused, so behaviour is unchanged.

**Tests** (`tests/qram/test_qram_read_dispatch.cpp`):
- All three container shapes (`std::array`, C-array, pointer + n)
  still bump the umbrella `qram_read_count` exactly once.
- Mask-OR dispatch still routes correctly — set `super_mask` on a
  middle element of the container and assert the qreg path fires.
- `i.super_mask` unchanged across the call (will become a hard pin in
  B5; here it's a regression guard for the refactor).
- Argument forwarding is observable: a sentinel record from a custom
  sink that captures the address of `a[0]` matches the caller's
  address. (Lightweight test sink subclass within the test file.)

**Production:** ~40 lines in `.hpp` (template helper signatures) +
~30 lines in `.cpp`. No behaviour change beyond passing args.

**Done when:** new + existing tests green.

### Beat B2a — Predicate compute/uncompute helper

**Goal.** Factor the predicate-ancilla compute/uncompute step out of
the QROM body. New header
`include/sturm/detail/lib/qram_read_predicate.hpp` provides

```cpp
template <std::size_t W>
inline void lib_qram_eq_k_compute(qint_t<W>& i, std::size_t k,
                                  std::size_t K, qbool& eq_k);

template <std::size_t W>
inline void lib_qram_eq_k_uncompute(qint_t<W>& i, std::size_t k,
                                    std::size_t K, qbool& eq_k);
```

The compute step bit-flips bits of `i` whose corresponding bit of `k`
is `0` (using `i.bit(j).flip()`), AND-reduces the active `K` bits onto
`eq_k` via `lib_c_n_AND_dsl`, then bit-flips back. The uncompute step
is the same body — predicate is self-inverse — and is provided as a
named entry-point so the QROM body and its adjoint both call by name
(supports placement-audit tooling, P9c spirit).

The helper does **not** modify `i` net — every `flip()` is paired —
but it does temporarily perturb `i`'s value bits during the call.
That's fine: the qubits are unchanged at scope exit.

**Tests** (`tests/lib/test_qram_read_predicate.cpp`):
- For `K = 1, 2, 3, 4` and every classical `i ∈ [0, 2^K)` and every
  `k ∈ [0, 2^K)`, after compute `eq_k.value == (i == k)`.
- After `compute`+`uncompute`, `eq_k.value == 0` and `i.value`
  unchanged.
- For superposed `i` (set `super_mask` bits manually), recording-sink
  pins the gate-set: only `quantum_xor` (X-flips lift to themselves at
  control depth 0) and the `quantum_and` chain from `c_n_AND`.
- *Statevector test:* `K = 2`, `i` initialised to a uniform
  superposition over 4 basis states by direct `apply_h` on its qubits
  via `OrkanBridge`, then run `lib_qram_eq_k_compute` with `k = 1` →
  measure-equivalent: assert `P(eq_k = 1) ≈ 0.25`, then run
  `lib_qram_eq_k_uncompute` and assert `P(eq_k = 0) ≈ 1.0`. Total
  qubit count: `2 (i) + 1 (eq_k) + ≤ 1 (c_n_AND ancilla for K=2) =
  4`.

**Production:** ~80 LoC in `qram_read_predicate.hpp`. Reuses
`lib_c_n_AND_dsl`; depends on B0/B1 only via header surface.

**Done when:** new test green; full suite green.

### Beat B2 — Sequential XOR-fanout DSL body

**Goal.** Implement the QROM body in pure DSL form per PRD §4. New
header `include/sturm/detail/lib/qram_read_dsl.hpp`:

```cpp
template <std::size_t W>
inline void lib_qram_read_qrom_dsl(const qint_t<W>* a, std::size_t n,
                                   qint_t<W>& i, qint_t<W>& b) {
    constexpr auto K = /* ceil_log2(n) computed from n */;
    qbool eq_k;                          // single shared predicate
    for (std::size_t k = 0; k < n; ++k) {
        lib_qram_eq_k_compute(i, k, K, eq_k);
        WHEN(eq_k) {
            const auto val = a[k].value; // mask=0 by precondition
            for (std::size_t j = 0; j < W; ++j) {
                if ((val >> j) & 1u) b.bit(j).flip();
            }
        }
        lib_qram_eq_k_uncompute(i, k, K, eq_k);
    }
}
```

The `if ((val >> j) & 1u)` branch is *classical* — `a[k]` is fully
classical by the QROM precondition (PRD §3 / §5), so the loop is a
classical fan over set bits of `a[k]`. Each `b.bit(j).flip()` lifts to
a CX controlled on `eq_k` via the `WHEN(eq_k)` scope (B5a depth-1
invariant: `eq_k` is the single live control bit). No raw gate calls;
no rotations.

The header wires the QROM helper from B1 (`qram_read_qrom_impl`) to
call this DSL body. Counter-mode bumps remain in `qram_read.cpp` —
the gate emission is purely a side-effect of the DSL ops.

**Tests:**

- `tests/lib/test_qram_read_dsl_recording.cpp` (G2). Install
  `RecordingSink`. For `(N, W) ∈ {(2, 2), (4, 4), (8, 4)}` and a
  fixed classical container `a` (e.g. random but seeded), assert:
  - Every record's `op ∈ {quantum_xor, quantum_and}`.
  - No rotations, no prepares.
  - The gate count matches the §4 cheat-sheet formula
    parameterised on `(N, W, popcount(a[k]))`. The test recomputes
    the expected count — no hardcoded numbers.
- `tests/lib/test_qram_read_dsl_simulate.cpp` (G1). For
  `(N, W) = (4, 4)`:
  - Initialise `b` to `|0⟩^W`, container `a = {0xA, 0x5, 0xF, 0x0}`.
  - For every classical `i ∈ {0, 1, 2, 3}`: run
    `lib_qram_read_qrom_dsl`; assert via `OrkanBridge` that
    `P(b == a[i]) ≈ 1.0`. Reset between iterations.
  - Then with `i` in superposition `(|0⟩ + |1⟩) / √2` (apply H to
    `i.bit(0)` only), run the body; assert
    `P(b = a[0]) ≈ 0.5` and `P(b = a[1]) ≈ 0.5` — the read becomes a
    coherent superposition of QROM outputs.
  - Assert `i.super_mask` unchanged across each call.
- `tests/qram/test_qram_read_stub.cpp` (existing). Continues to assert
  umbrella `qram_read_count` increments by 1 per call. Must stay
  green.

**Production:** ~150 LoC. The classical-classical fast path (every
`a[k]` and `i` classical) folds into a plain `b.value ^= a[i].value`
via the underlying ops (B3 dispatch-time specialization) — this is a
free win, not extra code.

**Done when:** all three tests green; full suite green; LoC ≤ 300.

### Beat B3 — Adjoint sibling header + `STURM_REGISTER_ADJOINT`

**Goal.** New header
`include/sturm/detail/lib/qram_read_dsl_adj.hpp` defines

```cpp
template <std::size_t W>
inline void __lib_qram_read_qrom_dsl_adj(const qint_t<W>* a,
                                         std::size_t n,
                                         qint_t<W>& i, qint_t<W>& b);
```

whose body re-runs the forward sweep verbatim — the QROM body is
self-adjoint (PRD §4 paragraph 4). Auto-included from the bottom of
the forward header (matches `c_and_dsl.hpp` ↔ `c_and_dsl_adj.hpp`).
Registers via `STURM_REGISTER_ADJOINT(lib_qram_read_qrom_dsl,
__lib_qram_read_qrom_dsl_adj)` under `STURM_BACKEND_ENABLED`.

The `__QRAM_read_adj` *public* overloads in `qram_read.hpp` (touched
in B1) now forward to `__lib_qram_read_qrom_dsl_adj` for the QROM
path.

**Tests** (`tests/lib/test_qram_read_dsl_adjoint.cpp`):
- *Recording-sink round trip* (G3 part 1). Forward + adjoint emits a
  *balanced* gate stream: every `(op, qubit_groups, control)`
  triple appears an even number of times across the round trip
  (custom counter in the test).
- *Statevector round trip* (G3 part 2). For `(N, W) = (4, 4)`, every
  classical `i ∈ {0..3}`: run forward `QRAM_read(a, i, b)` then
  `__QRAM_read_adj(a, i, b)`; assert `P(b == 0) ≈ 1.0`.
- *Adjoint-of-superposed-i round trip*. Apply H to `i.bit(0)` then
  run forward + adjoint; assert `P(b == 0) ≈ 1.0` and
  `i.super_mask` unchanged.

**Production:** ~50 LoC — body is one line that calls the forward
helper.

**Done when:** new test green; full suite green.

### Beat B4 — Wire split counters through QROM/qreg helpers

**Goal.** With the sink surface in place (B0), wire the QROM helper
to call `current_sink()->qrom_read()` and the qreg helper to call
`current_sink()->qreg_read()`. The umbrella `qram_read()` hook
**continues to fire** in both helpers so the existing
`tests/qram/test_qram_read_stub.cpp` and
`transpiler/tests/test_qram_e2e.cpp` stay green.

The QROM helper's call order is:

1. Bump umbrella `qram::g_qram_read_count` (preserves D1 contract).
2. `current_sink()->qrom_read()` (new split counter).
3. Call `lib_qram_read_qrom_dsl(a, n, i, b)` — *the actual gate
   emission*.
4. **Do not** call `current_sink()->qram_read()` from inside the
   helper — the umbrella is bumped at the public entry-point in the
   `qram_read.hpp` overload, so split + umbrella both fire from
   non-overlapping sites and cannot be double-counted.

(The existing `qram_read()` hook on `Sink` stays for source
compatibility; its only caller is the public overload, which keeps
calling it for the umbrella.)

**Tests** (`tests/qram/test_qram_telemetry_split.cpp`):
- Counter-sink: classical container → after one `QRAM_read`,
  `qrom_read == 1`, `qreg_read == 0`, umbrella `qram_read == 1`.
- Counter-sink: container with one superposed slot → after one
  `QRAM_read`, `qrom_read == 0`, `qreg_read == 1`, umbrella
  `qram_read == 1`.
- Recording-sink: split counters appear as records exactly once each
  per dispatched call along the corresponding path.

**Production:** ~20 lines touching `qram_read.hpp` (public overloads)
and `qram_read.cpp` (helpers).

**Done when:** new + all existing telemetry-shaped tests green.

### Beat B5 — End-to-end gate-stream + statevector test

**Goal.** Pin the v1 contract end-to-end with the public
`QRAM_read(a, i, b)` entry-point — not the internal helper — so that
the test exercises every layer (overload dispatch → mask-OR routing →
QROM helper → DSL body → adjoint → split counter).

**Test** (`tests/qram/test_qram_read_qrom_gates.cpp`):

For `(N, W) = (4, 4)` with `a = {0xA, 0x5, 0xF, 0x0}`:

1. **Forward record pin** (G5). Install `RecordingSink`. Call
   `QRAM_read(a, i, b)` with classical `i = 2`. Assert:
   - exact CX count and CCX count match the per-`k` budget computed
     from `(N, W, popcount(a[k]))`;
   - every record's `op ∈ {quantum_xor, quantum_and}`;
   - one record for `qrom_read`, zero for `qreg_read`;
   - umbrella `qram::qram_read_count() == 1`;
   - `i.super_mask == 0` after the call (PRD §11.2.7 item 3).
2. **Round-trip pin** (G3). Same fixture. Forward + adjoint leaves
   `b` empty: assert via statevector that `P(b == 0) ≈ 1.0`. Assert
   the recorded gate stream is balanced (every primitive appears an
   even number of times across the round trip).
3. **Superposed-index pin** (G1). With `i` in
   `(|0⟩ + |2⟩) / √2` (H on `i.bit(1)`), run the forward read and
   assert `P(b = a[0]) + P(b = a[2]) ≈ 1.0`, with each ≈ 0.5; assert
   `i.super_mask` unchanged.
4. **Out-of-range UB document** (PRD §5). The test does *not* call
   `i = 5` on `N = 4` — that's UB. A comment in the test explains
   why and refers to PRD §5.

**Production:** none; only the test lands. All production code is
already in place from B0–B4.

**Done when:** test green; **full library + qram + transpiler test
suite green** under `ctest --parallel 6`. **The PRD's Status is
flipped from `Draft` to `Implemented`** in this commit.

---

## §6 What does NOT land in v1 (PRD §3 cross-check)

- **Quantum-register path.** B1's qreg helper continues to be a
  counter-only stub — the new `qreg_read` counter bumps but no gates
  are emitted. Filed as a successor PRD when a real call site
  surfaces.
- **Mixed containers.** Routed to qreg path by entry-time mask-OR;
  out of scope here.
- **`std::vector<qint_t<W>>`.** Pointer overload covers the runtime;
  container-shape v2.
- **Bucket-brigade / log-depth multiplexers.** Sequential XOR-fanout
  is the v1 algorithm.
- **`[[sturm::qrom_hint]]`.** Skipping the entry-time OR-reduction is
  a v2 optimisation.

---

## §7 Quality gates per beat

A beat is **not** closed until:

1. The named test for the beat's goal is green.
2. `ctest --parallel 6` passes the **full** `qram`, `lib`, `qtypes`,
   `backend`, and `transpiler` labels.
3. The production module is ≤ 300 LoC.
4. No new TODO refers to anything inside the v1 scope (PRD §3
   non-goals are fair game for follow-up TODOs).
5. The bd issue is closed with a one-line note pointing at the
   landing commit (`bd close <id>`).

---

## §8 Session-close protocol (CLAUDE.md)

Each beat's session ends with:

1. `bd ready` to confirm queue state.
2. Quality gates above.
3. `bd close <id>` for the beat.
4. `git pull --rebase` → `bd dolt push` → `git push`.
5. `git status` shows "up to date with origin".

Work is not complete until `git push` succeeds.

---

## §9 References

- `docs/prd_qram_backend.md` — this plan's PRD.
- `docs/01_principles.md` — P5 (DSL primitive set), P9 / P9c, B5a
  (depth-1 control), B7, B10.
- `docs/archive/prd_qram_subscript.md` — frontend PRD; §11.2 (D0b
  runtime classicality dispatch), §11.2.7 (counter telemetry), §11.4
  (D0d adjoint registration).
- `docs/archive/plan_qram_subscript.md` — frontend beat plan; the
  D1/D2/G1 beats define the dispatch and counter-mode contract this
  plan extends.
- `include/sturm/detail/lib/c_and_dsl.hpp` — DSL-header reference
  and `c_n_AND` primitive used by the predicate compute.
- `include/sturm/control/when.hpp` — `WHEN` macro and the depth-1
  invariant.
- `tests/backend/test_bitwise_and_or_simulate.cpp` — reference
  pattern for the `OrkanBridge` statevector tests.
