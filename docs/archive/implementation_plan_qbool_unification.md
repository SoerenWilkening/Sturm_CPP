# Implementation Plan: Unify qbool with qint_t<1>

**PRD:** `docs/prd_qbool_unification.md`
**Approach:** Test-driven, modular (<= 300 LOC per module), sequential dependency chain

---

## Module Overview

```
M1  Add owning_ to qint_t<W>                    [~80 LOC]  Phase 1    ✅
M2  Tests for owning_ on qint_t                  [~120 LOC] Phase 1    ✅
 |
M3  Break include cycle                          [~100 LOC] Phase 2    ✅
M4  Tests: clean build after cycle break         [~0 LOC]   Phase 2    ✅
 |
M5  qbool inherits qint_t<1> — class shell       [~200 LOC] Phase 3a   ✅
M6  Tests for qbool-as-subclass basics           [~150 LOC] Phase 3a   ✅
 |
M7  qbool operators migration                    [~180 LOC] Phase 3b   ✅
M8  Tests for migrated operators                 [~100 LOC] Phase 3b   ✅
 |
M9  BITWISE_SELF apply() in uncompute_op         [~120 LOC] Phase 3c   ✅
M10 Tests for BITWISE_SELF uncompute             [~150 LOC] Phase 3c   ✅
 |
M11 Field access migration: is_super             [~200 LOC] Phase 4a   ✅
M12 Field access migration: value (bool context) [~250 LOC] Phase 4b   ✅
 |
M13 Explicit instantiation + cleanup             [~60 LOC]  Phase 5+6  ✅
M14 Final acceptance tests                       [~150 LOC] Phase 6    ✅
 |
M15 Auto-promote classical bits in PhiProxy      [~40 LOC]  Phase 7
M16 Auto-promote classical bits in ThetaProxy    [~40 LOC]  Phase 7
M17 Update tests for auto-promotion              [~60 LOC]  Phase 7
```

**Total estimated:** ~1860 LOC across 14 modules (M1-M14 complete) + ~140 LOC for Phase 7

---

## M1: Add `owning_` to `qint_t<W>` [~80 LOC]

**Goal:** Give every `qint_t<W>` an ownership flag so its destructor conditionally releases qubits. Currently only `qbool` has `owning_`.

**Files changed:**
- `include/sturm/qtypes/qint_core.hpp`

**Changes:**

1. Add public field after `uncompute_`:
   ```cpp
   bool owning_ = true;  // destructor releases qubits only if true
   ```

2. Guard destructor qubit-release loop (currently lines ~148-166):
   ```cpp
   // BEFORE:
   for (auto idx : qubits) {
       if (idx >= 0) QubitPool::instance().release(idx);
   }
   // AFTER:
   if (owning_) {
       for (auto idx : qubits) {
           if (idx >= 0) QubitPool::instance().release(idx);
       }
   }
   ```

3. Update **move constructor** (~line 110): transfer `owning_` from source, set source `owning_ = false`:
   ```cpp
   qint_t(qint_t&& o) noexcept
       : value(o.value), super_mask(o.super_mask), qubits(o.qubits),
         owning_(o.owning_)
   {
       o.owning_ = false;
       o.qubits.fill(-1);
       // ... existing uncompute transfer ...
   }
   ```

4. Update **move assignment** (~line 125): same pattern.

5. Update **copy constructor** (~line 102): copy does NOT share ownership — the copy gets `owning_ = true` with fresh (unallocated) qubits. This matches existing behavior where copies reset `qubits` to -1.

6. Update **copy assignment**: same as copy constructor logic.

7. Add static factory method:
   ```cpp
   static qint_t make_non_owning(std::array<int, Width> q, int64_t val, uint64_t mask) {
       qint_t result;
       result.qubits     = q;
       result.value      = val;
       result.super_mask = mask;
       result.owning_    = false;
       return result;
   }
   ```

**Dependencies:** None
**Risk:** Low — additive change, default `owning_ = true` preserves all existing behavior

---

## M2: Tests for `owning_` on `qint_t` [~120 LOC]

**Goal:** Verify the owning_ flag works correctly before building on it.

**File:** `tests/test_qint_owning.cpp` (new)

**Test cases (TDD — write these FIRST, then implement M1):**

1. **`qint_t_default_is_owning`**: Default-constructed `qint_t<4>` has `owning_ == true`.

