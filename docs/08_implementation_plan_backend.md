# Implementation Plan — STURM Backend

Derived from `07_prd_backend.md`. The frontend scaffold (Steps 0–12 of `06_implementation_plan.md`) is already complete: `QubitPool`, `qbool`, `qint_t<W>` skeleton, classical fast paths, WHEN macro, the provisional sink interface (`CounterSink`/`RecordingSink`), and the aggregate test runner are in place. This plan covers everything the backend PRD adds on top.

## Guiding constraints

- **Test-driven.** Every module lands with its tests in the same change. No module is considered done until its tests are green inside the aggregate runner (`tests/run_all`).
- **Module size budget: < 300 LOC** per translation unit (header + source counted separately). If a module approaches the limit, split along the seams already suggested below rather than inflating one file.
- **Additive only.** Existing frontend files are extended, not rewritten. The one exception is replacing the provisional `Sink` with the C-ABI `execute_gate` — and that replacement happens behind the existing call sites, so frontend tests keep passing throughout.
- **No runtime decomposition** except the single CRx/CRy/CRz → `CX + R` identity at the Orkan call site.
- **Bennett discipline is load-bearing.** Every operator leaves its inputs pristine; tests enforce this invariant directly on the recording sink.
- **One module = one bd issue** when we switch to execution. This document is the source of truth for the scope of each issue; issues will be filed module-by-module, not in a single batch, so the plan can flex as earlier modules reveal constraints.

## Directory layout (target)

```
include/sturm/
  core/
    gate_kind.h          # C: enum + taxonomy table (M1)
    core.h               # C ABI: execute_gate, BackendContext, mode, measure (M2)
    context.hpp          # C++ inline helpers over core.h (M3)
  backend/
    ir.hpp               # GateRecord + IR buffer (M6)
    exec_count.hpp       # COUNT_ONLY executor (M7)
    exec_append.hpp      # APPEND executor (M8)
    exec_simulate.hpp    # SIMULATE executor (M10/M11/M12)
    orkan_bridge.hpp     # thin wrapper around Orkan state_t (M9)
  dispatch/
    reduction_table.hpp  # (gate_kind, classical-pattern) -> reduced kind (M14)
    dispatch_gate.hpp    # Layer A entry point (M15/M16)
    promotion.hpp        # partial promotion + promotion_mask (M17)
  uncompute/
    uncompute_op.hpp     # tagged union (M19)
    uncompute_run.hpp    # RAII runner invoked from destructors (M20)
  qtypes/                # existing — extended in M21/M22
  control/               # existing — extended in M23/M24
src/sturm/
  core/context.cpp
  core/execute_gate.cpp  # Layer B sink (M13)
  backend/orkan_bridge.cpp
  backend/exec_simulate.cpp
  dispatch/reduction_table.cpp
  dispatch/dispatch_gate.cpp
  dispatch/promotion.cpp
  uncompute/uncompute_run.cpp
tests/backend/           # new test tree, one file per module
```

Each `.hpp`/`.cpp` pair holds one module. The 300-LOC budget is per file, not per module; most modules will be well under that because the logic is small and table-driven.

---

## Phase 0 — Doc sync (no code)

### M0. Spec edits required by PRD §14
- Edit `docs/01_principles.md` §B4: replace the four-primitive sink contract with the 18-gate `execute_gate` interface.
- Edit `docs/04_prd_frontend.md` §3 and §11: remove "no uncomputation, no ancilla" and the stubs that are now superseded by Strategy B.
- Edit `docs/05_spec_frontend.md` around line 349: delete the "virtual indices 0/1–64/..." text; replace with the `qint.qubits[i]` phrasing from PRD §14.
- **Test:** `grep` sanity — the stale phrases must be gone; `tests/docs/scan_stale_terms.sh` asserts.

---

## Phase 1 — Primitive gate set and sink ABI

