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
// Pattern (sturm-6qo fix): each free operator allocates a fresh result register,
// copies a's quantum state into it via CNOT, then delegates to the corresponding
// compound-assign operator.  Compound-assign bodies are defined in
// qint_arith_v3.hpp, visible at instantiation time.
//
// Must be included after qint_core.hpp and uncompute_op.hpp are visible.
// Do not include this header directly — include qint_arith.hpp instead.

#ifdef STURM_BACKEND_ENABLED

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/lib/mod_dsl.hpp"

#include <cstdint>

namespace sturm {

// ── copy_register_qubits ──────────────────────────────────────────────────────
//
// Allocates W fresh qubits from the pool and, if a BackendContext is active,
// emits CNOT(a.qubits[i], fresh[i]) for each bit to copy a's quantum state
// into the fresh register.  Returns a qint_t<W> whose qubits are the freshly
// allocated indices and whose classical metadata matches `a`.
//
// The returned register starts in |0...0> and after the CNOTs holds a copy
// of a's state.  Caller is responsible for releasing these qubits (via the
// qint_t<W> destructor or by calling the compound assign which may remap them).

template <std::size_t W>
static qint_t<W> copy_register(const qint_t<W>& a) {
    qint_t<W> result;
    result.value      = a.value;
    result.super_mask = a.super_mask;

    // Allocate W fresh qubits (all start |0>).
    for (std::size_t i = 0; i < W; ++i) {
        result.qubits[i] = QubitPool::instance().allocate();
    }

    // If a backend context is active, copy quantum state via CNOT per bit.
    if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
        for (std::size_t i = 0; i < W; ++i) {
            if (a.qubits[i] >= 0 && result.qubits[i] >= 0) {
                primitive_XOR(*ctx,
                              static_cast<uint32_t>(a.qubits[i]),
                              static_cast<uint32_t>(result.qubits[i]));
            }
        }
    }

    // Leave uncompute_ as default (NONE): result owns its fresh register.
    return result;
}

// ── operator+(qint_t, int64_t) — constant add ────────────────────────────────
//
// Returns a new qint_t<W> representing (a + c).  Fast path: fully classical.
// Otherwise: copy a into fresh register, then result += c (stub via add_const).
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
// sturm-6qo: allocate a fresh result register, copy a's state into it via
// CNOT, then call result += b (lib_add_dsl via qint_arith_v3.hpp).
// Stamps ADD_QINT so the destructor emits result -= b (inverse) when it fires.

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path: if neither operand has quantum qubits, skip circuits.
    if (a.qubits[0] < 0 || b.qubits[0] < 0) {
        qint_t<W> result;
        result.value      = a.value + b.value;
        result.super_mask = detail::mask_addsub(a.super_mask, b.super_mask,
                                                static_cast<int>(W));
        return result;
    }

    // Allocate fresh result register and copy a's quantum state.
    qint_t<W> result = copy_register(a);

    // Delegate to compound assign: result += b (emits adder circuit).
    result += b;

    // Stamp ADD_QINT: destroying result will emit result -= b (Bennett inverse).
    result.uncompute_ = uncompute_op::make_add_qint(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator- (binary, qint - qint) ──────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result -= b.
// Stamps SUB_QINT so the destructor emits result += b (Bennett inverse).

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path.
    if (a.qubits[0] < 0 || b.qubits[0] < 0) {
        qint_t<W> result;
        result.value      = a.value - b.value;
        result.super_mask = detail::mask_addsub(a.super_mask, b.super_mask,
                                                static_cast<int>(W));
        return result;
    }

    // Allocate fresh result register and copy a's quantum state.
    qint_t<W> result = copy_register(a);

    // Delegate to compound assign: result -= b (emits subtractor circuit).
    result -= b;

    // Stamp SUB_QINT: destroying result will emit result += b (Bennett inverse).
    result.uncompute_ = uncompute_op::make_sub_qint(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator* ─────────────────────────────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result *= b.
// operator*= internally allocates a 2W product register, releases the copy
// qubits, and remaps result.qubits to the lower W product bits.
// Stamps MUL_INVERSE (Bennett uncompute tag).

template <std::size_t W>
qint_t<W> operator*(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path.
    if (a.qubits[0] < 0 || b.qubits[0] < 0) {
        qint_t<W> result;
        result.value      = a.value * b.value;
        result.super_mask = detail::mask_muldiv(a.super_mask, b.super_mask,
                                                static_cast<int>(W));
        return result;
    }

    // Allocate fresh result register and copy a's quantum state.
    qint_t<W> result = copy_register(a);

    // Delegate to compound assign: result *= b.
    // operator*= will release the copy qubits and remap to product register.
    result *= b;

    // Stamp MUL_INVERSE: destroying result emits the inverse circuit (Bennett).
    result.uncompute_ = uncompute_op::make_mul_inverse(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator/ ─────────────────────────────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result /= b.
// operator/= allocates quotient and remainder registers, releases copy qubits,
// and remaps result.qubits to the quotient register.
// Stamps DIV_INVERSE (Bennett uncompute tag).

template <std::size_t W>
qint_t<W> operator/(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path.
    if (a.qubits[0] < 0 || b.qubits[0] < 0) {
        qint_t<W> result;
        result.value      = (b.value != 0) ? a.value / b.value : 0;
        result.super_mask = detail::mask_muldiv(a.super_mask, b.super_mask,
                                                static_cast<int>(W));
        return result;
    }

    // Allocate fresh result register and copy a's quantum state.
    qint_t<W> result = copy_register(a);

    // Delegate to compound assign: result /= b.
    result /= b;

    // Stamp DIV_INVERSE: destroying result emits the inverse circuit (Bennett).
    result.uncompute_ = uncompute_op::make_div_inverse(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

// ── operator% ─────────────────────────────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result %= b.
// operator%= allocates quotient and remainder registers, releases copy qubits,
// and remaps result.qubits to the remainder register.
//
// Qubit budget: use W=1 for SIMULATE tests (peak 11 qubits) or W=2 for
// COUNT_ONLY tests (peak ~18 qubits, exceeds OrkanBridge 17-qubit limit).
// See test_free_op_simulate.cpp comment block for the budget breakdown.
//
// Stamps MOD_INVERSE (Bennett uncompute tag).

template <std::size_t W>
qint_t<W> operator%(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path.
    if (a.qubits[0] < 0 || b.qubits[0] < 0) {
        qint_t<W> result;
        result.value      = (b.value != 0) ? a.value % b.value : 0;
        result.super_mask = detail::mask_muldiv(a.super_mask, b.super_mask,
                                                static_cast<int>(W));
        return result;
    }

    // Allocate fresh result register and copy a's quantum state.
    qint_t<W> result = copy_register(a);

    // Delegate to compound assign: result %= b.
    // operator%= will release the copy qubits and remap to remainder register.
    result %= b;

    // Stamp MOD_INVERSE: destroying result emits the inverse circuit (Bennett).
    result.uncompute_ = uncompute_op::make_mod_inverse(
        reinterpret_cast<const qint_base*>(static_cast<const void*>(&b)));
    return result;
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
