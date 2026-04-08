// qint_base.cpp — Non-inline implementations of qint_base::sub_const and
// qint_base::add_const.
//
// These were originally inline in qint_base.hpp but are moved here so that
// frontend test targets (which include qint.hpp but do not link execute_gate)
// can compile without undefined-reference errors.
//
// Design: the implementations call sturm::execute_gate which lives in
// src/sturm/core/execute_gate.cpp.  Backend test targets link both files;
// frontend test targets do not use these methods so the symbol is never needed
// at link time for them.

#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"   // BackendContext, execute_gate
#include "sturm/core/gate_kind.h"

namespace sturm {

// ── sub_const ──────────────────────────────────────────────────────────────────
// Emit STURM_GATE_X on each quantum bit as a placeholder for subtraction.
// TODO(backend): replace with actual constant subtraction circuit (Draper/ripple).

void qint_base::sub_const(int64_t c, BackendContext& ctx) const noexcept {
    for (uint8_t i = 0; i < width && i < QINT_BASE_MAX_WIDTH; ++i) {
        if ((super_mask >> i) & 1u) {
            uint32_t q[1] = {qubits[i]};
            double param = static_cast<double>(c);
            execute_gate(ctx, STURM_GATE_X, q, 1u, param);
        }
    }
}

// ── add_const ──────────────────────────────────────────────────────────────────
// Emit STURM_GATE_H on each quantum bit as a placeholder for addition.
// TODO(backend): replace with actual constant addition circuit (Draper/ripple).

void qint_base::add_const(int64_t c, BackendContext& ctx) const noexcept {
    for (uint8_t i = 0; i < width && i < QINT_BASE_MAX_WIDTH; ++i) {
        if ((super_mask >> i) & 1u) {
            uint32_t q[1] = {qubits[i]};
            double param = static_cast<double>(c);
            execute_gate(ctx, STURM_GATE_H, q, 1u, param);
        }
    }
}

// ── compare_forward ────────────────────────────────────────────────────────────
// Emit STURM_GATE_CX on each superposed bit, carrying cmp_kind as param, as a
// placeholder for the forward comparison circuit.
// TODO(backend): replace with an actual comparator (ancilla fanout) — M22+.

void qint_base::compare_forward(uint32_t cmp_kind,
                                BackendContext& ctx) const noexcept {
    for (uint8_t i = 0; i < width && i < QINT_BASE_MAX_WIDTH; ++i) {
        if ((super_mask >> i) & 1u) {
            uint32_t q[1] = {qubits[i]};
            execute_gate(ctx, STURM_GATE_CX, q, 1u,
                         static_cast<double>(cmp_kind));
        }
    }
}

// ── compare_inverse ────────────────────────────────────────────────────────────
// Emit STURM_GATE_CX with negated cmp_kind param as the inverse stub.
// CX is self-inverse; using a negated param lets tests distinguish forward from
// inverse in the IR record stream without a dedicated gate kind.
// TODO(backend): replace with the actual uncomputation circuit — M22+.

void qint_base::compare_inverse(uint32_t cmp_kind,
                                BackendContext& ctx) const noexcept {
    for (uint8_t i = 0; i < width && i < QINT_BASE_MAX_WIDTH; ++i) {
        if ((super_mask >> i) & 1u) {
            uint32_t q[1] = {qubits[i]};
            execute_gate(ctx, STURM_GATE_CX, q, 1u,
                         -static_cast<double>(cmp_kind));
        }
    }
}

} // namespace sturm
