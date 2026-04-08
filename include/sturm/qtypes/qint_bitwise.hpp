#pragma once
// qint_bitwise.hpp — Bitwise operators for qint_t<Width>.
// Step 6 Module D, spec §3, Implementation Plan §6.
//
// Defines: operator& operator| operator^ operator~
//          compound assigns &= |= ^=
//          operator<< operator>> compound <<=  >>=
//
// Must be included after qint_core.hpp (via qint.hpp umbrella).

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/dispatch.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/counter_sink.hpp"

namespace sturm {

// ── operator& ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator&(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x & y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_bitwise(ma, mb);
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_and(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator| ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator|(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x | y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_bitwise(ma, mb);
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_or(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator^ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator^(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x ^ y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_bitwise(ma, mb);
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_xor(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator~ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator~(const qint_t<W>& a) {
    return detail::dispatch_unary<qint_t<W>, qint_t<W>>(
        a,
        [](int64_t x) noexcept { return ~x; },
        [](uint64_t ma) noexcept { return detail::mask_not(ma); },
        [](const qint_t<W>& aa, const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_not(aa.qubits_vec(), ctrl);
        });
}

// ── operator<< ────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator<<(const qint_t<W>& a, int n) {
    return detail::dispatch_shift<qint_t<W>, qint_t<W>>(
        a, n,
        [](int64_t x, int s) noexcept {
            return (s >= 0 && s < 64) ? (x << s) : 0;
        },
        [](uint64_t ma, int s) noexcept {
            return detail::mask_shl(ma, s, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& /*out*/, int /*n*/, int ctrl) {
            current_sink()->quantum_shl(aa.qubits_vec(), {}, ctrl);
        });
}

// ── operator>> ────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator>>(const qint_t<W>& a, int n) {
    return detail::dispatch_shift<qint_t<W>, qint_t<W>>(
        a, n,
        [](int64_t x, int s) noexcept {
            return (s >= 0 && s < 64) ? (x >> s) : 0;
        },
        [](uint64_t ma, int s) noexcept {
            return detail::mask_shr(ma, s, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& /*out*/, int /*n*/, int ctrl) {
            current_sink()->quantum_shr(aa.qubits_vec(), {}, ctrl);
        });
}

// ── compound assign definitions ───────────────────────────────────────────────

template <std::size_t W>
qint_t<W>& qint_t<W>::operator&=(const qint_t<W>& b) {
    *this = *this & b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator|=(const qint_t<W>& b) {
    *this = *this | b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator^=(const qint_t<W>& b) {
    *this = *this ^ b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator<<=(int n) {
    *this = *this << n; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator>>=(int n) {
    *this = *this >> n; return *this;
}

} // namespace sturm
