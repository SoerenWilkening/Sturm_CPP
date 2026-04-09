// lower_expr.hpp — M9 (PRD v2): frontend lowering for binary-expression DSL forms.
//
// Dispatches `c = a op b` DSL forms to the correct library call.
//
// All out-of-place functions allocate a fresh result register, compute into it,
// then move the result into c via index relabel (uncontrolled, zero gates).
// c qubits are assumed to start at |0> so they are returned clean after the
// relabel without additional uncompute.
//
//   lower_expr_or(s, mgr, c, a, b, n)   — c = a | b
//   lower_expr_and(s, mgr, c, a, b, n)  — c = a & b
//   lower_expr_xor(s, mgr, c, a, b, n)  — c = a ^ b
//   lower_expr_add(s, mgr, c, a, b, n)  — c = a + b
//   lower_eq(s, mgr, a, b, out, n)      — out ^= (a == b)  (1-qubit result)
//   lower_lt(s, mgr, a, b, out, n)      — out ^= (a <  b)
//   lower_swap(s, mgr, a, b, n)         — SWAP(a,b) — uncontrolled index relabel
//
// Parameters (common):
//   c    : uint32_t[n]       — destination qubit-index array (modified by relabel).
//   a    : const uint32_t[n] — LHS source (not modified).
//   b    : const uint32_t[n] — RHS source (not modified).
//   n    : number of qubits per register.
//   out  : uint32_t           — single result qubit for comparison ops (|0> before call).
//   s    : SimState&.
//   mgr  : AncillaManager&.
//
// Target: <200 LoC (impl plan M9).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/lib/logic_basic.hpp"
#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/lib/compare.hpp"
#include "sturm/lib/swap.hpp"

#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── lower_expr_or ─────────────────────────────────────────────────────────────
//
// c = a | b  (out-of-place OR + move into c).
//
// 1. Allocate n fresh ancilla qubits r[] from mgr (start |0>).
// 2. Compute r[i] = a[i] | b[i] via lib_OR.
// 3. Relabel: c_idxs[i] ↔ r[i].
//    After relabel: c_idxs holds result qubits (pool-allocated);
//                  r holds old c qubits (input data, NOT pool-allocated).
//
// NOTE: old c qubits (now in r[]) are NOT freed — they are data qubits.
// If c started at |0> they are still |0> and can be ignored.
// After this call, mgr has n qubits "in use" (the result qubits now in c_idxs).
inline void lower_expr_or(SimState& s, AncillaManager& mgr,
                           uint32_t* c_idxs,
                           const uint32_t* a_idxs, const uint32_t* b_idxs,
                           uint32_t n) {
    std::vector<uint32_t> r(n);
    for (uint32_t i = 0; i < n; ++i) r[i] = mgr.allocate_ancilla();

    for (uint32_t i = 0; i < n; ++i) {
        lib_OR(s, mgr, a_idxs[i], b_idxs[i], r[i]);
    }

    // Relabel: c ← r (result), r ← old c.
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t tmp = c_idxs[i];
        c_idxs[i] = r[i];
        r[i] = tmp;
    }
    // Old c qubits (in r[]) are data qubits; not freed here.
}

// ── lower_expr_and ────────────────────────────────────────────────────────────
//
// c = a & b  (out-of-place AND + move into c).
// Same structure as lower_expr_or. Old c qubits NOT freed.
inline void lower_expr_and(SimState& s, AncillaManager& mgr,
                            uint32_t* c_idxs,
                            const uint32_t* a_idxs, const uint32_t* b_idxs,
                            uint32_t n) {
    std::vector<uint32_t> r(n);
    for (uint32_t i = 0; i < n; ++i) r[i] = mgr.allocate_ancilla();

    for (uint32_t i = 0; i < n; ++i) {
        primitive_AND(s, a_idxs[i], b_idxs[i], r[i]);
    }

    // Relabel: c ← r (result), r ← old c (data qubits; not freed).
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t tmp = c_idxs[i];
        c_idxs[i] = r[i];
        r[i] = tmp;
    }
}