### M1. Gate kind enum + taxonomy table
**Files:** `include/sturm/core/gate_kind.h`, `src/sturm/core/gate_kind.c`, `tests/backend/test_gate_kind.cpp`.
**Scope (≈60 LOC + ≈80 LOC test):**
- `typedef enum { STURM_GATE_X, …, STURM_GATE_SWAP } sturm_gate_kind_t;` exactly 18 entries, in the PRD §4 order.
- `typedef enum { STURM_CE_NONE, STURM_CE_FLIP, STURM_CE_BRANCH } sturm_classical_effect_t;`
- `struct sturm_gate_info { uint8_t arity; sturm_classical_effect_t effect; bool permutation; const char* name; };`
- `const sturm_gate_info* sturm_gate_info_of(sturm_gate_kind_t);`
- Static table literal in the `.c` file.
**Tests:** one entry per gate; arities match PRD §4; FLIP ⇒ permutation; NONE ⇒ not permutation (except Z/CZ/CRz — taxonomy table lookups compared against a literal expected array).

### M2. C ABI sink header (`core.h`)
**Files:** `include/sturm/core/core.h`, `tests/backend/test_core_abi_c.c`.
**Scope (≈120 LOC header):**
- `typedef struct sturm_backend_context sturm_backend_context_t;` opaque.
- `typedef enum { STURM_MODE_COUNT_ONLY, STURM_MODE_APPEND, STURM_MODE_SIMULATE } sturm_mode_t;`
- `sturm_backend_context_t* sturm_backend_create(sturm_mode_t, uint32_t max_qubits);`
- `void sturm_backend_destroy(sturm_backend_context_t*);`
- `void sturm_set_thread_context(sturm_backend_context_t*);`
- `sturm_backend_context_t* sturm_get_thread_context(void);`
- `void sturm_execute_gate(sturm_gate_kind_t, const uint32_t* qubits, uint8_t n, double param);`
- `uint64_t sturm_gate_count(const sturm_backend_context_t*);`
- `int sturm_measure(uint32_t qubit);`  // per-mode semantics
- **Tests:** compile the header from a `.c` translation unit (no C++), link against a stub implementation, verify symbols are reachable.

### M3. `BackendContext` + mode storage (C++ side)
**Files:** `include/sturm/core/context.hpp`, `src/sturm/core/context.cpp`, `tests/backend/test_context.cpp`.
**Scope:**
- `sturm::BackendContext` struct: mode, gate counter, owned `QubitPool`, pointer-to-IR (null unless APPEND), pointer-to-Orkan state (null unless SIMULATE).
- `thread_local sturm_backend_context_t*` with process-wide default instance created on first access.
- `sturm::set_mode(mode)` convenience wrapper.
**Tests:** default context exists; mode roundtrips; gate counter starts at 0; switching context does not leak the previous; thread-local default is shared within one thread.

---

## Phase 2 — Execution modes (executors)

Executors are pure functions over `(BackendContext&, gate_kind, qubits, n, param)`. The Layer B dispatcher (M13) picks one. Building them before the dispatcher lets each be unit-tested in isolation with a hand-constructed context.

### M6. IR buffer + `GateRecord`
**Files:** `include/sturm/backend/ir.hpp`, `tests/backend/test_ir.cpp`.
**Scope (<150 LOC):**
- `struct GateRecord { sturm_gate_kind_t kind; std::array<uint32_t,3> qubits; uint8_t n; double param; };`
- `class GateIR { std::vector<GateRecord> records_; public: void append(const GateRecord&); size_t size() const; const GateRecord& at(size_t) const; void clear(); };`
- **Tests:** append/size/at roundtrip; bulk append does not invalidate earlier records (capacity reserve check).

### M7. COUNT_ONLY executor
**Files:** `include/sturm/backend/exec_count.hpp`, `tests/backend/test_exec_count.cpp`.
**Scope (<40 LOC):** `inline void exec_count(BackendContext&, …) { /* counter handled by dispatcher */ }` — a no-op, but declared so the dispatcher's mode switch is symmetric. **Tests:** executing 1000 gates does not allocate (fuzz with a recording allocator).

