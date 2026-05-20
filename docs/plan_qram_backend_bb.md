# Implementation Plan — QRAM backend gate emission (bucket-brigade, v2)

**Status.** Draft, 2026-05-19.
**Tracks.** [`prd_qram_backend_bb.md`](prd_qram_backend_bb.md) §11
milestones B1–B7 (B8 is a deferred sibling PRD; not in this epic).
**Supersedes.** [`archive/plan_qram_backend.md`](archive/plan_qram_backend.md)
(v1 naive-sweep beat plan, status `Superseded`).
**Predecessors.** [`archive/prd_qram_subscript.md`](archive/prd_qram_subscript.md)
(frontend epic `sturm-u9ge`, closed) — the source-level rewrite
`qint b = a[i];` → `::sturm::QRAM_read(a, i, b);` is unchanged here.
**Related principles.** P5 (DSL primitive set), P9 / P9c (adjoint
synthesis), B5a (depth-1 control invariant), B6 (RAII ancilla),
B7 (qubit-index ownership), B10 (uncomputation is compile-time).

---

## §0 Reading guide

The plan decomposes the PRD into ordered **beats**. Each beat is a
self-contained, **test-driven** landing:

1. **Test first.** Every beat opens by writing the test module(s) that
   pin the contract the production module must satisfy. The test
   compiles and *fails* (or fails to link) before any production code
   is written. Production lands until the test goes green; only then
   is the beat closed.
2. **One production module per beat.** Hard cap **≤ 300 LoC**
   including comments and blank lines. If a module would exceed the
   cap, split along the seam identified in its description.
3. **One test module per beat (or one named test per goal — see §3).**
   Same 300-LoC ceiling; split along an axis (e.g. one file per
   `(N, W)` pair) before relaxing the cap.
4. Beats land in dependency order. Every beat is green (full library
   + qram + transpiler test suite passing under `ctest --parallel 6`)
   before the next is opened.

The plan will be filed as a bd epic (suggested mnemonic
`sturm-qram-bb`); children use numeric suffixes
(`sturm-qram-bb.1` … `sturm-qram-bb.N`). The mnemonic beat names
below (BB1–BB7) survive as documentation; the bd ids are the
authoritative work tracker — see §1a once filed.

**Threading limit (CLAUDE.md).** Every `cmake`, `ctest`, `make`, and
`ninja` invocation is capped at **6 parallel threads**. Restate this
in every subagent prompt.

**Host-clang invariant (CLAUDE.md / sturm-yial).** The transpiler
plugin requires `CMAKE_CXX_COMPILER` and `LLVM_DIR` to come from the
same LLVM install. The configure-time gate
`_sturm_check_host_clang_gate` FATAL_ERRORs on mismatch. Honour the
gate's diagnostic before touching any matcher-adjacent code.

---

## §1 Module map (bird's-eye view)

| Layer            | New / touched file                                                | Cap | Beat |
|------------------|-------------------------------------------------------------------|-----|------|
| BB routers       | `include/sturm/detail/lib/qram_read_bb_routers.hpp` (new)         | 250 | BB1  |
| Test             | `tests/lib/test_qram_bb_routers_recording.cpp` (new)              | 250 | BB1  |
| Test             | `tests/lib/test_qram_bb_routers_simulate.cpp` (new)               | 200 | BB1  |
| BB bus           | `include/sturm/detail/lib/qram_read_bb_bus.hpp` (new)             | 250 | BB2  |
| Test             | `tests/lib/test_qram_bb_bus_recording.cpp` (new)                  | 250 | BB2  |
| Test             | `tests/lib/test_qram_bb_bus_simulate.cpp` (new)                   | 250 | BB2  |
| BB DSL forward   | `include/sturm/detail/lib/qram_read_bb_dsl.hpp` (new)             | 200 | BB3  |
| BB DSL adjoint   | `include/sturm/detail/lib/qram_read_bb_dsl_adj.hpp` (new)         | 100 | BB3  |
| Test             | `tests/qram/test_qram_read_bb_qrom_gates.cpp` (rewrites v1)       | 300 | BB3  |
| Test             | `tests/qram/test_qram_read_bb_round_trip.cpp` (new)               | 250 | BB3  |
| Public surface   | `include/sturm/qram/qram_read.hpp` (touched)                      | 350 | BB4  |
| Public surface   | `src/sturm/qram/qram_read.cpp` (touched)                          | 200 | BB4  |
| Test             | `tests/qram/test_qram_read_bb_qreg_gates.cpp` (new)               | 250 | BB4  |
| Test             | `tests/qram/test_qram_read_bb_pointer_dispatch.cpp` (new)         | 300 | BB4  |
| Padding          | `include/sturm/detail/lib/qram_read_bb_dsl.hpp` (touched)         | 250 | BB5  |
| Test             | `tests/qram/test_qram_read_bb_padding.cpp` (new)                  | 200 | BB5  |
| Test             | `tests/qram/test_qram_read_bb_depth_scaling.cpp` (new)            | 250 | BB6  |
| Test             | `tests/qram/test_qram_read_bb_sequential.cpp` (new)               | 200 | BB6  |
| v1 hard-replace  | `include/sturm/detail/lib/qram_read_dsl.hpp` (deleted)            | —   | BB7  |
| v1 hard-replace  | `include/sturm/detail/lib/qram_read_dsl_adj.hpp` (deleted)        | —   | BB7  |
| v1 hard-replace  | `include/sturm/detail/lib/qram_read_predicate.hpp` (deleted iff unused) | — | BB7  |
| Docs             | `docs/qram_user_intro.md` (touched §1, §2.3)                      | —   | BB7  |
| Docs             | `docs/algorithm_authors_guide.md` (touched iff QRAM is referenced) | —  | BB7  |
| Docs             | `docs/prd_qram_backend_bb.md` (touched — Status flip)             | —   | BB7  |
| Build glue       | `tests/qram/CMakeLists.txt` (touched)                             | —   | BB1–BB7 |
| Build glue       | `tests/lib/CMakeLists.txt` (touched)                              | —   | BB1–BB2 |

