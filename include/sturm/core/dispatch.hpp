#pragma once
// dispatch.hpp — Generic dispatch helpers for every qint operator.
// Step 5, spec §4, Implementation Plan §5.
//
// Implements the 8-step operator dispatch algorithm from spec §4:
//   1. Compute new_mask via mask_fn.
//   2. Compute classical result via classical_fn.
//   3. Fast path if new_mask == 0 (return classical result immediately).
//   4. Lazy-allocate qubits on a, b, out at every set bit position.
//   5. Read current_control from when_fwd.hpp.
//   6. Invoke sink_fn.
//   7. Stamp result (value + mask).
//   8. Return.
//
// Templates are written against the duck-typed interface expected by qint_t:
//   - .value      : int64_t
//   - .super_mask : uint64_t
//   - .qubits     : std::array<int, Width>  (sentinel -1 = unallocated)
//   - .qubits_vec(): std::vector<int> of currently allocated indices
//
// Four dispatch variants:
//   dispatch_binary  — two qint operands → qint result
//   dispatch_unary   — one qint operand  → qint result
//   dispatch_compare — two qint operands → qbool result
//   dispatch_shift   — qint + int amount → qint result

#include "sturm/control/when_fwd.hpp"  // current_control TLS
#include "sturm/core/qubit_pool.hpp"
#include "sturm/qtypes/qbool.hpp"

#include <cstdint>
#include <cstddef>

