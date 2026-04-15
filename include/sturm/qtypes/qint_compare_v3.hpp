// qint_compare_v3.hpp -- M19 (PRD v3): qint_t<W> comparison operator bodies.
// Included by qint_compare.hpp when STURM_BACKEND_ENABLED is defined.
// Target: <150 LoC.
#pragma once
#ifndef STURM_BACKEND_ENABLED
#  error "qint_compare_v3.hpp must only be included when STURM_BACKEND_ENABLED is set"
#endif

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/lib/compare_dsl.hpp"
#include <array>
#include <cstddef>

namespace sturm {
namespace detail {

// Promote a classical qbool bit-view to quantum (allocate qubit + X if val=1).
// Used inside WHEN to ensure DSL functions receive valid qubit indices.
inline void promote_qbool_if_classical(qbool& q, sturm_backend_context_t* ctx) {
    if (q.qubits[0] >= 0 || current_control == nullptr) return;
    q.qubits[0] = QubitPool::instance().allocate();
    q.super_mask = 1ULL;
    q.owning_ = true;
    if (q.value && ctx)
        emit_X_lifted(*ctx, static_cast<uint32_t>(q.qubits[0]));
}

// ── bit_array_view ──────────────────────────────────────────────────────────
// Hoisted bit-extraction helper (sturm-999i Phase D).
//
// Build a std::array<qbool, W> of per-bit views over `q`, performing the same
// classical-to-quantum promotion used by `make_dsl_compare_result` so that
// every DSL-library consumer sees the exact same bit-view shape regardless of
// whether it is the forward comparison or its adjoint. Callers must pass the
// active BackendContext (obtained via sturm_get_thread_context()).
//
// The returned array aliases `q`'s qubits (each view is non-owning). Callers
// must not use the views after `q` is destroyed; lifetime of the views is
// tied to the array, which is a local on the caller's stack.
template <std::size_t W>
inline std::array<qbool, W> bit_array_view(const qint_t<W>& q,
                                            sturm_backend_context_t* ctx) {
    std::array<qbool, W> bits;
    for (std::size_t i = 0; i < W; ++i) {
        bits[i] = q[i];
        promote_qbool_if_classical(bits[i], ctx);
    }
    return bits;
}

// Build bit-view arrays and call compare_dsl; stamp COMPARE uncompute tag.
template <std::size_t W, typename DslFn>
inline qbool make_dsl_compare_result(
        const qint_t<W>& a, const qint_t<W>& b,
        bool classical_val, uint32_t cmp_sub_kind, DslFn dsl_fn) {
    const bool is_quantum = (mask_compare(a.super_mask, b.super_mask) != 0)
                             && (a.qubits[0] >= 0) && (b.qubits[0] >= 0);
    // Classical fast-path (M14: bypass inside WHEN).
    if (!is_quantum && current_control == nullptr) {
        qbool result;
        result.value      = classical_val ? 1 : 0;
        result.super_mask = (mask_compare(a.super_mask, b.super_mask) != 0) ? 1ULL : 0ULL;
        if (result.super_mask) result.ensure_qubit();
        result.uncompute_ = uncompute_op::make_compare(
            reinterpret_cast<const qint_base*>(static_cast<const void*>(&a)),
            reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
            cmp_sub_kind);
        return result;
    }
    // Quantum path: allocate result qubit, build bit-views, call DSL.
    qbool result;
    result.qubits[0]  = QubitPool::instance().allocate();
    result.owning_    = true;
    result.value      = classical_val ? 1 : 0;
    result.super_mask = 1ULL;
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = bit_array_view<W>(a, ctx);
    auto b_bits = bit_array_view<W>(b, ctx);
    dsl_fn(a_bits.data(), b_bits.data(), W, result);
    result.uncompute_ = uncompute_op::make_compare(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&a)),
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
        cmp_sub_kind);
    return result;
}
} // namespace detail

template <std::size_t W>
qbool qint_t<W>::operator==(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value == b.value, detail::CMP_EQ,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) { lib_eq_dsl(a, bb, n, r); });
}
template <std::size_t W>
qbool qint_t<W>::operator!=(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value != b.value, detail::CMP_NEQ,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) { lib_ne_dsl(a, bb, n, r); });
}
template <std::size_t W>
qbool qint_t<W>::operator<(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value < b.value, detail::CMP_LT,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) { lib_lt_dsl(a, bb, n, r); });
}
template <std::size_t W>
qbool qint_t<W>::operator<=(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value <= b.value, detail::CMP_LE,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) { lib_le_dsl(a, bb, n, r); });
}
template <std::size_t W>
qbool qint_t<W>::operator>(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value > b.value, detail::CMP_GT,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) { lib_gt_dsl(a, bb, n, r); });
}
template <std::size_t W>
qbool qint_t<W>::operator>=(const qint_t<W>& b) const {
    return detail::make_dsl_compare_result<W>(
        *this, b, value >= b.value, detail::CMP_GE,
        [](qbool* a, qbool* bb, std::size_t n, qbool& r) { lib_ge_dsl(a, bb, n, r); });
}

} // namespace sturm
