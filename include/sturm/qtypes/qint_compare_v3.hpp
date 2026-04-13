// qint_compare_v3.hpp — M19 (PRD v3): qint_t<W> comparison operator bodies.
// Included by qint_compare.hpp when STURM_BACKEND_ENABLED is defined.
//
// Each operator:
//   1. Allocates an output qubit for the result qbool.
//   2. Extracts qbool bit-views via a[i] / b[i] (non-owning, M18).
//   3. Calls the appropriate compare_dsl function to emit the comparison circuit.
//   4. Stamps the COMPARE uncompute tag so the qbool destructor can emit the
//      Bennett inverse when the result goes out of scope.
//
// Derive LE, GT, GE, NE via the corresponding lib_*_dsl functions.
// Target: <150 LoC.

#pragma once

#ifndef STURM_BACKEND_ENABLED
#  error "qint_compare_v3.hpp must only be included when STURM_BACKEND_ENABLED is set"
#endif

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/lib/compare_dsl.hpp"

#include <array>
#include <cstddef>

namespace sturm {

namespace detail {

// Build bit-view arrays and call compare_dsl; stamp COMPARE uncompute tag.
template <std::size_t W, typename DslFn>
inline qbool make_dsl_compare_result(
        const qint_t<W>& a, const qint_t<W>& b,
        bool classical_val, uint32_t cmp_sub_kind,
        DslFn dsl_fn) {
    const bool is_quantum = (detail::mask_compare(a.super_mask, b.super_mask) != 0)
                             && (a.qubits[0] >= 0) && (b.qubits[0] >= 0);

    // Classical fast-path: no DSL emission. M14: bypass when inside WHEN block.
    if (!is_quantum && detail::current_control == nullptr) {
        qbool result;
        result.value      = classical_val ? 1 : 0;
        result.super_mask = (detail::mask_compare(a.super_mask, b.super_mask) != 0) ? 1ULL : 0ULL;
        result.uncompute_ = uncompute_op::make_compare(
            reinterpret_cast<const qint_base*>(static_cast<const void*>(&a)),
            reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
            cmp_sub_kind);
        return result;
    }

    // Quantum path: allocate result qubit, call DSL, stamp tag.
    int result_idx = QubitPool::instance().allocate();
    qbool result;
    result.qubits[0]  = result_idx;
    result.owning_    = true;
    result.value      = classical_val ? 1 : 0;
    result.super_mask = 1ULL;

    // Build non-owning qbool bit-views for both operands.
    std::array<qbool, W> a_bits;
    std::array<qbool, W> b_bits;
    for (std::size_t i = 0; i < W; ++i) {
        a_bits[i] = a[i];
        b_bits[i] = b[i];
    }

    // Call the DSL comparison function to emit the forward circuit.
    dsl_fn(a_bits.data(), b_bits.data(), W, result);

    result.uncompute_ = uncompute_op::make_compare(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&a)),
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
        cmp_sub_kind);

    return result;
}

} // namespace detail

// ── operator== ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator==(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value == b.value, detail::CMP_EQ,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) {
            lib_eq_dsl(a, bb, n, r);
        });
}

// ── operator!= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator!=(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value != b.value, detail::CMP_NEQ,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) {
            lib_ne_dsl(a, bb, n, r);
        });
}

// ── operator< ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator<(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value < b.value, detail::CMP_LT,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) {
            lib_lt_dsl(a, bb, n, r);
        });
}

// ── operator<= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator<=(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value <= b.value, detail::CMP_LE,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) {
            lib_le_dsl(a, bb, n, r);
        });
}

// ── operator> ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator>(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value > b.value, detail::CMP_GT,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) {
            lib_gt_dsl(a, bb, n, r);
        });
}

// ── operator>= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator>=(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value >= b.value, detail::CMP_GE,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) {
            lib_ge_dsl(a, bb, n, r);
        });
}

} // namespace sturm
