// adder_dsl.hpp — M14 (PRD v3): Cuccaro adder/subtractor in DSL style.
//
// Rewrites the classic Cuccaro MAJ/UMA and in-place adder/subtractor using
// qbool operators (^=, &) instead of direct primitive calls.
//
// Functions in sturm:: namespace:
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
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace sturm {

// ── Ancilla helper ───────────────────────────────────────────────────────────
// Creates a Bit that views a qbool's fields.  For Bit=BitProxy, uses the
// BitProxy(qbool&) constructor (stores pointers into the qbool).  For
// Bit=qbool, creates a non-owning qbool aliasing the qbool's qubit.
// The owning qbool's destructor releases the qubit.
namespace detail_adder {

template <typename Bit>
Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

} // namespace detail_adder

// ── MAJ gate ─────────────────────────────────────────────────────────────────
//
// MAJ(a, b, c):
//   b ^= c;            CNOT(c, b)
//   a ^= c;            CNOT(c, a)
//   c ^= (a & b);      Toffoli via operator^=
//
// Result: c = majority(a_in, b_in, c_in)
//         b = b_in XOR c_in
//         a = a_in XOR c_in
//
// In the Cuccaro adder context:
//   c = carry_in qubit (before), carry_out qubit (after)
//   a = carry register bit (modified; restored by UMA)
//   b = b_i register bit (modified; restored by UMA)
template <typename Bit>
inline void maj_dsl(Bit& a, Bit& b, Bit& c) {
    b ^= c;           // CNOT: b ^= c
    a ^= c;           // CNOT: a ^= c
    c ^= (a & b);     // single Toffoli via operator^=
}

// ── UMA gate ─────────────────────────────────────────────────────────────────
//
// UMA(a, b, c) — "unmajority and add" (inverse of MAJ):
//   c ^= (a & b);     Toffoli (undo the MAJ Toffoli)
//   a ^= c;           CNOT: restores a to pre-MAJ value
//   b ^= a;           CNOT: b becomes the sum bit
//
// After UMA: a is restored, b = sum bit, c = carry_in (restored).
template <typename Bit>
inline void uma_dsl(Bit& a, Bit& b, Bit& c) {
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
template <typename Bit>
inline void lib_add_dsl(Bit* a_bits, Bit* b_bits, Bit& carry_out, size_t n) {
    if (n == 0u) return;

    // Owning qbool for the initial carry_in ancilla (starts |0>).
    // qbool destructor auto-releases the qubit.
    qbool carry_anc_qbool;
    carry_anc_qbool.qubits[0]  = QubitPool::instance().allocate();
    carry_anc_qbool.super_mask = 1;
    Bit carry_anc = detail_adder::make_ancilla_view<Bit>(carry_anc_qbool);

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

    // carry_anc_qbool destructor handles release (UMA restored it to |0>).
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
template <typename Bit>
inline void lib_sub_dsl(Bit* a_bits, Bit* b_bits, Bit& borrow_out, size_t n) {
    if (n == 0u) return;

    // Owning qbool for carry_in ancilla, initialized to |1> (+1 for two's complement).
    // qbool destructor auto-releases the qubit.
    qbool carry_anc_qbool;
    carry_anc_qbool.qubits[0]  = QubitPool::instance().allocate();
    carry_anc_qbool.super_mask = 1;
    carry_anc_qbool.value      = 1;  // classical 1 for two's complement +1
    Bit carry_anc = detail_adder::make_ancilla_view<Bit>(carry_anc_qbool);

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

    // carry_anc_qbool destructor handles release.
}

} // namespace sturm
