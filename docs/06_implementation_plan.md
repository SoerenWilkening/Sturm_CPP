# STURM C++ Front-End — Implementation Plan

Modular, test-driven, every file ≤ 300 LOC. Built strictly bottom-up: each step ships with its tests passing before the next begins. Tests are written *first* per step (red → green → next).

---

## Guiding rules

- **One header = one concept.** No header exceeds 300 LOC; if it would, split (e.g. `qint_arith.hpp`, `qint_bitwise.hpp` included by `qint.hpp`).
- **TDD loop per step:** (1) write/extend test file, (2) confirm it fails to build or assert, (3) implement minimal header to pass, (4) re-run full suite, (5) commit.
- **No forward references across steps.** Step N only depends on artifacts from steps < N.
- **Single dispatch helper** (`detail::dispatch_binary`) reused by every binary op to keep operator headers tiny and consistent (PRD §9 risk mitigation).
- **All TODOs tagged** `// TODO(backend):` (spec §10).

---

## Step 0 — Scaffold (no logic)

**Deliverables**
- `CMakeLists.txt` (spec §9, ~20 LOC).
- `tests/CMakeLists.txt` (foreach loop, ~15 LOC).
- Empty placeholder header `include/sturm/sturm.hpp` (umbrella include, grows over time).
- `tests/test_smoke.cpp`: `int main(){ return 0; }`.

**Exit criterion:** `cmake -B build && cmake --build build && ctest` passes with one trivial test.

---

## Step 1 — `QubitPool`  *(spec §1.1)*

**Module:** `include/sturm/core/qubit_pool.hpp` (~80 LOC)

**Bounded ancilla pool.** Real backends only expose a small number of ancilla qubits (working budget ≈ `4 * 64 = 256`). The pool must therefore *recycle* indices rather than handing out a fresh integer per allocation. A program touching 100 000 logical bits must still fit within the 256-index budget by reusing freed slots.

- Singleton via `instance()`.
- Compile-time capacity `static constexpr int kCapacity = STURM_ANCILLA_CAPACITY;` (default `256`, override via CMake).
- Internal free-list: `std::vector<int> free_`, plus `std::atomic<int> high_water_{0}` for never-yet-issued indices.
- `int allocate()`:
  - If `free_` non-empty → pop and return.
  - Else if `high_water_ < kCapacity` → return `high_water_.fetch_add(1)`.
  - Else throw `std::runtime_error("ancilla pool exhausted")` (or assert in debug).
- `void release(int idx)` → push onto `free_` (still tagged `// TODO(backend):` for the eventual reset-to-|0⟩ on the device).
- `void reset_for_testing()` clears `free_` and resets `high_water_` to 0.
- `int in_use() const`, `int capacity() const` for diagnostics/tests.
- Thread-safety: a single `std::mutex` around `free_`/`high_water_` is fine — allocation is not on the hot loop.

**Lifetime hooks.** `qbool` and `qint_t` destructors must call `QubitPool::release(idx)` for every owned index whose slot was lazily allocated. This is the only way the 256-slot budget stays bounded under heavy use.

**Test:** `tests/test_qubit_pool.cpp` (~120 LOC)
- Sequential `allocate()` returns monotonically increasing until capacity.
- `release` then `allocate` returns the released index (LIFO recycling).
- Allocating `kCapacity + 1` without releases throws.
- Loop: allocate + immediately release 10 000 times → `in_use() == 0`, `high_water() <= kCapacity`.
- After `reset_for_testing`, count restarts at 0.
- Multi-threaded: 8 threads × 1000 (allocate, release) pairs → no duplicate live indices, final `in_use() == 0`.
- Integration smoke (added later, after Step 6): construct and destroy 10 000 `qint`s in a loop; assert `QubitPool::in_use() == 0` and `high_water() <= kCapacity`.

---

## Step 2 — `Sink` interface + `CounterSink` + `RecordingSink`  *(spec §1.2, §1.3)*

Split into three files to stay well under 300 LOC each.

**Module A:** `include/sturm/core/sink.hpp` (~150 LOC)
- `struct Record { string op; vector<vector<int>> qubit_groups; vector<double> scalars; int control; };`
- Pure-virtual `Sink` with **every** method from PRD §10 (uniform signature: `(const vector<int>& a, const vector<int>& b, int control)` for binary; unary drops `b`; rotations take `(int qubit, double, int control)`; `prepare(int, double)`).
- `Sink* current_sink(); void set_current_sink(Sink*);` backed by `inline thread_local Sink* g_sink = &default_counter_sink();`.
- `struct ScopedSink { Sink* prev; ... };` RAII installer.

