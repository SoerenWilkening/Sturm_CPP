// c_quantum_add.hpp — M23: Controlled quantum addition (c_quantum_add).
//
// Implements the controlled variant of quantum addition on two qubit registers.
//
// Uncontrolled quantum_add: a ripple-carry adder (Vedral et al.) that emits
// CCX and CX gates to compute out += a in-place.  In the current backend
// (Phase 6), the full adder circuit is a TODO(backend) stub; the stub
// emits one CX per output bit to stand in for the forward circuit.
//
// Controlled quantum_add: each gate in the uncontrolled circuit is lifted by
// one control qubit:
//   - CX(a, out) → CCX(ctrl, a, out)
//   - CCX(a, b, out) → hand-coded ancilla decomposition (CCCX)
//
// Since the uncontrolled stub only emits CX gates, the controlled stub
// replaces each CX with a CCX, and the ancilla decomposition path is
// documented but not yet needed.
//
// TODO(backend): Replace stubs with a full Vedral ripple-carry adder and
//               its controlled variant using the ancilla decomposition of
//               CCCX = T/Td/CCX chain (see Selinger 2013, §4).
//
// Public API:
//   quantum_add_stub   — uncontrolled stub (emits 1 CX per output bit)
//   c_quantum_add_stub — controlled stub:
//       ctrl_val == 0 → no gates (classical short-circuit per PRD §7)
//       ctrl_val == 1 → 1 CCX per output bit (CX elevated by ctrl)
//
// LOC budget: < 100 (this file).

#pragma once

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>

namespace sturm {

// ── quantum_add_stub ──────────────────────────────────────────────────────────
//
// Uncontrolled quantum add stub: emits CX(a[i], out[i]) for each bit.
// This is a placeholder for the real ripple-carry adder.
//
// TODO(backend): replace with a Vedral/Draper adder circuit.

inline void quantum_add_stub(BackendContext& ctx,
                               const uint32_t* a_qubits,
                               const uint32_t* out_qubits,
                               uint8_t         n) {
    for (uint8_t i = 0; i < n; ++i) {
        uint32_t qs[2] = {a_qubits[i], out_qubits[i]};
        execute_gate(ctx, STURM_GATE_CX, qs, 2u, 0.0);
    }
}

// ── c_quantum_add_stub ────────────────────────────────────────────────────────
//
// Controlled quantum add stub.
//
// ctrl_val == 0: PRD §7 classical short-circuit — the gate can never fire
//               when the control is classically |0⟩.  No gates are emitted.
//
// ctrl_val == 1: each CX(a[i], out[i]) in the uncontrolled stub is elevated
//               to CCX(ctrl_qubit, a[i], out[i]).
//
// ctrl_val == -1 (quantum ctrl): same as ctrl_val == 1 but the caller indicates
//               the ctrl is in superposition.  The gate sequence is the same —
//               CCX is in the primitive set and handles superposed ctrl natively
//               at the SIMULATE layer.
//
// TODO(backend): When the full adder lands, CCX-in-uncontrolled becomes
//               CCCX which must be decomposed into a CCX ancilla chain.
//               The ancilla-based decomposition of CCCX (Lemma 6.1, Selinger):
//
//               CCCX(c0, c1, c2, t):
//                 ancilla = fresh |0⟩
//                 CCX(c0, c1, ancilla)       // compute AND(c0,c1)
//                 CCX(ancilla, c2, t)        // conditional on c0&c1
//                 CCX(c0, c1, ancilla)       // uncompute ancilla
//
//               This is left as a TODO(backend) pending the full adder.

inline void c_quantum_add_stub(BackendContext& ctx,
                                uint32_t        ctrl_qubit,
                                int             ctrl_val,
                                const uint32_t* a_qubits,
                                const uint32_t* out_qubits,
                                uint8_t         n) {
    // Classical ctrl == 0: gate cannot fire; emit nothing (PRD §7).
    if (ctrl_val == 0) {
        return;
    }

    // Classical ctrl == 1 or quantum ctrl: elevate CX → CCX.
    for (uint8_t i = 0; i < n; ++i) {
        // CCX(ctrl, a[i], out[i]): arity 3.
        uint32_t qs[3] = {ctrl_qubit, a_qubits[i], out_qubits[i]};
        execute_gate(ctx, STURM_GATE_CCX, qs, 3u, 0.0);
    }
}

} // namespace sturm