### M8. APPEND executor
**Files:** `include/sturm/backend/exec_append.hpp`, `tests/backend/test_exec_append.cpp`.
**Scope (<80 LOC):** push a `GateRecord` onto `ctx.ir`. **Tests:** gate order preserved; `param` preserved bit-exact for rotation gates; arity respected (extra slots zeroed).

### M9. Orkan vendoring + bridge skeleton
**Files:** `cmake/OrkanFetch.cmake`, `include/sturm/backend/orkan_bridge.hpp`, `src/sturm/backend/orkan_bridge.cpp`, `tests/backend/test_orkan_bridge.cpp`.
**Scope:**
- `FetchContent_Declare(orkan GIT_REPOSITORY https://github.com/Timo59/orkan GIT_TAG <pinned-sha>)` + `FetchContent_MakeAvailable`.
- `OrkanBridge` owns an `orkan::state_t` pre-allocated to 17 qubits.
- Only one bridge call is exposed in this module: `void allocate(uint32_t n)` + `void reset_zero()`.
**Tests:** CMake configure + build succeeds offline once fetched; constructing a bridge on 17 qubits initializes to `|0…0⟩` (probability of the zero basis state == 1.0 within 1e-12).

### M10. SIMULATE executor — 1-qubit gates
**Files:** `include/sturm/backend/exec_simulate.hpp`, `src/sturm/backend/exec_simulate.cpp`, `tests/backend/test_exec_simulate_1q.cpp`.
**Scope (<250 LOC):** dispatch `X/Y/Z/H/S/T/P/Rx/Ry/Rz` to Orkan. **Tests per gate:**
- Apply to a known input state, compare resulting amplitudes against a hand-computed target with tolerance 1e-10.
- Parameterized gates swept at θ ∈ {0, π/8, π/4, π/2, π}.

### M11. SIMULATE executor — 2/3-qubit permutation & phase gates
**Files:** same as M10 (extension), `tests/backend/test_exec_simulate_multiq.cpp`.
**Scope:** `CX/CY/CZ/CCX/SWAP`. **Tests:** Bell-pair via `H,CX`; Toffoli truth table over all 8 basis states; SWAP exchanges amplitudes.

### M12. SIMULATE executor — CRx/CRy/CRz decomposition
**Files:** same as M10 (extension), `tests/backend/test_exec_simulate_crot.cpp`.
**Scope (<60 LOC):** at call time, emit `CX(ctrl,tgt); R(±θ/2)(tgt); CX(ctrl,tgt); R(θ/2)(tgt)` per the standard controlled-rotation identity (axis-specific). **Tests:** compare against reference unitary on 2-qubit state vector; ensure only COUNT_ONLY and APPEND see the original `CRx`/`CRy`/`CRz` kind (enforced in M13 test).

### M13. Layer B — `execute_gate`
**Files:** `src/sturm/core/execute_gate.cpp`, `tests/backend/test_execute_gate.cpp`.
**Scope (<120 LOC):**
- Increment `ctx.gate_count` unconditionally.
- Switch on `ctx.mode` → one of the three executors.
- C ABI entry point `sturm_execute_gate` forwards to the C++ helper.
**Tests:** count is exactly N after N calls in each mode; APPEND records exactly N entries; SIMULATE produces correct final state; CRx/CRy/CRz recorded as-is under APPEND but decomposed under SIMULATE.

---

## Phase 3 — Mode selection, measurement, qubit cap

### M4. CLI + mode init helper
**Files:** `include/sturm/core/init.hpp`, `src/sturm/core/init.cpp`, `tests/backend/test_init.cpp`.
**Scope (<150 LOC):** `sturm::init_from_args(int argc, char** argv)` parses `--mode=count|append|simulate`; `sturm::set_mode`. **Tests:** all three args; missing arg defaults to COUNT_ONLY; invalid mode aborts with stable error.