**Existing tests retained — must remain green throughout the epic:**

- `tests/qram/test_qram_read_stub.cpp` — umbrella `qram_read_count`
  still bumps once per call (D1 contract).
- `tests/qram/test_qram_read_dispatch.cpp` — forwarding-trace pin
  (`(a, n, i, b)` quadruple is forwarded, not discarded).
- `tests/qram/test_qram_split_counters.cpp` — `Sink::qrom_read()` /
  `qreg_read()` hook surface.
- `tests/qram/test_qram_telemetry_split.cpp` — split + umbrella
  bump from non-overlapping sites.
- `transpiler/tests/test_qram_e2e.cpp` — frontend rewrite + dispatch
  end-to-end.
- `tests/backend/test_acceptance_prd.cpp` — backend acceptance pins.

LoC budgets are advisory ceilings. If a module would exceed its cap,
split along the noted seam — never relax the cap.

---

## §1a Beat → bd-issue id map

| Beat | bd id            | What it is                                                |
|------|------------------|-----------------------------------------------------------|
| —    | sturm-qram-bb    | epic (placeholder — actual id assigned at filing)         |
| BB1  | sturm-qram-bb.1  | BB router-state primitive + Phase 1/3 setup-and-teardown  |
| BB2  | sturm-qram-bb.2  | BB Phase 2 bus traversal + leaf XOR + reverse walk        |
| BB3  | sturm-qram-bb.3  | BB top-level DSL + self-adjoint registration              |
| BB4  | sturm-qram-bb.4  | Public surface wiring + pointer overload `switch(n)` table |
| BB5  | sturm-qram-bb.5  | Power-of-2 padding (phantom-leaf transit)                 |
| BB6  | sturm-qram-bb.6  | Depth-scaling + sequential-call telemetry tests           |
| BB7  | sturm-qram-bb.7  | v1 hard-replace + doc updates + PRD Status flip           |

---

## §2 Dependency graph

```
            ┌─────────────────────────────┐
            │ BB1  Router-state + setup/  │  (Phase 1 + Phase 3 of §4)
            │      teardown round trip    │
            └──────────┬──────────────────┘
                       │
            ┌──────────▼──────────────────┐
            │ BB2  Bus traversal + XOR    │  (Phase 2 of §4)
            │      + reverse walk         │
            └──────────┬──────────────────┘
                       │
            ┌──────────▼──────────────────┐
            │ BB3  Top-level DSL +        │  (forward = self-adjoint)
            │      adjoint registration   │
            └──────────┬──────────────────┘
                       │
            ┌──────────▼──────────────────┐
            │ BB4  Public surface +       │  (pointer switch(n) table;
            │      pointer dispatch       │   qreg path first-light)
            └──────────┬──────────────────┘
                       │
            ┌──────────▼──────────────────┐
            │ BB5  Power-of-2 padding     │  (phantom-leaf transit)
            └──────────┬──────────────────┘
                       │
            ┌──────────▼──────────────────┐
            │ BB6  Depth-scaling +        │  (asymptotic G4 pin +
            │      sequential-call tests  │   RAII round-trip pin)
            └──────────┬──────────────────┘
                       │
            ┌──────────▼──────────────────┐
            │ BB7  v1 hard-replace + doc  │  (Status: Implemented)
            └─────────────────────────────┘
```

BB1 → BB2 → BB3 are the core algorithm landing in test-driven slices.
BB4 routes the public surface into the new DSL helper and brings
**first-light gate emission for the qreg path**. BB5 lifts the
N-must-be-power-of-2 restriction. BB6 pins the asymptotic budget that
distinguishes v2 from v1. BB7 deletes the v1 implementation and flips
the PRD Status.

---

## §3 Test discipline

The PRD lists six goals (G1–G6 in §2). Each maps to a *named* test
that lives with a specific beat:

| Goal | Test                                                       | Beat |
|------|------------------------------------------------------------|------|
| G1   | `tests/lib/test_qram_bb_bus_simulate.cpp`                  | BB2  |
| G1   | `tests/qram/test_qram_read_bb_qrom_gates.cpp`              | BB3  |
| G1   | `tests/qram/test_qram_read_bb_qreg_gates.cpp`              | BB4  |
| G2   | `tests/lib/test_qram_bb_routers_recording.cpp`             | BB1  |
| G2   | `tests/lib/test_qram_bb_bus_recording.cpp`                 | BB2  |
| G2   | `tests/qram/test_qram_read_bb_qrom_gates.cpp`              | BB3  |
| G3   | `tests/qram/test_qram_read_bb_round_trip.cpp`              | BB3  |
| G3   | `tests/qram/test_qram_read_bb_sequential.cpp`              | BB6  |
| G4   | `tests/qram/test_qram_read_bb_depth_scaling.cpp`           | BB6  |
| G5   | `tests/qram/test_qram_read_bb_qrom_gates.cpp`              | BB3  |
| G5   | `tests/qram/test_qram_read_bb_qreg_gates.cpp`              | BB4  |
| G5   | `tests/qram/test_qram_telemetry_split.cpp` (existing)      | BB4  |
| G6   | `tests/qram/test_qram_read_bb_round_trip.cpp`              | BB3  |

A goal is **not** considered satisfied until its named test is green
and committed. Goals with multiple rows must satisfy all rows.

### §3.1 Statevector simulation policy

Orkan's qubit budget is 17 (`OrkanBridge::kMaxQubits = 17u` in
`include/sturm/backend/orkan_bridge.hpp:40`). BB's per-call ancilla
cost from PRD §4.5 is `2(N' − 1) + (N' − 1)W`. Add the user-side `i`
(`W` qubits) and `b` (`W` qubits) registers; the **simulator-side
total** is:

