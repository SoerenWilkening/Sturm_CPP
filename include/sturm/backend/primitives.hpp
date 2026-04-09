// primitives.hpp — M1 (PRD v2): frozen backend primitive surface.
//
// Declares exactly five primitives (PRD v2 §3):
//
//   Classical-reversible:
//     primitive_X(s, q)          — NOT (Pauli-X) on qubit q
//     primitive_XOR(s, ctrl, tgt) — CNOT: tgt ^= ctrl
//     primitive_AND(s, c0, c1, tgt) — Toffoli: tgt ^= (c0 & c1)
//
//   Phase:
//     primitive_phase(s, q, theta)   — R_y(theta) on qubit q
//     primitive_phi_add(s, q, theta) — R_z(theta) on qubit q
//
// All primitives operate on a SimState (see state.hpp).  No other ops are
// defined in this file — the backend surface is frozen at these five.
//
// Target: <200 LoC (impl plan M1).

#pragma once

#include "sturm/backend/state.hpp"

#include <cstdint>

namespace sturm {
namespace v2 {

// ── Classical-reversible primitives ──────────────────────────────────────────

// NOT / Pauli-X on a single qubit.
// Bit-flip: |0> <-> |1>.
inline void primitive_X(SimState& s, uint32_t qubit) {
    s.apply_x(qubit);
}

// CNOT: flip tgt iff ctrl == |1>.
// Reversible copy / XOR workhorse.
inline void primitive_XOR(SimState& s, uint32_t ctrl, uint32_t tgt) {
    s.apply_cx(ctrl, tgt);
}

// Toffoli (CCX): flip tgt iff ctrl0 == |1> AND ctrl1 == |1>.
// The only non-Clifford classical primitive; backbone of all arithmetic.
inline void primitive_AND(SimState& s, uint32_t ctrl0, uint32_t ctrl1, uint32_t tgt) {
    s.apply_ccx(ctrl0, ctrl1, tgt);
}

// ── Phase primitives ──────────────────────────────────────────────────────────

// R_y(theta) rotation on a single qubit.
// Convention: phase = R_y  (PRD v2 §3.2).
// Matrix: [[cos(t/2), -sin(t/2)], [sin(t/2), cos(t/2)]]
inline void primitive_phase(SimState& s, uint32_t qubit, double theta) {
    s.apply_ry(qubit, theta);
}

// R_z(theta) rotation on a single qubit.
// Convention: phi_add = R_z  (PRD v2 §3.2).
// Matrix: diag(e^{-i*t/2}, e^{i*t/2})
inline void primitive_phi_add(SimState& s, uint32_t qubit, double theta) {
    s.apply_rz(qubit, theta);
}

} // namespace v2
} // namespace sturm
