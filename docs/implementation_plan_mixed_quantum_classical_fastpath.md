# Implementation Plan: Mixed Quantum/Classical Fast-Path Bypass

**PRD:** `docs/prd_mixed_quantum_classical_fastpath.md`
**Approach:** Modular, TDD. Each module is one commit.

## Module Dependency Graph

```
M1  Fix bitwise compound-assign fast-paths        [3 lines changed]
M2  Fix arithmetic compound-assign fast-paths      [5 lines changed]
M3  Fix arithmetic free operator fast-paths        [5 lines changed]
M4  Fix free bitwise operators (BitProxy)          [~40 LOC changed]
M5  Tests: mixed quantum/classical operations      [~250 LOC new]

Execution:
  M1 ──┐
  M2 ──┼── M5 (tests verify all fixes)
  M3 ──┤
  M4 ──┘
```

M1-M4 are independent (no ordering dependency). M5 tests all of them.

---

## M1: Fix bitwise compound-assign fast-paths

**File:** `include/sturm/qtypes/qint_bitwise_v3.hpp`

Change `||` to `&&` at 3 locations:

**Line 50 (`operator^=`):**
```cpp
// BEFORE:
if ((qubits[0] < 0 || b.qubits[0] < 0) && detail::current_control == nullptr) {
// AFTER:
if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
```

**Line 68 (`operator&=`):** Same change.

**Line 103 (`operator|=`):** Same change.

### Why safe

Once the fast path is bypassed, the existing BitProxy per-bit loop handles all four combinations (q/q, q/c, c/q, c/c) correctly:

- `operator^=` (line 54-62): `BitProxy this_bit ^= b_bit` -- CNOT with classical folding
- `operator&=` (line 72-97): `res_bits[i] ^= (a_bit & b_bit)` -- Toffoli with classical folding
- `operator|=` (line 107-132): `res_bits[i] ^= (a_bit | b_bit)` -- OR decomposition with classical folding

BitProxy's `ensure_quantum()` promotes classical bits on demand. Classical folding skips no-op gates. The `detail_bw::make_b_mut()` + `release_temp_qubits()` pattern correctly manages temporary qubit allocations for classical operand bits.

### Regression safety

Both-classical case: when both `qubits[0] < 0`, `&&` is still TRUE, fast path still taken. No change in behavior.

Both-quantum case: when both `qubits[0] >= 0`, the fast path was already not taken (both `< 0` checks FALSE). No change.

---

## M2: Fix arithmetic compound-assign fast-paths

**File:** `include/sturm/qtypes/qint_arith_v3.hpp`

Change `||` to `&&` at 5 locations:

| Line | Operator |
|------|----------|
| 59 | `operator+=` |
| 77 | `operator-=` |
| 95 | `operator*=` |
| 122 | `operator/=` |
| 150 | `operator%=` |

Same one-character change at each. Same safety rationale: BitProxy arrays + DSL library functions (`lib_add_dsl`, `lib_sub_dsl`, `lib_mul_dsl`, `lib_div_dsl`, `lib_mod_dsl`) are already templated on BitProxy and handle mixed quantum/classical operands.

---

## M3: Fix arithmetic free operator fast-paths

**File:** `include/sturm/qtypes/qint_arith_backend.hpp`

Change `||` to `&&` at 5 locations:

| Line | Operator |
|------|----------|
| 128 | `operator+` |
| 156 | `operator-` |
| 186 | `operator*` |
| 217 | `operator/` |
| 252 | `operator%` |

### How these work after the fix

Free operators delegate to `copy_register(a)` + compound-assign:
```cpp
qint_t<W> result = copy_register(a);  // fresh register, copies a's quantum state
result += b;                           // compound-assign (fixed in M2)
```

`copy_register` (line 48-76) only allocates qubits where source has them (`if (a.qubits[i] >= 0)`), which is already correct. After `&&` fix, the compound-assign from M2 enters the BitProxy path when operands are mixed.

---

## M4: Fix free bitwise operators for -1 qubit indices

**Files:**
- `include/sturm/qtypes/qint_bitwise_backend.hpp` -- gate loop rewrite
- `include/sturm/qtypes/qint_bitwise.hpp` -- include reorder

### Problem

`operator&` and `operator|` create `qbool::make_non_owning(b.qubits[i])` where `b.qubits[i]` is -1 for classical operands. This wraps an invalid qubit index.

### Fix: include reorder

In `qint_bitwise.hpp`, the backend include (line 244) comes BEFORE the v3 include (line 256). The `detail_bw::make_b_mut` and `detail_bw::release_temp_qubits` helpers are defined in v3. Swap order:

```cpp
// BEFORE (line 243-257):
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_bitwise_backend.hpp"
#endif
// ...
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_bitwise_v3.hpp"
#endif

// AFTER:
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_bitwise_v3.hpp"
#endif
// ...
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_bitwise_backend.hpp"
#endif
```

### Fix: BitProxy gate loop

Add `#include "sturm/qtypes/bit_proxy.hpp"` to `qint_bitwise_backend.hpp`.

**`operator&` (lines 45-52) -- replace:**

```cpp
// BEFORE:
if ((a.super_mask | b.super_mask) != 0 && sturm_get_thread_context()) {
    for (std::size_t i = 0; i < W; ++i) {
        qbool r_bit = qbool::make_non_owning(result.qubits[i]);
        qbool a_bit = qbool::make_non_owning(a.qubits[i]);
        qbool b_bit = qbool::make_non_owning(b.qubits[i]);
        r_bit ^= (a_bit & b_bit);
    }
}

// AFTER:
if ((a.super_mask | b.super_mask) != 0 && sturm_get_thread_context()) {
    auto a_mut = detail_bw::make_b_mut(a);
    auto b_mut = detail_bw::make_b_mut(b);
    for (std::size_t i = 0; i < W; ++i) {
        qbool r_q = qbool::make_non_owning(result.qubits[i]);
        BitProxy r_bit(r_q);
        BitProxy a_bit(a_mut, i);
        BitProxy b_bit(b_mut, i);
        r_bit ^= (a_bit & b_bit);
    }
    detail_bw::release_temp_qubits(a_mut, a);
    detail_bw::release_temp_qubits(b_mut, b);
}
```