**Module B:** `include/sturm/core/counter_sink.hpp` (~120 LOC)
- `class CounterSink : public Sink` overrides every method, increments `unordered_map<string,size_t>`.
- `size_t count(string_view) const;`
- Default sink is a function-local `static CounterSink` returned by `default_counter_sink()`.

**Module C:** `include/sturm/core/recording_sink.hpp` (~150 LOC)
- `class RecordingSink : public Sink` appending `Record`s.
- `const vector<Record>& records() const; void clear();`

**Tests**
- `tests/test_sink_counter.cpp`: install `CounterSink`, call `quantum_add({1},{2},-1)`, assert `count("quantum_add")==1`.
- `tests/test_sink_recording.cpp`: install `RecordingSink` via `ScopedSink`, dispatch a few ops, assert vector contents and that destructor restores prior sink.

---

## Step 3 — `qbool`  *(spec §2)*

**Module:** `include/sturm/qtypes/qbool.hpp` (~120 LOC)
- Fields `bool value`, `bool is_super`, `array<int,1> qubits{-1}`.
- Ctors: default, `qbool(bool)` implicit, `explicit qbool(double p)` → sets `is_super=true`, allocates qubit, calls `current_sink()->prepare(qubits[0], p)`.
- `explicit operator bool() const` returns `value` with `// TODO(backend): real measurement`.
- `void ensure_qubit()` helper (used later by `WHEN`).
- No `qint` dependency yet (no conversions in this header — those live in `qint.hpp` to avoid cycles).

**Test:** `tests/test_qbool.cpp` (~80 LOC)
- Default: `value==false`, `is_super==false`, `qubits[0]==-1`.
- `qbool(true)`: implicit, `value==true`.
- `qbool(0.5)`: `is_super==true`, `qubits[0]>=0`, recording sink saw one `prepare(qubit, 0.5)`.
- `static_cast<bool>` round trip.

---

## Step 4 — Mask transfer pure functions  *(spec §5)*

**Module:** `include/sturm/core/mask_ops.hpp` (~120 LOC, pure `constexpr` free functions in `sturm::detail`)
- `mask_bitwise(a,b)` = `a|b`
- `mask_not(a)` = `a`
- `mask_shl(a,n,width)`, `mask_shr(a,n,width)` (clamped)
- `mask_addsub(a,b,width)`: lowest set bit `i` = `__builtin_ctzll`; output = `m | (~0ULL << i)`, then clamp to `width` bits. Special-case `m==0`.
- `mask_muldiv(a,b,width)`: any bit set anywhere → `(width==64? ~0ULL : (1ULL<<width)-1)`, else 0.

**Test:** `tests/test_mask_ops.cpp` (~100 LOC) — pure table-driven assertions, no dependencies on qint. This isolates the trickiest logic from operator dispatch.

---

## Step 5 — Dispatch helper  *(spec §4)*

**Module:** `include/sturm/core/dispatch.hpp` (~150 LOC, header-only, in `sturm::detail`)

A single template that every operator overload calls:

```cpp
template <class Out, class A, class B, class ClassicalFn, class MaskFn, class SinkFn>
Out dispatch_binary(const A& a, const B& b,
                    ClassicalFn classical, MaskFn mask_fn, SinkFn sink_call);
```

Implements the 8-step algorithm from spec §4 verbatim:
1. Compute `new_mask`.
2. Compute classical result.
3. Fast path if `new_mask == 0`.
4. Lazy-allocate qubits on `a`, `b`, `out` at every set bit position (single helper `ensure_bit_qubit(arr, i)`).
5. Read `current_control` from `when.hpp` (forward-declared thread-local pointer; include order: `when_fwd.hpp` declares only the TLS, `when.hpp` defines guard later — avoids cycle).
6. Invoke `sink_call`.
7. Stamp result.
8. Return.

Also `dispatch_unary`, `dispatch_compare` (returns `qbool`, allocates target qubit), `dispatch_shift` (rhs is `int`).

