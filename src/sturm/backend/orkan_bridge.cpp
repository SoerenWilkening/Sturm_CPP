// orkan_bridge.cpp — M9: OrkanBridge implementation.
//
// Thin wrapper around the Orkan statevector API.  All Orkan calls are
// concentrated here; the rest of STURM never includes orkan headers directly.

#include "sturm/backend/orkan_bridge.hpp"

#include <complex>
#include <stdexcept>

namespace sturm {

// ── allocate ─────────────────────────────────────────────────────────────────

void OrkanBridge::allocate(uint32_t n) {
    if (n > kMaxQubits) {
        throw std::invalid_argument(
            "OrkanBridge::allocate: n exceeds STURM_MAX_QUBITS (17)");
    }
    orkan::allocate(state_, n);
}

// ── reset_zero ───────────────────────────────────────────────────────────────

void OrkanBridge::reset_zero() {
    orkan::reset_zero(state_);
}

// ── num_qubits ────────────────────────────────────────────────────────────────

uint32_t OrkanBridge::num_qubits() const {
    return state_.n_qubits;
}

// ── probability_of_zero_state ─────────────────────────────────────────────────

double OrkanBridge::probability_of_zero_state() const {
    if (state_.n_qubits == 0u) return 0.0;
    std::complex<double> a0 = orkan::amplitude(state_, 0u);
    return a0.real() * a0.real() + a0.imag() * a0.imag();
}

// ── apply_x ───────────────────────────────────────────────────────────────────

void OrkanBridge::apply_x(uint32_t qubit) {
    orkan::apply_x(state_, qubit);
}

} // namespace sturm
