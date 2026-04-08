// vendor/orkan/orkan.hpp — Local stub of the Orkan statevector library.
//
// NOTE: This file is a STUB used when github.com/Timo59/orkan is unavailable
// (e.g. offline CI / sandboxed builds).  The real Orkan library is fetched via
// cmake/OrkanFetch.cmake when network access is available.
//
// The stub provides the minimal subset of the Orkan C++ API required by
// OrkanBridge (M9):
//   - orkan::state_t  — owns a statevector of 2^n complex amplitudes
//   - orkan::allocate(state_t&, uint32_t n)  — resize + init to |0…0⟩
//   - orkan::reset_zero(state_t&)            — re-initialize to |0…0⟩
//   - orkan::apply_x(state_t&, uint32_t qubit) — Pauli-X on one qubit
//   - orkan::amplitude(const state_t&, uint64_t idx) — read amplitude
//
// All operations match the interface expected from the real Orkan library so
// that OrkanBridge.cpp compiles against either the stub or the real library
// without modification.

#pragma once

#include <complex>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace orkan {

// ── State type ────────────────────────────────────────────────────────────────

struct state_t {
    uint32_t                          n_qubits{0};
    std::vector<std::complex<double>> amplitudes; // length = 2^n_qubits
};

// ── Allocate / initialize ─────────────────────────────────────────────────────

// Allocate or reallocate the statevector for n qubits and initialize to |0…0⟩.
// The |0…0⟩ state has amplitude[0] = 1.0 and all others = 0.
inline void allocate(state_t& s, uint32_t n) {
    if (n > 30u) {
        // Guard against absurd sizes; 17 qubits = 2^17 = 131 072 amplitudes.
        throw std::invalid_argument("orkan::allocate: n_qubits too large");
    }
    s.n_qubits = n;
    uint64_t dim = uint64_t{1} << n;
    s.amplitudes.assign(dim, {0.0, 0.0});
    s.amplitudes[0] = {1.0, 0.0};
}

// ── Reset to |0…0⟩ ───────────────────────────────────────────────────────────

inline void reset_zero(state_t& s) {
    for (auto& a : s.amplitudes) a = {0.0, 0.0};
    if (!s.amplitudes.empty()) s.amplitudes[0] = {1.0, 0.0};
}

// ── Read amplitude ────────────────────────────────────────────────────────────

inline std::complex<double> amplitude(const state_t& s, uint64_t idx) {
    return s.amplitudes.at(idx);
}

// ── Pauli-X ───────────────────────────────────────────────────────────────────
// Flips qubit `qubit` (0 = least-significant).  All basis states that differ
// only in bit `qubit` swap amplitudes.

inline void apply_x(state_t& s, uint32_t qubit) {
    if (qubit >= s.n_qubits) {
        throw std::out_of_range("orkan::apply_x: qubit index out of range");
    }
    uint64_t mask = uint64_t{1} << qubit;
    uint64_t dim  = uint64_t{1} << s.n_qubits;
    for (uint64_t i = 0; i < dim; ++i) {
        if ((i & mask) == 0u) {
            std::swap(s.amplitudes[i], s.amplitudes[i | mask]);
        }
    }
}

} // namespace orkan
