# PRD: Unify qbool with qint_t<1>

**Status:** Phases 1-6 Complete; Phase 7 (auto-promotion) In Progress
**Date:** 2026-04-10 (updated 2026-04-11)

## 1. Problem Statement

`qbool` and `qint_t<W>` are currently separate, unrelated classes. A `qbool` is semantically a 1-bit quantum integer but lacks `phi()`, `theta()`, arithmetic operators, and all other `qint_t` functionality. This violates principle P5 (the user works with `qint` and `qbool` using the same four primitives) and forces unnecessary code duplication.

**Immediate symptom:** You cannot write `qbool(0.5).phi() += delta` or `qbool(0.5).theta() += delta`.

**Root cause:** `qbool` was written as a standalone class with its own field layout (`bool value`, `bool is_super`) instead of building on `qint_t<1>` (`int64_t value`, `uint64_t super_mask`). There is no inheritance or aliasing relationship.

## 2. Goal

Make `qbool` a subclass of `qint_t<1>` so that it inherits all `qint_t` functionality (phi/theta proxies, arithmetic, bitwise, comparison operators) while preserving qbool-specific behavior (probabilistic construction, ensure_qubit, flip, operator~, lazy AND/OR expression consumption, WHEN integration).

## 3. Non-Goals

- Changing user-facing API beyond what's needed for unification. Existing code using `qbool` should continue to compile with minimal changes.
- Adding new uncompute_op kinds. The existing `qint_t` uncompute machinery (BITWISE_SELF, COMPARE, etc.) already covers the operations that `QboolUncompute` handles separately. The `QboolUncompute` enum is eliminated, not replaced.
- Merging `qbool` into a typedef for `qint_t<1>`. A subclass is needed because `qbool` has unique methods (ensure_qubit, flip, operator~, make_non_owning, the ^= overloads for AndExpr/OrExpr) and WHEN's static_assert requires a distinct type.

## 4. Current Architecture

### 4.1 qbool (include/sturm/qtypes/qbool.hpp)

```
class qbool {
    bool              value;           // classical boolean
    bool              is_super;        // superposition flag
    std::array<int,1> qubits{-1};     // single qubit index
    bool              owning_;         // ownership flag for RAII
    QboolUncompute    qbool_uncompute_;  // X/AND/OR uncompute tag
    uint32_t          uncompute_a_qubit_, uncompute_b_qubit_;  // saved for AND/OR uncompute
    uncompute_op      uncompute_;      // comparison uncompute (backend only)
};
```

**Constructors:** default, `qbool(bool)` (implicit), `qbool(double p)` (probabilistic).
**Unique methods:** `make_non_owning()`, `ensure_qubit()`, `flip()`, `operator~()`, `operator^=(AndExpr)`, `operator^=(OrExpr)`, `operator^=(qbool)`.
**Destructor:** Two-phase: (1) emit uncompute gates via `qbool_uncompute_` tag and/or `uncompute_op`, (2) release qubit if `owning_`.

### 4.2 qint_t\<W\> (include/sturm/qtypes/qint_core.hpp)

```
template <std::size_t Width>
class qint_t {
    int64_t  value;
    uint64_t super_mask;
    std::array<int, Width> qubits{};
    uncompute_op uncompute_;           // backend only
};
```

**Has but qbool lacks:** `phi()`, `theta()`, arithmetic (+,-,*,/,%), bitwise (&,|,^,<<,>>), comparison (==,!=,<,<=,>,>=), operator[].
**Lacks but qbool has:** `owning_` flag, `QboolUncompute`, `ensure_qubit()`, `flip()`, `operator~()`, `make_non_owning()`, probabilistic constructor.

### 4.3 Field Equivalences

| qbool field | qint_t\<1\> equivalent | Notes |
|---|---|---|
| `bool value` | `int64_t value` | `0` or `1`; `value & 1` |
| `bool is_super` | `uint64_t super_mask` | `super_mask & 1`; single-bit mask |
| `std::array<int,1> qubits` | `std::array<int,1> qubits` | Identical |
| `bool owning_` | *(missing)* | Must be added to qint_t |
| `QboolUncompute` enum | `uncompute_op` | Subsumed; see section 6 |
| `uncompute_a_qubit_`, `uncompute_b_qubit_` | `uncompute_op` data union | Subsumed |

