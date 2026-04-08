// orkan_bridge.hpp — M9: thin wrapper around orkan::state_t.
//
// OrkanBridge owns a single orkan::state_t pre-allocated to STURM_MAX_QUBITS
// (17) qubits.  It is the only place in STURM that touches the Orkan API
// directly; every other module goes through this bridge.
//
// Exposed in this module (M9) — allocate + reset_zero only:
//
//   void allocate(uint32_t n)     — resize + init to |0…0⟩
//   void reset_zero()             — re-initialize current state to |0…0⟩
//   uint32_t num_qubits() const   — number of qubits currently allocated
//
//   double probability_of_zero_state() const
//                                 — |<0…0|ψ>|^2; used by tests to confirm
//                                   initialization to |0…0⟩
//
//   void apply_x(uint32_t qubit)  — Pauli-X on one qubit; used by tests to
//                                   perturb the state before reset_zero().
//
// Full gate dispatch (X/Y/Z/H/CX/… → Orkan) is added in M10/M11/M12.

#pragma once

#include <cstdint>

// Select the real Orkan header or the local stub depending on what
// OrkanFetch.cmake resolved at configure time.
#ifdef ORKAN_USING_STUB
#  include "orkan/orkan.hpp"          // vendor/orkan/orkan.hpp via vendor/ include path
#else
#  include "orkan/orkan.hpp"          // real Orkan install
#endif

namespace sturm {

// ── Maximum qubits (PRD §6 hardcoded limit) ───────────────────────────────────
static constexpr uint32_t kMaxQubits = 17u;

// ── OrkanBridge ───────────────────────────────────────────────────────────────

class OrkanBridge {
public:
    // Default-constructs without allocating.  Call allocate() before any
    // operation on the statevector.
    OrkanBridge() = default;

    // Non-copyable, non-movable (owns unique Orkan state).
    OrkanBridge(const OrkanBridge&)            = delete;
    OrkanBridge& operator=(const OrkanBridge&) = delete;
    OrkanBridge(OrkanBridge&&)                 = delete;
    OrkanBridge& operator=(OrkanBridge&&)      = delete;

    ~OrkanBridge() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    // Allocate (or reallocate) the statevector for n qubits.
    // n must be <= kMaxQubits (17).
    // Initializes to |0…0⟩.
    void allocate(uint32_t n);

    // Re-initialize the current statevector to |0…0⟩.
    // Requires a prior call to allocate().
    void reset_zero();

    // ── Diagnostics ───────────────────────────────────────────────────────

    // Number of qubits currently allocated.  Returns 0 before allocate().
    uint32_t num_qubits() const;

    // Return |<0…0|ψ>|^2.  Used by tests to confirm |0…0⟩ initialization.
    // Returns 0.0 before allocate().
    double probability_of_zero_state() const;

    // ── Test helpers (exposed only in M9; superseded by full gate API in M10) ─

    // Apply Pauli-X to the given qubit.  Used by tests to perturb the state
    // before verifying reset_zero().
    // Requires a prior call to allocate().
    void apply_x(uint32_t qubit);

    // ── Raw state access (for M10/M11/M12 extensions) ─────────────────────

    // Returns a reference to the underlying Orkan state.
    // TODO(backend): used by M10 exec_simulate to call per-gate Orkan fns.
    orkan::state_t& state() { return state_; }
    const orkan::state_t& state() const { return state_; }

private:
    orkan::state_t state_;
};

} // namespace sturm