| (N, W)  | Routers | Transits | Bus | i + b | **Total** | Fits orkan?  |
|---------|---------|----------|-----|-------|-----------|---------------|
| (2, 2)  | 2       | 0        | 2   | 4     | **8**     | ✓             |
| (2, 4)  | 2       | 0        | 4   | 8     | **14**    | ✓             |
| (4, 2)  | 6       | 4        | 2   | 4     | **16**    | ✓ (tight)     |
| (4, 4)  | 6       | 8        | 4   | 8     | **26**    | ✗             |
| (8, 2)  | 14      | 12       | 2   | 6     | **34**    | ✗             |

**Statevector test policy:**
- BB1 `..._simulate.cpp` runs at `(N=4, W=2)` — 16 qubits, tightest fit.
- BB2 `..._simulate.cpp` runs at `(N=4, W=2)` and `(N=2, W=4)`.
- BB3 `test_qram_read_bb_round_trip.cpp` runs at `(N=2, W=4)` and
  `(N=4, W=2)`. The PRD's nominal `(N=8, W=4)` round-trip pin is
  recorded via `RecordingSink` only (G3-via-gate-balance, not via
  statevector).
- BB4 `test_qram_read_bb_qreg_gates.cpp` runs at `(N=4, W=2)` with
  one superposed `a[k]`.

Tests that exceed orkan's budget run with `RecordingSink` only and
assert on the gate stream (gate count, gate-set membership, balance
for round trips). Each test prints the qubit budget it consumed and
skips loudly with `GTEST_SKIP` if a future change pushes it over.

### §3.2 Recording-sink discipline

Each gate-stream test installs a `RecordingSink` via `ScopedSink`,
clears the records, runs the helper / `QRAM_read`, then asserts:

1. **Gate-set membership.** Every record's `op` is one of
   `quantum_xor` (X / CX / CCX class) or `quantum_and` (the CCX class
   emitted by `qbool::operator&` / `c_n_AND` if it appears inside any
   transitive sub-call). No rotation, prepare, or measurement op
   appears.
2. **Counter parity.** Per beat — `qrom_read` bumps once on
   classical-only `a[]`, `qreg_read` bumps once when any `a[k]` has
   `super_mask != 0`, umbrella `qram::qram_read_count() == 1` per
   dispatched call.
3. **Index immutability.** `i.super_mask` and `i.value` unchanged
   from entry to exit (paired flips inside the helper net to zero).
4. **Exact gate budget** for a representative `(N, W)` pair (BB3
   asserts the value computed in §4 from the BB phase walk).
5. **Round-trip balance.** Each `(op, qubit-group, control-group)`
   triple appears an even number of times across forward + adjoint.

---

## §4 Gate-budget cheat sheet (BB at `(N, W)`)

For padded `N' = 2^⌈log₂ N⌉` and width `W`, per call to the BB body:

### §4.1 Phase 1 (router setup) — from PRD §4.1

- **Root.** `root.is_right ^= addr[0]` and `root.is_left ^= addr[0]`
  each lift to a CNOT controlled on `addr[0]`'s qubit. The
  `X(root.is_left)` is bare (no WHEN) — an X gate.
  Subtotal: **2 CNOT + 1 X**.
- **Each non-root router (N′ − 2 of them).** Under
  `WHEN(p.is_X)`: `r.is_right ^= addr[ℓ]` lifts to CCX, `r.is_left ^=
  addr[ℓ]` lifts to CCX, `X(r.is_left)` lifts to CNOT. Subtotal per
  router: **2 CCX + 1 CNOT**. Across `N′ − 2` non-root routers:
  **2(N′ − 2) CCX + (N′ − 2) CNOT**.
- **Phase 1 total:** `2(N′ − 2) CCX + N′ CNOT + 1 X`.

### §4.2 Phase 2 (bus traversal + XOR + reverse walk) — from PRD §4.2

- **Per internal node.** Two W-wide controlled swaps (one per WHEN
  arm). Each W-wide controlled swap = `W` scalar CSWAPs = `W` × (1
  CCX + 2 CNOT) = `W CCX + 2W CNOT`. Both arms emit (the other arm
  is a quantum no-op when its WHEN bit is `|0⟩`, but the gate stream
  is identical). Per internal node: **2W CCX + 4W CNOT**.
- **Forward walk** (N′ − 1 internal nodes): **2W(N′ − 1) CCX + 4W(N′ − 1) CNOT**.
- **XOR payload** `b ^= bus`: **W CNOT** (W parallel uncontrolled CXs).
- **Reverse walk** (same as forward): **2W(N′ − 1) CCX + 4W(N′ − 1) CNOT**.
- **Phase 2 total:** `4W(N′ − 1) CCX + (8W(N′ − 1) + W) CNOT`.

### §4.3 Phase 3 (teardown) — from PRD §4.3

Phase 1 in reverse: same gate count, reversed order. **2(N′ − 2)
CCX + N′ CNOT + 1 X**.

### §4.4 Closed-form per-call totals

```
CCX(N, W)  = 4(N′ − 2)   +   4W(N′ − 1)
CNOT(N, W) = 2N′         +   W · (8N′ − 7)
X(N, W)    = 2
```

For the BB3 / BB5 pin fixtures:

| (N, W)  | N′ | CCX | CNOT | X | Total prim. |
|---------|----|-----|------|---|-------------|
| (2, 2)  | 2  | 0   | 22   | 2 | 24          |
| (2, 4)  | 2  | 0   | 58   | 2 | 60          |
| (4, 2)  | 4  | 32  | 26   | 2 | 60          |
| (4, 4)  | 4  | 56  | 108  | 2 | 166         |
| (3, 2)  | 4  | 32  | 26   | 2 | 60          |
| (8, 4)  | 8  | 136 | 244  | 2 | 382         |

