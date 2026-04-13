// qint_arith_v3.hpp — M19 (PRD v3): Wire qint_t<W> arithmetic compound-assigns
// to DSL library functions. Included by qint_arith.hpp when STURM_BACKEND_ENABLED.
//
// Operators: +=, -=, *=, /=, %=  (DSL library call per operator).
// Each operator extracts qbool refs via a[i] (M18 subscript).
// Target: <250 LoC.

#pragma once

#ifndef STURM_BACKEND_ENABLED
#  error "qint_arith_v3.hpp must only be included when STURM_BACKEND_ENABLED is set"
#endif

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/lib/adder_dsl.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"
#include "sturm/lib/mod_dsl.hpp"

#include <array>
#include <cstddef>

namespace sturm {

// ── operator+= ────────────────────────────────────────────────────────────────
// In-place: *this += b  (this is the target, b is the addend).
// Calls lib_add_dsl(b_bits, this_bits, carry_out, W).
// carry_out is a fresh ancilla qubit (released after the call).

template <std::size_t W>
qint_t<W>& qint_t<W>::operator+=(const qint_t<W>& b) {
    // Classical fast-path: if no valid qubit indices are set, just update value.
    // This handles the case where qint_t objects are used classically (qubits == -1).
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value = (value + b.value);
        return *this;
    }

    // Extract non-owning qbool arrays for 'this' (target) and 'b' (addend).
    qbool this_bits[W];
    qbool b_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        this_bits[i] = static_cast<const qint_t<W>&>(*this)[i];
        b_bits[i]    = b[i];
    }

    // Allocate carry_out ancilla (starts |0>).
    int carry_idx = QubitPool::instance().allocate();
    qbool carry   = qbool::make_non_owning(carry_idx);

    // In-place: this += b  (lib_add_dsl computes this_bits += b_bits)
    lib_add_dsl(b_bits, this_bits, carry, W);

    // Release carry ancilla (lib_add_dsl restored it to |0>; we re-release).
    QubitPool::instance().release(carry_idx);

    // Update classical value.
    value = (value + b.value);
    return *this;
}

// ── operator-= ────────────────────────────────────────────────────────────────
// In-place: *this -= b.
// Calls lib_sub_dsl(b_bits, this_bits, borrow_out, W).

template <std::size_t W>
qint_t<W>& qint_t<W>::operator-=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value = (value - b.value);
        return *this;
    }

    qbool this_bits[W];
    qbool b_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        this_bits[i] = static_cast<const qint_t<W>&>(*this)[i];
        b_bits[i]    = b[i];
    }

    int borrow_idx = QubitPool::instance().allocate();
    qbool borrow   = qbool::make_non_owning(borrow_idx);

    // lib_sub_dsl: this_bits -= b_bits
    lib_sub_dsl(b_bits, this_bits, borrow, W);

    QubitPool::instance().release(borrow_idx);

    value = (value - b.value);
    return *this;
}

// ── operator*= ────────────────────────────────────────────────────────────────
// Out-of-place: result = this * b, then move lower W bits into 'this'.
// Allocates a 2*W-bit result register (all |0>), calls lib_mul_dsl, then
// moves result's qubits into this->qubits (lower W bits only).

template <std::size_t W>
qint_t<W>& qint_t<W>::operator*=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value = (value * b.value);
        return *this;
    }

    // Extract non-owning qbool arrays.
    qbool a_bits[W];
    qbool b_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_bits[i] = static_cast<const qint_t<W>&>(*this)[i];
        b_bits[i] = b[i];
    }

    // Allocate result register: 2*W qubits (all start |0>).
    static constexpr std::size_t RW = 2u * W;
    int res_idx[RW];
    qbool res_bits[RW];
    for (std::size_t i = 0; i < RW; ++i) {
        res_idx[i]  = QubitPool::instance().allocate();
        res_bits[i] = qbool::make_non_owning(res_idx[i]);
    }

    // Compute: result = a * b (out-of-place).
    lib_mul_dsl(a_bits, W, b_bits, W, res_bits, RW);

    // Move result qubits into 'this' register.
    // Step 1: Release old 'this' qubits (they still hold the original a value).
    for (std::size_t i = 0; i < W; ++i) {
        if (qubits[i] >= 0) {
            QubitPool::instance().release(qubits[i]);
        }
    }
    // Step 2: Assign first W result qubits to this->qubits.
    for (std::size_t i = 0; i < W; ++i) {
        qubits[i] = res_idx[i];
    }
    // Step 3: Release the upper W result qubits (bits W..2W-1 of the product).
    for (std::size_t i = W; i < RW; ++i) {
        QubitPool::instance().release(res_idx[i]);
    }

    // Update classical value (truncated to W bits).
    value = (value * b.value);
    return *this;
}

// ── operator/= ────────────────────────────────────────────────────────────────
// Out-of-place: quotient_bits = this / b, then move quotient into 'this'.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator/=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value = (b.value != 0) ? (value / b.value) : 0;
        return *this;
    }

    qbool a_bits[W];
    qbool b_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_bits[i] = static_cast<const qint_t<W>&>(*this)[i];
        b_bits[i] = b[i];
    }

    // Allocate quotient and remainder registers (W qubits each, all |0>).
    int quot_idx[W];
    int rem_idx[W];
    qbool quot_bits[W];
    qbool rem_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        quot_idx[i]  = QubitPool::instance().allocate();
        rem_idx[i]   = QubitPool::instance().allocate();
        quot_bits[i] = qbool::make_non_owning(quot_idx[i]);
        rem_bits[i]  = qbool::make_non_owning(rem_idx[i]);
    }

    lib_div_dsl(a_bits, W, b_bits, W, quot_bits, rem_bits);

    // Release old 'this' qubits.
    for (std::size_t i = 0; i < W; ++i) {
        if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
    }
    // Assign quotient qubits to 'this'.
    for (std::size_t i = 0; i < W; ++i) {
        qubits[i] = quot_idx[i];
    }
    // Release remainder qubits.
    for (std::size_t i = 0; i < W; ++i) {
        QubitPool::instance().release(rem_idx[i]);
    }

    // Classical value: division by zero → 0.
    value = (b.value != 0) ? (value / b.value) : 0;
    return *this;
}

// ── operator%= ────────────────────────────────────────────────────────────────
// Out-of-place: remainder_bits = this % b, then move remainder into 'this'.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator%=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value = (b.value != 0) ? (value % b.value) : 0;
        return *this;
    }

    qbool a_bits[W];
    qbool b_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_bits[i] = static_cast<const qint_t<W>&>(*this)[i];
        b_bits[i] = b[i];
    }

    // Allocate remainder register (W qubits, all |0>).
    int rem_idx[W];
    qbool rem_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        rem_idx[i]  = QubitPool::instance().allocate();
        rem_bits[i] = qbool::make_non_owning(rem_idx[i]);
    }

    lib_mod_dsl(a_bits, W, b_bits, W, rem_bits);

    // Release old 'this' qubits.
    for (std::size_t i = 0; i < W; ++i) {
        if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
    }
    // Assign remainder qubits to 'this'.
    for (std::size_t i = 0; i < W; ++i) {
        qubits[i] = rem_idx[i];
    }

    // Classical value.
    value = (b.value != 0) ? (value % b.value) : 0;
    return *this;
}

} // namespace sturm
