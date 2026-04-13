# Implementation Plan: Per-Bit Lazy WHEN Promotion via BitProxy

**PRD:** `docs/prd_bitproxy_when_promotion.md`
**Approach:** Phased, TDD, backend-only (`STURM_BACKEND_ENABLED`)
**Total estimated:** ~800 LOC new/modified across 4 phases

## Module Dependency Graph

```
Phase A (bitwise foundation)
  M1  BitProxy struct + operator bodies               [~250 LOC] new header
  M2  Non-const operator[] returning BitProxy          [~30 LOC]  qint_core.hpp + qint_compare.hpp
  M3  Tests: BitProxy per-bit promotion (XOR, AND)     [~120 LOC] new test
  M4  Bitwise compound-assigns use BitProxy            [~60 LOC]  qint_bitwise_v3.hpp
  M5  Tests: WHEN + bitwise ops on qint_t              [~80 LOC]  extend test

Phase B (arithmetic DSL)
  M6  Template adder DSL (maj, uma, add, sub)          [~30 LOC]  adder_dsl.hpp
  M7  Template mul/div/mod DSL                         [~30 LOC]  mul_dsl.hpp, div_dsl.hpp, mod_dsl.hpp
  M8  Template logic/swap/compare/c_and DSL            [~30 LOC]  logic_dsl.hpp, swap_dsl.hpp, compare_dsl.hpp, c_and_dsl.hpp
  M9  Arithmetic compound-assigns use BitProxy         [~100 LOC] qint_arith_v3.hpp
  M10 Tests: WHEN + arithmetic ops on qint_t           [~80 LOC]  extend test

Phase C (free operators + shifts)
  M11 Backend free operators: fast-path bypass         [~40 LOC]  qint_arith_backend.hpp
  M12 copy_register: only copy quantum bits            [~10 LOC]  qint_arith_backend.hpp
  M13 Shift operators: fast-path bypass + BitProxy     [~40 LOC]  qint_shift_backend.hpp
  M14 Comparison operators: fast-path bypass           [~20 LOC]  qint_compare_v3.hpp
  M15 Tests: WHEN + free ops, shifts, comparisons      [~60 LOC]  extend test

Phase D (cleanup)
  M16 Revert dispatch.hpp full-mask WHEN promotion     [revert]   dispatch.hpp
  M17 Revert qint_arith_backend.hpp mask promotion     [revert]   qint_arith_backend.hpp
  M18 Final regression + acceptance tests              [~50 LOC]  test updates
```

---

## Phase A: Bitwise Foundation

### M1: BitProxy struct + operator bodies (~250 LOC)

**New file:** `include/sturm/qtypes/bit_proxy.hpp`

**Includes:** `sturm/control/when_fwd.hpp`, `sturm/qtypes/lazy_expr.hpp`, `sturm/core/qubit_pool.hpp`, `sturm/backend/primitives.hpp`, `sturm/core/context.hpp`

**Guard:** `#ifdef STURM_BACKEND_ENABLED` (entire file)

```cpp
namespace sturm {

struct BitProxy {
    int*      qubit_ptr;   // &parent.qubits[i]  or  &qbool.qubits[0]
    uint64_t* mask_ptr;    // &parent.super_mask  or  &qbool.super_mask
    int64_t*  value_ptr;   // &parent.value       or  &qbool.value
    size_t    bit_pos;     // which bit in the parent (0 for qbool)

    // ── Constructors ──

    // From qint_t<W> parent + bit index (implemented as template in header)
    template <std::size_t W>
    BitProxy(qint_t<W>& parent, size_t i);

    // From standalone qbool (wraps qbool's own fields)
    BitProxy(qbool& q);

    // Default (null — for array construction, must be assigned before use)
    BitProxy() : qubit_ptr(nullptr), mask_ptr(nullptr),
                 value_ptr(nullptr), bit_pos(0) {}

    // ── Accessors ──

    int  qubit_index() const { return *qubit_ptr; }
    bool is_quantum()  const { return *qubit_ptr >= 0; }
    bool bit_value()   const { return (*value_ptr >> bit_pos) & 1; }

    void set_bit_value(bool v) {
        if (v) *value_ptr |=  (int64_t(1) << bit_pos);
        else   *value_ptr &= ~(int64_t(1) << bit_pos);
    }

    // ── Promotion ──

    // Allocate qubit if unallocated. Initialize to classical value via X gate.
    // Update parent's super_mask. Requires active BackendContext.
    void ensure_quantum();

    // ── Gate operators ──

    BitProxy& operator^=(const BitProxy& other);          // CNOT
    BitProxy& operator^=(const AndExpr<BitProxy>& expr);  // Toffoli
    BitProxy& operator^=(const OrExpr<BitProxy>& expr);   // OR pattern
    BitProxy& flip();                                      // X gate
};

// ── Lazy expressions ──
AndExpr<BitProxy> operator&(const BitProxy& a, const BitProxy& b);
OrExpr<BitProxy>  operator|(const BitProxy& a, const BitProxy& b);

// ── Materialization (allocates ancilla qbool) ──
template<> AndExpr<BitProxy>::operator qbool() const;
template<> OrExpr<BitProxy>::operator qbool() const;

} // namespace sturm
```

