// c_quantum_not.hpp — M23: Controlled quantum NOT (c_quantum_not).
//
// Implements the controlled variant of bitwise-NOT on a qubit register.
// Uncontrolled quantum_not emits X(bit) for each bit in the target register.
// This controlled variant emits CX(ctrl, bit) instead, so the flip only
// fires when the ctrl qubit is |1⟩.
//
// Gate translation (PRD §10):
//   uncontrolled: X(tgt[i])       for i in [0, n)
//   controlled:   CX(ctrl, tgt[i]) for i in [0, n)
//
// This function takes a physical qubit index for ctrl and an array of
// physical qubit indices for the target register.  It emits gates directly
// through execute_gate on the supplied BackendContext — it is agnostic to
// mode (COUNT_ONLY / APPEND / SIMULATE).
//
// LOC budget: < 60 (this file).

#pragma once

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>

namespace sturm {

// ── c_quantum_not ─────────────────────────────────────────────────────────────
//
// Controlled NOT: for each bit in target_qubits[0..n), emit CX(ctrl, tgt[i]).
//
// Parameters:
//   ctx           — active BackendContext; gates are counted / recorded / simulated.
//   ctrl_qubit    — physical qubit index of the control qubit.
//   target_qubits — array of n physical qubit indices (one per register bit).
//   n             — number of target bits.
//
// This is a pure gate-emission function; it does not perform any classical
// value manipulation or super_mask tracking.  Callers are responsible for
// those bookkeeping tasks at the Layer A / dispatch level.

inline void c_quantum_not(BackendContext& ctx,
                           uint32_t        ctrl_qubit,
                           const uint32_t* target_qubits,
                           uint8_t         n) {
    for (uint8_t i = 0; i < n; ++i) {
        // CX(ctrl, tgt[i]): arity 2, qubits = [ctrl, tgt[i]], param = 0.
        uint32_t qs[2] = {ctrl_qubit, target_qubits[i]};
        execute_gate(ctx, STURM_GATE_CX, qs, 2u, 0.0);
    }
}

} // namespace sturm
