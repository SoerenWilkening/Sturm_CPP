// div_dsl.hpp — M16 (PRD v3): non-restoring binary division in DSL style.
//
// lib_div_dsl(dividend_bits, n, divisor_bits, d, quotient_bits, remainder_bits):
//   Out-of-place n-bit non-restoring division: quotient = a/b, remainder = a%b.
//   quotient_bits and remainder_bits must start |0>. a and b are unchanged.
//
// Algorithm: non-restoring shift-subtract/add with sign tracking.
//   Mirrors div_nonrestoring.hpp but uses qbool operators + lib_add_dsl.
//   For each bit i = n-1 downto 0:
//     Compute overflow, conditionally negate scratch for sub vs add,
//     run inline Cuccaro with carry_anc, set quotient bit + sign, uncompute.
//   Final correction: WHEN NOT(q[0]): P += b.
//
// Target: <300 LoC.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <cstddef>
#include <cassert>
#include <vector>

namespace sturm {

// ── compute_overflow_or_dsl ───────────────────────────────────────────────────
// overflow ^= OR(b_bits[lo..n-1]).  Self-inverse (call twice to restore).
// lo >= n: no-op.
static inline void compute_overflow_or_dsl(
        qbool& overflow, qbool* b_bits, size_t lo, size_t n) {
    if (lo >= n) return;
    overflow ^= b_bits[lo];
    for (size_t j = lo + 1u; j < n; ++j) {
        // OR(x,y) = x XOR y XOR (x AND y): compute AND before modifying overflow.
        qbool tmp = (overflow & b_bits[j]);  // materializes ancilla; uncomputes on destruct
        overflow ^= b_bits[j];
        overflow ^= tmp;
    }
}

// ── lib_div_dsl ───────────────────────────────────────────────────────────────
// Non-restoring n-bit division: quotient_bits = a/b, remainder_bits = a%b.
// d must equal n (asserted). quotient_bits and remainder_bits must start |0>.
inline void lib_div_dsl(qbool* dividend_bits, size_t n,
                        qbool* divisor_bits,  size_t d,
                        qbool* quotient_bits,
                        qbool* remainder_bits) {
    if (n == 0u) return;
    assert(d == n && "lib_div_dsl: d must equal n");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_div_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;

    // ── Allocate ancilla ──────────────────────────────────────────────────────
    std::vector<int>   scratch_idx(n);
    std::vector<qbool> scratch(n);
    for (size_t k = 0u; k < n; ++k) {
        scratch_idx[k] = QubitPool::instance().allocate();
        scratch[k]     = qbool::make_non_owning(scratch_idx[k]);
    }

    size_t num_sgn = (n >= 2u) ? n - 1u : 0u;
    std::vector<int>   sgn_idx(num_sgn);
    std::vector<qbool> sgn(num_sgn);
    for (size_t k = 0u; k < num_sgn; ++k) {
        sgn_idx[k] = QubitPool::instance().allocate();
        sgn[k]     = qbool::make_non_owning(sgn_idx[k]);
    }

    int   overflow_idx  = QubitPool::instance().allocate();
    qbool overflow      = qbool::make_non_owning(overflow_idx);
    int   carry_anc_idx = QubitPool::instance().allocate();
    qbool carry_anc     = qbool::make_non_owning(carry_anc_idx);

    // Copy dividend → partial remainder P.
    for (size_t i = 0u; i < n; ++i) remainder_bits[i] ^= dividend_bits[i];

    // ── Main loop ─────────────────────────────────────────────────────────────
    for (int32_t bit = static_cast<int32_t>(n) - 1; bit >= 0; --bit) {
        size_t i     = static_cast<size_t>(bit);
        bool   first = (i == n - 1u);
        size_t lo    = n - i;  // lo=1 when i=n-1; lo=n when i=0 (no overflow)

        // Compute overflow_i = OR(b[lo..n-1]). Flip so 1 = "proceed".
        compute_overflow_or_dsl(overflow, divisor_bits, lo, n);
        overflow.flip();

        uint32_t ov_q = static_cast<uint32_t>(overflow.qubits[0]);
        ctx.control_stack.push_control(ov_q);  // WHEN NOT(original overflow)

        // Load scratch = b << i.
        for (size_t j = i; j < n; ++j) scratch[j] ^= divisor_bits[j - i];

        // Conditionally negate scratch for subtract (sign_in=0) vs add (sign_in=1).
        // WHEN NOT(sign_in): flip scratch + set carry_anc=1 (two's complement).
        if (first) {
            for (size_t j = 0u; j < n; ++j) scratch[j].flip();
            carry_anc.flip();
        } else {
            uint32_t sgn_q = static_cast<uint32_t>(sgn[i].qubits[0]);
            sgn[i].flip();
            ctx.control_stack.push_control(sgn_q);
            for (size_t j = 0u; j < n; ++j) scratch[j].flip();
            carry_anc.flip();
            ctx.control_stack.pop_control();
            sgn[i].flip();
        }

        // Inline Cuccaro MAJ/UMA with carry_anc as carry_in.
        // Computes: remainder += scratch + carry_anc. carry_out → quotient[i].
        maj_dsl(carry_anc, remainder_bits[0], scratch[0]);
        for (size_t k = 1u; k < n; ++k)
            maj_dsl(scratch[k - 1u], remainder_bits[k], scratch[k]);
        quotient_bits[i] ^= scratch[n - 1u];  // carry_out → q[i]
        for (size_t k = n - 1u; k >= 1u; --k)
            uma_dsl(scratch[k - 1u], remainder_bits[k], scratch[k]);
        uma_dsl(carry_anc, remainder_bits[0], scratch[0]);

        // Uncompute carry_anc and scratch flip (same conditions as negate step).
        if (first) {
            carry_anc.flip();
            for (size_t j = 0u; j < n; ++j) scratch[j].flip();
        } else {
            uint32_t sgn_q = static_cast<uint32_t>(sgn[i].qubits[0]);
            sgn[i].flip();
            ctx.control_stack.push_control(sgn_q);
            carry_anc.flip();
            for (size_t j = 0u; j < n; ++j) scratch[j].flip();
            ctx.control_stack.pop_control();
            sgn[i].flip();
        }

        // Unload scratch.
        for (size_t j = i; j < n; ++j) scratch[j] ^= divisor_bits[j - i];

        // Set sgn[i-1] = NOT(q[i]) for i > 0.
        if (i > 0u) {
            quotient_bits[i].flip();
            sgn[i - 1u] ^= quotient_bits[i];
            quotient_bits[i].flip();
        }

        ctx.control_stack.pop_control();
        overflow.flip();  // restore overflow

        // Overflow propagation: WHEN overflow AND i>0: sgn[i-1] ^= sgn[i].
        if (!first && i > 0u) {
            uint32_t ov_orig_q = static_cast<uint32_t>(overflow.qubits[0]);
            ctx.control_stack.push_control(ov_orig_q);
            sgn[i - 1u] ^= sgn[i];
            ctx.control_stack.pop_control();
        }

        // Uncompute sgn[i] (set by step i+1).
        if (!first) {
            size_t lo_prev = lo - 1u;  // = n - (i+1)
            compute_overflow_or_dsl(overflow, divisor_bits, lo_prev, n);
            sgn[i] ^= quotient_bits[i + 1u];
            overflow.flip();
            {
                uint32_t ov_prev_q = static_cast<uint32_t>(overflow.qubits[0]);
                ctx.control_stack.push_control(ov_prev_q);
                sgn[i].flip();
                ctx.control_stack.pop_control();
            }
            overflow.flip();
            compute_overflow_or_dsl(overflow, divisor_bits, lo_prev, n);
        }

        // Uncompute overflow_i.
        compute_overflow_or_dsl(overflow, divisor_bits, lo, n);
    }

    // ── Final correction ──────────────────────────────────────────────────────
    // WHEN NOT(q[0]): P += b. carry_anc starts |0>.
    quotient_bits[0].flip();
    {
        uint32_t q0q = static_cast<uint32_t>(quotient_bits[0].qubits[0]);
        ctx.control_stack.push_control(q0q);
        lib_add_dsl(divisor_bits, remainder_bits, carry_anc, n);
        ctx.control_stack.pop_control();
    }
    quotient_bits[0].flip();
    // carry_anc = NOT(q[0]) after correction; uncompute to |0>.
    carry_anc ^= quotient_bits[0];
    carry_anc.flip();

    // ── Release ancilla ───────────────────────────────────────────────────────
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(overflow_idx);
    for (size_t k = 0u; k < num_sgn; ++k)
        QubitPool::instance().release(sgn_idx[num_sgn - 1u - k]);
    for (size_t k = 0u; k < n; ++k)
        QubitPool::instance().release(scratch_idx[n - 1u - k]);
}

} // namespace sturm