namespace sturm::detail {

// ── Internal helper ────────────────────────────────────────────────────────────
// Ensure qubits[i] is allocated. Called for every set bit in a mask.
template <class QintLike>
void ensure_bit_qubit(QintLike& q, int i) {
    if (q.qubits[i] < 0) {
        q.qubits[i] = QubitPool::instance().allocate();
    }
}

// ── dispatch_binary ────────────────────────────────────────────────────────────
// Template parameters:
//   Out         — result type (same as qint_t<W>)
//   A, B        — operand types (same as qint_t<W>, may be const-ref)
//   ClassicalFn — (int64_t, int64_t) -> int64_t
//   MaskFn      — (uint64_t, uint64_t) -> uint64_t
//   SinkFn      — (const A&, const B&, const Out&, int ctrl) -> void
//
// A and B are taken by const-ref but for qubit allocation we need mutable
// copies. The const casts are safe because we own the copies.
template <class Out, class A, class B,
          class ClassicalFn, class MaskFn, class SinkFn>
Out dispatch_binary(const A& a, const B& b,
                    ClassicalFn classical_fn,
                    MaskFn      mask_fn,
                    SinkFn      sink_fn) {
    // Step 1: compute new mask.
    const uint64_t new_mask = mask_fn(a.super_mask, b.super_mask);

    // Step 2: compute classical result.
    const int64_t  classical_val = classical_fn(a.value, b.value);

    // Step 3: fast path.
    if (new_mask == 0) {
        Out result;
        result.value      = classical_val;
        result.super_mask = 0;
        // qubits already filled to -1 by Out default ctor
        return result;
    }

    // Step 4: lazy-allocate qubits.
    // We work on mutable copies of a and b to allow allocation.
    A a_mut = a;
    B b_mut = b;
    Out out;

    constexpr int kWidth = static_cast<int>(
        std::tuple_size<decltype(Out{}.qubits)>::value);

    for (int i = 0; i < kWidth; ++i) {
        const uint64_t bit = 1ULL << i;
        if (new_mask & bit) {
            ensure_bit_qubit(out, i);
        }
        if (a_mut.super_mask & bit) {
            ensure_bit_qubit(a_mut, i);
        }
        if (b_mut.super_mask & bit) {
            ensure_bit_qubit(b_mut, i);
        }
    }

    // Step 5: read current_control.
    const int ctrl = current_control ? current_control_qubit : -1;

    // Step 6: invoke sink callback.
    sink_fn(a_mut, b_mut, out, ctrl);

    // Step 7: stamp result.
    out.value      = classical_val;
    out.super_mask = new_mask;

    // Step 8: return.
    return out;
}

// ── dispatch_unary ─────────────────────────────────────────────────────────────
// Template parameters:
//   Out         — result type
//   A           — operand type
//   ClassicalFn — (int64_t) -> int64_t
//   MaskFn      — (uint64_t) -> uint64_t
//   SinkFn      — (const A&, const Out&, int ctrl) -> void
template <class Out, class A,
          class ClassicalFn, class MaskFn, class SinkFn>
Out dispatch_unary(const A& a,
                   ClassicalFn classical_fn,
                   MaskFn      mask_fn,
                   SinkFn      sink_fn) {
    // Step 1: compute new mask.
    const uint64_t new_mask = mask_fn(a.super_mask);

    // Step 2: compute classical result.
    const int64_t  classical_val = classical_fn(a.value);

    // Step 3: fast path.
    if (new_mask == 0) {
        Out result;
        result.value      = classical_val;
        result.super_mask = 0;
        return result;
    }

    // Step 4: lazy-allocate qubits.
    A a_mut = a;
    Out out;

    constexpr int kWidth = static_cast<int>(
        std::tuple_size<decltype(Out{}.qubits)>::value);

    for (int i = 0; i < kWidth; ++i) {
        const uint64_t bit = 1ULL << i;
        if (new_mask & bit) {
            ensure_bit_qubit(out, i);
        }
        if (a_mut.super_mask & bit) {
            ensure_bit_qubit(a_mut, i);
        }
    }

    // Step 5: current_control.
    const int ctrl = current_control ? current_control_qubit : -1;

    // Step 6: sink callback.
    sink_fn(a_mut, out, ctrl);

    // Step 7: stamp.
    out.value      = classical_val;
    out.super_mask = new_mask;

    // Step 8: return.
    return out;
}

// ── dispatch_compare ──────────────────────────────────────────────────────────
// Returns a qbool.  The result is superposed whenever either operand has any
// superposed bit (i.e. new_mask != 0).
//
// Template parameters:
//   A, B        — operand types
//   ClassicalFn — (int64_t, int64_t) -> bool
//   MaskFn      — (uint64_t, uint64_t) -> uint64_t   (0 = classical result)
//   SinkFn      — (const A&, const B&, qbool& out, int ctrl) -> void
template <class QintLike,
          class ClassicalFn, class MaskFn, class SinkFn>
qbool dispatch_compare(const QintLike& a, const QintLike& b,
                       ClassicalFn classical_fn,
                       MaskFn      mask_fn,
                       SinkFn      sink_fn) {
    // Step 1: check if any operand is superposed.
    const uint64_t combined = mask_fn(a.super_mask, b.super_mask);

    // Step 2: classical comparison.
    const bool classical_val = classical_fn(a.value, b.value);

    // Step 3: fast path.
    if (combined == 0) {
        qbool result;
        result.value      = classical_val ? 1 : 0;
        result.super_mask = 0;
        // qubits[0] stays -1
        return result;
    }

    // Step 4: lazy-allocate qubits on a and b; allocate result qubit.
    QintLike a_mut = a;
    QintLike b_mut = b;

    constexpr int kWidth = static_cast<int>(
        std::tuple_size<decltype(QintLike{}.qubits)>::value);

    for (int i = 0; i < kWidth; ++i) {
        const uint64_t bit = 1ULL << i;
        if (a_mut.super_mask & bit) ensure_bit_qubit(a_mut, i);
        if (b_mut.super_mask & bit) ensure_bit_qubit(b_mut, i);
    }

    qbool out;
    out.super_mask = 1ULL;
    out.value      = classical_val ? 1 : 0;
    out.ensure_qubit();  // allocate result qubit

    // Step 5: current_control.
    const int ctrl = current_control ? current_control_qubit : -1;

    // Step 6: sink callback.
    sink_fn(a_mut, b_mut, out, ctrl);

    // Steps 7–8: return (value + is_super already set above).
    return out;
}

// ── dispatch_shift ────────────────────────────────────────────────────────────
// Like dispatch_binary but the second operand is a plain int (shift amount).
//
// Template parameters:
//   Out         — result type
//   A           — operand type
//   ClassicalFn — (int64_t, int n) -> int64_t
//   MaskFn      — (uint64_t, int n) -> uint64_t
//   SinkFn      — (const A&, const Out&, int n, int ctrl) -> void
template <class Out, class A,
          class ClassicalFn, class MaskFn, class SinkFn>
Out dispatch_shift(const A& a, int n,
                   ClassicalFn classical_fn,
                   MaskFn      mask_fn,
                   SinkFn      sink_fn) {
    // Step 1: compute new mask.
    const uint64_t new_mask = mask_fn(a.super_mask, n);

    // Step 2: compute classical result.
    const int64_t  classical_val = classical_fn(a.value, n);

    // Step 3: fast path.
    if (new_mask == 0) {
        Out result;
        result.value      = classical_val;
        result.super_mask = 0;
        return result;
    }

    // Step 4: lazy-allocate qubits.
    A a_mut = a;
    Out out;

    constexpr int kWidth = static_cast<int>(
        std::tuple_size<decltype(Out{}.qubits)>::value);

    for (int i = 0; i < kWidth; ++i) {
        const uint64_t bit = 1ULL << i;
        if (new_mask & bit) ensure_bit_qubit(out, i);
        if (a_mut.super_mask & bit) ensure_bit_qubit(a_mut, i);
    }

    // Step 5: current_control.
    const int ctrl = current_control ? current_control_qubit : -1;

    // Step 6: sink callback.
    sink_fn(a_mut, out, n, ctrl);

    // Step 7: stamp.
    out.value      = classical_val;
    out.super_mask = new_mask;

    // Step 8: return.
    return out;
}

} // namespace sturm::detail