**Key operator logic:**

`ensure_quantum()`:
```
if *qubit_ptr >= 0: return  (already allocated)
*qubit_ptr = QubitPool::instance().allocate()
*mask_ptr |= (1ULL << bit_pos)
if bit_value():
    emit_X_lifted(get_ctx(), *qubit_ptr)  // init |0> → |1>
```

`operator^=(const BitProxy& other)` — CNOT with classical folding:
```
if other is quantum (other.qubit_ptr >= 0):
    this->ensure_quantum()
    emit_CX_lifted(ctx, other.qubit_index(), this->qubit_index())
else:  // other is classical
    if other.bit_value() == 0:
        // XOR with 0 = identity → skip
    else:  // other.bit_value() == 1
        if current_control != nullptr or this->is_quantum():
            this->ensure_quantum()
            emit_X_lifted(ctx, this->qubit_index())  // controlled by WHEN stack
        else:
            // pure classical: flip this bit's value
            this->set_bit_value(!this->bit_value())
// update classical value
*value_ptr ^= (int64_t(other.bit_value()) << bit_pos)
```

Wait — the classical value update is wrong here. The XOR of the *bit* at bit_pos, not the whole value. Let me correct:

Actually, the classical value update should happen via the gate emission or classical path. Let me re-think. In the current qbool::operator^=, the classical value is NOT updated — only the quantum state changes. The compound-assign updates the classical value after the DSL call (`value ^= b.value`). So BitProxy operators should NOT update the classical value. The caller (compound-assign) does that.

Correction: BitProxy operators modify quantum state only. Classical value is updated by the compound-assign after the DSL call, as currently done.

`operator^=(const BitProxy& other)` — corrected:
```
if other is quantum:
    this->ensure_quantum()
    emit_CX_lifted(ctx, other.qubit_index(), this->qubit_index())
else:  // other is classical
    if other.bit_value() == 0:
        skip  // XOR with 0 = identity
    else:  // other.bit_value() == 1
        if current_control != nullptr or this->is_quantum():
            this->ensure_quantum()
            emit_X_lifted(ctx, this->qubit_index())
        else:
            skip  // no quantum effect, classical value updated by caller
```

`operator^=(const AndExpr<BitProxy>& expr)` — Toffoli with classical folding:
```
a = expr.a, b = expr.b
if a is quantum AND b is quantum:
    this->ensure_quantum()
    emit_CCX_lifted(ctx, a.qubit_index(), b.qubit_index(), this->qubit_index())
elif a is quantum AND b is classical:
    if b.bit_value() == 1:
        this->ensure_quantum()
        emit_CX_lifted(ctx, a.qubit_index(), this->qubit_index())
    // b == 0: AND result always 0, skip
elif a is classical AND b is quantum:
    if a.bit_value() == 1:
        this->ensure_quantum()
        emit_CX_lifted(ctx, b.qubit_index(), this->qubit_index())
    // a == 0: skip
else:  // both classical
    classical_result = a.bit_value() & b.bit_value()
    if classical_result == 1:
        if current_control != nullptr or this->is_quantum():
            this->ensure_quantum()
            emit_X_lifted(ctx, this->qubit_index())
    // else: skip
```

`operator^=(const OrExpr<BitProxy>& expr)` — OR decomposition:
```
// c ^= (a | b)  =  c ^= a; c ^= b; c ^= (a & b)
// But with classical folding for each step.
// Reuse the XOR and AND-XOR logic above:
*this ^= expr.a;
*this ^= expr.b;
AndExpr<BitProxy> and_expr(expr.a, expr.b);
*this ^= and_expr;
```

