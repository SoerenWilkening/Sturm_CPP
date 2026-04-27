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
// Must be included after qint_core.hpp is visible.
// Do not include this header directly — include qint_arith.hpp instead.
//
// Phase K PK-3 (sturm-pzye): uncompute_op tagged union retired — forward
// operators no longer stamp an `uncompute_` field on the result. Inverse
// gate streams are emitted by transpiler-synthesised `uncompute_*` free
// functions (see include/sturm/uncompute/uncompute_api.hpp). Destructors
// are release-only per principle B10.

#ifdef STURM_BACKEND_ENABLED

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/mask_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/control/when_fwd.hpp"         // current_control TLS
#include "sturm/backend/primitives.hpp"
#include "sturm/detail/lib/mod_dsl.hpp"

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

    // Allocate fresh qubits only where the source has them (quantum bits).
    // Classical bits (qubits[i] == -1) are left unallocated; BitProxy will
    // handle lazy promotion if needed downstream.
    for (std::size_t i = 0; i < W; ++i) {
        if (a.qubits[i] >= 0) {
            result.qubits[i] = QubitPool::instance().allocate();
        }
        // else: leave result.qubits[i] = -1 (classical, BitProxy will handle)
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
    // Note: owning_ defaults to true on result; we intentionally share qubit
    // indices with `a` here under the same stub discipline as pre-PK-3 — the
    // add_const stub emitted gates on the same register rather than a fresh
    // one. `result.owning_` is set to false so we don't double-release with
    // `a`.
    result.qubits  = a.qubits;
    result.owning_ = false;

    if (a.super_mask != 0) {
        // Emit add_const gates via the active BackendContext (if any).
        // Phase K PK-3: this is the pre-existing stub (one X per quantum
        // bit with c as param), inlined here since qint_base::add_const has
        // been retired. TODO(backend): replace with a Draper/ripple-carry
        // constant-adder circuit when the library-op layer lands (M22+).
        if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
            for (std::size_t i = 0; i < W; ++i) {
                if (((result.super_mask >> i) & 1u) && result.qubits[i] >= 0) {
                    uint32_t q[1] = { static_cast<uint32_t>(result.qubits[i]) };
                    double param = static_cast<double>(c);
                    execute_gate(*ctx, STURM_GATE_H, q, 1u, param);
                }
            }
        }
    }

    // Phase K PK-3: inverse emission is now the transpiler's responsibility
    // (see uncompute_api.hpp uncompute_add_qint). Destructor is release-only.
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
// Uncomputation is the transpiler's responsibility (Phase C): sturm-transpile
// injects `uncompute_add_qint(result, b);` before the enclosing scope closes.

template <std::size_t W>
qint_t<W> operator+(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path: if neither operand has quantum qubits and no WHEN
    // scope is active, skip circuits.  Inside WHEN the fast-path is bypassed so
    // the compound-assign (which uses BitProxy) handles per-bit promotion.
    if ((a.qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
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

    return result;
}

// ── operator- (binary, qint - qint) ──────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result -= b.
// Uncomputation is the transpiler's responsibility (Phase C): sturm-transpile
// injects `uncompute_sub_qint(result, b);` before the enclosing scope closes.

template <std::size_t W>
qint_t<W> operator-(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path (bypassed inside WHEN so BitProxy handles promotion).
    if ((a.qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
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

    return result;
}

// ── operator* ─────────────────────────────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result *= b.
// operator*= internally allocates a 2W product register, releases the copy
// qubits, and remaps result.qubits to the lower W product bits.
// Uncomputation is the transpiler's responsibility (Phase C): sturm-transpile
// injects `uncompute_mul_qint(result, b);` before the enclosing scope closes.

template <std::size_t W>
qint_t<W> operator*(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path (bypassed inside WHEN so BitProxy handles promotion).
    if ((a.qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
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

    return result;
}

// ── operator/ ─────────────────────────────────────────────────────────────────
//
// sturm-6qo: copy a into fresh register, then result /= b.
// operator/= allocates quotient and remainder registers, releases copy qubits,
// and remaps result.qubits to the quotient register.
// Uncomputation is the transpiler's responsibility (Phase C): sturm-transpile
// injects `uncompute_div_qint(result, b);` before the enclosing scope closes.

template <std::size_t W>
qint_t<W> operator/(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path (bypassed inside WHEN so BitProxy handles promotion).
    if ((a.qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
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
// Uncomputation is the transpiler's responsibility (Phase C): sturm-transpile
// injects `uncompute_mod_qint(result, b);` before the enclosing scope closes.
// `uncompute_mod_qint` ships with a stub body today (no clean dual).

template <std::size_t W>
qint_t<W> operator%(const qint_t<W>& a, const qint_t<W>& b) {
    // Classical fast-path (bypassed inside WHEN so BitProxy handles promotion).
    if ((a.qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
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

    return result;
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
