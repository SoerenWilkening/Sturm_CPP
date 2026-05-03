# PRD — QRAM backend gate emission (QROM path, v1)

**Status.** Draft, 2026-05-03.
**Predecessors.** `docs/archive/prd_qram_subscript.md` (frontend epic
`sturm-u9ge`, closed) and `docs/archive/plan_qram_subscript.md` (its
beat plan, closed). Those documents pin the user-visible shape
(`qint b = a[i];` rewritten by the C1 matcher / D2 emitter into a
`::sturm::QRAM_read(a, i, b)` call) and the runtime entry-point
contract (D0a–D0d). This PRD picks up where the closed epic stops:
the runtime body today is a counter-mode stub that bumps a `qram_read`
counter and emits no gates.

This PRD covers **only the QROM path** — the case where every element
of `a` is fully classical at call time (`super_mask == 0` for every
slot). The quantum-register path (any element superposed) is
deferred to a sibling PRD when a real call site needs it. The
`super_mask` OR-reduction at `QRAM_read`'s entry already routes to
two TU-private helpers (`qram_read_qrom_impl` /
`qram_read_qreg_impl`), so the qreg path stays a counter-bump stub
for now.

## §1 Problem statement

After `sturm-u9ge`, `qint b = a[i];` rewrites to a real call that
takes the runtime QROM dispatch path, but the QROM body is empty —
zero gates, one counter bump. The frontend looks correct from
source, but no simulator state changes when `QRAM_read` runs. We
need the QROM body to actually emit the unitary that copies `a[i]`
into `b` at the gate-stream level, so direct-mode and
recording-sink consumers see the right primitives, and so the round
trip with the registered `__QRAM_read_adj` is bit-exact.

## §2 Goals

- **G1.** A `QRAM_read(a, i, b)` call where every element of `a` is
  classical (`super_mask == 0`) emits a primitive sequence whose
  net effect is `b ^= a[i]` (each set bit of `a[i]` flips the
  matching bit of `b`), and leaves `i.super_mask` unchanged across
  the call.
- **G2.** The body uses **only** the DSL primitives from
  `docs/01_principles.md` P5: `^=` (CNOT/Toffoli family) and `&=`
  (bitwise AND). No phase / amplitude rotations. No raw gate calls.
  No multi-controlled primitives beyond what `WHEN` lifts to (B5a
  depth ≤ 1).
- **G3.** Round-trip correctness: forward `QRAM_read(a, i, b)`
  followed by `__QRAM_read_adj(a, i, b)` returns `b` to its
  pre-forward state at the gate-stream level. The QROM body is
  unitarily self-adjoint; the registered adjoint re-executes the
  forward body (P9c keeps the adjoint as a distinct named function
  for placement audit).
- **G4.** Counter-mode telemetry splits into the two PRD-pinned
  counters (`qrom_read`, `qreg_read`) per archived
  `prd_qram_subscript.md` §11.2.7. The umbrella `qram_read` counter
  remains and continues to be bumped, so the existing test
  contracts (`tests/qram/test_qram_read_stub.cpp`,
  `transpiler/tests/test_qram_e2e.cpp`) stay green.
- **G5.** A new gate-stream test pins the exact primitive count
  emitted by the QROM body for a representative `(N, W)` pair on
  a `RecordingSink`, and pins `i.super_mask` unchanged across the
  call.

## §3 Non-goals (v1)

- **Quantum-register path.** The qreg helper stays a counter-bump
  stub. Filed as a follow-up PRD; will land when the first call
  site genuinely has a superposed slot.
- **Mixed containers** (some classical, some superposed). The
  OR-reduction at `QRAM_read` entry routes any such container to
  the qreg path, which is out of scope here.
- **`std::vector<qint_t<W>>`.** Container-shape v2 question; the
  pointer overload already covers the runtime side.
- **Bucket-brigade / log-depth multiplexers.** v1 uses the simple
  sequential XOR-fanout (PRD §4 below). Log-depth or
  data-structure-aware variants are a v2 micro-optimization.
