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

} // namespace sturm