### M5. Measurement op per mode
**Files:** `include/sturm/core/measure.hpp`, `src/sturm/core/measure.cpp`, `tests/backend/test_measure.cpp`.
**Scope (<120 LOC):**
- COUNT_ONLY, APPEND: return stored classical value of the qint.
- SIMULATE: sample from Orkan, update the classical value, clear the relevant `super_mask` bits, clear corresponding `promotion_mask` bits.
**Tests:** deterministic placeholder under non-SIMULATE; single-sample correctness on `H|0⟩` under SIMULATE (repeated runs → roughly 50/50 within statistical bound).

### M18. 17-qubit cap enforcement
**Files:** extend `QubitPool` (already present), `tests/backend/test_qubit_cap.cpp`.
**Scope (<40 LOC delta):** `acquire()` past 17 triggers `std::abort` with a stable message (matching PRD §6). **Tests:** allocating 18 aborts; allocating 17 then releasing 1 then allocating 1 succeeds.

---

## Phase 4 — Layer A (dispatch intelligence)

### M14. Classical-control reduction table
**Files:** `include/sturm/dispatch/reduction_table.hpp`, `src/sturm/dispatch/reduction_table.cpp`, `tests/backend/test_reduction_table.cpp`.
**Scope (<200 LOC):**
- Static map keyed by `(gate_kind, classical_pattern_bits)` → `(reduced_gate_kind, remaining_operand_indices)`.
- Entries for every gate with controls: CX, CY, CZ, CRx/CRy/CRz, CCX.
- CCX expands to 9 combinations across its two control slots (`00,01,0q,10,11,1q,q0,q1,qq` where `q` = quantum).
**Tests:** exhaustive table drive — for each entry, feed a synthetic invocation and assert the reduction matches a hand-written expected result. Table must not contain unreachable patterns (covered by a generator test).

### M15. Layer A entry — all-classical path
**Files:** `include/sturm/dispatch/dispatch_gate.hpp`, `src/sturm/dispatch/dispatch_gate.cpp`, `tests/backend/test_dispatch_all_classical.cpp`.
**Scope (<150 LOC):**
- Inspect operand `super_mask` to decide classicality.
- NONE → skip. FLIP → mutate classical values in-place per a small permutation-lookup function (separate helper for X, CX, SWAP, CCX, etc.). BRANCH → forward to M17 promotion then re-enter dispatch.
**Tests:** classical X flips bit; classical CX behaves as xor; classical SWAP exchanges; classical H triggers promotion (verified by spying on promotion).

### M16. Layer A entry — mixed and all-quantum paths
**Files:** same as M15 (extension), `tests/backend/test_dispatch_mixed.cpp`.
**Scope:**
- Mixed: consult reduction table; if reduced to no-op, return without calling Layer B; otherwise, after reduction, resolve physical qubit indices via `qint.qubits[i]` and call `execute_gate`.
- All-quantum: direct index lookup and call.
**Tests:** CX with classical ctrl=0 does nothing (counter unchanged); CX with classical ctrl=1 emits X on target (counter +1); CCX with one classical 1 and one quantum control emits CX; fully-quantum CCX emits CCX.

### M17. Operand promotion
**Files:** `include/sturm/dispatch/promotion.hpp`, `src/sturm/dispatch/promotion.cpp`, `tests/backend/test_promotion.cpp`.
**Scope (<180 LOC):**
- `void promote_bits(qint_base&, uint64_t bit_mask)`: allocates qubits from the pool for each bit in `bit_mask & ~super_mask`; for bits whose classical value is 1, emit `X` via Layer B; sets those bits in `super_mask` and `promotion_mask`.
- Partial-promotion invariant: untouched bits remain classical.
- Rejects `const` via `static_assert`/overload absence at the call site.
**Tests:** promotes exactly the requested bits; X-emits equal popcount of classical-1 bits inside the requested mask; idempotent on already-quantum bits; subsequent operations on quantum bits go through Layer B.

---

## Phase 5 — Uncomputation (Strategy B)

