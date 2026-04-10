// adder_dsl.hpp — M14 (PRD v3): Cuccaro adder/subtractor in DSL style.
//
// Rewrites the classic Cuccaro MAJ/UMA and in-place adder/subtractor using
// qbool operators (^=, &) instead of direct primitive calls.
//
// Functions in sturm:: namespace (NOT sturm::v2::):
//
//   maj_dsl(a, b, c)            — MAJ gate via qbool operators.
//   uma_dsl(a, b, c)            — UMA gate (inverse of MAJ) via qbool operators.
//   lib_add_dsl(a, b, carry, n) — in-place n-bit Cuccaro adder: b += a.
//   lib_sub_dsl(a, b, borrow, n)— in-place n-bit subtraction: b -= a.
//
// No explicit BackendContext parameter — all operators read the TLS context
// via sturm_get_thread_context() internally (inside qbool_ops.hpp).
// WHEN lifting is automatic: qbool operators consult the control stack.
//
// Gate cost for n-bit addition:
//   n MAJ gates: each = 2 CX + 1 CCX => 3n gates
//   n UMA gates: each = 1 CCX + 2 CX => 3n gates
//   1 CX for carry copy
//   Total: 6n + 1 gates
//
// Target: <250 LoC.

#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cstddef>
#include <cassert>

