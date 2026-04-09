// add_cuccaro.hpp — M6 (PRD v2): Cuccaro ripple-carry in-place adder.
//
// lib_add_cuccaro(s, mgr, a_idxs, b_idxs, carry_out, n):
//   In-place addition: b += a  (PRD v2 §7, §8).
//   - a is left unchanged.
//   - b is overwritten with (a + b) mod 2^n.
//   - carry_out qubit receives bit n of the sum (overflow flag).
//     carry_out must be in |0⟩ before the call.
//   - No extra ancilla qubits borrowed: the MAJ/UMA chain reuses the a qubits
//     as scratch during the forward pass and restores them in the UMA pass.
//
// Algorithm: Cuccaro et al. 2004 ripple-carry adder.
//   Forward pass — MAJ gates propagate the carry through the a register:
//     MAJ(carry_in, b[i], a[i])  for i = 0 .. n-1
//     where carry_in for i=0 is a borrowed ancilla (starts |0⟩, returned clean).
//   After the forward pass a[n-1] holds the carry-out.
//   XOR(a[n-1], carry_out) copies carry to the output qubit.
//   Backward pass — UMA gates extract the sum into b and restore a:
//     UMA(carry[n-2], b[n-1], a[n-1])  ... down to
//     UMA(carry_ancilla, b[0], a[0])
//
// Gate cost (n-bit): 5n - 2  (Cuccaro 2004 Table 1) plus 2 CNOTs for carry copy.
//
// lib_add_cuccaro_when(wl, s, mgr, a_idxs, b_idxs, carry_out, n):
//   Controlled version: all gates emitted through WhenLift.
//   If wl.control_depth() == 0 this is identical to lib_add_cuccaro.
//   If wl.control_depth() > 0 every gate is lifted under the active controls.
//
// Overflow behaviour (documented per M6 requirements):
//   carry_out = 0: a + b < 2^n (no overflow).
//   carry_out = 1: a + b >= 2^n; b holds (a+b) mod 2^n.
//   The carry_out qubit is left in the state it was set to — it is NOT
//   uncomputed by this function.  The caller is responsible for managing it.
//
// Target: <250 LoC (impl plan M6).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cstdint>
#include <stdexcept>

