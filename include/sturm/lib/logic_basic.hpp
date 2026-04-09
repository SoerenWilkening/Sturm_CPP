// logic_basic.hpp — M3 (PRD v2): boolean operators as library functions over
// {X, XOR, AND}.
//
// Functions:
//   NOT_reg(s, first, n)          — flip n consecutive qubits starting at first
//   lib_OR(s, mgr, a, b, tgt)     — tgt ^= (a | b); uses 1 borrowed ancilla
//   lib_NAND(s, a, b, tgt)        — tgt ^= NOT(a AND b); no ancilla needed
//   lib_NOR(s, mgr, a, b, tgt)    — tgt ^= NOT(a | b); uses 1 borrowed ancilla
//   lib_XNOR(s, a, b, tgt)        — tgt ^= NOT(a XOR b); no ancilla needed
//
// All functions operate on a SimState reference and borrow ancillas from an
// AncillaManager when needed.  Each function uncomputes borrowed ancillas
// before returning — no leaked entanglement.
//
// Derivations (all from {X, XOR, AND}):
//
//   OR:  a|b = NOT(NOT(a) AND NOT(b))           [De Morgan]
//     1. NOT a  (flip a in-place; will uncompute)
//     2. NOT b  (flip b in-place; will uncompute)
//     3. AND(a', b', anc)                        [anc = a' AND b']
//     4. NOT anc  → anc = NOT(a' AND b') = a|b
//     5. XOR(anc, tgt)                           [tgt ^= anc]
//     6. NOT anc  (uncompute step 4)
//     7. AND(a', b', anc)  (uncompute step 3)
//     8. NOT b  (uncompute step 2)
//     9. NOT a  (uncompute step 1)
//
//   NAND:  NOT(a AND b)
//     1. AND(a, b, tgt)
//     2. NOT tgt
//
//   NOR:  NOT(a|b)  — compute OR into ancilla, then NOT + copy.
//     1-9. OR into anc (as above)
//     10. NOT anc  → anc = NOT(a|b)
//     11. XOR(anc, tgt)
//     12. NOT anc  (restore anc to a|b for uncompute chain)
//     [then uncompute the OR into anc]
//     Alternatively: directly NOT(a|b) = a'|b'? No, NOR = NAND applied after OR.
//     Simpler: NOR(a,b) = NOT(a) AND NOT(b).
//     1. NOT a
//     2. NOT b
//     3. AND(a', b', tgt)   [tgt ^= NOT(a) AND NOT(b) = NOR(a,b)]
//     4. NOT b  (uncompute)
//     5. NOT a  (uncompute)
//     No ancilla required! Uses an ancilla only for the borrowed-style API.
//
//   XNOR:  NOT(a XOR b)
//     1. XOR(a, b, tgt)   [tgt ^= a XOR b]
//     2. NOT tgt           [tgt = NOT(a XOR b)]
//
// Target: <200 LoC total (impl plan M3).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cstdint>

namespace sturm {
namespace v2 {

// ── NOT_reg ───────────────────────────────────────────────────────────────────
//
// Apply X to each qubit in the range [first_qubit, first_qubit + n_qubits).
// This is the quantum NOT on a multi-qubit register.
inline void NOT_reg(SimState& s, uint32_t first_qubit, uint32_t n_qubits) {
    for (uint32_t i = 0; i < n_qubits; ++i) {
        primitive_X(s, first_qubit + i);
    }
}

// ── lib_OR ────────────────────────────────────────────────────────────────────
//
// tgt ^= (a | b).  tgt must be initialised to |0⟩ for the result to equal a|b.
//
// Uses one borrowed ancilla.  Ancilla is returned to |0⟩ before return.
//
// Implementation: OR(a,b) = NOT(NOT(a) AND NOT(b)).
// We borrow an ancilla, compute NOT(a) AND NOT(b) into it, flip it to get a|b,
// copy to tgt, then uncompute everything in reverse order.
inline void lib_OR(SimState& s, AncillaManager& mgr,
                   uint32_t a, uint32_t b, uint32_t tgt) {
    uint32_t anc = mgr.allocate_ancilla();

    // Step 1-2: flip a and b temporarily (in-place; will uncompute)
    primitive_X(s, a);
    primitive_X(s, b);

    // Step 3: anc = NOT(a) AND NOT(b)  [currently a',b' after flips]
    primitive_AND(s, a, b, anc);

    // Step 4: NOT anc → anc = NOT(NOT(a) AND NOT(b)) = a | b
    primitive_X(s, anc);

    // Step 5: tgt ^= anc   (copies the OR result)
    primitive_XOR(s, anc, tgt);

    // Uncompute: reverse steps 4, 3, 2, 1
    // Step 6: un-NOT anc (restore anc to NOT(a') AND NOT(b'))
    primitive_X(s, anc);

    // Step 7: un-AND (restore anc to 0)
    primitive_AND(s, a, b, anc);

    // Step 8-9: restore a and b
    primitive_X(s, b);
    primitive_X(s, a);

    mgr.free_ancilla(anc);
}

// ── lib_NAND ──────────────────────────────────────────────────────────────────
//
// tgt ^= NOT(a AND b).  tgt must be initialised to |0⟩ for the result to
// equal NAND(a,b).
//
// No ancilla needed.
inline void lib_NAND(SimState& s,
                     uint32_t a, uint32_t b, uint32_t tgt) {
    // tgt ^= (a AND b)
    primitive_AND(s, a, b, tgt);
    // tgt ^= 1   →  tgt = NOT(a AND b)
    primitive_X(s, tgt);
}

// ── lib_NOR ───────────────────────────────────────────────────────────────────
//
// tgt ^= NOT(a | b).  tgt must be initialised to |0⟩ for the result to
// equal NOR(a,b).
//
// NOR(a,b) = NOT(a) AND NOT(b)  [De Morgan].
// Flip a, flip b, AND into tgt, unflip b, unflip a.
// Uses AncillaManager for API consistency (no ancilla actually borrowed).
inline void lib_NOR(SimState& s, AncillaManager& /*mgr*/,
                    uint32_t a, uint32_t b, uint32_t tgt) {
    primitive_X(s, a);
    primitive_X(s, b);
    primitive_AND(s, a, b, tgt);
    primitive_X(s, b);
    primitive_X(s, a);
}

// ── lib_XNOR ─────────────────────────────────────────────────────────────────
//
// tgt ^= NOT(a XOR b).  tgt must be initialised to |0⟩ for the result to
// equal XNOR(a,b).
//
// No ancilla needed.
inline void lib_XNOR(SimState& s,
                     uint32_t a, uint32_t b, uint32_t tgt) {
    // tgt ^= (a XOR b)
    primitive_XOR(s, a, tgt);
    primitive_XOR(s, b, tgt);
    // tgt ^= 1  →  tgt = NOT(a XOR b)
    primitive_X(s, tgt);
}

} // namespace v2
} // namespace sturm
