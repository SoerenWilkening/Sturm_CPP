// c_quantum_xor.hpp — M23: Controlled quantum XOR (c_quantum_xor).
//
// Implements the controlled variant of bitwise-XOR on two qubit registers.
// Uncontrolled quantum_xor emits CX(a[i], b[i]) per bit (XOR of a into b).
// This controlled variant emits CCX(ctrl, a[i], b[i]) instead, so the
// XOR only fires when the ctrl qubit is |1⟩.
//
// Gate translation (PRD §10):
//   uncontrolled: CX(a[i], b[i])        for i in [0, n)
//   controlled:   CCX(ctrl, a[i], b[i]) for i in [0, n)
//
// The CCX gate is in the 18-gate primitive set (PRD §4), so this controlled
// variant stays within the primitive set and requires no ancilla.
//
// LOC budget: < 60 (this file).

#pragma once

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>

namespace sturm {

// ── c_quantum_xor ─────────────────────────────────────────────────────────────
//
// Controlled XOR: for each bit i, emit CCX(ctrl, a[i], b[i]).
// Result: b[i] ^= (ctrl == |1⟩) ? a[i] : 0.
//
// Parameters:
//   ctx        — active BackendContext.
//   ctrl_qubit — physical qubit index of the control qubit.
//   a_qubits   — array of n physical qubit indices (source register).
//   b_qubits   — array of n physical qubit indices (target/output register).
//   n          — number of bits.

inline void c_quantum_xor(BackendContext& ctx,
                           uint32_t        ctrl_qubit,
                           const uint32_t* a_qubits,
                           const uint32_t* b_qubits,
                           uint8_t         n) {
    for (uint8_t i = 0; i < n; ++i) {
        // CCX(ctrl, a[i], b[i]): arity 3.
        uint32_t qs[3] = {ctrl_qubit, a_qubits[i], b_qubits[i]};
        execute_gate(ctx, STURM_GATE_CCX, qs, 3u, 0.0);
    }
}

} // namespace sturm
