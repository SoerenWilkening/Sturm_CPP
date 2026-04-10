// primitives_v3.hpp — M11 (PRD v3): frozen backend primitive surface via execute_gate.
//
// Declares exactly five primitives in the sturm:: namespace (not sturm::v2::).
// Each primitive is a one-liner calling execute_gate() via BackendContext.
// Gate kinds: STURM_GATE_X, STURM_GATE_CX, STURM_GATE_CCX, STURM_GATE_RY, STURM_GATE_RZ.
//
// The v2 overloads in primitives.hpp (sturm::v2:: namespace, operate on SimState)
// are intentionally left untouched — v2 library code still uses them.
//
// Target: <80 LoC (implementation plan M11).

#pragma once

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>

namespace sturm {

// ── Classical-reversible primitives ──────────────────────────────────────────

// NOT / Pauli-X on a single qubit.
inline void primitive_X(BackendContext& ctx, uint32_t q) {
    execute_gate(ctx, STURM_GATE_X, &q, 1u, 0.0);
}

// CNOT: flip tgt iff ctrl == |1>.
inline void primitive_XOR(BackendContext& ctx, uint32_t c, uint32_t t) {
    const uint32_t qubits[2] = {c, t};
    execute_gate(ctx, STURM_GATE_CX, qubits, 2u, 0.0);
}

// Toffoli (CCX): flip tgt iff ctrl0 == |1> AND ctrl1 == |1>.
inline void primitive_AND(BackendContext& ctx, uint32_t c0, uint32_t c1, uint32_t t) {
    const uint32_t qubits[3] = {c0, c1, t};
    execute_gate(ctx, STURM_GATE_CCX, qubits, 3u, 0.0);
}

// ── Phase primitives ──────────────────────────────────────────────────────────

// R_y(theta) rotation on a single qubit.
inline void primitive_phase(BackendContext& ctx, uint32_t q, double theta) {
    execute_gate(ctx, STURM_GATE_RY, &q, 1u, theta);
}

// R_z(theta) rotation on a single qubit.
inline void primitive_phi_add(BackendContext& ctx, uint32_t q, double theta) {
    execute_gate(ctx, STURM_GATE_RZ, &q, 1u, theta);
}

} // namespace sturm
