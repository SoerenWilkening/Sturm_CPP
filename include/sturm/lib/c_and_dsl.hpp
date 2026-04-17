// c_and_dsl.hpp — M15 (PRD v3): c_AND and c_n_AND in DSL style using qbool operators.
//
// lib_c_AND_dsl(c0, c1, tgt):
//   2-control AND (Toffoli): tgt ^= (c0 & c1).
//   Direct Toffoli — no ancilla needed for 2 controls.
//   Gate count: 1 CCX.
//
// lib_c_n_AND_dsl(controls, n_controls, tgt):
//   n-control AND via Nielsen-Chuang sandwich decomposition.
//   n=2: single Toffoli (same as lib_c_AND_dsl).
//   n>=3: recursive decomposition using owning ancilla qbools.
//     Forward sweep:
//       ancillas[0] ^= (controls[0] & controls[1])
//       ancillas[i] ^= (controls[i+1] & ancillas[i-1])   for i=1..n-3
//       tgt         ^= (controls[n-1] & ancillas[n-3])
//     Reverse sweep (mirror the forward sweep to clean ancillas):
//       ancillas[i] ^= (controls[i+1] & ancillas[i-1])   for i=n-3..1
//       ancillas[0] ^= (controls[0] & controls[1])
//
//   Space: n-2 ancilla qbools allocated as owning, all returned |0> after.
//   Gate count (n>=3): 2*(n-1) CCX gates (forward n-1 + reverse n-2, actually
//     2*(n-2)+1 = 2n-3 CCX total: n-1 forward, n-2 reverse excluding target).
//
// All functions in sturm:: namespace.
// No _when variants. WHEN lifting is automatic via qbool operators.
//
// Target: <150 LoC.

#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace sturm {

// ── Ancilla helper ───────────────────────────────────────────────────────────
namespace detail_c_and {
template <typename Bit>
Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) return qbool::make_non_owning(owner.qubits[0]);
    else return Bit(owner);
}
} // namespace detail_c_and

// ── lib_c_AND_dsl ─────────────────────────────────────────────────────────────
// 2-control AND: tgt ^= (c0 & c1).  Single Toffoli (CCX) — no ancilla needed.
// Gate cost: 1 CCX.
template <typename Bit>
inline void lib_c_AND_dsl(Bit& c0, Bit& c1, Bit& tgt) {
    tgt ^= (c0 & c1);   // AndExpr path: single CCX
}

// ── lib_c_n_AND_dsl ───────────────────────────────────────────────────────────
//
// n-control AND (C^n-X) gate: flips tgt iff ALL n control qubits are |1>.
//
// controls   — pointer to array of n qbool objects (non-owning ok).
// n_controls — number of controls (must be >= 1).
// tgt        — target qbool to flip.
//
// n=1: CNOT (CX) via operator^=.
// n=2: Toffoli (CCX) via lib_c_AND_dsl.
// n>=3: Nielsen-Chuang linear decomposition.
//
// For n>=3, allocates (n-2) owning ancilla qbools from QubitPool.
// Each ancilla is in |0> before the call and is restored to |0> after.
//
// Gate cost (n>=3): (n-1) CCX forward + (n-2) CCX reverse = (2n-3) CCX total.
template <typename Bit>
inline void lib_c_n_AND_dsl(Bit* controls, size_t n_controls, Bit& tgt) {
    if (n_controls == 0u) {
        // No controls: unconditional X.
        tgt.flip();
        return;
    }
    if (n_controls == 1u) {
        // Single control: CNOT.
        tgt ^= controls[0];
        return;
    }
    if (n_controls == 2u) {
        // Two controls: single Toffoli.
        lib_c_AND_dsl(controls[0], controls[1], tgt);
        return;
    }

    // n>=3: Nielsen-Chuang linear decomposition.
    // Allocate (n-2) ancilla qbools as owning.
    const size_t n_anc = n_controls - 2u;

    // Allocate ancilla qubit indices from the pool.
    // Using a fixed-size stack array (max controls bounded by kMaxAnc).
    static constexpr size_t kMaxAnc = 30u;
    assert(n_anc <= kMaxAnc && "lib_c_n_AND_dsl: too many controls");

    int anc_idx[kMaxAnc];
    for (size_t i = 0; i < n_anc; ++i) {
        anc_idx[i] = QubitPool::instance().allocate();
    }

    // Build owning qbool storage + Bit views for ancillas.
    qbool anc_own[kMaxAnc];
    Bit   anc[kMaxAnc];
    for (size_t i = 0; i < n_anc; ++i) {
        anc_own[i] = qbool::make_non_owning(anc_idx[i]);
        anc[i]     = detail_c_and::make_ancilla_view<Bit>(anc_own[i]);
    }

    // ── Forward sweep ─────────────────────────────────────────────────────────
    // anc[0] ^= (controls[0] & controls[1])
    anc[0] ^= (controls[0] & controls[1]);

    // anc[i] ^= (controls[i+1] & anc[i-1])  for i = 1..n_anc-1
    for (size_t i = 1u; i < n_anc; ++i) {
        anc[i] ^= (controls[i + 1u] & anc[i - 1u]);
    }

    // Apply to target: tgt ^= (controls[n-1] & anc[n_anc-1])
    tgt ^= (controls[n_controls - 1u] & anc[n_anc - 1u]);

    // ── Reverse sweep (uncompute ancillas) ────────────────────────────────────
    // Mirror of forward sweep (excluding target application).
    for (size_t i = n_anc - 1u; i >= 1u; --i) {
        anc[i] ^= (controls[i + 1u] & anc[i - 1u]);
    }
    anc[0] ^= (controls[0] & controls[1]);

    // Release ancilla qubit indices back to pool.
    for (size_t i = 0; i < n_anc; ++i) {
        QubitPool::instance().release(anc_idx[i]);
    }
}

} // namespace sturm
