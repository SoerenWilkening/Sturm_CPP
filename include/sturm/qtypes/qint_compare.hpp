#pragma once
// qint_compare.hpp — Comparison operators and bit subscript for qint_t<Width>.
// Step 6 Module E, spec §3, Implementation Plan §6.
//
// Defines: == != < <= > >=  (all return qbool)
//          operator[](size_t i) — returns a qbool view of bit i
//
// Must be included after qint_core.hpp (via qint.hpp umbrella).

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

// ── operator[] — bit subscript ────────────────────────────────────────────────
// Returns a qbool that shares qubits[i].
// If bit i is superposed (super_mask has bit i set), the result is superposed
// and the qubit index is shared (not a copy).
// The value is the classical bit value at position i.

template <std::size_t W>
qbool qint_t<W>::operator[](std::size_t i) const {
    qbool out;
    out.value    = ((value >> static_cast<int>(i)) & 1) != 0;
    out.is_super = (super_mask & (1ULL << i)) != 0;
    // Share the qubit index (not owned by this qbool — the qint still owns it).
    // The caller must not destroy the source qint while using this qbool.
    out.qubits[0] = (i < W) ? qubits[i] : -1;
    return out;
}

} // namespace sturm
