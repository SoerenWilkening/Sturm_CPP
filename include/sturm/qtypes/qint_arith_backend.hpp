#pragma once
// qint_arith_backend.hpp — Backend-enabled arithmetic operator bodies for
// qint_t<Width>.  Included by qint_arith.hpp when STURM_BACKEND_ENABLED is set.
//
// Contains the STURM_BACKEND_ENABLED bodies of:
//   operator+(qint, int64_t) and symmetric
//   operator+(qint, qint)
//   operator-(qint, qint)
//   operator*(qint, qint)
//   operator/(qint, qint)
//   operator%(qint, qint)
//
// Must be included after qint_core.hpp and uncompute_op.hpp are visible.
// Do not include this header directly — include qint_arith.hpp instead.

#ifdef STURM_BACKEND_ENABLED

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/context.hpp"

#include <cstdint>

namespace sturm {

// ── operator+(qint_t, int64_t) — M21 pilot: uncompute-wired constant add ──────
//
// Returns a new qint_t<W> representing (a + c).  The result carries
// uncompute_op = ADD_CONST(c) so that when the result is destroyed its
// destructor emits the inverse (-= c) via the active BackendContext.
//
// Gate emission:
//   - If a is fully classical (super_mask == 0), returns a classical result
//     with no gates emitted (fast path).
//   - Otherwise calls qint_base::add_const(c, ctx) on a view of the result
//     register to emit the forward gate sequence via the backend context.
//
// Bennett discipline: `a` is never modified; the result is a fresh register.
//
// TODO(backend): replace add_const stub with a full Draper/ripple-carry
//                adder circuit when the library-op layer lands (M22+).

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, int64_t c) {
    qint_t<W> result;
    result.value      = a.value + c;
    result.super_mask = a.super_mask;
    // Copy qubit indices (result shares the register structure for the stub).
    // TODO(backend): full Bennett requires allocating a fresh register and
    //                running the adder circuit into it; for the pilot test the
    //                stub emits gates on the same qubit set so the gate
    //                sequence is deterministic and invertible.
    result.qubits = a.qubits;

    if (a.super_mask != 0) {
        // Emit add_const gates via the active BackendContext (if any).
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            qint_base view = result.as_qint_base();
            view.add_const(c, *ctx);
        }
    }

    // Stamp the uncompute op: destroying `result` will emit -= c.
    result.uncompute_ = uncompute_op::make_add_const(c);
    return result;
}

// Symmetric: operator+(int64_t, qint_t<W>)
template <std::size_t W>
qint_t<W> operator+(int64_t c, const qint_t<W>& a) {
    return a + c;
}

// ── operator+ (qint + qint) ───────────────────────────────────────────────────
//
// M22 backend-enabled version: stamps ADD_QINT on the result so its destructor
// emits the inverse (subtraction) via the active BackendContext.

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = a.value + b.value;
    result.super_mask = detail::mask_addsub(a.super_mask, b.super_mask,
                                            static_cast<int>(W));
    result.qubits     = a.qubits;   // stub: share qubit set; TODO(backend): fresh register

    if ((a.super_mask | b.super_mask) != 0) {
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            // TODO(backend): emit actual adder circuit (Draper/ripple-carry) — M22+
            (void)ctx;
        }
    }
    // Stamp ADD_QINT so the destructor subtracts b from self.
    result.uncompute_ = uncompute_op::make_add_qint(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator- (binary, qint - qint) ──────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = a.value - b.value;
    result.super_mask = detail::mask_addsub(a.super_mask, b.super_mask,
                                            static_cast<int>(W));
    result.qubits     = a.qubits;   // stub: share; TODO(backend): fresh register

    if ((a.super_mask | b.super_mask) != 0) {
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            // TODO(backend): emit subtraction circuit — M22+
            (void)ctx;
        }
    }
    // Stamp SUB_QINT so the destructor adds b back to self.
    result.uncompute_ = uncompute_op::make_sub_qint(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator* ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator*(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = a.value * b.value;
    result.super_mask = detail::mask_muldiv(a.super_mask, b.super_mask,
                                            static_cast<int>(W));
    result.qubits     = a.qubits;   // stub; TODO(backend): fresh register

    if ((a.super_mask | b.super_mask) != 0) {
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            // TODO(backend): emit multiplication circuit — M22+
            (void)ctx;
        }
    }
    result.uncompute_ = uncompute_op::make_mul_inverse(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator/ ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator/(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = (b.value != 0) ? a.value / b.value : 0;
    result.super_mask = detail::mask_muldiv(a.super_mask, b.super_mask,
                                            static_cast<int>(W));
    result.qubits     = a.qubits;   // stub; TODO(backend): fresh register

    if ((a.super_mask | b.super_mask) != 0) {
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            // TODO(backend): emit division circuit — M22+
            (void)ctx;
        }
    }
    result.uncompute_ = uncompute_op::make_div_inverse(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator% ─────────────────────────────────────────────────────────────────

template <std::size_t W>
qint_t<W> operator%(const qint_t<W>& a, const qint_t<W>& b) {
    qint_t<W> result;
    result.value      = (b.value != 0) ? a.value % b.value : 0;
    result.super_mask = detail::mask_muldiv(a.super_mask, b.super_mask,
                                            static_cast<int>(W));
    result.qubits     = a.qubits;   // stub; TODO(backend): fresh register

    if ((a.super_mask | b.super_mask) != 0) {
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            // TODO(backend): emit modulo circuit — M22+
            (void)ctx;
        }
    }
    result.uncompute_ = uncompute_op::make_mod_inverse(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
