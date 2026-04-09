// mul.hpp — M7 (PRD v2): shift-and-add multiplication via controlled ADD.
//
// lib_mul(s, mgr, a_idxs, b_idxs, prod_idxs, n):
//   Out-of-place n-bit × n-bit multiplication.
//   prod_idxs must point to 2n qubits initialised to |0>.
//   a and b are left unchanged.
//   prod = a * b  (full 2n-bit product).
//
// Algorithm: shift-and-add.
//   For each bit i of b (i = 0..n-1):
//     If b[i] == 1: prod += (a << i)
//   Implemented as controlled ADD under the control of each b[i]:
//     For bit i: add a_shifted (a placed at positions i..i+n-1 of prod)
//     into prod, controlled on b[i].
//
//   Each conditional add is lib_add_cuccaro_when with a single-qubit WHEN
//   context (b[i] as the control).  The adder adds into a 2n-bit window:
//   the n bits of shifted-a mapped into the product register.
//
// Gate cost: n × cost(n-bit controlled Cuccaro ADD).
// Ancilla: 1 per ADD call (Cuccaro carry scratch) + WHEN-fold scratch for
//          each controlled ADD.  All returned clean.
//
// Target: <250 LoC (impl plan M7).

#pragma once

#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── lib_mul ───────────────────────────────────────────────────────────────────
//
// Out-of-place n-bit multiplication: prod = a * b.
//
// Parameters:
//   s         — simulator state.
//   mgr       — ancilla manager; borrows 1 ancilla per partial ADD.
//   a_idxs    — qubit indices for a (n elements, LSB = index 0). Unchanged.
//   b_idxs    — qubit indices for b (n elements, LSB = index 0). Unchanged.
//   prod_idxs — qubit indices for the product (2n elements, all start |0>).
//               prod_idxs[0] is the LSB of the product.
//   n         — register width.
//
// n == 0: no-op.
inline void lib_mul(SimState& s, AncillaManager& mgr,
                    const uint32_t* a_idxs,
                    const uint32_t* b_idxs,
                    uint32_t*       prod_idxs,
                    uint32_t        n) {
    if (n == 0u) return;

    // For each bit i of b, conditionally add a << i into prod.
    // "a << i" means: the n-bit value a occupies bits [i..i+n-1] of the
    // 2n-bit product.  We pass a slice of prod_idxs starting at position i
    // as the b_idxs of the adder (adder writes into that window).
    //
    // The carry output from the ADD lands at prod_idxs[i + n], which is
    // always within the 2n-bit product register.  For the last partial add
    // (i = n-1), the carry goes to prod_idxs[n + n - 1] = prod_idxs[2n-1],
    // which is the MSB of the product and is valid.
    //
    // Carry ancilla: we need a dedicated carry_out qubit for each controlled
    // add.  We borrow one ancilla for that; the controlled add may leave it
    // set if there is a carry, so we must uncompute it after each add.
    // However, for this implementation we fold the carry directly into the
    // product register: carry_out = prod_idxs[i + n].
    //
    // For the last bit (i = n-1): carry would go to prod_idxs[2n-1].
    // That is safe since the product of two n-bit numbers fits in 2n bits.

    WhenLift wl(s, &mgr);

    for (uint32_t i = 0u; i < n; ++i) {
        // carry_out slot = prod_idxs[i + n].
        // adder window = prod_idxs[i .. i+n-1] (the b_idxs of the adder).
        // a_idxs is passed as-is (the a side of the adder).
        uint32_t carry_slot = prod_idxs[i + n];
        uint32_t* window = prod_idxs + i;   // n qubits starting at i

        // Push b[i] as the control: this ADD is conditional on bit i of b.
        wl.push_control(b_idxs[i]);
        lib_add_cuccaro_when(wl, s, mgr, a_idxs, window, carry_slot, n);
        wl.pop_control();
    }
    // All ancillas returned inside lib_add_cuccaro_when.
}

} // namespace v2
} // namespace sturm
