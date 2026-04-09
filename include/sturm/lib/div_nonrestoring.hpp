// div_nonrestoring.hpp — M7 (PRD v2): true non-restoring binary division.
//
// lib_div(s, mgr, a_idxs, b_idxs, q_idxs, r_idxs, n):
//   Out-of-place n-bit non-restoring division: q = a/b, r = a%b.
//   q_idxs and r_idxs must start |0>.  a and b are unchanged.
//
//   Zero-divisor (b==0): scratch=0 every iteration; subtracting 0 from P
//   always yields carry_out=1 → q bits all 1 → q = 2^n−1, r = a. ✓
//
// Algorithm: non-restoring shift-subtract/add with sign tracking.
//   P = a (initial partial remainder; unsigned, treated as non-negative).
//   n-1 sign qubits sgn[0..n-2] track the sign of P between steps:
//     sgn[i] = NOT(carry_out of step i+1) = sign of P entering step i.
//
//   For each bit i = n-1 downto 0:
//     overflow_i = OR(b[n-i..n-1]): true iff b<<i >= 2^n.
//     sign_in = 0 (positive) for i=n-1; else sign_in = sgn[i].
//     When NOT(overflow_i):
//       Compute P OP (b<<i) where OP = subtract if sign_in=0, add if sign_in=1.
//         Subtract: scratch = ~(b<<i), carry_in = 1.  P += scratch + 1 = P − (b<<i).
//         Add:      scratch =  (b<<i), carry_in = 0.  P += scratch     = P + (b<<i).
//       carry_out → q[i].  carry_out = 1 iff result non-negative.
//       Uncompute carry_in, scratch flip, scratch load.
//       If i > 0: sgn[i-1] ^= NOT(q[i]) (sign of P = NOT(carry_out)).
//     When overflow_i and i > 0 and i < n-1:
//       Propagate sign: sgn[i-1] ^= sgn[i].
//     After the step, for i < n-1 (sgn[i] was set by step i+1):
//       Uncompute sgn[i] (BEFORE uncomputing overflow_i):
//         XOR(q_idxs[i+1], sgn[i]); WHEN NOT(overflow_i): X(sgn[i]) → 0.
//       (overflow step: sgn[i]=0 already, skip X.)
//     Uncompute overflow_i.
//   Final correction: if final P < 0 (q[0] = 0), P += b.
//
// Ancilla usage:
//   scratch: n qubits (carry wire for inline Cuccaro adder).
//   sgn[0..n-2]: n-1 qubits.
//   overflow: 1 qubit.
//   carry_anc: 1 qubit (carry_in for adder).
//   Total permanent: 2n+1.
//
// Qubit budget (n=2 tests, 17-qubit simulator cap):
//   4*2 data + 2*2+1 ancilla = 13 qubits.  Correction peak: 14. ✓
//   NOTE: n=2 used in tests; n=4 would need ~25+ qubits exceeding the cap.
//   2-bit exhaustive coverage validates the algorithm correctness.
//
// LoC target: <300 (impl plan M7).

#pragma once

#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── compute_overflow_or ──────────────────────────────────────────────────────
//
// overflow ^= OR(b_idxs[lo..n-1]).  Self-inverse (call twice to reset).
// lo >= n: no-op.  1 bit: single XOR.  More bits: 1 ancilla per extra bit.
static inline void compute_overflow_or(
        SimState& s, AncillaManager& mgr,
        const uint32_t* b_idxs, uint32_t lo, uint32_t n,
        uint32_t overflow) {
    if (lo >= n) return;
    primitive_XOR(s, b_idxs[lo], overflow);
    for (uint32_t j = lo + 1u; j < n; ++j) {
        uint32_t anc = mgr.allocate_ancilla();
        primitive_AND(s, overflow, b_idxs[j], anc);
        primitive_XOR(s, b_idxs[j], overflow);
        primitive_XOR(s, anc, overflow);
        primitive_AND(s, overflow, b_idxs[j], anc); // uncompute anc (self-inverse)
        mgr.free_ancilla(anc);
    }
}