2. **`qint_t_make_non_owning_flag`**: `make_non_owning(...)` returns instance with `owning_ == false`.

3. **`qint_t_non_owning_does_not_release`**: Create a qint_t<4>, manually set qubit indices. Create a non-owning view of those qubits. Destroy the view. Verify the original's qubits are still valid (not returned to pool).

4. **`qint_t_owning_does_release`**: Create qint_t<4> with allocated qubits. Let it go out of scope. Verify qubits returned to pool (pool count increases).

5. **`qint_t_move_transfers_ownership`**: Move-construct from owning. Verify: destination `owning_ == true`, source `owning_ == false`, source qubits cleared.

6. **`qint_t_copy_does_not_share_ownership`**: Copy an owning qint. Verify: copy has `owning_ == true` but qubits are -1 (no shared ownership).

7. **`qint_t_move_assign_transfers_ownership`**: Same as move-construct but via `operator=`.

**Build:** Add to `tests/CMakeLists.txt` as frontend test (no STURM_BACKEND_ENABLED needed).

**Acceptance:** All 7 tests green. Existing `test_qint_classical` and `test_qint_superposed` still pass.

---

## M3: Break Include Cycle [~100 LOC]

**Goal:** Currently `qint_core.hpp` includes `qbool.hpp` for the `qint_t(const qbool&)` constructor and `explicit operator qbool()`. After unification, `qbool.hpp` must include `qint_core.hpp` (for inheritance), creating a cycle. Break it now.

**Files changed:**
- `include/sturm/qtypes/qint_core.hpp` — remove `#include "qbool.hpp"`, add forward declaration
- `include/sturm/qtypes/qint_qbool_conv.hpp` — **new file**, conversion bodies
- `include/sturm/qtypes/qint.hpp` — update umbrella include order

**Changes:**

### 3a. `qint_core.hpp`

Remove (near top):
```cpp
#include "sturm/qtypes/qbool.hpp"  // DELETE THIS LINE
```

Add forward declaration (after qint_fwd.hpp include):
```cpp
class qbool;  // forward declaration; full definition in qbool.hpp
```

Change the `qint_t(const qbool&)` constructor and `explicit operator qbool()` from inline definitions to **declarations only**:
```cpp
// Declaration only — body in qint_qbool_conv.hpp
explicit qint_t(const qbool& b);
explicit operator qbool() const;
```

### 3b. New file: `qint_qbool_conv.hpp` (~60 LOC)

```cpp
#pragma once
#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"

namespace sturm {

template <std::size_t W>
qint_t<W>::qint_t(const qbool& b) {
    value      = b.value ? 1 : 0;
    super_mask = b.is_super ? 1ULL : 0ULL;
    qubits[0]  = b.qubits[0];
    // remaining qubits stay -1 (default)
}

template <std::size_t W>
qint_t<W>::operator qbool() const {
    qbool result;
    result.value      = (value & 1) != 0;
    result.is_super   = (super_mask & 1) != 0;
    result.qubits[0]  = qubits[0];
    result.owning_    = false;  // view, not owner
    return result;
}

} // namespace sturm
```

### 3c. `qint.hpp` — updated umbrella order

```cpp
#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint_core.hpp"      // qint_t<W> (forward-declares qbool)
#include "sturm/qtypes/qbool.hpp"           // qbool (standalone, pre-unification)
#include "sturm/qtypes/qint_qbool_conv.hpp" // conversion bodies (needs both)
#include "sturm/qtypes/lazy_expr.hpp"       // AndExpr/OrExpr
#include "sturm/qtypes/qbool_ops.hpp"       // qbool operator bodies
#include "sturm/qtypes/qint_arith.hpp"
#include "sturm/qtypes/qint_bitwise.hpp"
#include "sturm/qtypes/qint_compare.hpp"
```

### 3d. Audit transitive includes

Files that previously got `qbool.hpp` through `qint_core.hpp` may now need explicit `#include "sturm/qtypes/qbool.hpp"`. Check:
- `when.hpp` — already includes qbool.hpp directly? If not, add it.
- `dispatch.hpp` — uses `qbool*` via `when_fwd.hpp`; verify forward declaration suffices.
- `swap_dsl.hpp` — uses qbool by value; needs full definition.
- Test files — typically include `qint.hpp` umbrella, so they're fine.

**Dependencies:** None (M1 is independent; M3 can be done in parallel)
**Risk:** Medium — transitive include breakage. Fix is mechanical: add missing includes. Full rebuild is the test.

