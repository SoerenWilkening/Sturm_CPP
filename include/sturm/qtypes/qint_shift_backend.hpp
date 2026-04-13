#pragma once
// qint_shift_backend.hpp — Backend-enabled shift operator bodies for
// qint_t<Width>.  Included by qint_bitwise.hpp when STURM_BACKEND_ENABLED is set.
//
// Contains the STURM_BACKEND_ENABLED bodies of:
//   operator<<(qint_t<W>, int)  — left shift via CNOT-copy of qubit indices
//   operator>>(qint_t<W>, int)  — right shift via CNOT-copy of qubit indices
//   operator<<=(int)            — in-place left shift via qubit relabeling
//   operator>>=(int)            — in-place right shift via qubit relabeling
//
// Pattern: same as qint_arith_backend.hpp — operator bodies that require
// QubitPool/primitives only when STURM_BACKEND_ENABLED is active.
//
// Do not include this header directly — include qint_bitwise.hpp instead.

#ifdef STURM_BACKEND_ENABLED

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/control/when_fwd.hpp"         // current_control TLS
#include "sturm/backend/primitives.hpp"

#include <cstddef>

namespace sturm {

// ── operator<< ────────────────────────────────────────────────────────────────
//
// Backend-enabled: allocates W fresh qubits for the result; emits CNOT(a[i],
// result[i+n]) for each non-vacated bit (bits 0..n-1 stay |0>).  This
// correctly copies the quantum state with the left-shift applied.

template <std::size_t W>
qint_t<W> operator<<(const qint_t<W>& a, int n) {
    qint_t<W> result;
    result.value = (n >= 0 && n < 64) ? (a.value << n) : 0;
    result.super_mask = detail::mask_shl(a.super_mask, n, static_cast<int>(W));

    // Classical fast-path: no valid qubit indices or shift >= W.
    // Bypassed inside WHEN (current_control != nullptr) so that classical
    // operands get promoted to quantum with CNOT-based copy gates.
    if ((a.qubits[0] < 0 && detail::current_control == nullptr)
        || n <= 0 || n >= static_cast<int>(W)) {
        if (n <= 0 && a.qubits[0] >= 0) {
            // n==0: copy all bits via CNOT.
            for (std::size_t i = 0; i < W; ++i) {
                result.qubits[i] = QubitPool::instance().allocate();
            }
            if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                for (std::size_t i = 0; i < W; ++i) {
                    if (a.qubits[i] >= 0 && result.qubits[i] >= 0) {
                        primitive_XOR(*ctx,
                                      static_cast<uint32_t>(a.qubits[i]),
                                      static_cast<uint32_t>(result.qubits[i]));
                    }
                }
            }
        }
        return result;
    }

    // Allocate W fresh qubits for the result (all start |0>).
    for (std::size_t i = 0; i < W; ++i) {
        result.qubits[i] = QubitPool::instance().allocate();
    }

    // Emit CNOT to copy bits: result.qubits[i+n] ^= a.qubits[i] for i in 0..W-n-1.
    if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
        int shift = n;
        for (int i = 0; i < static_cast<int>(W) - shift; ++i) {
            if (a.qubits[i] >= 0 && result.qubits[i + shift] >= 0) {
                primitive_XOR(*ctx,
                              static_cast<uint32_t>(a.qubits[i]),
                              static_cast<uint32_t>(result.qubits[i + shift]));
            }
        }
    }

    return result;
}

// ── operator>> ────────────────────────────────────────────────────────────────
//
// Backend-enabled: allocates W fresh qubits for the result; emits CNOT(a[i+n],
// result[i]) for each non-vacated bit (bits W-n..W-1 stay |0>).

template <std::size_t W>
qint_t<W> operator>>(const qint_t<W>& a, int n) {
    qint_t<W> result;
    result.value = (n >= 0 && n < 64) ? (a.value >> n) : 0;
    result.super_mask = detail::mask_shr(a.super_mask, n, static_cast<int>(W));

    // Classical fast-path (bypassed inside WHEN so CNOT copy gates are emitted).
    if ((a.qubits[0] < 0 && detail::current_control == nullptr)
        || n <= 0 || n >= static_cast<int>(W)) {
        if (n <= 0 && a.qubits[0] >= 0) {
            // n==0: copy all bits via CNOT.
            for (std::size_t i = 0; i < W; ++i) {
                result.qubits[i] = QubitPool::instance().allocate();
            }
            if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                for (std::size_t i = 0; i < W; ++i) {
                    if (a.qubits[i] >= 0 && result.qubits[i] >= 0) {
                        primitive_XOR(*ctx,
                                      static_cast<uint32_t>(a.qubits[i]),
                                      static_cast<uint32_t>(result.qubits[i]));
                    }
                }
            }
        }
        return result;
    }

    // Allocate W fresh qubits for the result.
    for (std::size_t i = 0; i < W; ++i) {
        result.qubits[i] = QubitPool::instance().allocate();
    }

    // Emit CNOT to copy bits: result.qubits[i] ^= a.qubits[i+n] for i in 0..W-n-1.
    if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
        int shift = n;
        for (int i = 0; i < static_cast<int>(W) - shift; ++i) {
            if (a.qubits[i + shift] >= 0 && result.qubits[i] >= 0) {
                primitive_XOR(*ctx,
                              static_cast<uint32_t>(a.qubits[i + shift]),
                              static_cast<uint32_t>(result.qubits[i]));
            }
        }
    }

    return result;
}