**`operator|` (lines 79-86):** Same pattern, using `r_bit ^= (a_bit | b_bit)`.

### Why BitProxy

BitProxy checks `is_quantum()` (i.e. `*qubit_ptr >= 0`) before emitting gates. When a bit is classical:
- `AND(quantum, classical_1, result)` -> emits CX (not Toffoli)
- `AND(quantum, classical_0, result)` -> skips (result stays |0>)
- `OR(quantum, classical_1, result)` -> emits X (result always 1)
- `OR(quantum, classical_0, result)` -> emits CX (copies quantum bit)

This is exactly the classical folding described in `bit_proxy.hpp` lines 119-170.

---

## M5: Tests for mixed quantum/classical operations

**New file:** `tests/backend/test_mixed_quantum_classical.cpp`

**CMakeLists update:** `tests/backend/CMakeLists.txt` -- add test target:
```cmake
add_executable(test_mixed_quantum_classical
    test_mixed_quantum_classical.cpp
    ${M19_SOURCES}
)
target_include_directories(test_mixed_quantum_classical PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mixed_quantum_classical PRIVATE cxx_std_20)
target_compile_definitions(test_mixed_quantum_classical PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mixed_quantum_classical PRIVATE orkan_headers)
add_test(NAME test_mixed_quantum_classical COMMAND test_mixed_quantum_classical)
set_tests_properties(test_mixed_quantum_classical PROPERTIES LABELS "backend")
```

### Test cases

**Test harness:** APPEND mode BackendContext. Record `ctx->ir.size()` before and after each operation. Use `W=4` for reasonable gate counts.

| # | Test | Setup | Assert |
|---|------|-------|--------|
| 1 | XOR: quantum ^= classical | `a.theta()+=2`, b classical, `a ^= b` | gates emitted, value correct |
| 2 | XOR: classical ^= quantum | a classical, `b.theta()+=2`, `a ^= b` | a promoted, gates emitted |
| 3 | AND: quantum &= classical | `a.theta()+=2`, b classical, `a &= b` | gates emitted for bits where b=1 |
| 4 | OR: quantum \|= classical | `a.theta()+=2`, b classical, `a \|= b` | gates emitted |
| 5 | Free &: quantum & classical | `a.theta()+=2`, b classical, `c = a & b` | gates emitted, c has qubits |
| 6 | Free \|: quantum \| classical | `a.theta()+=2`, b classical, `c = a \| b` | gates emitted, c has qubits |
| 7 | ADD: quantum += classical | `a.theta()+=2`, b classical, `a += b` | adder circuit emitted |
| 8 | Regression: both classical | a, b both classical, `a ^= b` | zero gates, super_mask == 0 |

---

## Verification

### Build and test
```bash
cmake --build build_debug -j4
ctest --test-dir build_debug -L backend --output-on-failure
```

### Example verification
```bash
cmake --build build_debug --target example_or_circuit
./build_debug/examples/example_or_circuit
# Expected: 5 gates (4 from theta + 1 X from ^= with classical b=1)
```

### Acceptance checklist
- [ ] AC1: `a ^= b` quantum/classical emits X gates
- [ ] AC2: `a ^= b` classical/quantum promotes and emits CNOT
- [ ] AC3: `a &= b`, `a |= b` mixed emit correct gates
- [ ] AC4: Free `&`, `|` mixed emit correct gates
- [ ] AC5: `a += b` mixed emits adder circuit
- [ ] AC6: Both-classical fast path preserved
- [ ] AC7: All existing backend tests pass
- [ ] AC8: or_circuit example shows correct output

---

## Risk Areas

1. **`qint_t` copy constructor resets qubits to -1.** The `detail_bw::make_b_mut()` pattern (manual field copy with `owning_ = false`) is already proven in `qint_bitwise_v3.hpp`. Same for `detail_arith::make_b_mut()`.

2. **Include order for M4.** Swapping v3/backend includes in `qint_bitwise.hpp` -- verify no circular dependency. Both are guarded by `STURM_BACKEND_ENABLED` and include `qint_core.hpp` independently.

3. **Arithmetic free operators + copy_register.** When one operand is classical, `copy_register` only copies quantum bits (skips classical). The compound-assign then uses BitProxy which handles the classical bits via `ensure_quantum()`. This chain is already tested for the both-quantum case; the mixed case is new.

## Files Summary

| File | Change | Module |
|------|--------|--------|
| `include/sturm/qtypes/qint_bitwise_v3.hpp` | `\|\|` -> `&&` (3 lines) | M1 |
| `include/sturm/qtypes/qint_arith_v3.hpp` | `\|\|` -> `&&` (5 lines) | M2 |
| `include/sturm/qtypes/qint_arith_backend.hpp` | `\|\|` -> `&&` (5 lines) | M3 |
| `include/sturm/qtypes/qint_bitwise_backend.hpp` | BitProxy gate loop (~40 LOC) | M4 |
| `include/sturm/qtypes/qint_bitwise.hpp` | Include reorder | M4 |
| `tests/backend/test_mixed_quantum_classical.cpp` | New test file (~250 LOC) | M5 |
| `tests/backend/CMakeLists.txt` | Add test target | M5 |