- **Compile-time `[[sturm::qrom_hint]]`.** Skipping the entry-time
  OR-reduction is a v2 optimization; v1 always reduces.

## §4 Approach: sequential XOR-fanout in DSL form

For a container `a` of `N` elements of width `W` and a `qint_t<W>`
index `i`, the QROM body sweeps `k = 0 .. N-1`. For each `k`:

1. Compute a single 1-qubit predicate ancilla `eq_k` such that
   `eq_k = (i == k)` after the compute step. Built by bit-flipping
   `i`'s bits whose corresponding bit of `k` is `0`, AND-reducing
   onto `eq_k`, then bit-flipping back. This uses **only `^=` and
   `&=`** on `i`'s bits and `eq_k`. Cost: `O(W)` flips + one
   `eq_k ^= AND_W(i_bits)` Toffoli chain.

2. `WHEN(eq_k) { b ^= a[k]; }`. Because `a[k]` is fully classical,
   `b ^= a[k]` lowers to one `b[j] ^= 1` per set bit `j` of `a[k]`,
   and the surrounding `WHEN(eq_k)` lifts each bit-flip to a CX
   controlled on `eq_k`. B5a depth-1 invariant holds — `eq_k` is
   the single live control bit.

3. Uncompute `eq_k` by re-running the compute step (it is its own
   inverse: AND of bits, sandwiched by self-inverse bit flips).
   The single predicate ancilla is reused across all `N` outer
   iterations.

The sweep is its own inverse as a unitary — every `^=` is
self-inverse, the predicate compute/uncompute is symmetric, and the
order of `k`-iterations does not matter for the net effect (each
`eq_k` is non-overlapping with the others on the index basis). The
adjoint body therefore re-runs the forward sweep verbatim.

Gate budget per call:
- Predicate compute: `W` X-flips + one `(W-1)`-Toffoli AND-reduce
  on `eq_k`, plus `W` X-flips to undo. Lifts via sturm's `c_n_AND`
  decomposition into `O(W)` Toffolis with `O(W)` ancillas (these
  are `c_n_AND`'s own ancillas, allocated and freed within its
  call — they are not "element-side" ancillas in the sense PRD §3
  forbids).
- XOR-fanout payload: at most `W` `^=` ops under `WHEN(eq_k)`,
  each becoming one CX. Per-`k` cost `O(W)`.
- Predicate uncompute: same as compute, `O(W)` Toffolis.

Total: `O(N · W)` Toffolis per call, matching the archived PRD
§11.2.1 budget. No element-side ancilla (the predicate ancilla is
shared across iterations, and `c_n_AND`'s internal ancillas are
allocated inside the AND primitive, not held across the outer
sweep).

## §5 Address-bit contract

`i` is a `qint_t<W>` with `W` ≥ `⌈log₂ N⌉` from the existing width
inference (D0c). The high `W − ⌈log₂ N⌉` bits of `i` are
**required to be zero** at call entry; passing `i ≥ N` is
**undefined behavior**, mirroring classical out-of-range subscript.

The body sweeps `k = 0 .. N-1` only. Bits of `i` above
`⌈log₂ N⌉` are not consulted by any predicate, so they remain
unchanged. This is consistent with classical UB: an out-of-range
`i` makes every `eq_k` false, and `b` is left at its entry value
(typically `|0⟩`). No runtime range check is emitted.

## §6 File layout

- **`include/sturm/detail/lib/qram_read_dsl.hpp`** (new, header-only,
  ≤ 200 LoC). Defines `lib_qram_read_qrom_dsl(a, i, b)` as a
  template parameterised on `(W, N)` and the container shape. Uses
  the existing `qbool` / `BitProxy` primitives, `c_n_AND`, and
  `WHEN`. No raw gate calls.