// ── operator<<= (backend) ─────────────────────────────────────────────────────
// Pure qubit relabeling: zero gates. Shifts qubit indices in-place.
// - Release qubits that shift out (positions W-n..W-1).
// - Move remaining qubits up: qubits[i] = qubits[i-n] for i = W-1..n.
// - Allocate fresh |0> qubits for vacated positions 0..n-1.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator<<=(int n) {
    if (n <= 0) { if (n < 0) { /* no-op */ } return *this; }
    if (qubits[0] < 0 && detail::current_control == nullptr) {
        // Classical fast-path (bypassed inside WHEN for quantum promotion).
        value = (n < 64) ? (value << n) : 0;
        return *this;
    }
    // Inside WHEN with classical operand: allocate qubits for all bits so
    // the relabeling below operates on valid qubit indices.
    if (qubits[0] < 0) {
        for (std::size_t i = 0; i < W; ++i) {
            qubits[i] = QubitPool::instance().allocate();
        }
    }
    if (n >= static_cast<int>(W)) {
        // All bits shift out — release all qubits and return 0.
        for (std::size_t i = 0; i < W; ++i) {
            if (qubits[i] >= 0) {
                QubitPool::instance().release(qubits[i]);
                qubits[i] = -1;
            }
        }
        value = 0;
        return *this;
    }
    // Release qubits that shift out (top n positions).
    for (int i = static_cast<int>(W) - n; i < static_cast<int>(W); ++i) {
        if (qubits[i] >= 0) {
            QubitPool::instance().release(qubits[i]);
            qubits[i] = -1;
        }
    }
    // Shift qubit indices up by n.
    for (int i = static_cast<int>(W) - 1; i >= n; --i) {
        qubits[i] = qubits[i - n];
    }
    // Allocate fresh |0> qubits for the lower n positions.
    for (int i = 0; i < n; ++i) {
        qubits[i] = QubitPool::instance().allocate();
    }
    value = (n < 64) ? (value << n) : 0;
    return *this;
}

// ── operator>>= (backend) ─────────────────────────────────────────────────────
// Pure qubit relabeling: zero gates. Shifts qubit indices in-place.
// - Release qubits that shift out (positions 0..n-1).
// - Move remaining qubits down: qubits[i] = qubits[i+n] for i = 0..W-n-1.
// - Allocate fresh |0> qubits for vacated positions W-n..W-1.

template <std::size_t W>
qint_t<W>& qint_t<W>::operator>>=(int n) {
    if (n <= 0) { if (n < 0) { /* no-op */ } return *this; }
    if (qubits[0] < 0 && detail::current_control == nullptr) {
        // Classical fast-path (bypassed inside WHEN for quantum promotion).
        value = (n < 64) ? (value >> n) : 0;
        return *this;
    }
    // Inside WHEN with classical operand: allocate qubits for all bits so
    // the relabeling below operates on valid qubit indices.
    if (qubits[0] < 0) {
        for (std::size_t i = 0; i < W; ++i) {
            qubits[i] = QubitPool::instance().allocate();
        }
    }
    if (n >= static_cast<int>(W)) {
        // All bits shift out.
        for (std::size_t i = 0; i < W; ++i) {
            if (qubits[i] >= 0) {
                QubitPool::instance().release(qubits[i]);
                qubits[i] = -1;
            }
        }
        value = 0;
        return *this;
    }
    // Release qubits that shift out (bottom n positions).
    for (int i = 0; i < n; ++i) {
        if (qubits[i] >= 0) {
            QubitPool::instance().release(qubits[i]);
            qubits[i] = -1;
        }
    }
    // Shift qubit indices down by n.
    for (int i = 0; i < static_cast<int>(W) - n; ++i) {
        qubits[i] = qubits[i + n];
    }
    // Allocate fresh |0> qubits for the upper n positions.
    for (int i = static_cast<int>(W) - n; i < static_cast<int>(W); ++i) {
        qubits[i] = QubitPool::instance().allocate();
    }
    value = (n < 64) ? (value >> n) : 0;
    return *this;
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