// ── lower_expr_xor ────────────────────────────────────────────────────────────
//
// c = a ^ b  (XOR directly into c; c starts |0>).
// XOR is genuinely in-place on a target: c[i] ^= a[i]; c[i] ^= b[i].
// c starts |0> so c = a ^ b after the call.
inline void lower_expr_xor(SimState& s, AncillaManager& /*mgr*/,
                            uint32_t* c_idxs,
                            const uint32_t* a_idxs, const uint32_t* b_idxs,
                            uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        primitive_XOR(s, a_idxs[i], c_idxs[i]);
        primitive_XOR(s, b_idxs[i], c_idxs[i]);
    }
}

// ── lower_expr_add ────────────────────────────────────────────────────────────
//
// c = a + b  (out-of-place ADD + move into c).
//
// 1. Allocate n fresh ancilla r[] (start |0>).
// 2. Copy a into r: r[i] ^= a[i]  →  r = a.
// 3. In-place add: lib_add_cuccaro(b, r, carry_out, n)  →  r = (a+b) mod 2^n.
//    carry_out receives the overflow bit (remains set/unset after the call).
// 4. Relabel: c ← r, r ← old c (= |0>).
// 5. Free old c qubits (they were |0>).
//
// carry_out : a qubit (in |0>) supplied by the caller; holds overflow after the
//             call. NOT freed by this function — caller is responsible.
//             For tests where overflow is discarded, pass a dedicated qubit
//             outside the AncillaManager pool.
//
// a and b are left unchanged.
inline void lower_expr_add(SimState& s, AncillaManager& mgr,
                            uint32_t* c_idxs,
                            const uint32_t* a_idxs, const uint32_t* b_idxs,
                            uint32_t n, uint32_t carry_out) {
    // Scratch accumulator r[] (starts |0>).
    std::vector<uint32_t> r(n);
    for (uint32_t i = 0; i < n; ++i) r[i] = mgr.allocate_ancilla();

    // Copy a into r via XOR.
    for (uint32_t i = 0; i < n; ++i) {
        primitive_XOR(s, a_idxs[i], r[i]);
    }

    // In-place add: r += b; carry_out receives overflow bit.
    lib_add_cuccaro(s, mgr, b_idxs, r.data(), carry_out, n);

    // Relabel: c ← r (result), r ← old c (data qubits; not freed here).
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t tmp = c_idxs[i];
        c_idxs[i] = r[i];
        r[i] = tmp;
    }
    // Old c qubits (in r[]) are data qubits (started |0>); not freed here.
}

// ── lower_eq ─────────────────────────────────────────────────────────────────
//
// out ^= (a == b)  —  delegates to lib_EQ.
// Used to lower `if (a == b)` DSL construct.
// out must be |0> before the call for the result to equal (a == b).
inline void lower_eq(SimState& s, AncillaManager& mgr,
                     const uint32_t* a_idxs, const uint32_t* b_idxs,
                     uint32_t out, uint32_t n) {
    lib_EQ(s, mgr, a_idxs, b_idxs, out, n);
}

// ── lower_lt ─────────────────────────────────────────────────────────────────
//
// out ^= (a < b)  —  delegates to lib_LT.
inline void lower_lt(SimState& s, AncillaManager& mgr,
                     const uint32_t* a_idxs, const uint32_t* b_idxs,
                     uint32_t out, uint32_t n) {
    lib_LT(s, mgr, a_idxs, b_idxs, out, n);
}

// ── lower_swap ────────────────────────────────────────────────────────────────
//
// SWAP(a, b)  —  uncontrolled: pure index relabel, zero gates emitted.
// Delegates to lib_SWAP.
inline void lower_swap(SimState& s, AncillaManager& mgr,
                       uint32_t* a_idxs, uint32_t* b_idxs, uint32_t n) {
    lib_SWAP(s, mgr, a_idxs, b_idxs, n);
}

} // namespace v2
} // namespace sturm
