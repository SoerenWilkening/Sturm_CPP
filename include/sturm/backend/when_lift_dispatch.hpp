// when_lift_dispatch.hpp — M2 (PRD v2): dispatch table for primitives under control.
//
// Provides the c_and_impl function declared in when_lift.hpp.  This is the
// bridge between the backend WHEN lift machinery and the library c_AND /
// c_n_AND routines (M3).
//
// c_and_impl(s, mgr, ctrls, n_ctrls, tgt):
//   Implements a C^n-X gate (flip tgt iff all ctrls are 1) using the
//   Nielsen-Chuang relative-phase Toffoli sandwich pattern.
//
// For n_ctrls == 2: direct Toffoli (no ancilla needed).
// For n_ctrls == 3: one borrowed ancilla: anc = c0 AND c1, then CCX(anc,c2,tgt), uncompute.
// For n_ctrls >= 4: recursive: AND-fold first two controls into ancilla, recurse.
//
// This matches the PRD v2 §4 description:
//   "AND(b,c→d) under WHEN a → C³-X, which is library code (c_AND) using the
//    Nielsen-Chuang sandwich pattern and one borrowed ancilla."
//
// Target: <200 LoC (impl plan M2).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/ancilla.hpp"

#include <cstdint>
#include <stdexcept>

namespace sturm {
namespace v2 {

// ── c_and_impl ────────────────────────────────────────────────────────────────
//
// Implements C^n-X: flip tgt iff all qubits in ctrls[0..n_ctrls-1] are 1.
//
// Algorithm (Nielsen-Chuang sandwich, recursive):
//   n=1: CX(ctrls[0], tgt)
//   n=2: CCX(ctrls[0], ctrls[1], tgt)
//   n≥3: anc = allocate_ancilla()
//        CCX(ctrls[0], ctrls[1], anc)        [compute anc = c0 AND c1]
//        c_and_impl(s, mgr, {anc, c2,...,c_{n-1}}, n-1, tgt)  [recurse]
//        CCX(ctrls[0], ctrls[1], anc)        [uncompute ancilla]
//        free_ancilla(anc)

inline void c_and_impl(SimState& s, AncillaManager& mgr,
                       const uint32_t* ctrls, uint32_t n_ctrls, uint32_t tgt) {
    if (n_ctrls == 0u) {
        // Degenerate: flip unconditionally.
        primitive_X(s, tgt);
        return;
    }
    if (n_ctrls == 1u) {
        primitive_XOR(s, ctrls[0], tgt);
        return;
    }
    if (n_ctrls == 2u) {
        primitive_AND(s, ctrls[0], ctrls[1], tgt);
        return;
    }

    // n_ctrls >= 3: borrow ancilla, compute anc = ctrls[0] AND ctrls[1], recurse.
    uint32_t anc = mgr.allocate_ancilla();

    // Forward: anc = ctrls[0] AND ctrls[1]
    primitive_AND(s, ctrls[0], ctrls[1], anc);

    // Build the reduced control list: [anc, ctrls[2], ..., ctrls[n_ctrls-1]]
    // Use a small stack-allocated buffer for up to 20 controls.
    static constexpr uint32_t kMaxCtrls = 20u;
    if (n_ctrls > kMaxCtrls) {
        throw std::runtime_error("c_and_impl: too many controls (max 20)");
    }
    uint32_t reduced[kMaxCtrls];
    reduced[0] = anc;
    for (uint32_t i = 2u; i < n_ctrls; ++i) {
        reduced[i - 1u] = ctrls[i];
    }
    uint32_t reduced_n = n_ctrls - 1u;

    // Recurse.
    c_and_impl(s, mgr, reduced, reduced_n, tgt);

    // Uncompute: anc = ctrls[0] AND ctrls[1] again (self-inverse).
    primitive_AND(s, ctrls[0], ctrls[1], anc);

    mgr.free_ancilla(anc);
}

} // namespace v2
} // namespace sturm
