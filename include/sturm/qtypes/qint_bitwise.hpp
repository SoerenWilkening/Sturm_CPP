#pragma once
// qint_bitwise.hpp — Bitwise operators for qint_t<Width>.
// Step 6 Module D, spec §3, Implementation Plan §6.
//
// Defines: operator& operator| operator^ operator~
//          compound assigns &= |= ^=
//          operator<< operator>> compound <<=  >>=
//
// Must be included after qint_core.hpp (via qint.hpp umbrella).
//
// Backend AND/OR operator bodies are in qint_bitwise_backend.hpp, included below
// when STURM_BACKEND_ENABLED is set.
// Backend shift operator bodies (<< >> <<= >>=) are in qint_shift_backend.hpp,
// included below when STURM_BACKEND_ENABLED is set.

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/dispatch.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/counter_sink.hpp"
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/backend/primitives.hpp"
#  include "sturm/core/qubit_pool.hpp"
#  include "sturm/qtypes/qbool_ops.hpp"
#  include "sturm/qtypes/lazy_expr.hpp"
#endif

namespace sturm {

// ── Bitwise sub-kind constants for uncompute_op::BITWISE_SELF ────────────────
// Used as the `sub_kind` field so that apply() knows which op to re-run.
namespace detail {
    static constexpr uint32_t BITWISE_AND = 1u;
    static constexpr uint32_t BITWISE_OR  = 2u;
    static constexpr uint32_t BITWISE_XOR = 3u;
    static constexpr uint32_t BITWISE_NOT = 4u;
} // namespace detail

// ── operator& ─────────────────────────────────────────────────────────────────
// Backend-enabled body is in qint_bitwise_backend.hpp (included below).
// Non-backend body uses dispatch_binary.

#ifndef STURM_BACKEND_ENABLED

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

#endif  // !STURM_BACKEND_ENABLED

// ── operator| ─────────────────────────────────────────────────────────────────
// Backend-enabled body is in qint_bitwise_backend.hpp (included below).
// Non-backend body uses dispatch_binary.

#ifndef STURM_BACKEND_ENABLED

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

#endif  // !STURM_BACKEND_ENABLED

// ── operator^ ─────────────────────────────────────────────────────────────────

#ifdef STURM_BACKEND_ENABLED

template <std::size_t W>
qint_t<W> operator^(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = a.value ^ b.value;
    result.super_mask = detail::mask_bitwise(a.super_mask, b.super_mask);
    result.qubits     = a.qubits;   // stub; TODO(backend): fresh register

    if ((a.super_mask | b.super_mask) != 0) {
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            // TODO(backend): emit XOR circuit — M22+
            (void)ctx;
        }
    }
    result.uncompute_ = uncompute_op::make_bitwise_self(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
        detail::BITWISE_XOR);
    return result;
}

#else  // !STURM_BACKEND_ENABLED

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

#endif  // STURM_BACKEND_ENABLED

// ── operator~ ─────────────────────────────────────────────────────────────────

#ifdef STURM_BACKEND_ENABLED

template <std::size_t W>
qint_t<W> operator~(const qint_t<W>& a) {
    qint_t<W> result;
    result.value      = ~a.value;
    result.super_mask = detail::mask_not(a.super_mask);

    // Allocate a fresh result register (W qubits, all |0>).
    for (std::size_t i = 0; i < W; ++i) {
        result.qubits[i] = QubitPool::instance().allocate();
    }

    // Flip each bit in the result register (X gate per qubit) when a context
    // is installed (quantum execution context present).
    if (sturm_get_thread_context()) {
        for (std::size_t i = 0; i < W; ++i) {
            qbool bit = static_cast<const qint_t<W>&>(result)[i];
            bit.flip();
        }
    }

    // Unary NOT: re-run NOT on the result to get back to original (self-inverse).
    result.uncompute_ = uncompute_op::make_bitwise_self(nullptr, detail::BITWISE_NOT);
    return result;
}

#else  // !STURM_BACKEND_ENABLED

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

#endif  // STURM_BACKEND_ENABLED

// ── operator<< / operator>> ───────────────────────────────────────────────────
// Backend-enabled bodies are in qint_shift_backend.hpp (included below).
// Non-backend (dispatch_shift) bodies are defined here.

#ifndef STURM_BACKEND_ENABLED

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

#endif  // !STURM_BACKEND_ENABLED

// ── compound assign definitions ───────────────────────────────────────────────
// When STURM_BACKEND_ENABLED is set, &=, |=, ^= are defined in
// qint_bitwise_v3.hpp (DSL per-bit operators). <<= and >>= are defined in
// qint_shift_backend.hpp. Otherwise use free operator path below.

#ifndef STURM_BACKEND_ENABLED

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

#endif  // !STURM_BACKEND_ENABLED

} // namespace sturm

// ── Backend-enabled bitwise compound assign bodies (DSL logic) ────────────────
// When STURM_BACKEND_ENABLED is set, &=, |=, ^= call DSL logic per bit.
// Must come BEFORE backend so that detail_bw::make_b_mut and
// detail_bw::release_temp_qubits are available to the free operators.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_bitwise_v3.hpp"
#endif

// ── Backend-enabled AND/OR operator bodies ────────────────────────────────────
// operator& and operator| (Toffoli / OR-DSL circuits) are defined here when backend.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_bitwise_backend.hpp"
#endif

// ── Backend-enabled shift operator bodies ────────────────────────────────────
// operator<< operator>> operator<<= operator>>= are defined here when backend.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_shift_backend.hpp"
#endif