(The `(N=3, W=2)` row instantiates the padded `N′=4` body — gate
count matches `(4, 2)`; the phantom-leaf payload swap is still
emitted but acts as `|0⟩ ↔ |0⟩` at the simulator level.)

The pin test recomputes the expected count from the formula
parameterised on `(N′, W)` — no hardcoded numbers in the test source.

### §4.5 Depth shape (G4)

CCX-depth at the §4 phase walk:

- Phase 1: `O(log N′)` layers (level-by-level setup, sibling nodes
  emit in parallel within a level).
- Phase 2: `O(W · log N′)` layers — each level's controlled W-swap
  block decomposes to depth `O(W)` (`W` scalar CSWAPs of constant
  CCX-depth), and the bus traverses `O(log N′)` levels sequentially
  forward and reverse.
- Phase 3: `O(log N′)` (mirror of phase 1).

**Depth-scaling test (BB6).** At fixed `W` and `N ∈ {4, 8, 16, 32}`,
assert `depth(N) ≤ C₀ · W · log²(N′)` with `C₀` derived from the
`N = 4` measurement plus a 1.25× ceiling. This pins the asymptotic
shape claimed by G4; the exact constant is tuned in BB6.

---

## §5 Beats

### Beat BB1 — Router-state primitive + Phase 1/3 round trip

**Goal.** Introduce the BB router as a typed primitive and land
Phase 1 (setup) and Phase 3 (teardown) as mutual inverses. The
header exposes a recursive template parameterised on tree depth
`d`. Stand-alone — no bus, no payload yet.

**Files.**
- `include/sturm/detail/lib/qram_read_bb_routers.hpp` (new, ≤ 250 LoC).
  Exposes:
  ```cpp
  namespace sturm::detail_qram_bb {

  // Router occupies two qbools; structural invariant
  // is_left * is_right = 0 per PRD §4.1.
  struct BBRouter {
      qbool is_left;
      qbool is_right;
  };

  // Phase 1 — root + recursive descent. `routers` is a flat array
  // of length (N' - 1), level-order (root at routers[0], next two at
  // routers[1..2], etc.). `addr_bits` is the address qint's qubit
  // view (high-K bits, K = log2 N').
  template <std::size_t Nprime>
  inline void bb_setup_routers(qint_t<...>& addr,
                               std::array<BBRouter, Nprime - 1>& routers);

  // Phase 3 — Phase 1 in reverse. Same args; restores every router
  // to (0, 0). Self-defined for placement-audit clarity; calls
  // bb_setup_routers from a __reverse helper.
  template <std::size_t Nprime>
  inline void bb_teardown_routers(qint_t<...>& addr,
                                  std::array<BBRouter, Nprime - 1>& routers);

  }  // namespace
  ```
  The recursive walk is compile-time-unrolled via Q2'(b)
  (transpile-time recursion). RAII: callers own the `routers` array;
  each `BBRouter`'s `qbool` allocates lazily via `WHEN` /
  `BitProxy` materialisation when first written.

**Tests.**

- `tests/lib/test_qram_bb_routers_recording.cpp` (G2). Install
  `RecordingSink`. For `Nprime ∈ {2, 4, 8}` (tree depths d = 1, 2,
  3) and a classical address `addr ∈ [0, Nprime)`:
  1. Allocate `addr` + `routers`. Run `bb_setup_routers`. Assert:
     - Every record's `op ∈ {quantum_xor, quantum_and}`. No rotations.
     - Gate count equals **`2(Nprime − 2) CCX + Nprime CNOT + 1 X`**
       (matches §4.1, recomputed in the test from `Nprime`).
  2. Run `bb_teardown_routers`. Assert:
     - The recorded gate stream across (setup + teardown) is
       **balanced**: every `(op, qubit-group, control-group)` triple
       appears an even number of times.
     - The post-teardown classical state of every router is `(0, 0)`
       (assert via `qbool::value` / `super_mask`).
- `tests/lib/test_qram_bb_routers_simulate.cpp` (statevector,
  `(Nprime = 4)` — 6 router qubits + 2 address qubits = 8 simulator
  qubits, fits orkan). For each classical `addr ∈ {0, 1, 2, 3}`:
  1. Run `bb_setup_routers`. Read off the on-path routers via
     `OrkanBridge` measurement-free probability extraction; assert:
     - On-path nodes are in their `|L⟩` (1, 0) / `|R⟩` (0, 1) state
       with `P ≈ 1.0`.
     - Off-path nodes are in `|wait⟩` (0, 0) with `P ≈ 1.0`.
  2. Run `bb_teardown_routers`. Assert every router state has
     `P((is_left, is_right) = (0, 0)) ≈ 1.0`. Assert `addr` is
     unchanged.
- *Recursive-unrolling sanity* (in `..._recording.cpp`): assert that
  the compile-time-recursed template instantiates without invoking
  any runtime `for k = 0..N` loop — measured by total recorded gate
  count matching the closed form (not by inspecting compiler IR).

**Production.** ~200 LoC in `qram_read_bb_routers.hpp`. The recursive
template is `if constexpr`-dispatched on `d`: base case at `d == 0`
(no-op leaf), recursive case at `d > 0` (emit at root, recurse into
both children).

**Done when:** both new tests green; existing qram + lib suites
green; LoC ≤ 250.

---

### Beat BB2 — Bus traversal + leaf XOR + reverse walk

**Goal.** Land Phase 2 of §4 as a stand-alone helper that consumes
**already-set-up** routers (from BB1) and emits the bus walk →
leaf-side swap → `b ^= bus` → reverse walk gate stream. Routers
remain pinned in their level-order state at exit; the caller is
responsible for invoking `bb_teardown_routers` after the bus walk
completes (BB3 composes them).

