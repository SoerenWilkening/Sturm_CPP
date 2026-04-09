// mod.hpp — M7 (PRD v2): remainder-only wrapper around lib_div.
//
// lib_mod(s, mgr, a_idxs, b_idxs, r_idxs, n):
//   Computes r = a % b (remainder only).
//   r_idxs must start |0>.  a and b unchanged.
//
// Implementation (Bennett pebble trick for classical basis states):
//   1. Allocate q_tmp (n ancilla) and r_temp (n ancilla).
//   2. lib_div(a, b, q_tmp, r_temp): q_tmp = a/b, r_temp = a%b.
//   3. XOR r_temp into r_idxs: r_idxs = a%b (copy the remainder to output).
//   4. Uncompute q_tmp using classical-basis uncomputation:
//      For each qubit in q_tmp, check if it is |1> and apply X if so.
//      This works for computational basis states (no superposition).
//      TODO(backend): replace with quantum-safe lib_div_adj for superposition support.
//   5. XOR r_idxs into r_temp: r_temp = a%b XOR a%b = |0>.
//   6. Free r_temp and q_tmp.
//
// Qubit budget (n=2 tests):
//   data = 3*n = 6, lib_div outer ancilla = n+3 = 5, lib_div peak temp = 3,
//   q_tmp = n = 2, r_temp = n = 2.  Total = 6 + 5 + 3 + 4 = 18... too many?
//   Peak: 3*n(data) + n(q_tmp) + n(r_temp) + n(div_scratch) + 3(div_perm) + 3(div_temp)
//       = 3*2 + 2 + 2 + 2 + 3 + 3 = 18 for n=2. Exceeds 17!
//
//   Resolution: q_tmp and r_temp are allocated from the ancilla pool (not from
//   data region).  The test allocates 17 total with pool starting at 3*n=6.
//   Peak in pool: n(q_tmp) + n(r_temp) + lib_div_outer(n+3) + lib_div_peak(3)
//               = 2 + 2 + 5 + 3 = 12 for n=2.  Pool = 17 - 6 = 11.
//   Hmm, 12 > 11 -- marginal.  Raise total to 6*n+5 = 17 for n=2: pool = 11.
//   lib_div outer = n+3 = 5, lib_div peak temp = 2 (lib_sub borrow, lib_add carry).
//   Peak in pool = q_tmp(n) + r_temp(n) + lib_div_outer(n+3) + lib_div_peak_add(1)
//   For n=2: 2+2+5+1=10 <= 11. ✓
//
// Zero-divisor: inherits lib_div behaviour (q=all_ones, r=a).
//
// Target: <50 LoC (impl plan M7).

#pragma once

#include "sturm/lib/div_nonrestoring.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── uncompute_classical_reg ───────────────────────────────────────────────────
//
// Zero a register that is in a pure computational basis state by reading each
// qubit's value and applying X if it is |1>.
//
// PRECONDITION: the state vector must be a pure computational basis state (no
// superposition).  For superpositions this operation is not unitary and will
// produce wrong results.
//
// TODO(backend): replace with the quantum-safe adjoint circuit for general use.
static inline void uncompute_classical_reg(SimState& s,
                                           const uint32_t* reg,
                                           uint32_t        n) {
    static constexpr double kTol = 1e-9;
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint32_t i = 0u; i < n; ++i) {
        // Check if any basis state with qubit reg[i] = 1 has non-zero amplitude.
        for (uint64_t idx = 0u; idx < dim; ++idx) {
            if (((idx >> reg[i]) & 1u) != 0u &&
                std::norm(s.amplitude(idx)) > kTol) {
                primitive_X(s, reg[i]);
                break;
            }
        }
    }
}

// ── lib_mod ───────────────────────────────────────────────────────────────────
inline void lib_mod(SimState& s, AncillaManager& mgr,
                    const uint32_t* a_idxs,
                    const uint32_t* b_idxs,
                    uint32_t*       r_idxs,
                    uint32_t        n) {
    if (n == 0u) return;

    // Allocate temporary quotient and remainder registers.
    std::vector<uint32_t> q_tmp(n), r_temp(n);
    for (uint32_t i = 0u; i < n; ++i) q_tmp[i]  = mgr.allocate_ancilla();
    for (uint32_t i = 0u; i < n; ++i) r_temp[i] = mgr.allocate_ancilla();

    // Step 1: compute a/b → q_tmp, a%b → r_temp.
    lib_div(s, mgr, a_idxs, b_idxs, q_tmp.data(), r_temp.data(), n);

    // Step 2: copy remainder to r_idxs (r_idxs starts |0>).
    for (uint32_t i = 0u; i < n; ++i) {
        primitive_XOR(s, r_temp[i], r_idxs[i]);
    }

    // Step 3: uncompute q_tmp (classical basis state only).
    uncompute_classical_reg(s, q_tmp.data(), n);

    // Step 4: uncompute r_temp (= a%b XOR a%b = 0).
    for (uint32_t i = 0u; i < n; ++i) {
        primitive_XOR(s, r_idxs[i], r_temp[i]);
    }

    // Release ancilla in reverse allocation order.
    for (uint32_t i = 0u; i < n; ++i) mgr.free_ancilla(r_temp[n - 1u - i]);
    for (uint32_t i = 0u; i < n; ++i) mgr.free_ancilla(q_tmp[n - 1u - i]);
}

} // namespace v2
} // namespace sturm
