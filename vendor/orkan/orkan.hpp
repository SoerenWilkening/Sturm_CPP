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

// ── Generic 2x2 single-qubit gate ────────────────────────────────────────────
// Applies the 2x2 unitary [[u00, u01],[u10, u11]] to the given qubit.
// All 1-qubit gates delegate to this function.

inline void apply_2x2(state_t& s, uint32_t qubit,
                      std::complex<double> u00, std::complex<double> u01,
                      std::complex<double> u10, std::complex<double> u11) {
    if (qubit >= s.n_qubits) {
        throw std::out_of_range("orkan::apply_2x2: qubit index out of range");
    }
    uint64_t mask = uint64_t{1} << qubit;
    uint64_t dim  = uint64_t{1} << s.n_qubits;
    for (uint64_t i = 0; i < dim; ++i) {
        if ((i & mask) == 0u) {
            uint64_t j = i | mask;  // partner basis state (bit=1)
            std::complex<double> a0 = s.amplitudes[i];
            std::complex<double> a1 = s.amplitudes[j];
            s.amplitudes[i] = u00 * a0 + u01 * a1;
            s.amplitudes[j] = u10 * a0 + u11 * a1;
        }
    }
}

// ── Pauli-Y ───────────────────────────────────────────────────────────────────
// Y = [[0, -i],[i, 0]]

inline void apply_y(state_t& s, uint32_t qubit) {
    apply_2x2(s, qubit,
              {0.0, 0.0}, {0.0, -1.0},
              {0.0, 1.0}, {0.0,  0.0});
}

// ── Pauli-Z ───────────────────────────────────────────────────────────────────
// Z = diag(1, -1)

inline void apply_z(state_t& s, uint32_t qubit) {
    apply_2x2(s, qubit,
              {1.0, 0.0}, {0.0,  0.0},
              {0.0, 0.0}, {-1.0, 0.0});
}

// ── Hadamard ──────────────────────────────────────────────────────────────────
// H = 1/sqrt(2) * [[1,1],[1,-1]]

inline void apply_h(state_t& s, uint32_t qubit) {
    static const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    apply_2x2(s, qubit,
              {inv_sqrt2, 0.0}, { inv_sqrt2, 0.0},
              {inv_sqrt2, 0.0}, {-inv_sqrt2, 0.0});
}

// ── S gate ────────────────────────────────────────────────────────────────────
// S = diag(1, i)

inline void apply_s(state_t& s, uint32_t qubit) {
    apply_2x2(s, qubit,
              {1.0, 0.0}, {0.0, 0.0},
              {0.0, 0.0}, {0.0, 1.0});
}

// ── T gate ────────────────────────────────────────────────────────────────────
// T = diag(1, e^{i*pi/4})

inline void apply_t(state_t& s, uint32_t qubit) {
    static const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    apply_2x2(s, qubit,
              {1.0,       0.0},       {0.0, 0.0},
              {0.0,       0.0},       {inv_sqrt2, inv_sqrt2});
}

// ── Phase gate P(theta) ───────────────────────────────────────────────────────
// P(theta) = diag(1, e^{i*theta})

inline void apply_p(state_t& s, uint32_t qubit, double theta) {
    std::complex<double> phase = {std::cos(theta), std::sin(theta)};
    apply_2x2(s, qubit,
              {1.0, 0.0}, {0.0, 0.0},
              {0.0, 0.0}, phase);
}

// ── Rx(theta) ─────────────────────────────────────────────────────────────────
// Rx(theta) = [[cos(t/2), -i*sin(t/2)],[-i*sin(t/2), cos(t/2)]]

inline void apply_rx(state_t& s, uint32_t qubit, double theta) {
    double c = std::cos(theta / 2.0);
    double sv = std::sin(theta / 2.0);
    apply_2x2(s, qubit,
              {c, 0.0},  {0.0, -sv},
              {0.0, -sv}, {c, 0.0});
}

// ── Ry(theta) ─────────────────────────────────────────────────────────────────
// Ry(theta) = [[cos(t/2), -sin(t/2)],[sin(t/2), cos(t/2)]]

inline void apply_ry(state_t& s, uint32_t qubit, double theta) {
    double c  = std::cos(theta / 2.0);
    double sv = std::sin(theta / 2.0);
    apply_2x2(s, qubit,
              {c, 0.0}, {-sv, 0.0},
              {sv, 0.0}, {c,  0.0});
}

// ── Rz(theta) ─────────────────────────────────────────────────────────────────
// Rz(theta) = diag(e^{-i*t/2}, e^{i*t/2})

inline void apply_rz(state_t& s, uint32_t qubit, double theta) {
    std::complex<double> p0 = {std::cos(theta / 2.0), -std::sin(theta / 2.0)};
    std::complex<double> p1 = {std::cos(theta / 2.0),  std::sin(theta / 2.0)};
    apply_2x2(s, qubit,
              p0, {0.0, 0.0},
              {0.0, 0.0}, p1);
}

} // namespace orkan