**Files.**
- `include/sturm/detail/lib/qram_read_bb_bus.hpp` (new, ≤ 250 LoC).
  Exposes:
  ```cpp
  namespace sturm::detail_qram_bb {

  // Phase 2 — assumes `routers` already set up by bb_setup_routers.
  // Walks bus through CSWAP tree down to addressed leaf, XORs
  // b ^= bus, walks back up. At exit: bus = |0>, every transit = |0>,
  // a[addressed_leaf] qubits swapped out and back (restored), b
  // XORed with a[addressed_leaf]'s contents.
  template <std::size_t Nprime, std::size_t W>
  inline void bb_bus_traverse(const std::array<BBRouter, Nprime - 1>& routers,
                              std::array<std::array<qbool, W>, Nprime - 2>& transits,
                              std::array<qbool, W>& bus,
                              const qint_t<W>* a,
                              qint_t<W>& b);

  }  // namespace
  ```

**Tests.**

- `tests/lib/test_qram_bb_bus_recording.cpp` (G2). For
  `(Nprime, W) ∈ {(2, 2), (4, 2), (4, 4), (8, 4)}` and a fixed
  classical `a` (e.g. seeded random):
  1. Allocate routers / transits / bus / b. Pre-set routers via
     `bb_setup_routers` (from BB1 — already green).
  2. Clear `RecordingSink`. Run `bb_bus_traverse`.
  3. Assert gate count matches Phase 2 formula
     **`4W(Nprime − 1) CCX + (8W(Nprime − 1) + W) CNOT`**, no
     rotations / prepares.
- `tests/lib/test_qram_bb_bus_simulate.cpp` (G1, statevector).
  Fixtures `(Nprime, W) ∈ {(2, 4), (4, 2)}` — both ≤ 16 qubits:
  1. For each classical `i ∈ [0, Nprime)`: prepare `a` classical,
     `b = |0⟩^W`. Run `bb_setup_routers(addr_i) → bb_bus_traverse →
     bb_teardown_routers(addr_i)`. Assert via OrkanBridge:
     - `P(b == a[i]) ≈ 1.0`.
     - Every router, transit, bus qubit is in `|0⟩` (release-safe).
     - Every `a[k]` qubit is restored to its entry value.
  2. *Superposed-index sanity* (`Nprime=2, W=4`): apply `H` to
     `addr.bit(0)`, run setup + bus + teardown. Assert
     `P(b = a[0]) ≈ 0.5` and `P(b = a[1]) ≈ 0.5`.

**Production.** ~200 LoC in `qram_read_bb_bus.hpp`. Uses
`lib_swap_dsl(Bit&, Bit&)` (`include/sturm/detail/lib/swap_dsl.hpp`)
for the W-wide swaps, under `WHEN(router.is_X)` for the controlled
form. Both arms (`WHEN(is_left)` and `WHEN(is_right)`) emit per
internal node; structural invariant ensures only one fires
quantum-mechanically.

**Done when:** both new tests green; BB1 tests still green; full
suite green; LoC ≤ 250.

---

### Beat BB3 — Top-level DSL + self-adjoint registration

**Goal.** Compose BB1 + BB2 into the public `lib_qram_read_bb_dsl`
template. Register its self-adjoint sibling via
`STURM_REGISTER_ADJOINT`. This is the first beat where the **public
contract from PRD §2 G1 / G3 / G5 / G6** is testable end-to-end at
the DSL layer; BB4 then wires it into `QRAM_read`.

**Files.**
- `include/sturm/detail/lib/qram_read_bb_dsl.hpp` (new, ≤ 200 LoC).
  ```cpp
  namespace sturm {

  template <std::size_t W, std::size_t N>
  inline void lib_qram_read_bb_dsl(const qint_t<W>* a,
                                   qint_t<W>& i,
                                   qint_t<W>& b) {
      constexpr std::size_t Nprime = next_pow2(N);  // = N when N is pow2
      // RAII-allocate routers, transits, bus per PRD §4.5.
      std::array<detail_qram_bb::BBRouter, Nprime - 1> routers;
      std::array<std::array<qbool, W>, Nprime - 2> transits;
      std::array<qbool, W> bus;
      // Promote each qbool to quantum if backend context live
      // (matches qram_read_dsl.hpp:134-146 pattern; see also BB5
      // for phantom-leaf padding when N != Nprime).

      detail_qram_bb::bb_setup_routers<Nprime>(i, routers);
      detail_qram_bb::bb_bus_traverse<Nprime, W>(routers, transits, bus, a, b);
      detail_qram_bb::bb_teardown_routers<Nprime>(i, routers);

      // Release ancillae back to QubitPool (mirror qram_read_dsl.hpp:188-194).
  }

  }  // namespace sturm

  #include "sturm/detail/lib/qram_read_bb_dsl_adj.hpp"
  ```
  `next_pow2` is a `constexpr` helper. **In BB3** the template
  asserts `is_pow2(N)` and only instantiates at powers of 2. BB5
  lifts that gate and adds the phantom-leaf transit.
- `include/sturm/detail/lib/qram_read_bb_dsl_adj.hpp` (new, ≤ 100 LoC).
  Forward body is self-inverse per PRD §4.4:
  ```cpp
  template <std::size_t W, std::size_t N>
  inline void __lib_qram_read_bb_dsl_adj(const qint_t<W>* a,
                                         qint_t<W>& i,
                                         qint_t<W>& b) {
      lib_qram_read_bb_dsl<W, N>(a, i, b);
  }

  #ifdef STURM_BACKEND_ENABLED
  STURM_REGISTER_ADJOINT(::sturm::lib_qram_read_bb_dsl,
                         ::sturm::__lib_qram_read_bb_dsl_adj)
  #endif
  ```

**Tests.**