### M19. `uncompute_op` tagged union
**Files:** `include/sturm/uncompute/uncompute_op.hpp`, `tests/backend/test_uncompute_op.cpp`.
**Scope (<200 LOC):**
- Enum `kind`: `NONE, ADD_CONST, SUB_CONST, ADD_QINT, SUB_QINT, MUL_INVERSE, DIV_INVERSE, MOD_INVERSE, BITWISE_SELF, COMPARE` (extensible, one entry per operator that can produce an uncomputable result).
- Inline data union — no heap, no `std::function`; size capped (`static_assert(sizeof(uncompute_op) <= 32)`).
- Constructors per tag; `apply(BackendContext&, qint_base& self) const;` runs the inverse.
**Tests:** size cap holds; tag roundtrip; `apply` for `ADD_CONST(c)` emits the same gate sequence as `-=c` would (byte-for-byte against the recording sink).

### M20. RAII uncompute runner
**Files:** `include/sturm/uncompute/uncompute_run.hpp`, `src/sturm/uncompute/uncompute_run.cpp`, `tests/backend/test_uncompute_run.cpp`.
**Scope (<150 LOC):**
- Invoked from `qint_t<W>::~qint_t` and `qbool::~qbool`.
- Order: run `uncompute_op.apply`, then emit `X` for each bit set in `promotion_mask`, then release qubits to the pool.
- `NONE` tag = no inverse (measurement or explicit clear).
**Tests:** destruction order of temporaries in a single full-expression is reverse of construction; promotion-mask bits are flipped before release; `NONE` bypasses `apply` cleanly.

### M21. Wire `operator+=(int)` to uncompute (pilot)
**Files:** extend existing `include/sturm/qtypes/qint.hpp` + its source, `tests/backend/test_uncompute_add_const.cpp`.
**Scope:** single operator end-to-end to validate the Strategy B spine before fanning out.
**Tests:** after `{ auto t = a + 5; }` the recording sink shows the add gates followed by the exact inverse sequence, and `a` is byte-identical before and after.

### M22. Wire remaining operators to uncompute
**Files:** extend qtypes for `operator+`, `-`, `*`, `/`, `%`, bitwise ops, comparisons. `tests/backend/test_uncompute_each_op.cpp` (one test per operator, parameterized over W ∈ {4, 8, 16}).
**Scope:** each operator sets the appropriate tag on the returned temporary. Bennett discipline is checked per operator: the tests compare the pre-op state of the inputs against the post-destruction state and assert equality.

---

## Phase 6 — WHEN-block control propagation

### M23. Hand-written controlled op variants
**Files:** `include/sturm/ops/c_quantum_add.hpp`, …, `tests/backend/test_controlled_ops.cpp`.
**Scope:** per op, a `c_*` variant that accepts a `qbool& ctrl` and rewrites every `X`→`CX`, `CX`→`CCX`, `CCX`→hand-coded ancilla decomposition. Each op in its own file to honor the 300-LOC budget (most will be well under).
**Tests:** for each op, under `ctrl=|0⟩` the state is unchanged; under `ctrl=|1⟩` the state equals the uncontrolled op applied to the target; under `ctrl=(|0⟩+|1⟩)/√2` the state equals the expected superposition (checked in SIMULATE mode against hand-computed amplitudes).

### M24. WHEN entry-point dispatch
**Files:** extend `include/sturm/control/when.hpp`, `tests/backend/test_when_dispatch.cpp`.
**Scope (<100 LOC delta):**
- Public op entry inspects `WhenGuard::active_control()` and dispatches to either the uncontrolled or controlled variant.
- Nested WHEN collapses to a single ancilla control (principle B5 — already partially implemented; extended here to cover all ops).
**Tests:** op inside WHEN uses `c_*` variant; op outside uses uncontrolled; nested WHEN still has exactly one ancilla control.

---

## Phase 7 — Integration & polish

