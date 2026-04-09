// compare.hpp — M8 (PRD v2): EQ, LT, LE, GT, GE via tree-of-ANDs on XOR'd bits.
//
// lib_EQ(s, mgr, a_idxs, b_idxs, out, n): out ^= (a == b)
// lib_LT(s, mgr, a_idxs, b_idxs, out, n): out ^= (a <  b)  unsigned
// lib_LE(s, mgr, a_idxs, b_idxs, out, n): out ^= (a <= b) = lib_LT + lib_EQ
// lib_GT(s, mgr, a_idxs, b_idxs, out, n): out ^= (a >  b) = lib_LT(b,a)
// lib_GE(s, mgr, a_idxs, b_idxs, out, n): out ^= (a >= b) = lib_LE(b,a)
//
// EQ: out ^= AND(XNOR(a[i],b[i]) for all i).  n ancilla for XNOR bits.
// LT: MSB-to-LSB mutually-exclusive terms.
//     term[n-1] = NOT(a[n-1]) AND b[n-1]
//     term[i]   = AND(eq_prefix[n-1..i+1], NOT(a[i]), b[i])
//   Accumulated via XOR (terms are mutually exclusive).
// LE: lib_LT + lib_EQ (both results XOR'd in; they're mutually exclusive).
// GT: lib_LT(b,a).   GE: lib_LE(b,a).
//
// Ancilla budget (n=3): eq_chain(3) + eq_prefix(2) + lt_bit(1) + cnand(1) = 7.
// Fits within the 17-qubit cap (pool = 8 for the test layout).
//
// Target: <250 LoC (impl plan M8).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/lib/c_n_and.hpp"

#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── compute_xnor_bit ──────────────────────────────────────────────────────────
//
// anc ^= XNOR(a,b) = NOT(a XOR b).  Uncompute: call again (same circuit).
static inline void compute_xnor_into(SimState& s,
                                     uint32_t a, uint32_t b,
                                     uint32_t anc) {
    primitive_XOR(s, a, anc);
    primitive_XOR(s, b, anc);
    primitive_X(s, anc);
}

static inline void uncompute_xnor_from(SimState& s,
                                       uint32_t a, uint32_t b,
                                       uint32_t anc) {
    primitive_X(s, anc);
    primitive_XOR(s, b, anc);
    primitive_XOR(s, a, anc);
}

// ── compute_lt_bit ────────────────────────────────────────────────────────────
//
// anc ^= (NOT(a) AND b).  Self-inverse (call uncompute version to zero anc).
static inline void compute_lt_bit_into(SimState& s,
                                       uint32_t a, uint32_t b,
                                       uint32_t anc) {
    primitive_X(s, a);
    primitive_AND(s, a, b, anc);
    primitive_X(s, a);
}

static inline void uncompute_lt_bit_from(SimState& s,
                                         uint32_t a, uint32_t b,
                                         uint32_t anc) {
    // AND is self-inverse as a gate (Toffoli).
    primitive_X(s, a);
    primitive_AND(s, a, b, anc);  // uncompute anc
    primitive_X(s, a);
}

// ── lib_EQ ────────────────────────────────────────────────────────────────────
//
// out ^= (a == b).  n ancilla for XNOR bits; all returned clean.
inline void lib_EQ(SimState& s, AncillaManager& mgr,
                   const uint32_t* a_idxs,
                   const uint32_t* b_idxs,
                   uint32_t out,
                   uint32_t n) {
    if (n == 0u) {
        // Zero-width registers are vacuously equal; flip out unconditionally.
        primitive_X(s, out);
        return;
    }

    // Allocate n ancilla for XNOR bits.
    std::vector<uint32_t> eq(n);
    for (uint32_t i = 0u; i < n; ++i) {
        eq[i] = mgr.allocate_ancilla();
    }

    // Compute eq[i] = XNOR(a[i], b[i]) for each bit.
    for (uint32_t i = 0u; i < n; ++i) {
        compute_xnor_into(s, a_idxs[i], b_idxs[i], eq[i]);
    }

    // AND all eq[i] into out.
    lib_c_n_AND(s, mgr, eq.data(), n, out);

    // Uncompute eq[i] in reverse order.
    for (uint32_t i = n; i-- > 0u;) {
        uncompute_xnor_from(s, a_idxs[i], b_idxs[i], eq[i]);
    }

    // Free ancilla in reverse allocation order.
    for (uint32_t i = n; i-- > 0u;) {
        mgr.free_ancilla(eq[i]);
    }
}