- `tests/qram/test_qram_read_bb_qrom_gates.cpp` (G1 + G2 + G5,
  **rewrites** v1's `test_qram_read_qrom_gates.cpp`). At `(N=4, W=4)`,
  classical `a = {0xA, 0x5, 0xF, 0x0}`, classical `i = 2`:
  1. Install `RecordingSink`. Call `lib_qram_read_bb_dsl<4, 4>(a.data(), i, b)`.
  2. Assert exact CX + CCX + X counts match **§4.4 closed form**
     for `(N′=4, W=4)`: 108 CNOT, 56 CCX, 2 X. Counts are recomputed
     in the test from the formula — no magic numbers.
  3. Every record's `op ∈ {quantum_xor, quantum_and}` — no rotations.
  4. `i.super_mask == 0` and `i.value == 2` after the call.
  5. Sink counter parity is N/A at this layer (counters live on the
     public `QRAM_read` site, BB4 wires them) — pin a `TODO(BB4)`
     comment.
- `tests/qram/test_qram_read_bb_round_trip.cpp` (G3 + G6). At
  `(N=2, W=4)` (statevector, ≤ 14 qubits) and `(N=4, W=2)`
  (statevector, 16 qubits, tight):
  1. For each classical `i ∈ [0, N)`: prepare classical `a`, `b = 0`.
     Run forward `lib_qram_read_bb_dsl`. Assert `P(b == a[i]) ≈ 1.0`.
  2. Run `__lib_qram_read_bb_dsl_adj`. Assert `P(b == 0) ≈ 1.0` and
     every `a[k]` qubit is restored to its entry value.
  3. *Recorded gate balance* (at `(N=8, W=4)`, gate-stream-only —
     too large for orkan): every `(op, qubit-group, control-group)`
     triple appears an even number of times across forward + adjoint.
  4. *Superposed-`i` round trip*: apply `H` to `i.bit(0)` at
     `(N=2, W=4)`; forward + adjoint leaves `b` in `|0⟩^W` and
     `i.super_mask` unchanged.

**Production.** ~120 LoC across the two headers (the body is mostly
RAII boilerplate; the heavy lifting is in BB1/BB2).

**Done when:** both new tests green; full library / qram / transpiler
suite green; LoC ≤ 200 / 100 per header.

---

### Beat BB4 — Public surface + pointer dispatch + qreg first-light

**Goal.** Route `QRAM_read`'s three overloads (and the
`__QRAM_read_adj` mirrors) into `lib_qram_read_bb_dsl`. Implement
the pointer overload's `switch(n)` dispatch table per PRD §7. **Both
QROM and qreg helpers forward to the same BB body — the algorithm
is data-classicality-agnostic** (PRD §4).

**Files.**
- `include/sturm/qram/qram_read.hpp` (touched, ≤ 350 LoC).
  - Replace the `qram_read_qrom_impl` body's call to
    `lib_qram_read_qrom_dsl` with `lib_qram_read_bb_dsl<W, N_template>`
    (the std::array / C-array overloads have `N` at compile time;
    the pointer overload uses BB-dispatch below).
  - Make `qram_read_qreg_impl` **no longer a counter-only stub**:
    forward to `lib_qram_read_bb_dsl` exactly like the QROM impl.
    The split counter still fires (qreg path is identified by
    `any_super_mask != 0` at the public entry-point) — only the
    body changes.
  - **Pointer overload `switch(n)` table** (new, replacing the
    `qram_read_*_impl<W>` call):
    ```cpp
    template <std::size_t W>
    inline void QRAM_read(const qint_t<W>* a, std::size_t n,
                          const qint_t<W>& i, qint_t<W>& b) noexcept {
        if (Sink* s = current_sink()) s->qram_read();
        const auto any_super = _qram_detail::any_super_mask<W>(a, n);
        // Smallest N' >= n, N' in the family {2, 4, 8, ..., 1024}.
        switch (_qram_detail::ceil_log2_index(n)) {
            case 0:  /* n <= 1 */  return;  // no-op / single-element CNOT below
            case 1:  _qram_detail::qram_read_dispatch<W, 2>(a, n, i, b, any_super);   return;
            case 2:  _qram_detail::qram_read_dispatch<W, 4>(a, n, i, b, any_super);   return;
            ...
            case 10: _qram_detail::qram_read_dispatch<W, 1024>(a, n, i, b, any_super); return;
            default: _qram_detail::qram_read_n_over_cap_diagnose(n);                  return;
        }
    }
    ```
    `qram_read_dispatch` bumps the split counter via `dispatch_common`
    then calls `lib_qram_read_bb_dsl<W, N>` (compile-time `N`).
- `src/sturm/qram/qram_read.cpp` (touched, ≤ 200 LoC). The
  `dispatch_common` body is unchanged. Add
  `qram_read_n_over_cap_diagnose` — sink-side error counter +
  `assert(false)` in debug; production behaviour is documented as UB
  (no other QRAM helpers do runtime range checks per Q5 of §5).

**Tests.**

- `tests/qram/test_qram_read_bb_qreg_gates.cpp` (G1 + G5 — **first
  gate-emission test for the qreg path**). At `(N=4, W=2)` with
  `a = {0x1, 0x2, 0x3, |+⟩-on-bit-0 of slot 3}` (one slot superposed):
  1. Install `RecordingSink`. Call `QRAM_read(a, i_classical_2, b)`.
  2. Assert exact CX + CCX + X counts match the §4.4 formula for
     `(N′=4, W=2)`: 26 CNOT, 32 CCX, 2 X (identical to QROM —
     algorithm is data-agnostic).
  3. `current_sink()->get_count("qreg_read") == 1`,
     `qrom_read == 0`, umbrella `qram::qram_read_count() == 1`.
  4. Run statevector at `(N=4, W=2)` (16 qubits, tight): with
     classical `i = 3`, assert that `b`'s state matches the
     superposition of `a[3]`'s slot.