namespace sturm {
namespace v2 {

// ── MAJ gate ─────────────────────────────────────────────────────────────────
//
// MAJ(a, b, c):
//   XOR(c, b)     b ^= c
//   XOR(c, a)     a ^= c
//   AND(a, b, c)  c ^= (a_new AND b_new) = (a_in XOR c_in) AND (b_in XOR c_in)
//
// Result: c = majority(a_in, b_in, c_in)
//         a = a_in XOR c_in
//         b = b_in XOR c_in
//
// In the context of the Cuccaro adder:
//   c = carry_in qubit (before), carry_out qubit (after)
//   a = a_i input bit (modified; restored by UMA)
//   b = b_i input bit (modified; restored by UMA — sum is written here)
inline void maj_gate(SimState& s,
                     uint32_t a, uint32_t b, uint32_t c) {
    primitive_XOR(s, c, b);
    primitive_XOR(s, c, a);
    primitive_AND(s, a, b, c);
}

// ── UMA gate ─────────────────────────────────────────────────────────────────
//
// UMA(a, b, c)  [inverse of MAJ — "unmajority and add"]:
//   AND(a, b, c)  c ^= (a AND b)   — uncomputes the Toffoli from MAJ
//   XOR(c, a)     a = a_in         — restores a to pre-MAJ value
//   XOR(a, b)     b = sum bit      — b ^= a (XOR with restored a propagates sum)
//
// After UMA: a is restored (same as before MAJ was called), b = sum bit,
//            c = carry_in (same as the c before MAJ was called — restored).
inline void uma_gate(SimState& s,
                     uint32_t a, uint32_t b, uint32_t c) {
    primitive_AND(s, a, b, c);
    primitive_XOR(s, c, a);
    primitive_XOR(s, a, b);
}

// ── Controlled MAJ gate ───────────────────────────────────────────────────────
//
// Lifts all three primitives in MAJ through the WhenLift context.
inline void maj_gate_when(WhenLift& wl,
                          uint32_t a, uint32_t b, uint32_t c) {
    wl.lift_XOR(c, b);
    wl.lift_XOR(c, a);
    wl.lift_AND(a, b, c);
}

// ── Controlled UMA gate ───────────────────────────────────────────────────────
inline void uma_gate_when(WhenLift& wl,
                          uint32_t a, uint32_t b, uint32_t c) {
    wl.lift_AND(a, b, c);
    wl.lift_XOR(c, a);
    wl.lift_XOR(a, b);
}

// ── lib_add_cuccaro ───────────────────────────────────────────────────────────
//
// In-place n-bit addition: b += a.
//
// Parameters:
//   s          — simulator state.
//   mgr        — ancilla manager; borrows exactly 1 ancilla (carry_in scratch).
//   a_idxs     — qubit-index array for a (n elements, LSB = index 0).
//                a is left unchanged after the call.
//   b_idxs     — qubit-index array for b (n elements, LSB = index 0).
//                b is overwritten with (a + b) mod 2^n.
//   carry_out  — qubit index for the carry output (must be in |0⟩).
//                Set to 1 iff a + b >= 2^n (overflow).
//   n          — register width in qubits (must be >= 1).
//
// Ancilla usage: 1 qubit borrowed and returned clean.
//
// n == 0: no-op.
inline void lib_add_cuccaro(SimState& s, AncillaManager& mgr,
                             const uint32_t* a_idxs, uint32_t* b_idxs,
                             uint32_t carry_out, uint32_t n) {
    if (n == 0u) return;

    // Borrow 1 ancilla for the initial carry_in (starts |0⟩).
    uint32_t carry_anc = mgr.allocate_ancilla();

    // ── Forward pass: MAJ chain ───────────────────────────────────────────────
    // Carry propagates through the a qubits (they act as carry wires).
    // MAJ(carry_prev, b[i], a[i]) → carry ends up in a[i] (third argument).
    //
    // Calling convention: maj_gate(carry_in, b[i], a[i])
    //   After each MAJ: a[i] = MAJ(carry_in, b_orig[i], a_orig[i]) = carry_out
    //                   b[i] = b_orig[i] XOR carry_in (intermediate)
    //                   carry_in qubit = carry_in XOR a_orig[i] (intermediate)
    //
    // Carry chain: carry_anc → a[0] → a[1] → … → a[n-1]

    // First MAJ: carry_in = carry_anc
    maj_gate(s, carry_anc, b_idxs[0], a_idxs[0]);

    for (uint32_t i = 1u; i < n; ++i) {
        // carry is now in a_idxs[i-1]
        maj_gate(s, a_idxs[i - 1u], b_idxs[i], a_idxs[i]);
    }

    // After the last MAJ, the carry-out of bit n-1 is in a_idxs[n-1].
    // Copy it to carry_out.
    primitive_XOR(s, a_idxs[n - 1u], carry_out);

    // ── Backward pass: UMA chain ──────────────────────────────────────────────
    // UMA(carry_prev, b[i], a[i]) — same argument convention as MAJ.
    // Reverse: from i=n-1 down to 1, then UMA(carry_anc, b[0], a[0]).
    for (uint32_t i = n - 1u; i >= 1u; --i) {
        uma_gate(s, a_idxs[i - 1u], b_idxs[i], a_idxs[i]);
    }
    // Last UMA: carry_in = carry_anc
    uma_gate(s, carry_anc, b_idxs[0], a_idxs[0]);

    // Return carry ancilla (must be |0⟩ — UMA restored it).
    mgr.free_ancilla(carry_anc);
}

// ── lib_add_cuccaro_when ──────────────────────────────────────────────────────
//
// Controlled version of lib_add_cuccaro.
//
// All gates are emitted through WhenLift, so they are implicitly lifted under
// any active WHEN controls.  When wl.control_depth() == 0 this is identical to
// lib_add_cuccaro (no overhead: lift_XOR == primitive_XOR, etc.)
//
// Parameters: identical to lib_add_cuccaro plus:
//   wl — WhenLift context; may have 0 or more active controls.
//
// Ancilla usage: 1 qubit borrowed and returned clean.
inline void lib_add_cuccaro_when(WhenLift& wl, SimState& s, AncillaManager& mgr,
                                  const uint32_t* a_idxs, uint32_t* b_idxs,
                                  uint32_t carry_out, uint32_t n) {
    if (n == 0u) return;

    uint32_t carry_anc = mgr.allocate_ancilla();

    // ── Forward pass ──────────────────────────────────────────────────────────
    maj_gate_when(wl, carry_anc, b_idxs[0], a_idxs[0]);
    for (uint32_t i = 1u; i < n; ++i) {
        maj_gate_when(wl, a_idxs[i - 1u], b_idxs[i], a_idxs[i]);
    }

    // Copy carry to carry_out.
    wl.lift_XOR(a_idxs[n - 1u], carry_out);

    // ── Backward pass ─────────────────────────────────────────────────────────
    for (uint32_t i = n - 1u; i >= 1u; --i) {
        uma_gate_when(wl, a_idxs[i - 1u], b_idxs[i], a_idxs[i]);
    }
    uma_gate_when(wl, carry_anc, b_idxs[0], a_idxs[0]);

    mgr.free_ancilla(carry_anc);
}

} // namespace v2
} // namespace sturm