// ── lib_div ───────────────────────────────────────────────────────────────────
//
// Non-restoring n-bit division (quotient + remainder).
inline void lib_div(SimState& s, AncillaManager& mgr,
                    const uint32_t* a_idxs,
                    const uint32_t* b_idxs,
                    uint32_t*       q_idxs,
                    uint32_t*       r_idxs,
                    uint32_t        n) {
    if (n == 0u) return;

    // ── Allocate persistent ancilla ──────────────────────────────────────────
    std::vector<uint32_t> scratch(n);
    for (uint32_t k = 0u; k < n; ++k) scratch[k] = mgr.allocate_ancilla();

    uint32_t num_sgn = (n >= 2u) ? n - 1u : 0u;
    std::vector<uint32_t> sgn(num_sgn);
    for (uint32_t k = 0u; k < num_sgn; ++k) sgn[k] = mgr.allocate_ancilla();

    uint32_t overflow  = mgr.allocate_ancilla();
    uint32_t carry_anc = mgr.allocate_ancilla();

    // Copy a → working partial remainder P.
    for (uint32_t i = 0u; i < n; ++i) {
        primitive_XOR(s, a_idxs[i], r_idxs[i]);
    }

    // ── Main loop ─────────────────────────────────────────────────────────────
    for (int32_t bit = static_cast<int32_t>(n) - 1; bit >= 0; --bit) {
        uint32_t i    = static_cast<uint32_t>(bit);
        bool     first = (i == n - 1u);
        uint32_t sign_q = first ? 0u : sgn[i];  // 0u unused for first step

        // Step 1: overflow_i = OR(b[lo..n-1]).
        uint32_t lo = n - i;
        compute_overflow_or(s, mgr, b_idxs, lo, n, overflow);

        // Step 2: execute step WHEN NOT(overflow).
        // Flip overflow so that overflow==1 means "no original overflow" (proceed).
        primitive_X(s, overflow);
        {
            WhenLift wl(s, &mgr);
            wl.push_control(overflow);

            // A. Load scratch = (b << i): scratch[j] ^= b[j-i] for j in [i, n).
            for (uint32_t j = i; j < n; ++j) {
                wl.lift_XOR(b_idxs[j - i], scratch[j]);
            }

            // B. Conditionally negate scratch for subtract mode.
            //    Subtract when sign_in=0 (positive P).  Flip scratch and set carry_anc=1.
            //    "WHEN NOT(sign_q)" = lift_X(sign_q); push_control(sign_q); ...; pop; lift_X.
            if (first) {
                for (uint32_t j = 0u; j < n; ++j) wl.lift_X(scratch[j]);
                wl.lift_X(carry_anc);
            } else {
                wl.lift_X(sign_q);
                wl.push_control(sign_q);
                for (uint32_t j = 0u; j < n; ++j) wl.lift_X(scratch[j]);
                wl.lift_X(carry_anc);
                wl.pop_control();
                wl.lift_X(sign_q);
            }

            // C. Inline Cuccaro MAJ/UMA: P += scratch + carry_anc.
            //    carry_out → q[i].  scratch and carry_anc restored by UMA.
            // First MAJ(carry_anc, r[0], scratch[0]):
            wl.lift_XOR(scratch[0], r_idxs[0]);
            wl.lift_XOR(scratch[0], carry_anc);
            wl.lift_AND(carry_anc, r_idxs[0], scratch[0]);
            for (uint32_t k = 1u; k < n; ++k) {  // MAJ(scratch[k-1], r[k], scratch[k])
                wl.lift_XOR(scratch[k], r_idxs[k]);
                wl.lift_XOR(scratch[k], scratch[k - 1u]);
                wl.lift_AND(scratch[k - 1u], r_idxs[k], scratch[k]);
            }
            wl.lift_XOR(scratch[n - 1u], q_idxs[i]);  // carry_out → q[i]
            for (uint32_t k = n - 1u; k >= 1u; --k) {  // UMA(scratch[k-1], r[k], scratch[k])
                wl.lift_AND(scratch[k - 1u], r_idxs[k], scratch[k]);
                wl.lift_XOR(scratch[k], scratch[k - 1u]);
                wl.lift_XOR(scratch[k - 1u], r_idxs[k]);
            }
            // Last UMA(carry_anc, r[0], scratch[0]):
            wl.lift_AND(carry_anc, r_idxs[0], scratch[0]);
            wl.lift_XOR(scratch[0], carry_anc);
            wl.lift_XOR(carry_anc, r_idxs[0]);

            // D. Uncompute carry_anc and scratch flip (self-inverse under same
            //    sign_q control; sign_q is NOT modified by the adder).
            if (first) {
                wl.lift_X(carry_anc);
                for (uint32_t j = 0u; j < n; ++j) wl.lift_X(scratch[j]);
            } else {
                wl.lift_X(sign_q);
                wl.push_control(sign_q);
                wl.lift_X(carry_anc);
                for (uint32_t j = 0u; j < n; ++j) wl.lift_X(scratch[j]);
                wl.pop_control();
                wl.lift_X(sign_q);
            }

            // E. Unload scratch = (b << i) (self-inverse).
            for (uint32_t j = i; j < n; ++j) {
                wl.lift_XOR(b_idxs[j - i], scratch[j]);
            }

            // F. Set sign for the next iteration.
            //    sgn[i-1] = NOT(carry_out) = NOT(q[i]).
            //    q[i] was just set to carry_out by lift_XOR(scratch[n-1], q[i]).
            //    We set sgn[i-1] ^= NOT(q[i]) = 1 XOR q[i]:
            //      lift_X(q[i])            → q[i] = NOT(carry_out)
            //      lift_XOR(q[i], sgn[i-1]) → sgn[i-1] ^= NOT(carry_out)
            //      lift_X(q[i])            → restore q[i] = carry_out
            if (i > 0u) {
                wl.lift_X(q_idxs[i]);
                wl.lift_XOR(q_idxs[i], sgn[i - 1u]);
                wl.lift_X(q_idxs[i]);
            }

            wl.pop_control();
        }
        primitive_X(s, overflow);  // restore overflow

        // G. Overflow propagation: when overflow and sign should propagate.
        //    If this step was overflow and i > 0 and i < n-1:
        //    sgn[i-1] ^= sgn[i] (propagate unchanged sign).
        //    For first step (i=n-1) overflow: sign_in=0, sgn stays 0. No-op. ✓
        //    For i=0: no sgn[i-1] to set. ✓
        if (!first && i > 0u) {
            WhenLift wl(s, &mgr);
            wl.push_control(overflow);      // active when overflow=1 (original overflow)
            wl.lift_XOR(sgn[i], sgn[i - 1u]);
            wl.pop_control();
        }

        // H. Uncompute sgn[i] (set by step i+1).
        //    Non-overflow i+1: sgn[i]=NOT(q[i+1]).  XOR(q[i+1],sgn[i])→1; X→0. ✓
        //    Overflow i+1: sgn[i]=0, q[i+1]=0.  XOR→0; skip X. ✓
        //    Use recomputed overflow_{i+1}=OR(b[lo_prev..n-1]) to distinguish.
        if (!first) {
            uint32_t prev_i  = i + 1u;
            uint32_t lo_prev = lo - 1u;  // lo = n - i, lo_prev = n - (i+1)

            // Recompute overflow for step i+1.
            compute_overflow_or(s, mgr, b_idxs, lo_prev, n, overflow);

            primitive_XOR(s, q_idxs[prev_i], sgn[i]);

            // WHEN NOT(overflow_{i+1}): X(sgn[i])
            primitive_X(s, overflow);
            {
                WhenLift wl(s, &mgr);
                wl.push_control(overflow);
                wl.lift_X(sgn[i]);
                wl.pop_control();
            }
            primitive_X(s, overflow);

            // Uncompute overflow_{i+1}.
            compute_overflow_or(s, mgr, b_idxs, lo_prev, n, overflow);
        }

        // Step 3: uncompute overflow_i.
        compute_overflow_or(s, mgr, b_idxs, lo, n, overflow);
    }

    // ── Final correction ──────────────────────────────────────────────────────
    // If final P < 0 (q[0] = 0), add b to get the true remainder.
    // WHEN NOT(q[0]): P += b.  Implement as: X(q[0]); WHEN q[0]: lib_add; X(q[0]).
    //
    // Re-use carry_anc (currently |0>) as the carry_out for lib_add to save
    // one ancilla allocation.
    //
    // carry_anc analysis: when correction applied (q[0]=0 before flip), the unsigned
    // P_unsigned in [2^n-b, 2^n-1]; adding b gives [2^n, 2^n+b-1], carry_anc=1.
    // When no correction: lib_add not executed → carry_anc stays 0.
    // Therefore carry_anc = NOT(q[0]) after the block.
    // Uncompute: XOR(q[0], carry_anc); X(carry_anc) → 0. ✓
    primitive_X(s, q_idxs[0]);
    {
        WhenLift wl(s, &mgr);
        wl.push_control(q_idxs[0]);
        lib_add_cuccaro_when(wl, s, mgr, b_idxs, r_idxs, carry_anc, n);
        wl.pop_control();
    }
    primitive_X(s, q_idxs[0]);
    // Uncompute carry_anc = NOT(q[0]).
    primitive_XOR(s, q_idxs[0], carry_anc);  // carry_anc = NOT(q[0]) XOR q[0] = 1
    primitive_X(s, carry_anc);               // carry_anc = 0 ✓

    // ── Release ancilla in reverse allocation order ───────────────────────────
    mgr.free_ancilla(carry_anc);
    mgr.free_ancilla(overflow);
    for (uint32_t k = 0u; k < num_sgn; ++k) mgr.free_ancilla(sgn[num_sgn - 1u - k]);
    for (uint32_t k = 0u; k < n; ++k) mgr.free_ancilla(scratch[n - 1u - k]);
}

} // namespace v2
} // namespace sturm