- `tests/qram/test_qram_read_bb_pointer_dispatch.cpp` (new). Pointer
  overload, table coverage:
  1. For each `n ∈ {0, 1, 2, 3, 4, 5, 7, 8, 16, 32, 1023, 1024}`
     (every family arm + a phantom-padding case at `n=3, 5, 7, 1023`):
     run `QRAM_read(a_ptr, n, i, b)`. Assert:
     - Umbrella `qram_read` bumps once.
     - Split counter on the correct arm (QROM vs qreg).
     - No `qram_read_n_over_cap_diagnose` fires.
  2. *Over-cap*: `n = 1025` triggers `qram_read_n_over_cap_diagnose`.
     The body asserts in debug; in release the sink-side error
     counter increments. Test pins the release behaviour via a
     custom sink subclass.
  3. *Argument forwarding pin* (regression guard from B1 of v1):
     each call still fires the `set_forwarding_trace` hook with the
     correct `(a, n, i, b, path_tag)` quadruple.

**Production.** ~80 LoC of new `switch(n)` dispatch logic on top of
the existing `qram_read.hpp`; ~30 LoC tweak in `qram_read.cpp`.

**Done when:** new + all existing qram / lib / backend / transpiler
tests green. The `tests/qram/test_qram_telemetry_split.cpp` and
`tests/qram/test_qram_split_counters.cpp` modules continue to pass
unchanged.

---

### Beat BB5 — Power-of-2 padding (phantom-leaf transit)

**Goal.** Lift BB3's `is_pow2(N)` precondition. For `N` not a power
of 2, instantiate at `N' = 2^⌈log₂ N⌉` and realise phantom leaves
(`N ≤ k < N'`) as `W`-qubit transit ancilla blocks at call entry,
initialised to `|0⟩`. PRD §6.

**Files.**
- `include/sturm/detail/lib/qram_read_bb_dsl.hpp` (touched, ≤ 250 LoC).
  Replace the `static_assert(is_pow2(N))` in BB3 with:
  - Compute `Nprime = next_pow2(N)` at compile time.
  - Allocate `Nprime - N` phantom-leaf transit blocks of W qubits
    each, initialised to `|0⟩`. Pass them into `bb_bus_traverse` as
    the leaf-side targets for slots `[N, Nprime)`.
  - At the bus-traverse layer (touched), accept a "leaf pointer
    list" of length `Nprime`, where slots `[0, N)` point at `a[k]`'s
    qubits and slots `[N, Nprime)` point at the phantom transit
    blocks. The Phase 2 leaf swap becomes uniform across real and
    phantom slots.

**Tests.**
- `tests/qram/test_qram_read_bb_padding.cpp` (new). At `(N=3, W=2)`,
  classical `a = {0x2, 0x1, 0x3}` (slots 0–2 real; slot 3 phantom):
  1. Forward `QRAM_read(a, i=0, b)` → `b == 0x2`.
  2. Forward `QRAM_read(a, i=2, b)` → `b == 0x3`.
  3. Forward `QRAM_read(a, i=3, b)` → `b == 0x0` (phantom leaf is
     `|0⟩^W`, XOR is a no-op on `b`).
  4. **UB note in test comment**: `i ≥ 4` is UB per §5; not tested.
  5. Recording-sink gate count at `(N=3, W=2)` matches the formula
     for `(N′=4, W=2)`: 26 CNOT, 32 CCX, 2 X (identical to a real
     `(N=4, W=2)` call — phantom leaves contribute the same gate
     stream as real leaves).

**Production.** ~50 LoC delta on `qram_read_bb_dsl.hpp`; the bus
helper signature gains an optional `phantom_leaves` array.

**Done when:** new test green; padding tests + all prior tests pass.

---

### Beat BB6 — Depth-scaling + sequential-call telemetry

**Goal.** Pin the asymptotic depth shape claimed by G4 (PRD §2) and
the RAII / ancilla-pool round-trip claimed by §4.5.

**Files.**
- `tests/qram/test_qram_read_bb_depth_scaling.cpp` (new). Fixed
  `W = 4`, `N ∈ {4, 8, 16, 32}` — all RecordingSink only (no orkan
  budget). For each `N`:
  1. Run `QRAM_read(a, i, b)` once.
  2. Parse the `RecordingSink` records into a per-qubit timeline;
     compute CCX-depth as the longest CCX chain across all qubits.
  3. Pin the §4.5 ceiling: `depth(N) ≤ C₀ · W · log²(N)` where
     `C₀` is derived from the `N = 4` measurement plus a 1.25×
     headroom. Print actual depth + bound on each fixture for
     forensic re-tuning.
- `tests/qram/test_qram_read_bb_sequential.cpp` (new). At
  `(N=4, W=2)` — 16 qubits — over 5 sequential calls:
  1. Snapshot `QubitPool` size before each call. Run `QRAM_read`.
     Snapshot after. Assert pool is back to entry size (RAII pin).
  2. Assert each call's recorded gate count equals the single-call
     pin from BB3 (no cross-call gate leak).
  3. Sink counters: `qrom_read == 5`, umbrella `qram_read == 5`
     after the 5-call loop. Reset and re-run with one superposed
     `a[k]`: `qreg_read == 5`, `qrom_read == 0`.

**Production.** None (tests-only beat). All production lands in
BB1–BB5.

**Done when:** both tests green; full suite green. The depth-scaling
ceiling constant `C₀` is captured in the test source as a `constexpr`
with a comment pointing at the §4.5 derivation.

---

### Beat BB7 — v1 hard-replace + doc updates + Status flip

**Goal.** Delete the v1 naive-sweep code (now dead — every consumer
routes through BB), regenerate user-facing docs, and flip the PRD
Status from `Proposed` to `Implemented`. Closes the epic.