`flip()`:
```
if this->is_quantum() or current_control != nullptr:
    this->ensure_quantum()
    emit_X_lifted(ctx, this->qubit_index())
// else: no quantum effect, caller handles classical value
```

`AndExpr<BitProxy>::operator qbool()` — materialization:
```
// Allocate ancilla qubit for result
int anc_idx = QubitPool::instance().allocate();
// Emit Toffoli (or folded gate) into ancilla
qbool result;
result.qubits[0] = anc_idx;
result.owning_ = true;
result.super_mask = 1ULL;
// Determine gate based on classical folding:
if a is quantum AND b is quantum:
    emit_CCX_lifted(ctx, a.qubit, b.qubit, anc)
elif one is classical with value 1:
    emit_CX_lifted(ctx, quantum_one.qubit, anc)
elif one is classical with value 0:
    // result is 0, no gate needed (ancilla stays |0>)
else:  // both classical
    if a.bit_value() & b.bit_value():
        emit_X_lifted(ctx, anc)  // set to |1> under WHEN control
// stamp uncompute tag
result.uncompute_ = uncompute_op::make_bitwise_qbool(...)
return result;
```

### M2: Non-const operator[] returning BitProxy (~30 LOC)

**Files:**
- `include/sturm/qtypes/qint_core.hpp` — declare `BitProxy operator[](std::size_t i);` (non-const)
- `include/sturm/qtypes/qint_compare.hpp` — define the body (next to existing const `operator[]`)

The existing const `operator[]` (returns `qbool` by value) remains for read-only access. The new non-const overload returns `BitProxy` for mutable access with writeback.

```cpp
// qint_core.hpp — add declaration alongside existing const version (line ~273)
BitProxy operator[](std::size_t i);

// qint_compare.hpp — add definition alongside existing const version (line ~167)
template <std::size_t W>
BitProxy qint_t<W>::operator[](std::size_t i) {
    return BitProxy(*this, i);
}
```

### M3: Tests for BitProxy per-bit promotion (~120 LOC)

**New file:** `tests/backend/test_bitproxy_when_promotion.cpp`
**Register in:** `tests/backend/CMakeLists.txt`

Test cases:
1. **BitProxy XOR: quantum source promotes target.** Create two qbool-backed BitProxies, one quantum, one classical. `target ^= source` promotes target.
2. **BitProxy XOR: classical 0 source is identity.** No qubit allocated, no gate.
3. **BitProxy XOR: classical 1 source inside WHEN.** Target promoted, X_lifted emitted.
4. **BitProxy AND-XOR: classical folding.** `c ^= (a & b)` where b is classical 1 → emits CX not CCX.
5. **BitProxy AND-XOR: classical 0 folds to skip.** `c ^= (a & b)` where b is 0 → no gate.
6. **ensure_quantum: initializes with X for value=1.** Classical bit with value 1 gets X gate on allocation.

Harness: APPEND mode BackendContext + GateIR inspection (same pattern as `test_when_dispatch.cpp`).

### M4: Bitwise compound-assigns use BitProxy (~60 LOC)

**File:** `include/sturm/qtypes/qint_bitwise_v3.hpp`

Changes to `operator^=`, `operator&=`, `operator|=`:

1. **Fast-path tweak:** Add `&& detail::current_control == nullptr`:
   ```cpp
   if ((qubits[0] < 0 || b.qubits[0] < 0) && detail::current_control == nullptr) {
       value ^= b.value;
       return *this;
   }
   ```