---

## M4: Build Verification After Cycle Break [~0 LOC]

**Goal:** Full clean build of all targets (frontend tests, backend tests, examples).

**Commands:**
```bash
cd build && cmake --build . --clean-first 2>&1
```

**Acceptance:** Zero compilation errors. All existing tests pass unchanged.

---

## M5: qbool Inherits `qint_t<1>` — Class Shell [~200 LOC]

**Goal:** Rewrite `qbool.hpp` so `class qbool : public qint_t<1>`. Remove duplicated fields. Keep all method signatures. This is the structural change; operator bodies are migrated in M7.

**Files changed:**
- `include/sturm/qtypes/qbool.hpp` (major rewrite)

**Changes:**

### 5a. Class declaration
```cpp
class qbool : public qint_t<1> {
public:
    // --- NO NEW DATA MEMBERS ---
    // Inherited from qint_t<1>:
    //   int64_t  value      (was: bool value)
    //   uint64_t super_mask (was: bool is_super)
    //   std::array<int,1> qubits
    //   bool owning_        (from M1)
    //   uncompute_op uncompute_  (ifdef STURM_BACKEND_ENABLED)
```

### 5b. Remove deleted fields
Delete:
- `bool value`
- `bool is_super`
- `std::array<int,1> qubits`
- `bool owning_`
- `QboolUncompute` enum and `qbool_uncompute_` field
- `uncompute_a_qubit_`, `uncompute_b_qubit_`
- `uncompute_op uncompute_` (inherited)

### 5c. Constructors

**Default:**
```cpp
qbool() : qint_t<1>(0) {}
// value=0, super_mask=0, qubits={-1}, owning_=true
```

**Bool (implicit):**
```cpp
qbool(bool v) : qint_t<1>(v ? 1 : 0) {}
```

**Probabilistic:**
```cpp
explicit qbool(double p);
// Body: allocate qubit, set super_mask=1, value from prepare(), owning_=true
```

### 5d. Backward-compatible accessors

```cpp
// These ease the migration in M11/M12 but are also useful long-term.
bool get_is_super() const noexcept { return (super_mask & 1) != 0; }
void set_is_super(bool s) noexcept { super_mask = s ? 1ULL : 0ULL; }

bool get_bool_value() const noexcept { return (value & 1) != 0; }
void set_bool_value(bool v) noexcept { value = v ? 1 : 0; }

// operator bool — measure/read classical bit
explicit operator bool() const noexcept { return (value & 1) != 0; }
```

### 5e. Preserved methods (declarations; bodies in qbool_ops.hpp or inline)

```cpp
static qbool make_non_owning(int qubit_idx);
void ensure_qubit();
qbool& flip();
qbool operator~() const;
qbool& operator^=(const qbool& other);
qbool& operator^=(const AndExpr<qbool>& expr);
qbool& operator^=(const OrExpr<qbool>& expr);
```

### 5f. Copy/move semantics