// ── lib_LT ────────────────────────────────────────────────────────────────────
//
// out ^= (a < b) unsigned.  MSB-to-LSB mutually-exclusive term accumulation.
// eq_chain[n] stays live; per-term ancilla allocated and freed each iteration.
inline void lib_LT(SimState& s, AncillaManager& mgr,
                   const uint32_t* a_idxs,
                   const uint32_t* b_idxs,
                   uint32_t out,
                   uint32_t n) {
    if (n == 0u) return;  // empty register: never less than

    if (n == 1u) {
        // Simple: out ^= NOT(a[0]) AND b[0]
        uint32_t lt_anc = mgr.allocate_ancilla();
        compute_lt_bit_into(s, a_idxs[0], b_idxs[0], lt_anc);
        primitive_XOR(s, lt_anc, out);
        uncompute_lt_bit_from(s, a_idxs[0], b_idxs[0], lt_anc);
        mgr.free_ancilla(lt_anc);
        return;
    }

    // eq_chain[i] = XNOR(a[i], b[i]), stays live for the full prefix-AND tree.
    std::vector<uint32_t> eq_chain(n);
    for (uint32_t i = 0u; i < n; ++i) {
        eq_chain[i] = mgr.allocate_ancilla();
    }
    for (uint32_t i = 0u; i < n; ++i) {
        compute_xnor_into(s, a_idxs[i], b_idxs[i], eq_chain[i]);
    }

    for (uint32_t bit = n; bit-- > 0u;) {
        uint32_t i      = bit;
        uint32_t lt_anc = mgr.allocate_ancilla();
        compute_lt_bit_into(s, a_idxs[i], b_idxs[i], lt_anc);

        if (i == n - 1u) {
            primitive_XOR(s, lt_anc, out);  // top bit: no eq prefix
        } else {
            uint32_t prefix_len    = n - 1u - i;
            uint32_t eq_prefix_anc = 0u;
            bool     alloc_prefix  = false;
            if (prefix_len == 1u) {
                eq_prefix_anc = eq_chain[n - 1u];
            } else {
                eq_prefix_anc = mgr.allocate_ancilla();
                alloc_prefix  = true;
                std::vector<uint32_t> ctrls(prefix_len);
                for (uint32_t k = 0u; k < prefix_len; ++k)
                    ctrls[k] = eq_chain[i + 1u + k];
                lib_c_n_AND(s, mgr, ctrls.data(), prefix_len, eq_prefix_anc);
            }
            uint32_t term_anc      = mgr.allocate_ancilla();
            uint32_t ctrls2[2]     = { eq_prefix_anc, lt_anc };
            lib_c_n_AND(s, mgr, ctrls2, 2u, term_anc);
            primitive_XOR(s, term_anc, out);
            lib_c_n_AND(s, mgr, ctrls2, 2u, term_anc);  // uncompute
            mgr.free_ancilla(term_anc);
            if (alloc_prefix) {
                std::vector<uint32_t> ctrls(prefix_len);
                for (uint32_t k = 0u; k < prefix_len; ++k)
                    ctrls[k] = eq_chain[i + 1u + k];
                lib_c_n_AND(s, mgr, ctrls.data(), prefix_len, eq_prefix_anc);
                mgr.free_ancilla(eq_prefix_anc);
            }
        }
        uncompute_lt_bit_from(s, a_idxs[i], b_idxs[i], lt_anc);
        mgr.free_ancilla(lt_anc);
    }

    // Uncompute eq_chain in reverse.
    for (uint32_t i = n; i-- > 0u;) {
        uncompute_xnor_from(s, a_idxs[i], b_idxs[i], eq_chain[i]);
    }
    for (uint32_t i = n; i-- > 0u;) {
        mgr.free_ancilla(eq_chain[i]);
    }
}

// ── lib_LE ────────────────────────────────────────────────────────────────────
//
// out ^= (a <= b) = (a < b) OR (a == b).  LT and EQ are mutually exclusive.
inline void lib_LE(SimState& s, AncillaManager& mgr,
                   const uint32_t* a_idxs,
                   const uint32_t* b_idxs,
                   uint32_t out,
                   uint32_t n) {
    lib_LT(s, mgr, a_idxs, b_idxs, out, n);
    lib_EQ(s, mgr, a_idxs, b_idxs, out, n);
}

// ── lib_GT ────────────────────────────────────────────────────────────────────
//
// out ^= (a > b) unsigned.  a > b iff b < a.
inline void lib_GT(SimState& s, AncillaManager& mgr,
                   const uint32_t* a_idxs,
                   const uint32_t* b_idxs,
                   uint32_t out,
                   uint32_t n) {
    lib_LT(s, mgr, b_idxs, a_idxs, out, n);
}

// ── lib_GE ────────────────────────────────────────────────────────────────────
//
// out ^= (a >= b) unsigned.  a >= b iff b <= a.
inline void lib_GE(SimState& s, AncillaManager& mgr,
                   const uint32_t* a_idxs,
                   const uint32_t* b_idxs,
                   uint32_t out,
                   uint32_t n) {
    lib_LE(s, mgr, b_idxs, a_idxs, out, n);
}

} // namespace v2
} // namespace sturm