**File deletions** (per PRD §8 / Q13'(γ) hard-replace):
- `include/sturm/detail/lib/qram_read_dsl.hpp`
- `include/sturm/detail/lib/qram_read_dsl_adj.hpp`
- `include/sturm/detail/lib/qram_read_predicate.hpp` —
  **conditional**: BB1–BB3 do not depend on it; delete only if a
  `grep -rn lib_qram_eq_k_` across the tree returns no other
  consumers. (If something else uses it, file a TODO and skip.)

**File updates.**
- `docs/qram_user_intro.md` §1 / §2.3:
  - Drop the "QROM precondition" framing — the algorithm now handles
    any container.
  - Mention that the qreg path emits gates (no longer a stub).
  - Update the resource-budget table to point at PRD §4.5.
- `docs/algorithm_authors_guide.md` — search for references to the
  QRAM cost model. If `qram` appears, update the cost characterisation
  from `O(N · W)` Toffoli depth to `O(log² N · W)` T-depth.
- `docs/prd_qram_backend_bb.md` — **Status:** `Proposed, 2026-05-19`
  → `Implemented, <commit date>`.

**Tests.** None new. The deletions are validated by:
- `ctest --parallel 6` continues to pass (zero references to
  `lib_qram_read_qrom_dsl` or `lib_qram_eq_k_*` survive).
- `grep -rn "qram_read_dsl\|lib_qram_eq_k_" include/ src/ tests/`
  returns nothing.

**Production.** Header deletions + ≤ 30 LoC of doc updates.

**Done when:**
- `ctest --parallel 6` green on `qram`, `lib`, `qtypes`, `backend`,
  `transpiler` labels.
- `grep` for the deleted symbol names returns clean.
- PRD Status reads `Implemented, <date>`.
- A `bd close sturm-qram-bb` closes the epic with a link to the
  landing commit.

---

## §6 What does NOT land in v2 (PRD §3 cross-check)

- **Hann-Lee noise-resilient form.** Simple CSWAP-tree only. Polylog
  noise resilience is academic in the FT execution model per §3.
- **Polynomial-encoding fast path** (`O(log log N)` T-depth via
  Möbius transform). Deferred to a sibling PRD conditioned on
  `[[sturm::const_data]]` containers — PRD §11 B8.
- **Persistent qubit pool across calls.** Pure RAII per call (B6).
- **`std::vector<qint>` container shape.** Unchanged; pointer overload
  covers the runtime-sized case.
- **Small-N fast path / naive-sweep fallback.** Always BB. The small-
  N overhead is accepted (see §4.4 table: `(N=4, W=4)` is 166
  primitives vs. ~56 for v1 naive sweep). Single-algorithm
  simplicity wins.
- **Runtime address range check.** UB-on-violation, inherited from
  v1 §5 verbatim.
- **Peephole pass for phantom-leaf CSWAP elision.** PRD §12 — not in
  v2 scope.

---

## §7 Quality gates per beat

A beat is **not** closed until:

1. The named test(s) for the beat's goal(s) are green.
2. `ctest --parallel 6` passes the **full** `qram`, `lib`, `qtypes`,
   `backend`, and `transpiler` labels.
3. The production module is ≤ its LoC cap from §1.
4. No new TODO refers to anything inside the v2 scope (PRD §3
   non-goals are fair game for follow-up TODOs).
5. The bd issue is closed with a one-line note pointing at the
   landing commit (`bd close <id>`).

---

## §8 Session-close protocol (CLAUDE.md)

Each beat's session ends with:

1. `bd ready` to confirm queue state.
2. Quality gates above.
3. `bd close <id>` for the beat.
4. `git pull --rebase` → `bd dolt push` (note: this repo is currently
   local-only — see `bd prime` output; the `bd dolt push` step is
   skipped when no remote is configured) → `git push`.
5. `git status` shows "up to date with origin" (or "no remote
   configured" if local-only).

Work is not complete until `git push` succeeds (or, if local-only,
the local commit lands).

---

## §9 References

- [`docs/prd_qram_backend_bb.md`](prd_qram_backend_bb.md) — this
  plan's PRD.
- [`docs/01_principles.md`](01_principles.md) — P5 (DSL primitive
  set), P9 / P9c (adjoint synthesis), B5a (depth-1 control), B6
  (RAII ancilla), B7, B10.
- [`docs/archive/prd_qram_subscript.md`](archive/prd_qram_subscript.md)
  — frontend rewrite contract (closed; unchanged here).
- [`docs/archive/prd_qram_backend.md`](archive/prd_qram_backend.md)
  — superseded v1 PRD (naive sweep).
- [`docs/archive/plan_qram_backend.md`](archive/plan_qram_backend.md)
  — superseded v1 plan (this document's template).
- `include/sturm/qram/qram_read.hpp` — public surface touched in BB4.
- `include/sturm/detail/lib/swap_dsl.hpp` — `lib_swap_dsl` primitive
  used by BB2's W-wide controlled swaps.
- `include/sturm/control/when.hpp` — `WHEN` macro and the depth-1
  invariant relied on throughout BB1–BB2.
- `include/sturm/qtypes/qbool.hpp` — `qbool` / `ensure_qubit()` /
  `QubitPool::allocate()` used for router / transit / bus
  allocation.
- `include/sturm/backend/orkan_bridge.hpp` — `kMaxQubits = 17u`;
  caps statevector test fixtures per §3.1.
- `tests/backend/test_bitwise_and_or_simulate.cpp` — reference
  pattern for `OrkanBridge` statevector tests.
- Giovannetti, Lloyd, Maccone, "Quantum Random Access Memory"
  (Phys Rev Lett 2008,
  [arXiv:0708.1879](https://arxiv.org/abs/0708.1879)) — original
  bucket-brigade architecture.
