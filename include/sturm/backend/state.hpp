// state.hpp — M1 (PRD v2): simulator state vector and gate application.
//
// SimState owns a small statevector (via the Orkan stub) and exposes the
// minimal interface consumed by the v2 backend primitives and ancilla manager.
//
// Design notes (PRD v2 §2, impl plan M1):
//   - Wraps orkan::state_t directly to stay thin (<300 LoC target).
//   - load_basis(idx) initialises a pure computational-basis state.
//   - amplitude(idx) reads one amplitude for test assertions.
//   - apply_* methods are thin delegates into the Orkan gate set; the public
//     API visible to the rest of the backend is the five primitives in
//     primitives.hpp — not these methods directly.
//
// Maximum qubits: 17 (matches kMaxQubits from orkan_bridge.hpp).

#pragma once

#include <complex>
#include <cstdint>
#include <stdexcept>

// Select the real Orkan header or the local stub depending on what
// OrkanFetch.cmake resolved at configure time.
#ifdef ORKAN_USING_STUB
#  include "orkan/orkan.hpp"
#else
#  include "orkan/orkan.hpp"
#endif

namespace sturm {
namespace v2 {

// ── SimState ──────────────────────────────────────────────────────────────────
//
// Lightweight wrapper around orkan::state_t.  Not copyable (owns the
// statevector).  Construct, call allocate(n), then use via primitives.

class SimState {
public:
    static constexpr uint32_t kMaxQubits = 17u;

    SimState() = default;
    ~SimState() = default;

    // Non-copyable, movable.
    SimState(const SimState&)            = delete;
    SimState& operator=(const SimState&) = delete;
    SimState(SimState&&)                 = default;
    SimState& operator=(SimState&&)      = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    // Allocate (or reallocate) the statevector for n qubits, initialised to
    // |0…0⟩.  n must be > 0 and <= kMaxQubits.
    void allocate(uint32_t n) {
        if (n == 0 || n > kMaxQubits) {
            throw std::invalid_argument("SimState::allocate: n out of range");
        }
        orkan::allocate(sv_, n);
    }

    // Re-initialise the current statevector to |0…0⟩.
    // Requires a prior call to allocate().
    void reset_zero() {
        orkan::reset_zero(sv_);
    }

    // Initialise to a pure computational-basis state |idx⟩.
    // idx must be < 2^n_qubits.
    void load_basis(uint64_t idx) {
        uint64_t dim = uint64_t{1} << sv_.n_qubits;
        if (idx >= dim) {
            throw std::invalid_argument("SimState::load_basis: idx out of range");
        }
        for (auto& a : sv_.amplitudes) a = {0.0, 0.0};
        sv_.amplitudes[idx] = {1.0, 0.0};
    }

    // ── Diagnostics ───────────────────────────────────────────────────────────

    uint32_t num_qubits() const { return sv_.n_qubits; }

    // Read one amplitude (for tests).
    std::complex<double> amplitude(uint64_t idx) const {
        return orkan::amplitude(sv_, idx);
    }

    // ── Gate application (called by primitives.hpp) ───────────────────────────

    void apply_x(uint32_t qubit) {
        orkan::apply_x(sv_, qubit);
    }

    void apply_ry(uint32_t qubit, double theta) {
        orkan::apply_ry(sv_, qubit, theta);
    }

    void apply_rz(uint32_t qubit, double theta) {
        orkan::apply_rz(sv_, qubit, theta);
    }

    void apply_cx(uint32_t ctrl, uint32_t tgt) {
        orkan::apply_cx(sv_, ctrl, tgt);
    }

    void apply_ccx(uint32_t ctrl0, uint32_t ctrl1, uint32_t tgt) {
        orkan::apply_ccx(sv_, ctrl0, ctrl1, tgt);
    }

    // ── Raw state access ──────────────────────────────────────────────────────

    orkan::state_t&       raw()       { return sv_; }
    const orkan::state_t& raw() const { return sv_; }

private:
    orkan::state_t sv_;
};

} // namespace v2
} // namespace sturm
