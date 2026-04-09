// sub.hpp — M6 (PRD v2): subtraction via two's-complement wrapper around ADD.
// lib_sub(s, mgr, a_idxs, b_idxs, borrow_out, n): in-place b -= a.
// Identity: b - a = b + (~a) + 1 mod 2^n  (Cuccaro MAJ/UMA adder).
// borrow_out = NOT(carry_out): 1 iff b < a (underflow).
// Target: <100 LoC (impl plan M6).

#pragma once

#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cstdint>

namespace sturm {
namespace v2 {

// In-place n-bit subtraction: b -= a.
// a_idxs and b_idxs are n-element qubit-index arrays (LSB = index 0).
// borrow_out must be in |0>; set to 1 iff b < a after the call.
// Borrows exactly 1 ancilla (carry_in scratch); n == 0 is a no-op.
inline void lib_sub(SimState& s, AncillaManager& mgr,
                    const uint32_t* a_idxs, uint32_t* b_idxs,
                    uint32_t borrow_out, uint32_t n) {
    if (n == 0u) return;

    uint32_t carry_anc = mgr.allocate_ancilla();

    // Set carry_anc = |1> (+1 for two's complement).
    primitive_X(s, carry_anc);

    // Flip a in-place: compute ~a.
    for (uint32_t i = 0u; i < n; ++i) {
        primitive_X(s, a_idxs[i]);
    }

    // Forward MAJ chain: adds b + (~a) + 1 with carry_anc=1 as carry_in.
    maj_gate(s, carry_anc, b_idxs[0], a_idxs[0]);
    for (uint32_t i = 1u; i < n; ++i) {
        maj_gate(s, a_idxs[i - 1u], b_idxs[i], a_idxs[i]);
    }

    // Final carry in a[n-1]: 1 iff b >= a.  borrow = NOT(carry).
    primitive_XOR(s, a_idxs[n - 1u], borrow_out);
    primitive_X(s, borrow_out);

    // Backward UMA chain: writes sum into b, restores a bits.
    for (uint32_t i = n - 1u; i >= 1u; --i) {
        uma_gate(s, a_idxs[i - 1u], b_idxs[i], a_idxs[i]);
    }
    uma_gate(s, carry_anc, b_idxs[0], a_idxs[0]);

    // Restore a and carry_anc to original values.
    for (uint32_t i = 0u; i < n; ++i) {
        primitive_X(s, a_idxs[i]);
    }
    primitive_X(s, carry_anc);

    mgr.free_ancilla(carry_anc);
}

} // namespace v2
} // namespace sturm