2. **Replace qbool arrays with BitProxy arrays:**
   ```cpp
   qint_t<W> b_mut = b;  // mutable copy (for temp qubit allocation)
   // Note: qint_t copy ctor sets qubits to -1, value/super_mask copied.
   // We need to preserve b's qubit indices for the circuit:
   b_mut.qubits = b.qubits;  // copy qubit indices
   b_mut.owning_ = false;     // b_mut does NOT own b's qubits

   BitProxy this_bits[W], b_bits[W];
   for (std::size_t i = 0; i < W; ++i) {
       this_bits[i] = BitProxy(*this, i);
       b_bits[i]    = BitProxy(b_mut, i);
   }
   ```

   Wait — we need b_mut to have the same qubit indices as b (so the circuit operates on the right qubits), but also be mutable so BitProxy can allocate new qubits for classical bits. The copy should preserve existing qubit indices (for quantum bits) and allow allocation for classical bits (qubits[i] == -1).

   The problem: `qint_t` copy constructor sets `qubits` to -1 (doesn't copy indices). We need a shallow clone that preserves qubit indices but doesn't own them.

   Solution: manual field copy with `owning_ = false`:
   ```cpp
   qint_t<W> b_mut;
   b_mut.value      = b.value;
   b_mut.super_mask = b.super_mask;
   b_mut.qubits     = b.qubits;  // preserve existing qubit indices
   b_mut.owning_    = false;      // don't release b's qubits on destruct
   ```

   After the DSL call, release any NEW qubits allocated on b_mut (where b had -1):
   ```cpp
   for (std::size_t i = 0; i < W; ++i) {
       if (b_mut.qubits[i] >= 0 && b.qubits[i] < 0) {
           QubitPool::instance().release(b_mut.qubits[i]);
       }
   }
   ```

3. **For `operator^=` (in-place):** Loop with BitProxy, no DSL needed:
   ```cpp
   for (std::size_t i = 0; i < W; ++i) {
       BitProxy this_bit(*this, i);
       BitProxy b_bit(b_mut, i);
       this_bit ^= b_bit;
   }
   value ^= b.value;
   ```

4. **For `operator&=` and `operator|=` (out-of-place):** Allocate result register as before (fresh qubits), but use BitProxy for operands:
   ```cpp
   int res_idx[W];
   qbool res_qbools[W];
   BitProxy res_bits[W];
   for (std::size_t i = 0; i < W; ++i) {
       res_idx[i]    = QubitPool::instance().allocate();
       res_qbools[i] = qbool::make_non_owning(res_idx[i]);
       res_bits[i]   = BitProxy(res_qbools[i]);
   }
   for (std::size_t i = 0; i < W; ++i) {
       BitProxy a_bit(*this, i);
       BitProxy b_bit(b_mut, i);
       res_bits[i] ^= (a_bit & b_bit);  // Toffoli with classical folding
   }
   ```

### M5: Tests for WHEN + bitwise ops on qint_t (~80 LOC)

Extend `test_bitproxy_when_promotion.cpp`:

1. **WHEN + classical qint XOR.** `WHEN(c) { a ^= b; }` where b = 0b0100. Assert only bit 2 of a is promoted.
2. **WHEN + classical qint AND.** `WHEN(c) { a &= b; }`. Assert result bits are quantum.
3. **No WHEN: classical fast-path preserved.** Assert `super_mask == 0`.

---

## Phase B: Arithmetic DSL

### M6: Template adder DSL (~30 LOC)

**File:** `include/sturm/lib/adder_dsl.hpp`

Add `template <typename Bit>` to 4 functions:
- `maj_dsl(Bit& a, Bit& b, Bit& c)` (line 52)
- `uma_dsl(Bit& a, Bit& b, Bit& c)` (line 66)
- `lib_add_dsl(Bit* a_bits, Bit* b_bits, Bit& carry_out, size_t n)` (line 90)
- `lib_sub_dsl(Bit* a_bits, Bit* b_bits, Bit& borrow_out, size_t n)` (line 143)

Bodies unchanged. Internal ancilla (carry_anc) changes from pre-allocated to lazy:
```cpp
// Before:
int carry_anc_idx = QubitPool::instance().allocate();
qbool carry_anc = qbool::make_non_owning(carry_anc_idx);

// After:
qbool carry_anc_qbool;           // classical 0, no qubit, owning=true
Bit carry_anc(carry_anc_qbool);  // BitProxy wrapping standalone qbool
// carry_anc_qbool destructor auto-releases if qubit was allocated
```

Remove manual `QubitPool::instance().release(carry_anc_idx)` — handled by qbool destructor.

For `lib_sub_dsl`: the carry_anc starts with value 1 (two's complement). With BitProxy:
```cpp
qbool carry_anc_qbool;
carry_anc_qbool.value = 1;  // classical 1 for two's complement +1
Bit carry_anc(carry_anc_qbool);
```

### M7: Template mul/div/mod DSL (~30 LOC)

**Files:**
- `include/sturm/lib/mul_dsl.hpp` — `template <typename Bit> void lib_mul_dsl(...)` (line 56)
- `include/sturm/lib/div_dsl.hpp` — `template <typename Bit> void compute_overflow_or_dsl(...)` (line 34), `template <typename Bit> void lib_div_dsl(...)` (line 49)
- `include/sturm/lib/mod_dsl.hpp` — `template <typename Bit> void lib_mod_dsl(...)` (line 19)

Same pattern: add `template <typename Bit>`, replace `qbool` parameter types with `Bit`.

Internal ancilla and temporary qbools: wrap in `Bit(qbool_var)`.

`div_dsl.hpp` line 40 has: `qbool tmp = (overflow & b_bits[j]);` — this materializes an `AndExpr<BitProxy>` into a `qbool` via `AndExpr<BitProxy>::operator qbool()`. Works because the materialization returns a standalone qbool (with its own ancilla qubit).

`mul_dsl.hpp` uses `ctx.control_stack.push_control(...)` — this takes a `uint32_t` qubit index. With BitProxy, access via `b_bits[i].qubit_index()`. The bit must be quantum for this to work. In the existing code, all bits are pre-allocated. With BitProxy + WHEN promotion, `b_bits[i]` might be classical. But `lib_mul_dsl` is called from `operator*=` which currently fast-paths on classical. Inside WHEN, the fast-path is bypassed, but the multiplication circuit pushes `b[i]` onto the control stack — this requires `b[i]` to have a qubit. The BitProxy's `ensure_quantum()` would need to be called before `push_control`. This needs careful handling in the mul_dsl body.

### M8: Template logic/swap/compare/c_and DSL (~30 LOC)

**Files:**
- `include/sturm/lib/logic_dsl.hpp` — 4 functions: `lib_or_dsl`, `lib_nand_dsl`, `lib_nor_dsl`, `lib_xnor_dsl`
- `include/sturm/lib/swap_dsl.hpp` — 1 function: `lib_swap_dsl`
- `include/sturm/lib/compare_dsl.hpp` — 6 functions: `lib_eq_dsl`, `lib_lt_dsl`, `lib_le_dsl`, `lib_gt_dsl`, `lib_ge_dsl`, `lib_ne_dsl`
- `include/sturm/lib/c_and_dsl.hpp` — 2 functions: `lib_c_AND_dsl`, `lib_c_n_AND_dsl`

Same mechanical change: `template <typename Bit>` + `qbool` → `Bit`.

### M9: Arithmetic compound-assigns use BitProxy (~100 LOC)

**File:** `include/sturm/qtypes/qint_arith_v3.hpp`

Changes to `operator+=`, `-=`, `*=`, `/=`, `%=`:

1. **Fast-path tweak:** Add `&& detail::current_control == nullptr`
2. **Build mutable b copy** (preserving qubit indices, non-owning)
3. **Build BitProxy arrays** for `*this` and `b_mut`
4. **Lazy carry/borrow ancilla** (no pre-allocation)
5. **Call templated DSL** with BitProxy arrays
6. **Release temporary qubits** on b_mut after DSL call
7. **Classical value update** unchanged

### M10: Tests for WHEN + arithmetic ops (~80 LOC)

Extend test file:
1. **WHEN + classical +=.** Verify `super_mask != 0` and correct value.
2. **WHEN + classical -=.** Same.
3. **WHEN + classical *=.** Same.
4. **No WHEN: fast-path preserved for all arithmetic ops.**
5. **Gate count comparison.** WHEN-promoted `a += b` emits same gate count as pre-promoted (all-quantum) `a += b`.

---

## Phase C: Free Operators + Shifts

### M11: Backend free operators fast-path bypass (~40 LOC)

**File:** `include/sturm/qtypes/qint_arith_backend.hpp`

For `operator+`, `-`, `*`, `/`, `%` (5 operators) and `operator+(qint, int64_t)`:

Change fast-path guard to include WHEN check:
```cpp
if (a.qubits[0] < 0 || b.qubits[0] < 0) {
    if (detail::current_control == nullptr) {
        // classical fast-path (existing code)
        return result;
    }
}
```

Remove the full-mask promotion code added in the initial fix (revert lines that set `result.super_mask` to full width).

### M12: copy_register only copies quantum bits (~10 LOC)

**File:** `include/sturm/qtypes/qint_arith_backend.hpp`

Change `copy_register` to only allocate qubits where the source has them:
```cpp
for (std::size_t i = 0; i < W; ++i) {
    if (a.qubits[i] >= 0) {
        result.qubits[i] = QubitPool::instance().allocate();
    }
    // else: leave result.qubits[i] = -1 (classical, BitProxy will handle)
}
```

The CNOT loop already skips unallocated bits (`if (a.qubits[i] >= 0 && result.qubits[i] >= 0)`).

### M13: Shift operators fast-path bypass (~40 LOC)

**File:** `include/sturm/qtypes/qint_shift_backend.hpp`

For `operator<<`, `>>`, `<<=`, `>>=`: add `&& detail::current_control == nullptr` to the `qubits[0] < 0` fast-path checks.

Shift operators do qubit relabeling (not DSL circuits), so BitProxy is less relevant here. The key change is just not fast-pathing inside WHEN. When inside WHEN with classical operands, shifts need to allocate qubits and emit CNOT-based copy gates.

### M14: Comparison operators fast-path bypass (~20 LOC)

**File:** `include/sturm/qtypes/qint_compare_v3.hpp`

Update `make_dsl_compare_result` helper: add WHEN check to its classical fast-path.

### M15: Tests for free ops, shifts, comparisons (~60 LOC)

Extend test file:
1. **WHEN + free operator+.** Classical a + b inside WHEN → quantum result.
2. **WHEN + shift.** Classical a << 1 inside WHEN → quantum result.
3. **WHEN + comparison.** Classical a == b inside WHEN → quantum qbool result.

---

## Phase D: Cleanup

### M16: Revert dispatch.hpp full-mask WHEN promotion

Revert `dispatch_binary`, `dispatch_unary`, `dispatch_shift` changes:
- Remove `when_promotion` variable
- Remove `effective_mask` computation
- Restore original fast-path (`if (new_mask == 0) { ... }`)
- Restore original qubit allocation loop (uses `new_mask`, not `effective_mask`)
- Restore original stamp (`out.super_mask = new_mask`)

The non-backend path doesn't use the DSL/BitProxy mechanism, but the dispatch functions are only active when `!STURM_BACKEND_ENABLED`, which is the counter/recording-sink path (not the circuit-building path). For non-backend builds, the dispatch WHEN promotion may still be useful — evaluate whether to keep or revert based on test results.

**Decision point:** If non-backend tests (in `tests/` not `tests/backend/`) need WHEN promotion, keep the dispatch.hpp changes. Otherwise revert.

### M17: Revert qint_arith_backend.hpp mask promotion

Remove:
- The `need_quantum` / `current_control` checks added to `operator+(qint, int64_t)` (line ~92)
- The `result.super_mask = full_width` lines in operators +, -, *, /, %

Keep:
- The fast-path bypass (`&& detail::current_control == nullptr`) from M11

### M18: Final regression + acceptance tests (~50 LOC)

1. Run full backend test suite: `ctest --test-dir build_debug -L backend -j4`
2. Run non-backend test suite: `ctest --test-dir build_debug -j4`
3. Verify acceptance criteria from PRD:
   - Per-bit promotion (only touched bits get qubits)
   - Classical folding (XOR with 0 skips)
   - Lazy ancilla (carry allocated only when needed)
   - All regression tests pass

---

## Execution Order

```
M1 → M2 → M3 (test BitProxy in isolation)
         → M4 → M5 (bitwise compound-assigns + test)
         → M6 → M7 → M8 (template DSL)
         → M9 → M10 (arithmetic compound-assigns + test)
         → M11 → M12 → M13 → M14 → M15 (free ops + shifts + test)
         → M16 → M17 → M18 (cleanup + final regression)
```

Modules within a phase can be done sequentially. Phases depend on prior phases.

## Test Commands

```bash
# Configure
cmake -S . -B build_debug -DCMAKE_BUILD_TYPE=Debug

# Build and run specific test
cmake --build build_debug --target test_bitproxy_when_promotion
./build_debug/tests/backend/test_bitproxy_when_promotion

# Full regression
cmake --build build_debug -j4
ctest --test-dir build_debug -j4
```

## Risk Areas

1. **`qint_t` copy constructor** resets `qubits` to -1. The mutable-b-copy pattern needs explicit field assignment, not the copy constructor. Must preserve existing qubit indices for quantum bits while allowing new allocation for classical bits.

2. **`mul_dsl` uses `control_stack.push_control(qubit_idx)`** — requires the bit to have an allocated qubit. Inside WHEN with classical b, b's bits are unallocated. Need `ensure_quantum()` before `push_control`.

3. **`div_dsl` materializes `qbool tmp = (overflow & b_bits[j])`** — this calls `AndExpr<BitProxy>::operator qbool()`. The specialization must handle classical folding correctly and return a properly-tagged qbool with uncompute.

4. **Backward compatibility** — DSL functions are now templates. Existing call sites that use `qbool*` still work (template deduction: `Bit = qbool`). Explicit instantiations in `instantiations.cpp` may need updating for `BitProxy`.