**Sub-module:** `include/sturm/control/when_fwd.hpp` (~15 LOC) — only:
```cpp
namespace sturm::detail { inline thread_local qbool* current_control = nullptr; }
```
Included by both `dispatch.hpp` and the future `when.hpp`.

**Test:** `tests/test_dispatch.cpp` (~120 LOC)
- Hand-construct two fake `qint`-shaped POD locals and call `dispatch_binary` directly with lambdas.
- Verifies fast-path (mask 0 → no sink call), allocation path (qubit indices materialize), control routing (manually set TLS to a `qbool` and assert `control` arg in record).

---

## Step 6 — `qint_t<Width>` skeleton + classical fast paths  *(spec §3)*

Split the type to keep each header ≤ 300 LOC:

**Module A:** `include/sturm/qtypes/qint_fwd.hpp` (~30 LOC) — forward declares template, the `qint = qint_t<64>` alias, and friend declarations.

**Module B:** `include/sturm/qtypes/qint_core.hpp` (~200 LOC)
- Class definition, data members (`value`, `super_mask`, `qubits`).
- All ctors, assignments, conversions to/from `int64_t`, `qbool` (per spec §7).
- `qubits_vec()` helper returning `vector<int>` of currently-allocated qubit indices (used by sink calls).
- `PhiProxy`, `ThetaProxy` nested types (definitions live here, ~30 LOC).

**Module C:** `include/sturm/qtypes/qint_arith.hpp` (~250 LOC)
- `+ - * / %`, compound assigns, unary `-`, `pow`. Each operator is ~5 lines: a single call into `dispatch_binary` with the matching `classical`/`mask`/`sink` lambdas.

**Module D:** `include/sturm/qtypes/qint_bitwise.hpp` (~200 LOC)
- `& | ^ ~`, compound assigns, `<< >>`, compound shift assigns. Same dispatch pattern.

**Module E:** `include/sturm/qtypes/qint_compare.hpp` (~150 LOC)
- `== != < <= > >=` returning `qbool`. Uses `dispatch_compare`.
- `operator[](size_t i)` returning a `qbool` view (spec §3, sharing `qubits[i]`).

**Module F:** `include/sturm/qtypes/qint.hpp` (~30 LOC) — umbrella that includes A–E in the right order, defines the `using qint = qint_t<64>;` alias, and the free `pow` overloads.

> All overload bodies are *thin* (one dispatch call). The 300-LOC budget stays comfortably below limit because logic lives in `dispatch.hpp` and `mask_ops.hpp`.

**Tests (TDD: written before each module above):**

- `tests/test_qint_classical.cpp` (~250 LOC) — covers Modules A–E. For every operator: classical inputs, exact int64 result, `super_mask==0`, `qubits` all `-1`, no sink call (use `RecordingSink` and assert `records().empty()`).
- `tests/test_qint_superposed.cpp` (~200 LOC) — for each op family:
  - Construct `qint` with `super_mask = 0x4` and `qubits[2]` pre-allocated.
  - Run op, assert mask widening rule from §8/§5.
  - Assert qubit allocations occurred at expected positions.
  - Assert exactly one matching `Record` in `RecordingSink`.

---

## Step 7 — `WHEN` macro and guard  *(spec §6)*

**Module:** `include/sturm/control/when.hpp` (~80 LOC)
- Includes `when_fwd.hpp` (TLS already declared in step 5).
- `struct WhenGuard` constructor:
  - classical false → `run=false`, no TLS change.
  - classical true → `run=true`, no TLS change.
  - super → `run=true`, `expr.ensure_qubit()`, save `prev_control`, set `current_control = &expr`.
- Destructor restores `prev_control` (only if it changed).
- `make_when_guard<T>` with `static_assert(is_same_v<decay_t<T>, qbool>)`.
- `WHEN(expr)` macro identical to spec.

**Test:** `tests/test_when.cpp` (~120 LOC)
- False classical → body not run (use a counter int).
- True classical → body run, `detail::current_control == nullptr` inside.
- `qbool(0.5)` → body run, `current_control != nullptr`, points to flag, qubit allocated; after scope `current_control == nullptr` again.
- Compile-time rejection: `WHEN(true)` not exercised at runtime; instead a separate `tests/test_when_static.cpp` uses `// static_assert` line commented out + a comment instructing manual verification (CI can skip). Alternatively use a `requires` clause check via `concept`.