### 4.4 Usage Statistics

| Usage | Files | Occurrences |
|---|---|---|
| `.is_super` | 10 | ~30 |
| `.value` (as bool on qbool) | 25 | ~241 |
| `.owning_` | 6 | ~16 |
| `QboolUncompute` / `qbool_uncompute_` | 2 | ~13 |
| `make_non_owning()` | 14 | ~104 |
| `ensure_qubit()` | 4 | ~14 |
| `qbool(double p)` constructor | 1 (tests) | 7 |
| WHEN interaction | 2 (when.hpp, when_fwd.hpp) | Core control flow |

### 4.5 Include Dependency

Current (no cycle):
```
qint_core.hpp --includes--> qbool.hpp
```

After unification (cycle if naive):
```
qbool.hpp --inherits--> qint_core.hpp --includes--> qbool.hpp  (CYCLE)
```

## 5. Target Architecture

### 5.1 qbool inherits qint_t\<1\>

```
qint_t<1>  (base: value, super_mask, qubits[1], uncompute_, owning_, phi(), theta(), ...)
    ^
    |
  qbool   (subclass: constructors, ensure_qubit, flip, ~, ^= overloads, make_non_owning)
```

`qbool` adds no new data members. All state lives in `qint_t<1>`.

### 5.2 qint_t\<W\> gains owning_ field

```cpp
template <std::size_t Width>
class qint_t {
    // ... existing fields ...
    bool owning_ = true;  // NEW: destructor releases qubits only if true

    ~qint_t() {
        if (uncompute_.tag != NONE) { /* emit inverse */ }
        if (owning_) {                // NEW guard
            for (int idx : qubits) {
                if (idx >= 0) QubitPool::instance().release(idx);
            }
        }
    }
};
```

This is independently useful: `operator[]` returns non-owning qbool bit slices, and DSL helpers create non-owning views via `make_non_owning()`. Currently only `qbool` supports this pattern; with `owning_` on `qint_t`, any width can have non-owning views.

### 5.3 QboolUncompute eliminated

The `QboolUncompute` enum (X, AND, OR) and its associated fields (`qbool_uncompute_`, `uncompute_a_qubit_`, `uncompute_b_qubit_`) are deleted. The operations they represented are:

- **X uncompute** (from `operator~`): The `operator~` implementation sets up an `uncompute_op` with tag `ADD_CONST` (value 1, which on a 1-bit register is equivalent to X). Alternatively, a new minimal `FLIP` tag can be added, but `ADD_CONST` with c=1 already works since `sub_const(1)` on a 1-bit register emits X.
- **AND uncompute** (from `AndExpr` materialization): The `AndExpr::operator qbool()` materialization emits a CCX(a,b,ancilla). Its uncompute is the same CCX (Toffoli is self-inverse). This maps to `BITWISE_SELF` with sub_kind `BITWISE_AND`.
- **OR uncompute** (from `OrExpr` materialization): Emits CX+CX+CCX; uncompute is the same three gates in reverse. This maps to `BITWISE_SELF` with sub_kind `BITWISE_OR`.

The existing `uncompute_op::apply()` path in `qint_t<1>`'s destructor handles all three cases. The `BITWISE_SELF` tag's `apply()` implementation (currently a TODO stub) must be completed to emit the actual gates. For the `operator~` case, a simple `ADD_CONST` with c=1 suffices, or a dedicated `FLIP` kind can be added if cleaner.

### 5.4 Backward-Compatible Accessors

Instead of shadow fields, `qbool` provides accessor properties that proxy to the base:

```cpp
class qbool : public qint_t<1> {
public:
    // Read/write accessors for backward compatibility.
    // Replace direct field access (.is_super, .value as bool) across the codebase.

    bool get_is_super() const { return (super_mask & 1) != 0; }
    void set_is_super(bool s) { super_mask = s ? 1ULL : 0ULL; }

    bool get_bool_value() const { return (value & 1) != 0; }
    void set_bool_value(bool v) { value = v ? 1 : 0; }
};
```

All ~30 uses of `.is_super` and ~241 uses of `.value` (in bool context on qbool) are updated via search-and-replace. This is mechanical but touches many files. The replacements are:

| Old | New |
|---|---|
| `q.is_super = true` | `q.super_mask = 1` |
| `q.is_super = false` | `q.super_mask = 0` |
| `q.is_super` (read) | `(q.super_mask & 1)` |
| `q.value = true` (on qbool) | `q.value = 1` |
| `q.value = false` (on qbool) | `q.value = 0` |
| `q.value` (bool read on qbool) | `(q.value & 1)` or `q.value != 0` |

**Note:** Many `.value` reads are in test assertions like `REQUIRE(result.value == true)`. Since `int64_t` compares with `bool` via implicit conversion, many of these work without changes. The ones that break are where `bool` type is expected explicitly (e.g., assigned to a `bool` variable).

### 5.5 Include Cycle Resolution

Break the cycle by splitting qbool-related conversions out of `qint_core.hpp`:

**Before:**
```
qint_core.hpp  (includes qbool.hpp for qint_t(const qbool&) and operator qbool())
```

**After:**
```
qint_core.hpp       (forward-declares class qbool; declares but doesn't define conversions)
qbool.hpp           (includes qint_core.hpp; defines class qbool : public qint_t<1>)
qint_qbool_conv.hpp (includes both; defines qint_t(const qbool&) and operator qbool() bodies)
```

Updated `qint.hpp` umbrella include order:
```
qint_fwd.hpp           // forward decl of qint_t<W>
qint_core.hpp          // full qint_t<W> definition (forward-declares qbool)
qbool.hpp              // qbool : public qint_t<1>
qint_qbool_conv.hpp    // conversion bodies between qint_t<W> and qbool
lazy_expr.hpp          // AndExpr/OrExpr (forward-declares qbool, already works)
qbool_ops.hpp          // qbool operator bodies
qint_arith.hpp         // arithmetic operators
qint_bitwise.hpp       // bitwise operators
qint_compare.hpp       // comparison + subscript operators
```

## 6. Implementation Phases

### Phase 1: Add owning_ to qint_t\<W\> -- COMPLETE

Added `bool owning_` to `qint_t<W>`, guarded destructor, updated move/copy semantics, added `make_non_owning()` factory. Tests in `test_qint_owning.cpp`.

### Phase 2: Break include cycle -- COMPLETE

Created `qint_qbool_conv.hpp`, forward-declared `qbool` in `qint_core.hpp`, updated umbrella include order.

### Phase 3: Rewrite qbool as subclass of qint_t\<1\> -- COMPLETE

`class qbool : public qint_t<1>`. Removed duplicated fields, `QboolUncompute` enum eliminated. All operators migrated to `uncompute_op`. Tests in `test_qbool_unified.cpp`.

### Phase 3b: Implement BITWISE_SELF apply() in uncompute_op -- COMPLETE

BITWISE_SELF case implemented in `uncompute_op.hpp` for AND/OR/XOR sub-kinds on qbool path.

### Phase 4: Search-and-replace field access -- COMPLETE

All `is_super` and `value` (bool context) references migrated to `super_mask` and `int64_t value`.

### Phase 5: Add explicit instantiation for qint_t\<1\> -- COMPLETE

`template class sturm::qint_t<1>` in `instantiations.cpp`, extern declaration in `qint.hpp`.

### Phase 6: Cleanup -- COMPLETE

Dead code removed, docs updated.

### Phase 7: Auto-promote classical qubits on theta()/phi() rotation

**Problem:** When `theta()` or `phi()` is applied to a classical (unallocated) qbool, the rotation gate is silently dropped. The proxies check `if (parent.qubits[i] >= 0)` and skip unallocated qubits. A classical qbool should auto-promote to quantum when a rotation gate is applied.

