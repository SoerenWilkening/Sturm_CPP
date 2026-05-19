# PRD — QRAM backend gate emission (bucket-brigade unified, v2)

**Status.** Proposed, 2026-05-19. Not yet implemented.
**Supersedes.**
[`archive/prd_qram_backend.md`](archive/prd_qram_backend.md) (v1
QROM-only naive sweep, Implemented 2026-05-03) and
[`archive/plan_qram_backend.md`](archive/plan_qram_backend.md) (its
beat plan). The v1 §2 G1–G5 contract is inherited verbatim for the
QROM path and extended to the qreg path.
**Predecessors.**
[`archive/prd_qram_subscript.md`](archive/prd_qram_subscript.md)
(frontend epic `sturm-u9ge`, closed) pins the source-level shape
`qint b = a[i];` rewritten by the C1 matcher / D2 emitter into a
`::sturm::QRAM_read(a, i, b)` call. That contract is unchanged here;
only the runtime body is replaced.
**Related principles.** P5 (DSL primitive set), P9 / P9c (adjoint
synthesis), B5a (depth-1 control invariant), B6 (RAII ancilla),
B7 (qubit-index ownership).

This PRD covers **both** the QROM path (every element of `a`
classical, `super_mask == 0`) **and** the qreg path (any element
superposed) under a single unified bucket-brigade algorithm. The qreg
path, currently a counter-bump stub since the v1 epic, emits gates
for the first time. Polynomial-encoding QRAM
([Phalak & Ghosh 2024, arXiv:2408.16794](https://arxiv.org/abs/2408.16794))
was evaluated and deferred to a sibling PRD for the
`[[sturm::const_data]]` specialization where its compile-time
classical Möbius transform precondition is met (§3 non-goal, §11 B8).

## §1 Problem statement

The v1 backend ships a sequential XOR-fanout QROM body with
`O(N · W)` Toffoli depth (`archive/prd_qram_backend.md` §4) and a
qreg-path stub that emits no gates. We need:

1. **Asymptotic depth** at the state-of-the-art for fault-tolerant
   execution. Bucket-brigade's `O(log² N · W)` depth (Giovannetti,
   Lloyd, Maccone 2008, [arXiv:0708.1879](https://arxiv.org/abs/0708.1879))
   is the literature baseline.
2. **First-light gate emission for the qreg path.** A unified
   algorithm — bucket-brigade is data-classicality-agnostic, so the
   same circuit serves both routes.
3. **No runtime caching, no compile-time data preconditions.**
   Bucket-brigade's circuit is determined entirely by `(N, W)`; no
   classical preprocessing of `a[]` is required (contrast with
   polynomial encoding, which needs the Möbius transform of `a[]`'s
   contents).

## §2 Goals

- **G1 (inherited from v1).** A `QRAM_read(a, i, b)` call emits a
  primitive sequence whose net effect is `b ^= a[i]` (each set bit of
  `a[i]` flips the matching bit of `b`), leaving `i.super_mask`
  unchanged across the call. **Extension over v1:** holds for both
  classical and superposed `a[]`.
- **G2 (inherited).** Body uses only P5 DSL primitives: `^=`
  (CNOT/Toffoli family) and `&=` (bitwise AND). No rotations.
- **G3 (inherited).** Round-trip: `QRAM_read(a, i, b)` followed by
  `__QRAM_read_adj(a, i, b)` returns `b` to its pre-forward state
  AND restores `a[]`'s qubits to their entry contents (in-place
  form: leaves are `a[]`'s qubits, swapped out and back).
- **G4.** T-depth of the routing layer is `O(log² N · W)`; Toffoli
  count is `O(N · W)`. The depth-scaling claim is asymptotically
  verified at the test layer (§10).
- **G5.** Telemetry — three counters (`qrom_read`, `qreg_read`,
  umbrella `qram_read`) survive unchanged from v1. Split is still
  driven by `super_mask` OR-reduction at the public entry point;
  the algorithm is shared.
- **G6.** Adjoint registration via `STURM_REGISTER_ADJOINT`
  satisfies P9c. The forward body is self-inverse at the gate level
  (§4.4), so the registered `__QRAM_read_adj` is a one-line forward
  call — distinct named function for audit, zero duplicated body.

## §3 Non-goals (v2)

- **Hann-Lee noise-resilient form.** The simple CSWAP-tree form is
  used (no "address bits wave through routers, untouched routers
  stay |wait⟩" enforcement). The polylog noise resilience
  ([Hann, Lee et al. 2021, arXiv:2012.05340](https://arxiv.org/abs/2012.05340))
  is academic in the FT execution model where every logical gate
  is already error-corrected by the layer beneath STURM.
- **Polynomial-encoding fast path.** Deferred to a sibling PRD
  conditioned on `[[sturm::const_data]]` containers whose values
  are known at transpile time. Polynomial encoding's
  `O(log log N)` T-depth wins asymptotically but its Möbius
  transform of `a[]` requires either compile-time classical data
  or runtime caching — neither of which compose with the qreg path
  or with the "no runtime caching" constraint of this PRD.
- **Pool / cached qubits across calls.** Pure RAII per call (B6).
  Each `QRAM_read` allocates router / transit / bus ancillae on
  entry and releases them on exit. No persistent backend state.
- **`std::vector<qint>` container shape.** Unchanged from v1; the
  pointer overload covers the runtime-sized case via the finite
  power-of-2 dispatch table (§7).
- **Small-N fast path / naive-sweep fallback.** Always BB. The
  small-N overhead (BB is heavier than naive at `N ≤ 8`) is
  accepted to keep a single algorithm.
- **Runtime address range check.** Address-bit zero-padding
  contract is UB-on-violation, inherited verbatim from v1 §5.

## §4 Approach — transit-register bucket-brigade

For a container `a[]` of `N` elements of width `W` and a
`qint_t<W_i>` index `i`, the algorithm constructs a binary tree of
routers over `N` leaves, walks a `W`-wide bus down the tree to the
addressed leaf, XORs the leaf's content into `b`, and walks the bus
back up — restoring the tree to its entry state.

Let `N' = 2^⌈log₂ N⌉` (the next power of 2; equals `N` when `N` is
itself a power of 2; see §6 for the padding contract). Let
`d = log₂ N'` be the tree depth.

### §4.1 Phase 1 — setup routers from address bits

The tree has `N' − 1` internal nodes (routers). Each router carries
two `qbool` qubits (`is_left`, `is_right`) with structural invariant
`is_left · is_right = 0` (per Q5'(ii)). Encoded states:

| Encoding | State | Meaning |
|---|---|---|
| `(0, 0)` | `\|wait⟩` | router has not been visited yet |
| `(1, 0)` | `\|L⟩` | bus should route to the left child |
| `(0, 1)` | `\|R⟩` | bus should route to the right child |
| `(1, 1)` | — | **forbidden**; algorithm preserves the invariant |

Setup is a transpile-time recursive walk (per Q2'(b)) from root to
leaves. At the root, conditioned on the high address bit `addr[0]`:

```
root.is_right ^= addr[0]
root.is_left  ^= addr[0]
X(root.is_left)              // is_left = 1 ^ addr[0] = ~addr[0]
```

For each non-root router `r` at level `ℓ > 0`, with parent `p`,
conditioned on whichever parent-state edge connects `r` to `p`
(`p.is_left` if `r` is the left child, `p.is_right` if right):

```
WHEN(p.is_X) {
    r.is_right ^= addr[ℓ]
    r.is_left  ^= addr[ℓ]
    X(r.is_left)
}
```

The `X` under a single `WHEN` lifts to a CNOT (controlled-X); each
`^=` under one `WHEN` lifts to a CCX (Toffoli). The B5a depth-1
invariant holds — only one ancilla control is live at each emission
site (the parent state bit). The recursive template emits the full
setup at transpile time per Q2'(b); pointer-form runtime-`N` cases
dispatch into a finite family of unrolled instantiations per §7.

### §4.2 Phase 2 — bus traversal, leaf-swap, bus reversal

The bus `B` is `W` ancilla qubits allocated at call entry and
initialized to `|0⟩`. Transit registers `T_d[k]` for each non-leaf
level `d ∈ [1, d_max)` and each node `k` at that level are
`W`-qubit ancilla blocks allocated at call entry, also `|0⟩`.

The traversal is a transpile-time recursive walk. At each internal
node `r` with children `c_L`, `c_R`:

```
WHEN(r.is_left)  swap_W(bus_in, T_for_c_L)
WHEN(r.is_right) swap_W(bus_in, T_for_c_R)
```

where `bus_in` is the bus position arriving at `r` (the bus itself
at the root, otherwise the parent's transit block), and `swap_W`
is the `W`-wide parallel swap. Each scalar swap under a single
`WHEN` is a CSWAP, which decomposes into the 18-gate set as
`CCX + 2 CNOT`.

At a leaf-side parent (the router whose children are `a[k]`'s
qubits, in-place per Q3'(c)):

```
WHEN(r.is_left)  swap_W(parent_transit, a[2k])
WHEN(r.is_right) swap_W(parent_transit, a[2k+1])
```

After all level-by-level swaps, the bus holds the addressed leaf's
data and the leaf itself holds `|0⟩` (transient hole; restored on
the reverse walk).

The payload XOR:

```
b ^= bus            // W parallel CNOTs (bus → b)
```

The **reverse walk** is the same sequence in reverse order, with
the same operations (CSWAPs and the leaf-side swaps are self-inverse;
running them again restores `a[k]` to its entry value and clears the
bus to `|0⟩`).

### §4.3 Phase 3 — teardown routers

Phase 1 in reverse. Restores every router to `|wait⟩` (= `(0, 0)`),
allowing the call's transit / bus / router ancillae to be released
in the `|0⟩` state expected by the RAII deallocator (B6).

### §4.4 Adjoint structure

Forward body = phase 1 → phase 2 → phase 3. Every primitive in
each phase is self-inverse:

- Phase 1 / phase 3: each `^=` and `X` is self-inverse; the two
  phases are mutual inverses by construction.
- Phase 2: every `CSWAP` appears exactly twice (once in the
  forward walk, once in the reverse walk); the XOR payload
  `b ^= bus` is sandwiched and is itself self-inverse.

Running the whole forward body twice gives the identity. Therefore
`__QRAM_read_adj` is registered per P9c as a one-line wrapper
calling `QRAM_read`. Distinct named function for placement audit;
zero duplicated implementation.

### §4.5 Resource budget per call

For `(N, W)` with padded `N' = 2^⌈log₂ N⌉`:

| Resource | Count |
|---|---|
| Router qubits | `2 · (N' − 1)` |
| Transit qubits | `(N' − 2) · W` (every internal node except the root carries a transit block; the root uses the bus directly) |
| Bus qubits | `W` |
| **Total ancilla per call** | `2(N' − 1) + (N' − 2)W + W = 2(N' − 1) + (N' − 1)W` |

For `(N=4, W=4)`: `2·3 + 3·4 = 18` ancilla qubits.
For `(N=1024, W=8)`: `2·1023 + 1023·8 = 10 230` ancilla qubits.

All ancillae are RAII-allocated at `QRAM_read` entry and released
on exit (B6). No pool, no persistence across calls. Per Q12'(α),
the per-call allocation overhead is accepted; pool optimization is
a future PRD if profiling shows it matters.

## §5 Address-bit contract

Inherited verbatim from v1 §5. `i` is `qint_t<W_i>` with
`W_i ≥ ⌈log₂ N⌉`. The high `W_i − ⌈log₂ N⌉` bits of `i` are
required to be zero at call entry; passing `i ≥ N` is **undefined
behavior**, mirroring classical out-of-range subscript. The body
consults only `i[0 .. ⌈log₂ N⌉ − 1]`. No runtime range check per
Q5 (carried over).

In the BB form, OOR addresses do *not* silently return `|0⟩` as in
the naive sweep — they instead route the bus to whatever phantom
or unaddressable leaf the high bits select. The failure mode is
louder; users are still on the hook for the UB contract.

## §6 Power-of-2 padding (Q6'(II))

For `N` not a power of 2, the transpiler synthesises a tree over
`N' = 2^⌈log₂ N⌉` leaves. Leaves indexed `N ≤ k < N'` are
**phantom**: their slot is realised at call entry as `W` transit
ancilla qubits initialised to `|0⟩` (not stored in `a[]`'s
container). Phantom leaves participate in the bus traversal
identically to real leaves; addressing one (`N ≤ i < N'`) deposits
`|0⟩` into the bus and the XOR step is a no-op on `b`. Addressing
beyond `N'` is UB per §5.

The padding is transpile-time: the template instantiates at `N'`,
not at the user's `N`. The compile-time `N` is preserved for the
runtime `qrom_read` / `qreg_read` counter bookkeeping and for the
gate-stream test pins (§10).

## §7 Container shape and runtime-N dispatch (Q10'(II))

The three v1 container shapes are preserved:

| Shape | Treatment |
|---|---|
| `std::array<qint, N>` | `N` is a template parameter; the recursive BB template instantiates directly. |
| C-style array `qint_t<W>[N]` | Same — `N` is compile-time-deducible. |
| Pointer `qint_t<W>*` + runtime `n` | The header ships a finite family of unrolled BB instantiations at `N ∈ {2, 4, 8, 16, 32, 64, 128, 256, 512, 1024}`. The pointer overload's runtime dispatch is a `switch (n)` over `⌈log₂ n⌉` selecting the smallest power-of-2 `N' ≥ n`, with phantom-leaf padding (§6) covering the remainder. Calls with `n > 1024` are diagnosed at the entry point with a runtime error pointing at the recompile knob to extend the family. |

The pointer overload's dispatch table is the only deliberately
non-unrolled element of the design; everything inside the
`switch` branch is transpile-time-unrolled per Q2'(b).

## §8 File layout

- **`include/sturm/detail/lib/qram_read_bb_dsl.hpp`** (new,
  header-only, target ≤ 300 LoC). Defines
  `lib_qram_read_bb_dsl<W, N>(a_ptr, i, b)` as a recursive template
  parameterised on `(W, N)` and the container shape. Uses
  `qbool` / `BitProxy`, `c_n_AND`, `WHEN`, and the existing
  `swap` primitive. No raw gate calls.
- **`include/sturm/detail/lib/qram_read_bb_dsl_adj.hpp`** (new).
  Defines `__lib_qram_read_bb_dsl_adj<W, N>(...)` as a one-line
  forward call (Q14'(ζ)). Registers via
  `STURM_REGISTER_ADJOINT(lib_qram_read_bb_dsl,
  __lib_qram_read_bb_dsl_adj)`. Auto-included from the bottom of
  the forward header (matches `c_and_dsl.hpp` ↔
  `c_and_dsl_adj.hpp`).
- **`include/sturm/qram/qram_read.hpp`** (touched). The three
  public `QRAM_read` overloads dispatch into the BB DSL. The
  pointer overload uses the `switch(n)` dispatch table per §7.
  The `__QRAM_read_adj` overloads remain as thin wrappers; their
  body is `QRAM_read(a, i, b)` (forward is self-adjoint).
- **`src/sturm/qram/qram_read.cpp`** (touched). The
  `qram_read_qrom_impl` and `qram_read_qreg_impl` helpers both
  forward into the same BB DSL — the algorithm is
  data-classicality-agnostic. The split counter bumps
  (`qrom_read` vs `qreg_read`, plus the umbrella
  `qram::bump_qram_read_count()`) survive verbatim.
- **Deletions** (per Q13'(γ) hard-replace):
  - `include/sturm/detail/lib/qram_read_dsl.hpp` (v1 naive sweep)
  - `include/sturm/detail/lib/qram_read_dsl_adj.hpp` (v1 naive adjoint)

## §9 Telemetry (Q15'(a))

Counter scheme is unchanged from v1:

- `qrom_read` — bumped once per call when `super_mask` OR-reduction
  is `0` (classical-data path).
- `qreg_read` — bumped once per call when any element of `a[]` is
  superposed (quantum-data path).
- `qram_read` (umbrella, thread-local) — bumped once per dispatched
  call at the public entry point.

Even though the underlying algorithm is now unified, the split
still reflects what users *do* (classical vs mixed/quantum
lookups), which is the telemetry signal worth keeping. No new
counter for "BB invocations" — BB is the only impl, so the
algorithm-level count equals the umbrella count.

## §10 Testing (Q16')

Tests are reorganised under `tests/qram/` to reflect the new
algorithm. The v1 `test_qram_read_qrom_gates.cpp` is rewritten per
Q13'(γ) hard-replace; v1 contracts that survive are folded into
the new pins.

| Test module | Pins |
|---|---|
| `test_qram_read_bb_qrom_gates.cpp` (rewrites v1 `test_qram_read_qrom_gates.cpp`) | `(N=4, W=4)` exact CX/CCX counts derived from §4 phase budget. `i.super_mask == 0` after the call. `qrom_read` bumps by 1, `qreg_read` stays at 0. |
| `test_qram_read_bb_qreg_gates.cpp` (new) | `(N=4, W=4)` with one `a[k]` superposed. Exact CX/CCX counts identical to the QROM case (algorithm is data-agnostic). `qreg_read` bumps by 1, `qrom_read` stays at 0. **First gate-emission test for the qreg path.** |
| `test_qram_read_bb_padding.cpp` (new) | `(N=3, W=2)` padded to `N'=4`. Address `i=3` returns `0` (phantom leaf). Address `i=4` is UB and not tested. |
| `test_qram_read_bb_round_trip.cpp` (new) | `(N=8, W=4)` forward + `__QRAM_read_adj` round-trip. Pins: `b` restored to `\|0⟩`, every `a[k]` qubit restored to its entry contents, every ancilla released in `\|0⟩`. |
| `test_qram_read_bb_sequential.cpp` (new) | 5× sequential `QRAM_read` calls on the same `a[]`. Pins: each call's gate count matches the single-call pin, each call cleanly RAII-allocates and releases its ancilla pool, no cross-call state leaks. |
| `test_qram_read_bb_depth_scaling.cpp` (new) | Depth (CCX-depth, parsed from `RecordingSink`) at `(N ∈ {4, 8, 16, 32}, W=4)`. Asserts depth grows as `O(log² N)` with explicit constant ceiling tuned to the §4 budget. |
| `tests/qram/test_qram_read_stub.cpp` (existing) | Continues to assert umbrella `qram_read_count` increments by 1 per call. No change required. |
| `transpiler/tests/test_qram_e2e.cpp` (existing) | Continues to pass; the umbrella + split counters still bump. No change required. |

Exact gate counts at `(N=4, W=4)` are derived in the implementation
plan; the count pin lands as a constant computed from §4 phase
walk plus the 18-gate-set CSWAP decomposition (`CSWAP = CCX + 2 CNOT`).

## §11 Milestones (Q17')

- **B1.** `qram_read_bb_dsl.hpp` — recursive template, router state
  encoding per Q5'(ii), transit register RAII per Q12'(α). Test
  module `test_qram_read_bb_qrom_gates.cpp` (rewritten) pins the
  contract before B1's production code lands.
- **B2.** Sibling `qram_read_bb_dsl_adj.hpp` — self-adjoint
  registration per Q14'(ζ). Test module
  `test_qram_read_bb_round_trip.cpp` opens.
- **B3.** `qram_read.hpp` / `qram_read.cpp` wiring — pointer-form
  `switch(n)` dispatch table per Q10'(II), `qrom_read_impl` /
  `qreg_read_impl` both forward to BB DSL. Test module
  `test_qram_read_bb_qreg_gates.cpp` opens.
- **B4.** Power-of-2 padding (§6) — phantom-leaf transit
  allocation. Test module `test_qram_read_bb_padding.cpp` opens.
- **B5.** Telemetry verification — three-counter scheme survives
  unchanged. Spot check with `tests/qram/test_qram_split_counters.cpp`
  / `test_qram_telemetry_split.cpp` (existing); no rewrite expected.
- **B6.** Depth-scaling test
  `test_qram_read_bb_depth_scaling.cpp` opens. Sequential-call
  test `test_qram_read_bb_sequential.cpp` opens.
- **B7.** Delete v1 naive sweep (`qram_read_dsl.hpp` /
  `qram_read_dsl_adj.hpp`); update
  `qram_user_intro.md` §1 / §2.3 (the QROM precondition section is
  generalised to "any container", qreg path is no longer a stub);
  update `algorithm_authors_guide.md` if it references the QRAM
  cost model.
- **B8** (deferred sibling PRD, not in this epic).
  Polynomial-encoding fast path for `[[sturm::const_data]]`
  containers — opens when a concrete call site benefits from the
  `O(log log N)` T-depth and meets the compile-time-classical-data
  precondition.

## §12 Open items deferred to the implementation plan

- **Exact `(N=4, W=4)` gate count.** Derived from §4 phase
  walk; nailed down in the impl plan before B1's test module
  lands.
- **Depth-scaling test tolerance.** The constant ceiling for the
  `O(log² N)` claim depends on the CSWAP decomposition; tuned in
  B6.
- **N-cap for pointer overload.** §7 caps at `N=1024`; revisit if a
  real call site needs more.
- **Phantom-leaf CSWAP no-op detection.** Phantom-leaf swaps emit
  `W` CSWAPs that swap `|0⟩` with `|0⟩`. A peephole pass could
  elide these at transpile time; not in v2 scope.

## §13 References

- `docs/01_principles.md` — P5 (DSL primitive set), P9 / P9c
  (adjoint synthesis), B5a (depth-1 control invariant), B6 (RAII
  ancilla), B7 (qubit-index ownership).
- `docs/archive/prd_qram_subscript.md` — frontend matcher / rewrite
  contract (closed).
- `docs/archive/prd_qram_backend.md` — superseded v1 PRD (naive
  sweep).
- `docs/archive/plan_qram_backend.md` — superseded v1 plan.
- `docs/qram_user_intro.md` — user-facing reference (updated in
  B7).
- Giovannetti, Lloyd, Maccone, "Quantum Random Access Memory"
  (Phys Rev Lett 2008,
  [arXiv:0708.1879](https://arxiv.org/abs/0708.1879)) — original
  bucket-brigade architecture.
- Hann, Lee et al., "Resilience of Quantum Random Access Memory to
  Generic Noise" (PRX Quantum 2021,
  [arXiv:2012.05340](https://arxiv.org/abs/2012.05340)) —
  polylog noise resilience (informative; not normative for FT
  context per §3).
- Phalak & Ghosh, "A quantum random access memory (QRAM) using a
  polynomial encoding of binary strings"
  ([arXiv:2408.16794](https://arxiv.org/abs/2408.16794), Sci
  Reports 2025) — `O(log log N)` T-depth alternative; deferred to
  the §11 B8 sibling PRD.
