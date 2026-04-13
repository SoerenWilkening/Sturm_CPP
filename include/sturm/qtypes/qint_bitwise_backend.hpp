#pragma once
// qint_bitwise_backend.hpp — Backend-enabled AND/OR operator bodies for
// qint_t<Width>.  Included by qint_bitwise.hpp when STURM_BACKEND_ENABLED is set.
//
// Contains the STURM_BACKEND_ENABLED bodies of:
//   operator&(qint_t<W>, qint_t<W>)  — per-bit Toffoli (AND into fresh register)
//   operator|(qint_t<W>, qint_t<W>)  — per-bit OR via qbool DSL
//
// Pattern: same as qint_arith_backend.hpp and qint_shift_backend.hpp — operator
// bodies that require QubitPool/primitives only when STURM_BACKEND_ENABLED is active.
//
// Do not include this header directly — include qint_bitwise.hpp instead.

#ifdef STURM_BACKEND_ENABLED

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/uncompute/uncompute_op.hpp"

#include <cstddef>

namespace sturm {

// ── operator& (backend) ───────────────────────────────────────────────────────
//
// Allocates W fresh result qubits (all |0>); emits per-bit Toffoli so that
// result[i] ^= (a[i] & b[i]).  Self-inverse: AND again restores result to |0>.

template <std::size_t W>
qint_t<W> operator&(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = a.value & b.value;
    result.super_mask = detail::mask_bitwise(a.super_mask, b.super_mask);

    // Allocate W fresh result qubits (all |0>).
    for (std::size_t i = 0; i < W; ++i) {
        result.qubits[i] = QubitPool::instance().allocate();
    }

    // Emit per-bit Toffoli: result[i] ^= (a[i] & b[i]).
    // Uses BitProxy instead of raw qbool::make_non_owning to handle -1 qubit
    // indices (classical bits) via classical folding.
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

    // BITWISE_SELF: AND is self-inverse (AND again with same inputs restores result).
    result.uncompute_ = uncompute_op::make_bitwise_self(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
        detail::BITWISE_AND);
    return result;
}

// ── operator| (backend) ───────────────────────────────────────────────────────
//
// Allocates W fresh result qubits (all |0>); emits per-bit OR via qbool DSL:
// CNOT(a,r) + CNOT(b,r) + Toffoli(a,b,r) per bit.

template <std::size_t W>
qint_t<W> operator|(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = a.value | b.value;
    result.super_mask = detail::mask_bitwise(a.super_mask, b.super_mask);

    // Allocate W fresh result qubits (all |0>).
    for (std::size_t i = 0; i < W; ++i) {
        result.qubits[i] = QubitPool::instance().allocate();
    }

    // Emit per-bit OR: result[i] ^= (a[i] | b[i]) via OrExpr
    // (CNOT(a,r) + CNOT(b,r) + Toffoli(a,b,r) per bit).
    // Uses BitProxy instead of raw qbool::make_non_owning to handle -1 qubit
    // indices (classical bits) via classical folding.
    if ((a.super_mask | b.super_mask) != 0 && sturm_get_thread_context()) {
        auto a_mut = detail_bw::make_b_mut(a);
        auto b_mut = detail_bw::make_b_mut(b);
        for (std::size_t i = 0; i < W; ++i) {
            qbool r_q = qbool::make_non_owning(result.qubits[i]);
            BitProxy r_bit(r_q);
            BitProxy a_bit(a_mut, i);
            BitProxy b_bit(b_mut, i);
            r_bit ^= (a_bit | b_bit);
        }
        detail_bw::release_temp_qubits(a_mut, a);
        detail_bw::release_temp_qubits(b_mut, b);
    }

    result.uncompute_ = uncompute_op::make_bitwise_self(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
        detail::BITWISE_OR);
    return result;
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
