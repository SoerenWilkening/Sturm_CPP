// qint_bitwise_v3.hpp — M19 (PRD v3): Wire qint_t<W> bitwise compound-assigns
// to DSL logic library functions. Included by qint_bitwise.hpp when STURM_BACKEND_ENABLED.
//
// Operators defined here:
//   operator^=(const qint_t& b) — per-bit CNOT: this[i] ^= b[i] for each bit i.
//   operator&=(const qint_t& b) — out-of-place AND: result[i] ^= (a[i] & b[i]),
//                                  then move result back into 'this'.
//   operator|=(const qint_t& b) — out-of-place OR: result[i] ^= (a[i] | b[i]),
//                                  then move result back into 'this'.
//
// Note: operator~() is defined in qint_bitwise.hpp (non-compound, returns new qint).
// The existing compound-assign bodies in qint_bitwise.hpp are guarded with
// #ifndef STURM_BACKEND_ENABLED; this file provides the v3 replacements.
//
// XOR (^=) is in-place: no ancilla, just per-bit CNOT.
// AND (&=) and OR (|=) are out-of-place: allocate result register, compute,
//   then release old 'this' qubits and move result in.
//
// Target: <150 LoC.

#pragma once

#ifndef STURM_BACKEND_ENABLED
#  error "qint_bitwise_v3.hpp must only be included when STURM_BACKEND_ENABLED is set"
#endif

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cstddef>

namespace sturm {

// ── operator^= ────────────────────────────────────────────────────────────────
// Per-bit CNOT: this[i] ^= b[i] for each bit i (in-place).
// Emits exactly W CX gates (one per bit, regardless of classical value).

template <std::size_t W>
qint_t<W>& qint_t<W>::operator^=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value ^= b.value;
        return *this;
    }
    for (std::size_t i = 0; i < W; ++i) {
        // Get non-owning qbools for bit i.
        qbool this_bit = static_cast<const qint_t<W>&>(*this)[i];
        qbool b_bit    = b[i];
        // Per-bit CNOT: this[i] ^= b[i].
        this_bit ^= b_bit;
    }
    // Update classical value.
    value ^= b.value;
    return *this;
}

// ── operator&= ────────────────────────────────────────────────────────────────
// Out-of-place AND: allocate result register, compute result[i] ^= (a[i] & b[i]),
// then move result into 'this'.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator&=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value &= b.value;
        return *this;
    }

    // Allocate result register: W qubits, all starting |0>.
    int res_idx[W];
    qbool res_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        res_idx[i]  = QubitPool::instance().allocate();
        res_bits[i] = qbool::make_non_owning(res_idx[i]);
    }

    // Compute result[i] ^= (this[i] & b[i]) per bit.
    for (std::size_t i = 0; i < W; ++i) {
        qbool a_bit = static_cast<const qint_t<W>&>(*this)[i];
        qbool b_bit = b[i];
        // Toffoli: result[i] ^= (a[i] & b[i]) via AndExpr.
        res_bits[i] ^= (a_bit & b_bit);
    }

    // Move result into 'this': release old qubits, assign result qubits.
    for (std::size_t i = 0; i < W; ++i) {
        if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
    }
    for (std::size_t i = 0; i < W; ++i) {
        qubits[i] = res_idx[i];
    }

    // Classical value.
    value &= b.value;
    return *this;
}

// ── operator|= ────────────────────────────────────────────────────────────────
// Out-of-place OR: allocate result register, compute result[i] ^= (a[i] | b[i]),
// then move result into 'this'.
// Uses OrExpr path: CNOT(a,r) + CNOT(b,r) + Toffoli(a,b,r) per bit = 3 gates/bit.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator|=(const qint_t<W>& b) {
    // Classical fast-path.
    if (qubits[0] < 0 || b.qubits[0] < 0) {
        value |= b.value;
        return *this;
    }

    // Allocate result register: W qubits, all starting |0>.
    int res_idx[W];
    qbool res_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        res_idx[i]  = QubitPool::instance().allocate();
        res_bits[i] = qbool::make_non_owning(res_idx[i]);
    }

    // Compute result[i] ^= (this[i] | b[i]) per bit via OrExpr.
    for (std::size_t i = 0; i < W; ++i) {
        qbool a_bit = static_cast<const qint_t<W>&>(*this)[i];
        qbool b_bit = b[i];
        // OrExpr: CNOT(a,r) + CNOT(b,r) + Toffoli(a,b,r).
        res_bits[i] ^= (a_bit | b_bit);
    }

    // Move result into 'this'.
    for (std::size_t i = 0; i < W; ++i) {
        if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
    }
    for (std::size_t i = 0; i < W; ++i) {
        qubits[i] = res_idx[i];
    }

    // Classical value.
    value |= b.value;
    return *this;
}

} // namespace sturm
