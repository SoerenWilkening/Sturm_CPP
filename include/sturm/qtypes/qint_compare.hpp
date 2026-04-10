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

// Compare sub-kind constants for uncompute_op::COMPARE.
static constexpr uint32_t CMP_EQ  = 1u;
static constexpr uint32_t CMP_NEQ = 2u;
static constexpr uint32_t CMP_LT  = 3u;
static constexpr uint32_t CMP_LE  = 4u;
static constexpr uint32_t CMP_GT  = 5u;
static constexpr uint32_t CMP_GE  = 6u;
} // namespace detail

// ── Helper: stamp COMPARE tag onto a qbool result ────────────────────────────
// Only compiled when STURM_BACKEND_ENABLED is set.

#ifdef STURM_BACKEND_ENABLED
namespace detail {
template <std::size_t W>
inline qbool make_compare_result(
        const qint_t<W>& a, const qint_t<W>& b,
        bool classical_val, uint32_t cmp_sub_kind) {
    qbool out;
    out.value    = classical_val;
    out.is_super = (detail::mask_compare(a.super_mask, b.super_mask) != 0);

    // Build a qint_base view of 'a' for forward gate emission and for the
    // COMPARE uncompute record.  The view pointer stored in uncompute_ is
    // intentionally to the local snapshot — it must not be dereferenced after
    // the enclosing scope; for the stub this is fine because apply() is called
    // from the qbool destructor while the outer qints are still alive (Bennett).
    // TODO(backend): allocate a proper ancilla qubit and wire the full comparator
    //                circuit (ancilla fanout) — M22+.
    // Stamp the COMPARE uncompute op so the qbool destructor can emit the
    // compare circuit and its inverse (Bennett uncomputation).
    // The apply(COMPARE) case emits both forward and inverse in sequence so
    // that the full compare-then-uncompute circuit is recorded in the IR.
    // TODO(backend): store typed lhs/rhs pointers to enable full comparator
    //                circuit uncomputation when ancilla wiring lands (M22+).
    out.uncompute_ = uncompute_op::make_compare(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&a)),
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)),
        cmp_sub_kind);
    return out;
}
} // namespace detail
#endif  // STURM_BACKEND_ENABLED

// ── operator== ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator==(const qint_t<W>& b) const {
#ifdef STURM_BACKEND_ENABLED
    return detail::make_compare_result<W>(*this, b, value == b.value, detail::CMP_EQ);
#else
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x == y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_eq(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
#endif
}

// ── operator!= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator!=(const qint_t<W>& b) const {
#ifdef STURM_BACKEND_ENABLED
    return detail::make_compare_result<W>(*this, b, value != b.value, detail::CMP_NEQ);
#else
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x != y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_neq(aa.qubits_vec(), bb.qubits_vec(),
                                        out.qubits[0], ctrl);
        });
#endif
}

// ── operator< ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator<(const qint_t<W>& b) const {
#ifdef STURM_BACKEND_ENABLED
    return detail::make_compare_result<W>(*this, b, value < b.value, detail::CMP_LT);
#else
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x < y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_lt(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
#endif
}

// ── operator<= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator<=(const qint_t<W>& b) const {
#ifdef STURM_BACKEND_ENABLED
    return detail::make_compare_result<W>(*this, b, value <= b.value, detail::CMP_LE);
#else
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x <= y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_le(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
#endif
}

// ── operator> ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator>(const qint_t<W>& b) const {
#ifdef STURM_BACKEND_ENABLED
    return detail::make_compare_result<W>(*this, b, value > b.value, detail::CMP_GT);
#else
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x > y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_gt(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
#endif
}

// ── operator>= ────────────────────────────────────────────────────────────────

template <std::size_t W>
qbool qint_t<W>::operator>=(const qint_t<W>& b) const {
#ifdef STURM_BACKEND_ENABLED
    return detail::make_compare_result<W>(*this, b, value >= b.value, detail::CMP_GE);
#else
    return detail::dispatch_compare<qint_t<W>>(
        *this, b,
        [](int64_t x, int64_t y) noexcept { return x >= y; },
        [](uint64_t ma, uint64_t mb) noexcept { return detail::mask_compare(ma, mb); },
        [](const qint_t<W>& aa, const qint_t<W>& bb, qbool& out, int ctrl) {
            current_sink()->quantum_ge(aa.qubits_vec(), bb.qubits_vec(),
                                       out.qubits[0], ctrl);
        });
#endif
}

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
    out.value    = ((value >> static_cast<int>(i)) & 1) != 0;
    out.is_super = (super_mask & (1ULL << i)) != 0;
    return out;
}

} // namespace sturm