---

## Step 8 — End-to-end sink dispatch tests  *(spec §8 `test_sink_dispatch.cpp`)*

**Test:** `tests/test_sink_dispatch.cpp` (~200 LOC)
- `qint += qint` with one super bit → exactly one `quantum_add` record, `control == -1`, mask widened upward.
- Same op inside `WHEN(qbool(0.5))` → record's `control` equals the flag's qubit index.
- `qint * qint` with one super bit → `quantum_mul` record AND output mask is all-ones (`~0ULL`).
- Bitwise `^` with disjoint super bits → `quantum_xor` record, mask is exact OR.
- Comparison with super inputs → `quantum_eq` record + result `qbool.is_super == true` + result qubit allocated.

---

## Step 9 — Conversion test sweep  *(spec §8 `test_conversions.cpp`)*

**Test:** `tests/test_conversions.cpp` (~120 LOC)
- All six rows of PRD §6 table.
- `qint(qbool(0.5))`: `mask & 1 == 1`, shared qubit index.
- `qbool(qint(0xFF))` explicit narrow: `value==true`, `is_super==false`, lowest qubit shared.
- Ensures conversion-only paths don't accidentally call any sink method (assert `RecordingSink::records().empty()` aside from `prepare`).

---

## Step 10 — Phase / amplitude proxies

Already declared in `qint_core.hpp` (Step 6 Module B). This step only writes the test:

**Test:** `tests/test_phase_amp.cpp` (~80 LOC)
- `q.phi() += 0.25;` → `RecordingSink` saw one `phi_add(qubit, 0.25, control)`.
- Same for `theta()`.
- Inside `WHEN(qbool(0.5))`: `control` arg matches flag qubit.
- Confirms rotation calls do **not** widen `super_mask` (rotations are phase-only).

---

## Step 11 — Width parameterization smoke  *(spec PRD §12)*

**Test:** `tests/test_width.cpp` (~60 LOC)
- Instantiate `qint_t<8>`, run a few classical ops, assert mask widening clamps to 8 bits in `mask_addsub`.
- Confirms the template compiles for non-default Width without changing public surface.

---

## Step 12 — Aggregate test runner & CI hook

- Update `tests/CMakeLists.txt` to register every test file added in steps 1–11.
- Add a top-level `make check` convenience target (optional).
- Verify `ctest --output-on-failure` is green from a clean build.

---

## File-size budget summary

| File | Est. LOC |
|---|---|
| `core/qubit_pool.hpp` | 40 |
| `core/sink.hpp` | 150 |
| `core/counter_sink.hpp` | 120 |
| `core/recording_sink.hpp` | 150 |
| `core/mask_ops.hpp` | 120 |
| `core/dispatch.hpp` | 150 |
| `control/when_fwd.hpp` | 15 |
| `control/when.hpp` | 80 |
| `qtypes/qbool.hpp` | 120 |
| `qtypes/qint_fwd.hpp` | 30 |
| `qtypes/qint_core.hpp` | 200 |
| `qtypes/qint_arith.hpp` | 250 |
| `qtypes/qint_bitwise.hpp` | 200 |
| `qtypes/qint_compare.hpp` | 150 |
| `qtypes/qint.hpp` | 30 |

Every header ≤ 300. The two largest (`qint_arith`, `qint_core`) have explicit room to split further if budget creeps.

---

## Order of execution (TDD-strict)

0 → 1 → 2 → 3 → 4 → 5 → 6 (A→F, each sub-module with its slice of `test_qint_classical` then `test_qint_superposed`) → 7 → 8 → 9 → 10 → 11 → 12.

At every step: write failing test → implement → `ctest` → commit. No step starts with prior step red.

---

## Risks & mitigations (carried from PRD §15)

- **Operator-allocation drift:** every overload routes through `dispatch_binary` — only one place performs lazy qubit allocation.
- **Mask saturation:** isolated in `mask_ops.hpp`, fully unit-tested in step 4 before any operator uses it.
- **Nested `WHEN`:** test suite never nests; `test_when.cpp` documents the limitation in a comment.
- **Stub measurement:** every `operator T()` that returns `value` carries `// TODO(backend):`.
- **Header cycles** between `qbool`/`qint`/`when`: broken by `qint_fwd.hpp` and `when_fwd.hpp`.
