#pragma once
// qint_compare.hpp — Comparison operators and bit subscript for qint_t<Width>.
// Step 6 Module E, spec §3, Implementation Plan §6.
//
// Defines: == != < <= > >=  (all return qbool)
//          operator[](size_t i) — returns a qbool view of bit i
//
// Must be included after qint_core.hpp (via qint.hpp umbrella).
//
// M19: When STURM_BACKEND_ENABLED is set, comparison operator bodies that call
// the DSL library (lib_eq_dsl, lib_lt_dsl, etc.) are provided by
// qint_compare_v3.hpp. The non-backend dispatch_compare bodies are here.
//
// Phase D retirement (2026-04-15): the COMPARE uncompute-tag stamping
// infrastructure (make_compare_result, CMP_* sub-kind constants) was removed
// once the transpiler path (uncompute_api.hpp::uncompute_{eq,ne,lt,le,gt,ge}_qint)
// covered qbool-from-qint-comparison uncomputation.

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/dispatch.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/counter_sink.hpp"

#include <cstddef>

namespace sturm {

// ── dispatch_compare mask: any bit set in either operand → superposed result ──

namespace detail {
inline uint64_t mask_compare(uint64_t ma, uint64_t mb) noexcept {
    return ma | mb;  // non-zero means superposed
}
} // namespace detail

// ── Comparison operator bodies (non-backend only) ────────────────────────────
// When STURM_BACKEND_ENABLED is set, bodies are provided by qint_compare_v3.hpp
// (included at the bottom of this file).

#ifndef STURM_BACKEND_ENABLED

// ── operator== ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator==(const qint_t<W>& b) const {
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x == y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_eq(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
}

// ── operator!= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator!=(const qint_t<W>& b) const {
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x != y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_neq(aa.qubits_vec(), bb.qubits_vec(),
                                        out.qubits[0], ctrl);
        });
}

// ── operator< ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator<(const qint_t<W>& b) const {
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x < y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_lt(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
}

// ── operator<= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator<=(const qint_t<W>& b) const {
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x <= y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_le(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
}

// ── operator> ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator>(const qint_t<W>& b) const {
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x > y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_gt(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
}

// ── operator>= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator>=(const qint_t<W>& b) const {
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x >= y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_ge(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
}

#endif  // !STURM_BACKEND_ENABLED

// ── operator[] — bit subscript ────────────────────────────────────────────────
// Returns a non-owning qbool that aliases qubits[i].
// The returned qbool has owning_ = false so its destructor will NOT release
// the qubit back to the pool — the qint_t<W> retains full ownership.
// The value and is_super fields reflect the classical state of bit i.
// The caller must not destroy the source qint while using this qbool.

template <std::size_t W>
qbool qint_t<W>::operator[](std::size_t i) const {
    const int idx = (i < W) ? qubits[i] : -1;
    qbool out = qbool::make_non_owning(idx);
    out.value      = ((value >> static_cast<int>(i)) & 1);
    out.super_mask = (super_mask >> i) & 1ULL;
    return out;
}

} // namespace sturm

// ── Non-const operator[] — returns mutable BitProxy (M2) ────────────────────
// BitProxy writes back to the parent register's qubit, value, and super_mask.
// Only available when STURM_BACKEND_ENABLED is set (BitProxy is backend-only).
#ifdef STURM_BACKEND_ENABLED
#include "sturm/detail/qtypes/bit_proxy.hpp"

namespace sturm {

template <std::size_t W>
BitProxy qint_t<W>::operator[](std::size_t i) {
    return BitProxy(*this, i);
}

} // namespace sturm
#endif  // STURM_BACKEND_ENABLED

// ── Backend-enabled comparison operator bodies (DSL library) ─────────────────
// When STURM_BACKEND_ENABLED is set, provide comparison bodies that call the
// DSL library functions (lib_eq_dsl, lib_lt_dsl, etc.). Phase D retired the
// COMPARE uncompute tag, so these bodies no longer stamp anything — the
// transpiler emits uncompute_{eq,ne,lt,le,gt,ge}_qint at scope exit.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_compare_v3.hpp"
#endif
