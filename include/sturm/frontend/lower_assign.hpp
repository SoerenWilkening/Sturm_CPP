// lower_assign.hpp — M9 (PRD v2): frontend lowering for assignment operators.
//
// Dispatches `a op= b` DSL forms to the correct library call:
//
//   In-place ops (PRD v2 §7):
//     lower_xor_assign(s, mgr, a, b, n)  — a ^= b  →  CNOT per bit
//     lower_not_assign(s, mgr, a, n)     — a = ~a  →  NOT_reg
//     lower_add_assign(s, mgr, a, b, n)  — a += b  →  lib_add_cuccaro (b→a)
//     lower_sub_assign(s, mgr, a, b, n)  — a -= b  →  lib_sub (b→a)
//
//   Out-of-place + move ops (PRD v2 §7):
//     lower_or_assign(s, mgr, a, b, n)   — a |= b  →  fresh r, lib_OR, relabel
//     lower_and_assign(s, mgr, a, b, n)  — a &= b  →  fresh r, AND-chain, relabel
//
// In all cases:
//   a    : uint32_t[n]        — LHS qubit-index array; modified in-place for
//                               out-of-place+move ops (indices swapped after relabel).
//   b    : const uint32_t[n]  — RHS qubit-index array (never modified).
//   n    : number of qubits per register.
//   s    : SimState&.
//   mgr  : AncillaManager&.
//
// For out-of-place+move ops, the displaced a qubits (now in r[]) are assumed
// to have been |0> (unentangled) so they are returned directly to the pool.
// If a had entangled history the caller must supply a GarbageManager; that
// extended API is a TODO for M10.
//
// Target: <200 LoC (impl plan M9).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/lib/logic_basic.hpp"
#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/lib/sub.hpp"

#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── lower_xor_assign ──────────────────────────────────────────────────────────
//
// a ^= b  —  in-place XOR (one CNOT per bit).
// b unchanged; a holds (a ^ b) after the call.
inline void lower_xor_assign(SimState& s, AncillaManager& /*mgr*/,
                              uint32_t* a_idxs, const uint32_t* b_idxs,
                              uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        primitive_XOR(s, b_idxs[i], a_idxs[i]);
    }
}

// ── lower_not_assign ─────────────────────────────────────────────────────────
//
// a = ~a  —  in-place NOT (X on each qubit).
inline void lower_not_assign(SimState& s, AncillaManager& /*mgr*/,
                              uint32_t* a_idxs, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        primitive_X(s, a_idxs[i]);
    }
}

// ── lower_add_assign ──────────────────────────────────────────────────────────
//
// a += b  —  in-place Cuccaro addition (PRD v2 §7, §8).
//
// lib_add_cuccaro(s, mgr, src, acc, carry_out, n) adds src into acc.
// We want a += b, so src=b, acc=a.
//
// carry_out : a qubit (in |0>) that will hold the overflow/carry after the add.
//             The caller provides this qubit; it is NOT freed by this function.
//             To discard overflow, the caller may free carry_out if it is |0>
//             (no overflow case) or use it as a result qubit.
//
// The caller is responsible for managing carry_out.  For simple modular
// arithmetic where overflow is discarded, allocate carry_out from a region
// of the state that is not in the AncillaManager pool.
inline void lower_add_assign(SimState& s, AncillaManager& mgr,
                              uint32_t* a_idxs, const uint32_t* b_idxs,
                              uint32_t n, uint32_t carry_out) {
    if (n == 0u) return;
    lib_add_cuccaro(s, mgr, b_idxs, a_idxs, carry_out, n);
}

// ── lower_sub_assign ─────────────────────────────────────────────────────────
//
// a -= b  —  in-place subtraction (PRD v2 §7: Cuccaro-style, genuinely in-place).
//
// lib_sub(s, mgr, src, acc, borrow_out, n) performs acc -= src.
// We want a -= b, so src=b, acc=a.
//
// borrow_out : a qubit (in |0>) that will hold the borrow flag after the sub.
//              NOT freed by this function — caller is responsible.
inline void lower_sub_assign(SimState& s, AncillaManager& mgr,
                              uint32_t* a_idxs, const uint32_t* b_idxs,
                              uint32_t n, uint32_t borrow_out) {
    if (n == 0u) return;
    lib_sub(s, mgr, b_idxs, a_idxs, borrow_out, n);
}

// ── lower_or_assign ───────────────────────────────────────────────────────────
//
// a |= b  —  out-of-place OR + uncontrolled index relabel (PRD v2 §7).
//
// 1. Allocate n fresh ancilla r[] from mgr (each starts |0>).
// 2. Compute r[i] = a[i] | b[i] via lib_OR.
// 3. Index relabel: swap a_idxs[i] ↔ r[i].
//    After swap: a_idxs[i] = result qubit (pool-allocated).
//               r[i] = old a qubit (input data, NOT pool-allocated).
//
// NOTE: The displaced old a qubits (now in r[]) hold entangled state
// (a_in | b) and are NOT freed here.  The caller is responsible for
// uncomputing them via GarbageManager (PRD v2 §7).  For tests where the
// input qubits are computational-basis data, the caller may verify the
// result values and simply ignore the displaced qubits.
//
// After this call, mgr has n qubits "in use" (the result qubits now owned
// by a_idxs).  They are not freed here because they ARE the result.
inline void lower_or_assign(SimState& s, AncillaManager& mgr,
                             uint32_t* a_idxs, const uint32_t* b_idxs,
                             uint32_t n) {
    std::vector<uint32_t> r(n);
    for (uint32_t i = 0; i < n; ++i) r[i] = mgr.allocate_ancilla();

    // Compute r[i] = a[i] | b[i].
    for (uint32_t i = 0; i < n; ++i) {
        lib_OR(s, mgr, a_idxs[i], b_idxs[i], r[i]);
    }

    // Index relabel: a ← r (result), r ← old a (displaced; caller manages).
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t tmp = a_idxs[i];
        a_idxs[i] = r[i];
        r[i] = tmp;
    }
    // r[] now holds the displaced old a qubit indices.
    // Caller is responsible for uncomputing / freeing them.
}

// ── lower_and_assign ─────────────────────────────────────────────────────────
//
// a &= b  —  out-of-place AND + uncontrolled index relabel.
//
// Same structure as lower_or_assign but with primitive_AND.
// Displaced old a qubits (held in r[] after relabel) are the caller's
// responsibility (GarbageManager path per PRD v2 §7).
inline void lower_and_assign(SimState& s, AncillaManager& mgr,
                              uint32_t* a_idxs, const uint32_t* b_idxs,
                              uint32_t n) {
    std::vector<uint32_t> r(n);
    for (uint32_t i = 0; i < n; ++i) r[i] = mgr.allocate_ancilla();

    for (uint32_t i = 0; i < n; ++i) {
        primitive_AND(s, a_idxs[i], b_idxs[i], r[i]);
    }

    // Index relabel.
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t tmp = a_idxs[i];
        a_idxs[i] = r[i];
        r[i] = tmp;
    }
    // r[] now holds displaced old a qubit indices (caller manages).
}

} // namespace v2
} // namespace sturm
