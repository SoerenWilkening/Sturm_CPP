#pragma once
// qint_arith.hpp — Arithmetic operators for qint_t<Width>.
// Step 6 Module C, spec §3, Implementation Plan §6.
//
// Defines: operator+  operator-  operator*  operator/  operator%
//          unary operator-
//          compound assigns += -= *= /= %=
//          free pow(qint, qint) and pow(qint, int64_t)
//
// Each operator body is a thin call into dispatch_binary / dispatch_unary.
// Must be included after qint_core.hpp (via qint.hpp umbrella).

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/dispatch.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/counter_sink.hpp"

#include <cstdint>
#include <vector>

namespace sturm {

// ── classical pow helper ──────────────────────────────────────────────────────
namespace detail {
inline int64_t classical_pow(int64_t base, int64_t exp) noexcept {
    if (exp < 0) return 0;
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}
} // namespace detail

// ── operator+ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x + y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_addsub(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_add(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator- (binary) ────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x - y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_addsub(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_sub(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── unary operator- ───────────────────────────────────────────────────────────
// Computed as 0 - a. Dispatched as quantum_sub(empty_vec, a.qubits_vec, ctrl).

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a) {
    return detail::dispatch_unary<qint_t<W>, qint_t<W>>(
        a,
        [](int64_t x) noexcept { return -x; },
        [](uint64_t ma) noexcept {
            return detail::mask_addsub(ma, 0ULL, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_sub(std::vector<int>{}, aa.qubits_vec(), ctrl);
        });
}

// ── operator* ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator*(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return x * y; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_mul(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator/ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator/(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return y != 0 ? x / y : 0; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_div(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── operator% ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator%(const qint_t<W>& a, const qint_t<W>& b) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        a, b,
        [](int64_t x, int64_t y) noexcept { return y != 0 ? x % y : 0; },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_mod(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

// ── pow (free function) ───────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> pow(const qint_t<W>& base, const qint_t<W>& exp) {
    return detail::dispatch_binary<qint_t<W>, qint_t<W>, qint_t<W>>(
        base, exp,
        [](int64_t b, int64_t e) noexcept { return detail::classical_pow(b, e); },
        [](uint64_t ma, uint64_t mb) noexcept {
            return detail::mask_muldiv(ma, mb, static_cast<int>(W));
        },
        [](const qint_t<W>& aa, const qint_t<W>& bb,
           const qint_t<W>& /*out*/, int ctrl) {
            current_sink()->quantum_pow(aa.qubits_vec(), bb.qubits_vec(), ctrl);
        });
}

template <std::size_t W>
qint_t<W> pow(const qint_t<W>& base, int64_t exp) {
    qint_t<W> e(exp);
    return pow(base, e);
}

// ── compound assign definitions ───────────────────────────────────────────────
// Defined here after the free binary operators are visible.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator+=(const qint_t<W>& b) {
    *this = *this + b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator-=(const qint_t<W>& b) {
    *this = *this - b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator*=(const qint_t<W>& b) {
    *this = *this * b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator/=(const qint_t<W>& b) {
    *this = *this / b; return *this;
}

template <std::size_t W>
qint_t<W>& qint_t<W>::operator%=(const qint_t<W>& b) {
    *this = *this % b; return *this;
}

} // namespace sturm
