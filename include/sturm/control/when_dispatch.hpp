// when_dispatch.hpp — M24: Public op entry-point dispatcher.
//
// Provides production dispatch wrappers that public ops call instead of
// directly invoking the controlled (c_*) or uncontrolled variants.  Each
// wrapper inspects WhenGuard::active_control():
//
//   active_control() == nullptr  →  call uncontrolled op variant
//   active_control() != nullptr  →  call corresponding c_* variant, passing
//                                    the control qubit through
//
// This ensures that any op called inside a WHEN scope automatically uses its
// controlled counterpart without the call site knowing about the WHEN context.
//
// Available wrappers:
//   when_dispatch_not(target, ctx)  — routes to X-per-bit or c_quantum_not
//   when_dispatch_xor(a, b, ctx)    — routes to CX-per-bit or c_quantum_xor
//   when_dispatch_add(a, out, ctx)  — TODO(backend): routes to quantum_add_stub
//                                     or c_quantum_add_stub
//
// Depends on:
//   when.hpp         — WhenGuard::active_control()
//   qint_base.hpp    — qint_base (target/operand type)
//   c_quantum_not.hpp, c_quantum_xor.hpp, c_quantum_add.hpp — c_* variants
//   gate_kind.h      — STURM_GATE_X, STURM_GATE_CX gate constants
//
// LOC budget: < 120 (this file).

#pragma once

#include "sturm/control/when.hpp"           // WhenGuard::active_control()
#include "sturm/uncompute/qint_base.hpp"    // qint_base
#include "sturm/core/context.hpp"           // BackendContext, execute_gate
#include "sturm/core/gate_kind.h"           // STURM_GATE_X, STURM_GATE_CX
#include "sturm/ops/c_quantum_not.hpp"      // c_quantum_not
#include "sturm/ops/c_quantum_xor.hpp"      // c_quantum_xor
#include "sturm/ops/c_quantum_add.hpp"      // quantum_add_stub, c_quantum_add_stub

#include <cstdint>

namespace sturm {

// ── when_dispatch_not ─────────────────────────────────────────────────────────
//
// Entry-point dispatcher for the NOT (bitwise-NOT) operation.
//
// Uncontrolled path (active_control == nullptr):
//   Emit X(target.qubits[i]) for each bit i where target.super_mask bit i is set.
//
// Controlled path (active_control != nullptr):
//   Call c_quantum_not(ctrl_qubit, target_qubits, n) which emits
//   CX(ctrl, target.qubits[i]) per superposed bit i.
//
// Only bits where target.super_mask is set carry physical qubit indices;
// classical bits (super_mask bit clear) are skipped by convention.

inline void when_dispatch_not(qint_base& target, BackendContext& ctx) {
    const qbool* ctrl = WhenGuard::active_control();

    if (ctrl == nullptr) {
        // Uncontrolled: emit X per superposed target bit.
        for (uint8_t i = 0; i < target.width; ++i) {
            if ((target.super_mask >> i) & 1u) {
                uint32_t qs[1] = {target.qubits[i]};
                execute_gate(ctx, STURM_GATE_X, qs, 1u, 0.0);
            }
        }
    } else {
        // Controlled: emit CX(ctrl, target.qubits[i]) per superposed target bit.
        // Gather the superposed target qubit indices.
        uint32_t tgt_qs[QINT_BASE_MAX_WIDTH];
        uint8_t  n = 0;
        for (uint8_t i = 0; i < target.width; ++i) {
            if ((target.super_mask >> i) & 1u) {
                tgt_qs[n++] = target.qubits[i];
            }
        }
        if (n > 0) {
            uint32_t ctrl_q = static_cast<uint32_t>(ctrl->qubits[0]);
            c_quantum_not(ctx, ctrl_q, tgt_qs, n);
        }
    }
}

// ── when_dispatch_xor ─────────────────────────────────────────────────────────
//
// Entry-point dispatcher for the XOR operation (b ^= a).
//
// Uncontrolled path: emit CX(a.qubits[i], b.qubits[i]) per bit where both
//   a and b have superposed bits (classical bits are skipped).
//
// Controlled path: call c_quantum_xor(ctrl, a_qubits, b_qubits, n) which emits
//   CCX(ctrl, a[i], b[i]) per bit.
//
// Precondition: a.width == b.width.

inline void when_dispatch_xor(const qint_base& a, qint_base& b, BackendContext& ctx) {
    const qbool* ctrl = WhenGuard::active_control();

    // Collect matching superposed bit pairs.
    uint32_t a_qs[QINT_BASE_MAX_WIDTH];
    uint32_t b_qs[QINT_BASE_MAX_WIDTH];
    uint8_t  n = 0;

    uint8_t w = (a.width < b.width) ? a.width : b.width;
    for (uint8_t i = 0; i < w; ++i) {
        if (((a.super_mask >> i) & 1u) && ((b.super_mask >> i) & 1u)) {
            a_qs[n] = a.qubits[i];
            b_qs[n] = b.qubits[i];
            ++n;
        }
    }

    if (n == 0) return;

    if (ctrl == nullptr) {
        // Uncontrolled: CX(a[i], b[i]).
        for (uint8_t i = 0; i < n; ++i) {
            uint32_t qs[2] = {a_qs[i], b_qs[i]};
            execute_gate(ctx, STURM_GATE_CX, qs, 2u, 0.0);
        }
    } else {
        uint32_t ctrl_q = static_cast<uint32_t>(ctrl->qubits[0]);
        c_quantum_xor(ctx, ctrl_q, a_qs, b_qs, n);
    }
}

// ── when_dispatch_add ─────────────────────────────────────────────────────────
//
// Entry-point dispatcher for the ADD operation (out += a).
//
// TODO(backend): Replace stubs with a full Vedral ripple-carry adder.
//   Uncontrolled path: quantum_add_stub(a_qubits, out_qubits, n)
//   Controlled path:   c_quantum_add_stub(ctrl_qubit, 1, a_qubits, out_qubits, n)

inline void when_dispatch_add(const qint_base& a, qint_base& out, BackendContext& ctx) {
    const qbool* ctrl = WhenGuard::active_control();

    uint32_t a_qs[QINT_BASE_MAX_WIDTH];
    uint32_t out_qs[QINT_BASE_MAX_WIDTH];
    uint8_t  n = 0;

    uint8_t w = (a.width < out.width) ? a.width : out.width;
    for (uint8_t i = 0; i < w; ++i) {
        if (((a.super_mask >> i) & 1u) && ((out.super_mask >> i) & 1u)) {
            a_qs[n]   = a.qubits[i];
            out_qs[n] = out.qubits[i];
            ++n;
        }
    }

    if (n == 0) return;

    if (ctrl == nullptr) {
        quantum_add_stub(ctx, a_qs, out_qs, n);
    } else {
        uint32_t ctrl_q = static_cast<uint32_t>(ctrl->qubits[0]);
        c_quantum_add_stub(ctx, ctrl_q, 1, a_qs, out_qs, n);
    }
}

} // namespace sturm