namespace sturm {

// ── MAJ gate ─────────────────────────────────────────────────────────────────
//
// MAJ(a, b, c):
//   b ^= c;            CNOT(c, b)
//   a ^= c;            CNOT(c, a)
//   c ^= (a & b);      Toffoli via AndExpr
//
// Result: c = majority(a_in, b_in, c_in)
//         b = b_in XOR c_in
//         a = a_in XOR c_in
//
// In the Cuccaro adder context:
//   c = carry_in qubit (before), carry_out qubit (after)
//   a = carry register bit (modified; restored by UMA)
//   b = b_i register bit (modified; restored by UMA)
inline void maj_dsl(qbool& a, qbool& b, qbool& c) {
    b ^= c;           // CNOT: b ^= c
    a ^= c;           // CNOT: a ^= c
    c ^= (a & b);     // single Toffoli via AndExpr
}

// ── UMA gate ─────────────────────────────────────────────────────────────────
//
// UMA(a, b, c) — "unmajority and add" (inverse of MAJ):
//   c ^= (a & b);     Toffoli (undo the MAJ Toffoli)
//   a ^= c;           CNOT: restores a to pre-MAJ value
//   b ^= a;           CNOT: b becomes the sum bit
//
// After UMA: a is restored, b = sum bit, c = carry_in (restored).
inline void uma_dsl(qbool& a, qbool& b, qbool& c) {
    c ^= (a & b);     // Toffoli (undo)
    a ^= c;           // CNOT
    b ^= a;           // CNOT
}

// ── lib_add_dsl ───────────────────────────────────────────────────────────────
//
// In-place n-bit Cuccaro ripple-carry adder: b += a.
//
// Parameters:
//   a_bits   — pointer to n qbool objects for a (LSB = [0], non-owning ok).
//              a is left unchanged after the call.
//   b_bits   — pointer to n qbool objects for b (LSB = [0], non-owning ok).
//              b is overwritten with (a + b) mod 2^n.
//   carry_out — qbool for the carry output (must be in |0> state).
//               Set to 1 iff a + b >= 2^n (overflow).
//   n         — register width (must be >= 1); n == 0 is a no-op.
//
// Ancilla usage: 1 qubit borrowed from QubitPool for carry_in scratch.
// BackendContext is read from TLS via sturm_get_thread_context().
// WHEN lifting is automatic via qbool operators consulting the control stack.
//
// Gate cost: 6n + 1 (n MAJ * 3 + n UMA * 3 + 1 CX for carry copy).
inline void lib_add_dsl(qbool* a_bits, qbool* b_bits, qbool& carry_out, size_t n) {
    if (n == 0u) return;

    // Borrow 1 ancilla for the initial carry_in (starts |0>).
    int carry_anc_idx = QubitPool::instance().allocate();
    qbool carry_anc   = qbool::make_non_owning(carry_anc_idx);

    // ── Forward pass: MAJ chain ───────────────────────────────────────────────
    // carry propagates through: carry_anc -> a[0] -> a[1] -> ... -> a[n-1]
    // MAJ arguments: (carry_prev, b[i], a[i])
    //   After each MAJ: a[i] holds carry_out; carry_prev is modified.

    // First MAJ: carry_in = carry_anc
    maj_dsl(carry_anc, b_bits[0], a_bits[0]);

    for (size_t i = 1u; i < n; ++i) {
        // carry is now in a_bits[i-1]
        maj_dsl(a_bits[i - 1u], b_bits[i], a_bits[i]);
    }

    // After the last MAJ, carry-out of bit n-1 is in a_bits[n-1].
    // Copy it to carry_out via CNOT.
    carry_out ^= a_bits[n - 1u];

    // ── Backward pass: UMA chain ──────────────────────────────────────────────
    // UMA(carry_prev, b[i], a[i]) — same argument convention as MAJ.
    // Reverse order: i = n-1 down to 1, then UMA(carry_anc, b[0], a[0]).
    for (size_t i = n - 1u; i >= 1u; --i) {
        uma_dsl(a_bits[i - 1u], b_bits[i], a_bits[i]);
    }
    // Last UMA: carry_in = carry_anc
    uma_dsl(carry_anc, b_bits[0], a_bits[0]);

    // Return carry ancilla (UMA restored it to |0>).
    QubitPool::instance().release(carry_anc_idx);
}

// ── lib_sub_dsl ───────────────────────────────────────────────────────────────
//
// In-place n-bit subtraction: b -= a.
// Uses two's complement: b - a = b + (~a) + 1 mod 2^n.
//
// Parameters:
//   a_bits    — pointer to n qbool objects for a (LSB = [0]).
//               a is left unchanged after the call.
//   b_bits    — pointer to n qbool objects for b (LSB = [0]).
//               b is overwritten with (b - a) mod 2^n.
//   borrow_out — qbool for the borrow/underflow output (must be in |0>).
//                Set to 1 iff b < a (underflow).
//   n          — register width (must be >= 1); n == 0 is a no-op.
//
// Ancilla: 1 qubit borrowed from QubitPool for carry_in scratch.
// WHEN lifting is automatic via qbool operators.
inline void lib_sub_dsl(qbool* a_bits, qbool* b_bits, qbool& borrow_out, size_t n) {
    if (n == 0u) return;

    // Borrow 1 ancilla as carry_in scratch, initialized to |1> (+1 for two's complement).
    int carry_anc_idx = QubitPool::instance().allocate();
    qbool carry_anc   = qbool::make_non_owning(carry_anc_idx);

    // Set carry_anc = |1> (+1 for two's complement).
    carry_anc.flip();

    // Flip a in-place: compute ~a.
    for (size_t i = 0u; i < n; ++i) {
        a_bits[i].flip();
    }

    // ── Forward MAJ chain: b + (~a) + 1 ──────────────────────────────────────
    maj_dsl(carry_anc, b_bits[0], a_bits[0]);
    for (size_t i = 1u; i < n; ++i) {
        maj_dsl(a_bits[i - 1u], b_bits[i], a_bits[i]);
    }

    // Final carry in a[n-1]: 1 iff b >= a. borrow = NOT(carry).
    // borrow_out ^= a[n-1] then borrow_out.flip() gives NOT(carry).
    borrow_out ^= a_bits[n - 1u];
    borrow_out.flip();

    // ── Backward UMA chain ────────────────────────────────────────────────────
    for (size_t i = n - 1u; i >= 1u; --i) {
        uma_dsl(a_bits[i - 1u], b_bits[i], a_bits[i]);
    }
    uma_dsl(carry_anc, b_bits[0], a_bits[0]);

    // Restore a and carry_anc to original values.
    for (size_t i = 0u; i < n; ++i) {
        a_bits[i].flip();
    }
    carry_anc.flip();  // restore carry_anc to |0>

    // Return carry ancilla.
    QubitPool::instance().release(carry_anc_idx);
}

} // namespace sturm
