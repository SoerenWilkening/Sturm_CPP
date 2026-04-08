// exec_simulate.cpp — M10/M11/M12: SIMULATE executor implementation.
//
// Implements exec_simulate_1q (M10), dispatching X/Y/Z/H/S/T/P/Rx/Ry/Rz to
// the corresponding orkan::apply_* functions.
//
// M11 (2/3-qubit gates) and M12 (CRx/CRy/CRz decomposition) extend this file.

#include "sturm/backend/exec_simulate.hpp"

#include <stdexcept>

namespace sturm {

// ── exec_simulate_1q ─────────────────────────────────────────────────────────

void exec_simulate_1q(orkan::state_t&   sv,
                      sturm_gate_kind_t kind,
                      uint32_t          qubit,
                      double            param) {
    switch (kind) {
        case STURM_GATE_X:
            orkan::apply_x(sv, qubit);
            break;
        case STURM_GATE_Y:
            orkan::apply_y(sv, qubit);
            break;
        case STURM_GATE_Z:
            orkan::apply_z(sv, qubit);
            break;
        case STURM_GATE_H:
            orkan::apply_h(sv, qubit);
            break;
        case STURM_GATE_S:
            orkan::apply_s(sv, qubit);
            break;
        case STURM_GATE_T:
            orkan::apply_t(sv, qubit);
            break;
        case STURM_GATE_P:
            orkan::apply_p(sv, qubit, param);
            break;
        case STURM_GATE_RX:
            orkan::apply_rx(sv, qubit, param);
            break;
        case STURM_GATE_RY:
            orkan::apply_ry(sv, qubit, param);
            break;
        case STURM_GATE_RZ:
            orkan::apply_rz(sv, qubit, param);
            break;
        default:
            throw std::invalid_argument(
                "exec_simulate_1q: unsupported gate kind (not a 1-qubit gate)");
    }
}

} // namespace sturm