- **`include/sturm/detail/lib/qram_read_dsl_adj.hpp`** (new). Defines
  `__lib_qram_read_qrom_dsl_adj(a, i, b)` whose body re-runs the
  forward sweep (the QROM body is self-adjoint). Registers via
  `STURM_REGISTER_ADJOINT(lib_qram_read_qrom_dsl,
  __lib_qram_read_qrom_dsl_adj)`. Auto-included from the bottom of
  the forward header (matches `c_and_dsl.hpp` ↔ `c_and_dsl_adj.hpp`).
- **`src/sturm/qram/qram_read.cpp`** (refactored). The two helpers
  `_qram_detail::qram_read_qrom_impl` and
  `_qram_detail::qram_read_qreg_impl` are refactored from no-arg
  shims into templates that receive `(a_ptr, n, i, b)` and forward
  to the DSL header (QROM) or stay as a counter bump (qreg). The
  three public `QRAM_read` overloads in `qram_read.hpp` change from
  no-arg dispatch into argument-passing dispatch.
- **`include/sturm/qram/qram_read.hpp`** (touched). The three
  public `QRAM_read` overloads and three `__QRAM_read_adj` overloads
  pass `(a, i, b)` through to the helper rather than discarding
  them.

## §7 Telemetry

Per archived PRD §11.2.7, gate emission lands the two split
counters:

- `qrom_read` — bumped once per QROM-path dispatch.
- `qreg_read` — bumped once per qreg-path dispatch (still a stub).

Both helpers continue to bump the umbrella `qram::g_qram_read_count`
thread-local so existing tests that observe the umbrella counter
keep passing. The split counters live on the active `Sink` (e.g.
`CounterSink`'s counts map gets new entries `"qrom_read"` and
`"qreg_read"`); the existing `Sink::qram_read()` hook is renamed
to two hooks `Sink::qrom_read()` / `Sink::qreg_read()`, and the
`CounterSink` / `RecordingSink` overrides update accordingly.

## §8 Testing

- **`tests/qram/test_qram_read_stub.cpp`** (existing). Continues to
  assert the umbrella `qram_read_count` increments by 1 per call.
  No change required.
- **`transpiler/tests/test_qram_e2e.cpp`** (existing). Continues to
  pass; the umbrella per-sink counter still bumps.
- **`tests/qram/test_qram_read_qrom_gates.cpp`** (new). Pins the
  gate-stream emitted by `QRAM_read` on a `RecordingSink` for a
  representative `(N, W) = (4, 4)` fully-classical container:
    - exact CX count and CCX count match the per-`k` budget;
    - `i.super_mask == 0` after the call (PRD §11.2.7 item 3);
    - forward + `__QRAM_read_adj` round trip leaves `b` empty and
      the gate-stream balanced (every CX/CCX appears an even number
      of times across the round trip);
    - the `qrom_read` split counter bumps by 1, `qreg_read` stays at 0.

## §9 Milestones

- **B1.** Refactor `qram_read_qrom_impl` / `__QRAM_read_adj` impls
  to receive `(a, i, b)`. Counter-mode bumps unchanged.
- **B2.** New `qram_read_dsl.hpp` body — sequential XOR-fanout in
  pure DSL form. Compiles under `STURM_BACKEND_ENABLED`.
- **B3.** Sibling `qram_read_dsl_adj.hpp` + `STURM_REGISTER_ADJOINT`.
- **B4.** Split telemetry — `qrom_read` / `qreg_read` counters land
  on `Sink`. Umbrella counter preserved.
- **B5.** Gate-stream test `test_qram_read_qrom_gates.cpp` green.

## §10 References

- `docs/01_principles.md` — P5 (DSL primitive set), P9 / P9c
  (adjoint synthesis), B5a (depth-1 control invariant).
- `docs/archive/prd_qram_subscript.md` — frontend PRD, in particular
  §11.2 (D0b runtime classicality dispatch), §11.2.7 (counter
  telemetry), §11.4 (D0d adjoint registration).
- `docs/archive/plan_qram_subscript.md` — frontend beat plan.
- `include/sturm/detail/lib/c_and_dsl.hpp` — reference for the
  DSL-header layout and `c_n_AND` primitive used by the predicate
  compute.