**Immediate symptom:**
```cpp
qbool c(true);   // classical: qubits[0]==-1, super_mask=0
c.theta() += 2;  // silent no-op — no qubit allocated, no RY emitted
```

**Files:** `include/sturm/qtypes/qint_core.hpp`, `tests/test_phase_amp.cpp`, `tests/backend/test_phi_theta_backend.cpp`

**Changes:**
- In `PhiProxy::operator+=` and `ThetaProxy::operator+=`, add a promotion loop before the gate-emission loop that allocates qubits, sets `super_mask`, and emits X gate (backend) or `prepare(qubit, 1.0)` (sink) for classical 1 bits.
- Update test 7 (`test_rotation_no_qubits_no_emission`) and test 9 (`test_phi_add_no_qubits_no_emission`) to expect auto-promotion + gate emission instead of no-op.

**Risk:** Low. Contained change in two methods. Existing tests for already-allocated qubits are unaffected.

**Tests:** Updated test_phase_amp + test_phi_theta_backend, plus or_circuit example verification.

## 7. Risks and Mitigations

### Object slicing
If a `qbool` is passed by value to a function expecting `qint_t<1>`, the object is sliced to the base. This is acceptable because `qbool` adds no data members — all state is in the base. The sliced `qint_t<1>` retains full quantum state. Qbool-specific methods (flip, ensure_qubit) are unavailable on the sliced copy, but this is correct behavior.

### sizeof growth
Current `qbool`: ~52 bytes (bool + bool + array<int,1> + bool + enum + 2*uint32 + uncompute_op).
New `qbool` = `qint_t<1>`: ~48 bytes (int64 + uint64 + array<int,1> + bool + uncompute_op). Roughly the same or smaller.

### WHEN static_assert
`when.hpp` has `static_assert(std::is_same_v<std::decay_t<T>, qbool>)`. Since `qbool` remains a distinct class (subclass, not typedef), this continues to work. `qint_t<1>` alone does not pass the assert, which is correct.

### Destructor ordering
`qbool`'s destructor runs before `qint_t<1>`'s. Since `qbool` no longer has its own uncompute/release logic (everything is in the base), `qbool`'s destructor can be defaulted or trivial. The base destructor handles uncompute + qubit release.

### make_non_owning in DSL files
14 files use `qbool::make_non_owning()`. This static method remains on `qbool` and returns a `qbool` with `owning_ = false`. No change needed beyond inheriting `owning_` from the base.

## 8. Acceptance Criteria

1. `qbool(0.5).phi() += 1.0` compiles and emits an RZ gate via the sink.
2. `qbool(0.5).theta() += 1.0` compiles and emits an RY gate via the sink.
3. `std::is_base_of_v<qint_t<1>, qbool>` is true.
4. `QboolUncompute` enum no longer exists.
5. All existing tests pass without modification to test logic (only field access syntax may change).
6. `WHEN(qbool_var) { ... }` continues to work for classical true, classical false, and superposed cases.
7. `qbool::make_non_owning()` continues to work in all DSL files.
8. `operator~`, `AndExpr::operator qbool()`, `OrExpr::operator qbool()` correctly uncompute via `uncompute_op` on destruction.
9. No include cycles. Full clean build succeeds.
10. `sizeof(qbool) <= 56` bytes (no bloat from unification).

## 9. Dependency Order

```
Phase 1 (owning_ on qint_t)           ✅
    |
Phase 2 (break include cycle)          ✅
    |
Phase 3 + 3b (rewrite qbool + BITWISE_SELF)  ✅
    |
Phase 4 (search-and-replace field access)     ✅
    |
Phase 5 (explicit instantiation)              ✅
    |
Phase 6 (cleanup)                             ✅
    |
Phase 7 (auto-promote on theta/phi)           ← IN PROGRESS
```

Phases 1-6 are complete. Phase 7 is a follow-on fix for a semantic gap: rotation gates on classical qubits should auto-promote rather than silently no-op.