**Copy constructor:** Copies value/super_mask, resets qubits to -1, `owning_ = true`. Delegates to `qint_t<1>` copy then resets qubits (matching current qbool copy behavior — copies don't share qubits).

**Move constructor:** Delegates to `qint_t<1>` move. Source becomes non-owning with cleared qubits.

**Copy/move assignment:** Same patterns.

### 5g. Destructor

```cpp
~qbool() = default;
// Base qint_t<1> destructor handles:
//   1. uncompute_op::apply() if tag != NONE
//   2. qubit release if owning_
```

The `qbool` destructor is now trivial because `QboolUncompute` is eliminated — all uncompute goes through the inherited `uncompute_op` (done in M7/M9).

### 5h. `as_qint_base()` — remove override

qbool previously had its own `as_qint_base()`. Delete it; the inherited `qint_t<1>::as_qint_base()` is correct.

### 5i. Update `qint_qbool_conv.hpp`

The conversion constructor `qint_t<W>::qint_t(const qbool&)` now reads base fields:
```cpp
template <std::size_t W>
qint_t<W>::qint_t(const qbool& b) {
    value      = b.value;          // int64_t, no conversion needed
    super_mask = b.super_mask;     // uint64_t, no conversion needed
    qubits[0]  = b.qubits[0];
}
```

The `operator qbool()` conversion also simplifies since qbool fields are now int64_t/uint64_t.

**Dependencies:** M1 (owning_), M3 (include cycle broken)
**Risk:** High — this is the core structural change. Many files may fail to compile until M11/M12 fix field access.

**Strategy:** Do M5 + M11 + M12 together in a single atomic commit to avoid broken intermediate states. But design and test them as separate modules.

---

## M6: Tests for qbool-as-Subclass Basics [~150 LOC]

**File:** `tests/test_qbool_unified.cpp` (new)

**Test cases (TDD):**

1. **`qbool_is_subclass_of_qint1`**:
   ```cpp
   static_assert(std::is_base_of_v<qint_t<1>, qbool>);
   ```

2. **`qbool_sizeof_not_bloated`**:
   ```cpp
   REQUIRE(sizeof(qbool) <= 56);
   ```

3. **`qbool_default_ctor`**: `value == 0`, `super_mask == 0`, `qubits[0] == -1`, `owning_ == true`.

4. **`qbool_bool_ctor_true`**: `qbool(true)` → `value == 1`, `super_mask == 0`.

5. **`qbool_bool_ctor_false`**: `qbool(false)` → `value == 0`, `super_mask == 0`.

6. **`qbool_has_phi`**: `qbool q(true); auto& p = q.phi();` — compiles. (Verifies inherited PhiProxy.)

7. **`qbool_has_theta`**: Same for theta.

8. **`qbool_explicit_bool_cast`**: `qbool q(true); REQUIRE(static_cast<bool>(q) == true);`

9. **`qbool_make_non_owning`**: Returns `owning_ == false`, correct qubit index.

10. **`qbool_copy_does_not_share_qubits`**: Copy has qubits[0] == -1.

11. **`qbool_move_transfers_ownership`**: Destination owns, source cleared.

12. **`qbool_implicit_from_bool`**: `qbool q = true;` compiles (non-explicit).

**Acceptance:** All 12 tests green.

---

## M7: qbool Operators Migration [~180 LOC]

**Goal:** Update `qbool_ops.hpp` to use inherited `qint_t<1>` fields and stamp `uncompute_op` instead of `QboolUncompute`.

**Files changed:**
- `include/sturm/qtypes/qbool_ops.hpp`

**Changes:**

### 7a. `operator~()` — use `uncompute_op` instead of `QboolUncompute::X`

```cpp
inline qbool qbool::operator~() const {
    int anc_idx = QubitPool::instance().allocate();
    // Emit X on ancilla (possibly lifted under controls)
    emit_X_lifted(get_ctx(), anc_idx);

    qbool result;
    result.qubits[0]   = anc_idx;
    result.value        = 1;           // ancilla flipped to |1>
    result.super_mask   = 1;           // in superposition
    result.owning_      = true;

    #ifdef STURM_BACKEND_ENABLED
    // Stamp uncompute: ADD_CONST with c=1 on a 1-bit register is X
    result.uncompute_ = uncompute_op{};
    result.uncompute_.tag = uncompute_op::kind::ADD_CONST;
    result.uncompute_.data.const_c = 1;
    #endif

    return result;
}
```

### 7b. `AndExpr<qbool>::operator qbool()` — use `BITWISE_SELF` with AND sub-kind

```cpp
template<>
inline AndExpr<qbool>::operator qbool() const {
    int anc_idx = QubitPool::instance().allocate();
    auto& ctx = get_ctx();
    emit_CCX_lifted(ctx, a.qubits[0], b.qubits[0], anc_idx);

    qbool result;
    result.qubits[0]   = anc_idx;
    result.value        = (a.value & 1) & (b.value & 1);
    result.super_mask   = ((a.super_mask | b.super_mask) & 1) ? 1ULL : 0ULL;
    result.owning_      = true;

    #ifdef STURM_BACKEND_ENABLED
    result.uncompute_ = uncompute_op::make_bitwise_self(
        &a, BITWISE_AND_SUB_KIND);
    // Store b's qubit index in the uncompute data for CCX replay
    result.uncompute_.data.bitwise.input_b_qubit = b.qubits[0];
    #endif

    return result;
}
```

### 7c. `OrExpr<qbool>::operator qbool()` — use `BITWISE_SELF` with OR sub-kind

Same pattern: emit CX+CX+CCX, stamp `BITWISE_SELF` with OR sub-kind.

### 7d. `flip()`, `operator^=` — unchanged

These don't set uncompute tags (they mutate in-place), so they only need field name updates:
- `b.value` → already int64_t (no change needed since qbool inherits qint_t<1>)
- `b.is_super` references → `(b.super_mask & 1)` (if any; check actual usage)

### 7e. `ensure_qubit()` — update field access

```cpp
inline void qbool::ensure_qubit() {
    if (qubits[0] < 0) {
        qubits[0] = QubitPool::instance().allocate();
        // If value is classically 1, emit X to match
        if ((value & 1) != 0) {
            emit_X_lifted(get_ctx(), qubits[0]);
        }
    }
}
```

### 7f. Update uncompute_op data struct (if needed)

The `BITWISE_SELF` data currently stores `{input_ptr, sub_kind}`. For AND/OR uncompute on 1-bit registers, we also need the two source qubit indices. Options:
- Store two qubit indices directly in the data union (fits within 32 bytes)
- Store a pointer to a small struct

Preferred: Store qubit indices directly:
```cpp
struct bitwise_data {
    uint32_t a_qubit;    // first operand qubit
    uint32_t b_qubit;    // second operand qubit (for AND/OR)
    uint32_t sub_kind;   // AND=0, OR=1, XOR=2
};
```

This must fit in the existing `data_t` union (32 bytes total for uncompute_op). At 12 bytes, it fits easily.

**Dependencies:** M5 (qbool is subclass), M9 (BITWISE_SELF apply must be implemented for destructor to work)

**Constraint:** M7 and M9 must land together — operators stamp BITWISE_SELF but apply() must handle it. Test in M8+M10.

---

## M8: Tests for Migrated Operators [~100 LOC]

**File:** `tests/backend/test_qbool_ops.cpp` (update existing)

**Test updates:**
The existing test file already tests `^=`, AND/OR expr, flip, `~`, WHEN context. After M7, these tests should pass unchanged since the observable behavior is identical (same gates emitted).

**New test cases:**

1. **`operator_not_stamps_ADD_CONST_uncompute`**: Create `qbool r = ~q;`. Inspect `r.uncompute_.tag == ADD_CONST` and `r.uncompute_.data.const_c == 1`.

2. **`and_expr_stamps_BITWISE_SELF_uncompute`**: Materialize `qbool r = (a & b);`. Inspect `r.uncompute_.tag == BITWISE_SELF` and sub_kind is AND.

3. **`or_expr_stamps_BITWISE_SELF_uncompute`**: Same for `(a | b)`.

4. **`no_QboolUncompute_enum_exists`**: Static assert that `QboolUncompute` is not a valid type (compilation test).

**Acceptance:** All existing qbool_ops tests pass. New tag-inspection tests pass.

---

## M9: Implement `BITWISE_SELF apply()` in `uncompute_op` [~120 LOC]

**Goal:** The `BITWISE_SELF` case in `uncompute_op::apply()` is currently a TODO stub. Implement it to emit inverse gates for AND/OR/XOR uncompute.

**Files changed:**
- `include/sturm/uncompute/uncompute_op.hpp` — update data struct and apply()

**Changes:**

### 9a. Update BITWISE_SELF data layout

```cpp
struct bitwise_data {
    uint32_t a_qubit;     // first source qubit
    uint32_t b_qubit;     // second source qubit (0xFFFFFFFF if unused)
    uint8_t  sub_kind;    // 0=AND, 1=OR, 2=XOR
};
```

Add factory:
```cpp
static uncompute_op make_bitwise_self(uint32_t a_q, uint32_t b_q, uint8_t sub) {
    uncompute_op op{};
    op.tag = kind::BITWISE_SELF;
    op.data.bitwise.a_qubit  = a_q;
    op.data.bitwise.b_qubit  = b_q;
    op.data.bitwise.sub_kind = sub;
    return op;
}
```

### 9b. Implement apply() for BITWISE_SELF

```cpp
case kind::BITWISE_SELF: {
    auto& bw = data.bitwise;
    uint32_t self_qubit = self.qubits[0];  // the ancilla being uncomputed

    switch (bw.sub_kind) {
    case 0: // AND — Toffoli is self-inverse
        // Emit CCX(a, b, self)
        execute_gate(ctx, STURM_GATE_CCX,
            {bw.a_qubit, bw.b_qubit, self_qubit}, 3u, 0.0);
        break;

    case 1: // OR — CX+CX+CCX is self-inverse sequence
        // Emit in reverse order: CCX(a,b,self), CX(b,self), CX(a,self)
        execute_gate(ctx, STURM_GATE_CCX,
            {bw.a_qubit, bw.b_qubit, self_qubit}, 3u, 0.0);
        execute_gate(ctx, STURM_GATE_CX,
            {bw.b_qubit, self_qubit}, 2u, 0.0);
        execute_gate(ctx, STURM_GATE_CX,
            {bw.a_qubit, self_qubit}, 2u, 0.0);
        break;

    case 2: // XOR — CNOT is self-inverse
        execute_gate(ctx, STURM_GATE_CX,
            {bw.a_qubit, self_qubit}, 2u, 0.0);
        break;
    }
    break;
}
```

### 9c. Handle control lifting in apply()

If the uncompute happens inside a WHEN scope, gates must be lifted. Check whether `apply()` already handles control context (it should — the `execute_gate` call respects the thread-local control stack).

**Dependencies:** M7 (operators stamp the tags that apply() must handle)
**Risk:** Medium — must exactly mirror the forward emission for correct uncomputation

---

## M10: Tests for BITWISE_SELF Uncompute [~150 LOC]

**File:** `tests/backend/test_qbool_uncompute.cpp` (update existing)

**Existing tests** already verify X/AND/OR uncompute via `QboolUncompute`. After M7+M9, the same observable behavior must hold but via `uncompute_op` path.

**New/updated test cases:**

1. **`and_uncompute_emits_ccx`**: Materialize `qbool r = (a & b);` then let `r` go out of scope. Capture gate log. Verify final gate is CCX(a_qubit, b_qubit, ancilla).

2. **`or_uncompute_emits_three_gates`**: Materialize `qbool r = (a | b);` then destroy. Verify gate log ends with CCX, CX, CX (reverse of forward emission).

3. **`not_uncompute_emits_x`**: `qbool r = ~q;` then destroy. Verify final gate is X (or sub_const(1) equivalent).

4. **`nested_when_and_uncompute`**: AND materialization inside WHEN scope. Verify control-lifted CCX in uncompute.

5. **`uncompute_after_move`**: Move a materialized AND result. Verify uncompute fires on the move destination, not the source.

**Acceptance:** All uncompute tests pass via `uncompute_op` path. No reference to `QboolUncompute` in test logic.

---

## M11: Field Access Migration — `is_super` [~200 LOC]

**Goal:** Replace all `qbool.is_super` reads/writes with `super_mask`-based equivalents. After M5, `qbool` no longer has a `bool is_super` field.

**Files to change (from PRD section 4.4):**

| File | Occurrences | Pattern |
|------|-------------|---------|
| `include/sturm/control/when.hpp` | 3 | `expr.is_super` → `(expr.super_mask & 1)` |
| `include/sturm/core/dispatch.hpp` | 2 | Read via control pointer |
| `include/sturm/qtypes/qint_core.hpp` | 3 | In qbool conversion (moved to qint_qbool_conv.hpp in M3) |
| `include/sturm/qtypes/qint_compare.hpp` | 2 | Result qbool setup |
| `include/sturm/qtypes/qint_compare_v3.hpp` | 2 | Result qbool setup |
| `include/sturm/lib/swap_dsl.hpp` | 1 | `std::swap(a.is_super, b.is_super)` |
| Test files | ~4 | Assertions on is_super |

**Replacement rules:**

| Old | New |
|-----|-----|
| `q.is_super = true` | `q.super_mask = 1` |
| `q.is_super = false` | `q.super_mask = 0` |
| `q.is_super` (read, bool context) | `(q.super_mask & 1)` |
| `std::swap(a.is_super, b.is_super)` | `std::swap(a.super_mask, b.super_mask)` |

**Strategy:** Mechanical search-and-replace. Each replacement is compile-checked — a typo causes a build failure.

**Dependencies:** M5 (field no longer exists)
**Risk:** Low — mechanical, self-checking via compilation

---

## M12: Field Access Migration — `value` (bool context) [~250 LOC]

**Goal:** Replace `qbool.value` usage that assumes `bool` type with `int64_t`-compatible patterns.

**Scope:** Only qbool-typed variables. `qint_t<W>.value` is already `int64_t` and unaffected.

**Replacement rules:**

| Old (qbool context) | New |
|---------------------|-----|
| `q.value = true` | `q.value = 1` |
| `q.value = false` | `q.value = 0` |
| `q.value` (bool read) | `(q.value & 1)` or `(q.value != 0)` |
| `std::swap(a.value, b.value)` | `std::swap(a.value, b.value)` (no change — int64_t swap works) |
| `REQUIRE(q.value == true)` | `REQUIRE(q.value == 1)` (or keep — int/bool compare works) |

**Key files:**
- `when.hpp` — `expr.value` in classical branch check
- `swap_dsl.hpp` — `std::swap(a.value, b.value)` (already fine for int64_t)
- `qbool_ops.hpp` — setting value after operations
- `qint_compare.hpp` / `qint_compare_v3.hpp` — result value setup
- Multiple test files — assertions

**Many of these will "just work"** because `int64_t` compares with `bool` via implicit conversion. Focus on:
1. Places where `bool` type is explicitly expected (assigned to bool variable)
2. Places where `value = true/false` is written (need `value = 1/0`)

**Dependencies:** M5 (field type changed from bool to int64_t)
**Risk:** Low — compilation catches mismatches

---

## M13: Explicit Instantiation + Cleanup [~60 LOC]

**Goal:** Add `qint_t<1>` to explicit instantiations. Remove dead code.

**Files changed:**
- `src/sturm/qtypes/instantiations.cpp`
- `include/sturm/qtypes/qint.hpp`
- `include/sturm/qtypes/qbool.hpp` (remove any leftover dead code)
- `docs/01_principles.md` (update B2 to reflect shared state layout)

**Changes:**

### 13a. Explicit instantiation
```cpp
// instantiations.cpp — add:
template class sturm::qint_t<1>;

// qint.hpp — add:
extern template class sturm::qint_t<1>;
```

### 13b. Dead code removal
- Delete `QboolUncompute` enum if any remnants exist
- Delete `uncompute_a_qubit_`, `uncompute_b_qubit_` if any remnants
- Delete any `#ifdef` branches for old qbool layout
- Remove `qbool::as_qint_base()` override if still present

### 13c. Update `docs/01_principles.md`
Change B2 from:
> Per `qbool`: a `bool` value and a `bool` classicality flag.

To:
> Per `qbool`: inherits `qint_t<1>` — an `int64_t` value and a `uint64_t` classicality mask (bit 0 only).

**Dependencies:** M5-M12 all complete
**Risk:** Low

---

## M14: Final Acceptance Tests [~150 LOC]

**File:** `tests/test_qbool_acceptance.cpp` (new)

**Tests mapping to PRD acceptance criteria:**

1. **AC1 — phi on qbool**:
   ```cpp
   qbool q(0.5);
   q.phi() += 1.0;  // Must compile and emit RZ gate
   ```

2. **AC2 — theta on qbool**:
   ```cpp
   qbool q(0.5);
   q.theta() += 1.0;  // Must compile and emit RY gate
   ```

3. **AC3 — is_base_of**:
   ```cpp
   static_assert(std::is_base_of_v<qint_t<1>, qbool>);
   ```

4. **AC4 — QboolUncompute gone**: Compilation test — any reference to `QboolUncompute` fails.

5. **AC5 — existing tests pass**: Run full suite (verified by CI, not a test case itself).

6. **AC6 — WHEN works**: Classical true, classical false, superposed — all three branches. (Backend test.)

7. **AC7 — make_non_owning works**: Create non-owning qbool, verify no qubit release on destruction.

8. **AC8 — operator~ uncomputes**: Gate log verification (backend test).

9. **AC9 — no include cycles**: Clean build (verified by CI).

10. **AC10 — sizeof**:
    ```cpp
    static_assert(sizeof(qbool) <= 56);
    ```

**Split:** Frontend tests (AC1-4, AC7, AC10) in `tests/test_qbool_acceptance.cpp`. Backend tests (AC6, AC8) in `tests/backend/test_qbool_acceptance_backend.cpp`.

---

## Execution Order & Atomic Commits

The modules form a strict dependency chain, but some can be committed independently:

### Commit 1: M1 + M2 (owning_ on qint_t)
Safe, additive, independently testable.

### Commit 2: M3 + M4 (break include cycle)
Safe, independently testable. No behavior change.

### Commit 3: M5 + M7 + M9 + M11 + M12 + M6 + M8 + M10 (the big rewrite)
These MUST land together — M5 removes qbool fields, M11/M12 fix all references, M7/M9 migrate uncompute. An intermediate state doesn't compile.

**Strategy for Commit 3:**
1. Write all test files first (M6, M8, M10) — they won't compile yet.
2. Apply M5 (class shell rewrite).
3. Apply M11 + M12 (field migration) — now it compiles.
4. Apply M7 + M9 (operator + uncompute migration).
5. Run all tests.

### Commit 4: M13 + M14 (cleanup + acceptance)
Safe, final polish.

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Commit 3 is too large | Do the work in M5-M12 order but commit atomically. Use `git stash` to checkpoint. |
| BITWISE_SELF apply() emits wrong gates | Compare gate logs before/after with existing uncompute tests. |
| Transitive include breakage (M3) | Full clean build after M3. Fix missing includes immediately. |
| Object slicing loses qbool methods | Acceptable per PRD — qbool adds no data, only methods. Test explicitly. |
| WHEN static_assert breaks | Won't break — qbool remains a distinct type, not a typedef. |

---

## M15: Auto-promote classical bits in PhiProxy [~40 LOC]

**Goal:** When `phi() += delta` is called on a qint/qbool with unallocated (classical) bits, auto-allocate qubits, set `super_mask`, emit X gate for bits with classical value 1, then emit RZ.

**File:** `include/sturm/qtypes/qint_core.hpp` (PhiProxy::operator+=)

**Changes:**

Add promotion loop before the gate-emission loop in both code paths:

**Backend path** (inside `if (ctx)` block):
```cpp
for (std::size_t i = 0; i < Width; ++i) {
    if (parent.qubits[i] < 0) {
        parent.qubits[i] = QubitPool::instance().allocate();
        parent.super_mask |= (1ULL << i);
        if ((parent.value >> i) & 1) {
            uint32_t tgt = static_cast<uint32_t>(parent.qubits[i]);
            execute_gate(*ctx, STURM_GATE_X, &tgt, 1u, 0.0);
        }
    }
}
```

**Sink path** (fallback):
```cpp
for (std::size_t i = 0; i < Width; ++i) {
    if (parent.qubits[i] < 0) {
        parent.qubits[i] = QubitPool::instance().allocate();
        parent.super_mask |= (1ULL << i);
        if ((parent.value >> i) & 1) {
            current_sink()->prepare(parent.qubits[i], 1.0);
        }
    }
}
```

**Dependencies:** M1-M14 (all complete)
**Risk:** Low — additive promotion before existing gate emission

---

## M16: Auto-promote classical bits in ThetaProxy [~40 LOC]

**Goal:** Same as M15, but for `theta() += delta` (RY gate path).

**File:** `include/sturm/qtypes/qint_core.hpp` (ThetaProxy::operator+=)

**Changes:** Identical promotion loop as M15, placed before the RY gate-emission loop in both backend and sink paths.

**Dependencies:** M15 (same file, same pattern)
**Risk:** Low

---

## M17: Update tests for auto-promotion [~60 LOC]

**Goal:** Update existing tests that assert the old "no emission" behavior, and verify auto-promotion works.

**Files:**
- `tests/test_phase_amp.cpp` — test 7 (`test_rotation_no_qubits_no_emission`)
- `tests/backend/test_phi_theta_backend.cpp` — test 9 (`test_phi_add_no_qubits_no_emission`)

**Changes:**

### 17a. `test_phase_amp.cpp` test 7
Rename to `test_rotation_classical_auto_promotes`. Change from `qint q(42)` to `qbool q(true)`. Assert:
- `q.qubits[0] >= 0` (qubit allocated)
- `q.super_mask == 1` (promoted)
- Sink records contain prepare (for value=1 init) and theta_add/phi_add

### 17b. `test_phi_theta_backend.cpp` test 9
Rename to `test_phi_add_classical_auto_promotes`. Change from `qint q(42)` to `qbool q(true)`. Assert:
- Gate count increased (X for init + RZ for phi)
- `q.qubits[0] >= 0`

**Verification:**
```bash
cmake --build build --target test_phase_amp && ./build/tests/test_phase_amp
cmake --build build --target test_phi_theta_backend && ./build/tests/test_phi_theta_backend
cmake --build build --target example_or_circuit && ./build/examples/example_or_circuit
```

**Dependencies:** M15, M16
**Risk:** Low

---

## Test Commands

```bash
# Build all (from project root)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run frontend tests
cd build && ctest --test-dir tests -V

# Run backend tests
cd build && ctest --test-dir tests/backend -V

# Run specific test
./build/tests/test_qbool_unified
./build/tests/backend/test_qbool_uncompute
```