### M25. Explicit template instantiation
**Files:** `src/sturm/qtypes/instantiations.cpp`, `tests/backend/test_instantiations.cpp`.
**Scope:** `template class qint_t<4>; template class qint_t<8>; template class qint_t<16>; template class qint_t<32>;` plus the `extern template` declarations in the header. **Tests:** link-time — a fixture TU that only declares `extern template` and calls each method must link successfully.

### M26. End-to-end acceptance: `c = (a + 5) >= 0`
**Files:** `tests/backend/test_end_to_end.cpp`.
**Scope:**
- Write the canonical one-liner from PRD §3.
- Run it under all three modes.
- COUNT_ONLY: assert expected gate count (pinned to current decomposition; updated if operators change).
- APPEND: assert gate sequence matches the stored golden record.
- SIMULATE: assert final classical register equals the mathematical answer for a swept range of inputs.

### M27. CI hook extension
**Files:** update `tests/run_all` / CMake ctest glue.
**Scope:** the backend test tree joins the existing aggregate runner. A new `ctest` label `backend` is added so the two trees can also be run independently. **Test:** `ctest -L backend` runs, passes, and reports ≥ one test from each module M1–M26.

---

## Build order and dependency graph

```
M0 ──► M1 ──► M2 ──► M3 ──► M6 ──► M7 ──► M8 ──┐
                                               ├──► M13 ──► M14 ──► M15 ──► M16 ──► M17 ──► M19 ──► M20 ──► M21 ──► M22 ──► M23 ──► M24 ──► M25 ──► M26 ──► M27
                  M9 ──► M10 ──► M11 ──► M12 ──┘                                   ▲
M4 ──────────────────────────────────────────────────────────────────────────────┘
M5 and M18 hang off M13 in parallel with M14 (no ordering constraint against Phase 4).
```

- **M0** is pure docs; do it first so the frontend/backend boundary in the spec is internally consistent before any code lands.
- **Phase 1 (M1–M3)** establishes the sink ABI and context so every later module has a stable surface to link against.
- **Phase 2 executors** can be developed in parallel with a stub dispatcher; they do not depend on Layer A.
- **M13** is the join point: once it exists, Layer A can start calling through it.
- **M19/M20** depend only on Layer B being present, so Strategy B can land independently of Layer A's mixed-operand logic.
- **Phase 6** is the last functional block because WHEN-block controlled variants reference library ops that must already have uncompute wired.

## Test strategy per phase

- **Unit tests** (per module) live in `tests/backend/test_<module>.cpp`. Each uses the existing lightweight test harness (no gtest dependency).
- **Golden records** for APPEND mode are stored under `tests/backend/golden/` as plain-text gate listings. They are regenerated by `ctest -L backend -R record_goldens` under human review.
- **Tolerance:** SIMULATE comparisons use 1e-10 unless the gate involves `T` or fractional rotations, in which case 1e-12.
- **Bennett invariant tests** are a dedicated file, not scattered across operator tests: one test per operator that snapshots inputs, runs the op, destroys the temporary, and asserts `memcmp`-equality on the inputs.
- **Recording sink** (existing `RecordingSink` from Step 2) is reused by retargeting the C ABI `execute_gate` at it inside the test context. This lets every Layer A test assert on emitted gate sequences without touching Orkan.
- **Orkan tests** are isolated to modules M9–M12 and M26. All other tests run in COUNT_ONLY or APPEND mode, keeping the test matrix fast.

## LOC budget tracking

Each module listed above declares a target size (the `<N LOC` annotations). `tests/backend/test_loc_budget.cpp` walks the file tree and asserts every file under `include/sturm/{core,backend,dispatch,uncompute}` and `src/sturm/{core,backend,dispatch,uncompute}` is under 300 LOC (counting non-blank, non-comment lines). Any module that breaks the budget fails CI and must be split before merge.

## Open items (tracked but not scheduled)

Inherited from PRD §15 — per-op uncompute opt-out, multi-threading, raising the 17-qubit cap, mid-circuit measurement correctness under non-SIMULATE modes, aliasing detection. They do not gate any module in this plan.

---

End of plan.
