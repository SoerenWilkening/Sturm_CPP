// exec_simulate.hpp — M10: SIMULATE executor, 1-qubit gates.
//
// Declares exec_simulate_1q: dispatch a 1-qubit gate to the Orkan statevector.
//
// For 2/3-qubit gates (M11) and CRx/CRy/CRz decomposition (M12), the same
// translation unit is extended; those entry points will be added there.
//
// PRD §5, §12: X/Y/Z/H/S/T/P/Rx/Ry/Rz map 1:1 to the corresponding Orkan
// apply_* functions.  Non-1-qubit gates are NOT handled here; callers must not
// pass a multi-qubit kind to exec_simulate_1q.
//
// M11: exec_simulate_multiq handles CX/CY/CZ/CCX/SWAP.
//   qubit0 = first qubit (ctrl for 2-qubit gates, ctrl0 for 3-qubit).
//   qubit1 = second qubit (tgt for 2-qubit gates, ctrl1 for 3-qubit).
//   qubit2 = third qubit (tgt for 3-qubit gates; unused for 2-qubit, pass 0).
//   param  = unused for permutation/phase gates.

#pragma once

#include "sturm/core/gate_kind.h"

#ifdef ORKAN_USING_STUB
#  include "orkan/orkan.hpp"
#else
#  include "orkan/orkan.hpp"
#endif

#include <cstdint>
#include <stdexcept>

namespace sturm {

// ── exec_simulate_1q ─────────────────────────────────────────────────────────
//
// Apply the gate identified by `kind` to `qubit` in the statevector `sv`.
//
// Parameters:
//   sv    — Orkan state; mutated in-place.
//   kind  — one of STURM_GATE_{X,Y,Z,H,S,T,P,RX,RY,RZ}.
//   qubit — physical qubit index; must be < sv.n_qubits.
//   param — rotation angle θ (radians) for P/Rx/Ry/Rz; unused for others.
//
// Throws std::invalid_argument for unsupported (non-1-qubit) gate kinds.
// Gate dispatch is implemented in exec_simulate.cpp.

void exec_simulate_1q(orkan::state_t& sv,
                      sturm_gate_kind_t kind,
                      uint32_t          qubit,
                      double            param);

// ── exec_simulate_multiq ──────────────────────────────────────────────────────
//
// Apply a 2- or 3-qubit gate to the statevector `sv`.
//
// Parameters:
//   sv     — Orkan state; mutated in-place.
//   kind   — one of STURM_GATE_{CX,CY,CZ,CCX,SWAP}.
//   qubit0 — first qubit operand:
//              CX/CY/CZ/SWAP: first qubit (ctrl for CX/CY/CZ; q0 for SWAP)
//              CCX: ctrl0
//   qubit1 — second qubit operand:
//              CX/CY/CZ: target qubit
//              SWAP: second qubit (q1)
//              CCX: ctrl1
//   qubit2 — third qubit operand (CCX target); pass 0 for 2-qubit gates.
//   param  — unused; pass 0.0.
//
// Throws std::invalid_argument for unsupported gate kinds.

void exec_simulate_multiq(orkan::state_t&   sv,
                          sturm_gate_kind_t kind,
                          uint32_t          qubit0,
                          uint32_t          qubit1,
                          uint32_t          qubit2,
                          double            param);

// ── exec_simulate_crot ────────────────────────────────────────────────────────
//
// M12: Decompose CRx/CRy/CRz into CX + single-qubit rotation sequences at
// call time (Orkan has no native controlled rotations).
//
// Decomposition identity (axis-specific, standard controlled-rotation identity):
//   CRx(θ): CX(ctrl,tgt); Rx(-θ/2)(tgt); CX(ctrl,tgt); Rx(+θ/2)(tgt)
//   CRy(θ): CX(ctrl,tgt); Ry(-θ/2)(tgt); CX(ctrl,tgt); Ry(+θ/2)(tgt)
//   CRz(θ): CX(ctrl,tgt); Rz(-θ/2)(tgt); CX(ctrl,tgt); Rz(+θ/2)(tgt)
//
// PRD §5: decomposition is performed only in SIMULATE mode; COUNT_ONLY and
// APPEND see the original gate kind (enforced in M13).
//
// Parameters:
//   sv    — Orkan state; mutated in-place.
//   kind  — one of STURM_GATE_CRX, STURM_GATE_CRY, STURM_GATE_CRZ.
//   ctrl  — physical index of the control qubit.
//   tgt   — physical index of the target qubit.
//   theta — rotation angle θ (radians).
//
// Throws std::invalid_argument if kind is not CRX/CRY/CRZ.

void exec_simulate_crot(orkan::state_t&   sv,
                        sturm_gate_kind_t kind,
                        uint32_t          ctrl,
                        uint32_t          tgt,
                        double            theta);

} // namespace sturm
