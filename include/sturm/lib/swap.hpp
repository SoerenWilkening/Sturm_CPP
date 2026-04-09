// swap.hpp — M4 (PRD v2): SWAP / Fredkin with context-aware dispatch.
//
// Two implementations, same name (PRD v2 §6):
//
//   Uncontrolled SWAP — zero gates:
//     lib_SWAP(s, mgr, a_idxs, b_idxs, n)
//       Swaps the qubit-index arrays for registers a and b in place.
//       No primitive is emitted; the SimState is untouched.  This is the
//       "compile-time index relabel" described in PRD v2 §6.
//
//   Controlled SWAP (Fredkin):
//     lib_c_SWAP(s, mgr, ctrl, a_idxs, b_idxs, n)
//       Emits per qubit pair: XOR(a,b); AND(ctrl, b, a); XOR(a,b).
//       That is 2 XOR + 1 AND per pair.  No ancilla needed.
//
//   Context-aware dispatch:
//     lib_SWAP_when(wl, s, mgr, ctrl, a_idxs, b_idxs, n)
//       If WhenLift has an active control stack (wl.control_depth() > 0),
//       routes to lib_c_SWAP (Fredkin) using the provided ctrl qubit.
//       Otherwise falls back to lib_SWAP (index relabel, zero gates).
//
// Trap avoided (PRD v2 §6): SWAP is never implemented as three XORs in the
// uncontrolled case. The Fredkin decomposition (XOR;AND;XOR) is only used
// inside lib_c_SWAP for the controlled case.  Routing WHEN-SWAP through
// lib_c_SWAP ensures the controlled path costs 2 XOR + 1 AND (not 3 AND
// from three CCNOTs).
//
// Target: <200 LoC (impl plan M4).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/when_lift.hpp"

#include <cstdint>

namespace sturm {
namespace v2 {

// ── lib_SWAP ──────────────────────────────────────────────────────────────────
//
// Uncontrolled SWAP: zero gates emitted.
//
// Swaps the contents of the index arrays a_idxs[0..n-1] and b_idxs[0..n-1]
// in place.  The SimState is left completely unchanged — no physical gate is
// applied.
//
// Precondition: a_idxs and b_idxs are non-null, each pointing to n elements.
// n == 0 is a no-op.
inline void lib_SWAP(SimState& /*s*/, AncillaManager& /*mgr*/,
                     uint32_t* a_idxs, uint32_t* b_idxs, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t tmp = a_idxs[i];
        a_idxs[i]   = b_idxs[i];
        b_idxs[i]   = tmp;
    }
}

// ── lib_c_SWAP (Fredkin) ──────────────────────────────────────────────────────
//
// Controlled SWAP (Fredkin gate) for n qubit pairs.
//
// For each pair (a_idxs[i], b_idxs[i]):
//   XOR(a, b)           —  b ^= a
//   AND(ctrl, b, a)     —  a ^= ctrl AND b
//   XOR(a, b)           —  b ^= a
//
// This implements the Fredkin gate: if ctrl==1 the qubit pair is swapped;
// if ctrl==0 the pair is unchanged.
//
// Gate cost: 2 XOR + 1 AND per qubit pair.  No ancilla borrowed.
//
// The index arrays are NOT modified (unlike lib_SWAP) because a physical gate
// sequence was emitted to perform the conditional swap.
//
// Precondition: ctrl must not alias any qubit in a_idxs or b_idxs.
inline void lib_c_SWAP(SimState& s, AncillaManager& /*mgr*/,
                       uint32_t ctrl,
                       uint32_t* a_idxs, uint32_t* b_idxs, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t a = a_idxs[i];
        uint32_t b = b_idxs[i];

        // Step 1: b ^= a  (CNOT: ctrl=a, tgt=b)
        primitive_XOR(s, a, b);

        // Step 2: a ^= ctrl AND b  (Toffoli: ctrl0=ctrl, ctrl1=b, tgt=a)
        primitive_AND(s, ctrl, b, a);

        // Step 3: b ^= a  (CNOT: ctrl=a, tgt=b)
        primitive_XOR(s, a, b);
    }
}

// ── lib_SWAP_when ─────────────────────────────────────────────────────────────
//
// Context-aware dispatch: routes SWAP to the correct implementation based on
// whether a control context is active in the WhenLift object.
//
// Parameters:
//   wl       — active WhenLift object; control_depth() determines the path.
//   s        — simulator state.
//   mgr      — ancilla manager.
//   ctrl     — the control qubit for the Fredkin path.  Only used when
//              wl.control_depth() > 0.  Ignored (and no gate emitted) when
//              wl.control_depth() == 0.
//   a_idxs   — qubit-index array for register a (modified in-place on relabel).
//   b_idxs   — qubit-index array for register b (modified in-place on relabel).
//   n        — number of qubit pairs.
//
// If wl.control_depth() == 0:
//   → lib_SWAP: zero gates, index relabel only.
//
// If wl.control_depth() >= 1:
//   → lib_c_SWAP: Fredkin using the supplied ctrl qubit.
//     2 XOR + 1 AND per qubit pair.  Index arrays are NOT modified.
//
// PRD v2 §6 contract: do NOT three-XOR and then WHEN-lift.  That would
// produce 3 CCX gates.  Instead, dispatch here directly to the Fredkin form
// which costs 2 XOR + 1 AND.
inline void lib_SWAP_when(WhenLift& wl, SimState& s, AncillaManager& mgr,
                          uint32_t ctrl,
                          uint32_t* a_idxs, uint32_t* b_idxs, uint32_t n) {
    if (wl.control_depth() == 0u) {
        // Uncontrolled: index relabel, zero gates.
        lib_SWAP(s, mgr, a_idxs, b_idxs, n);
    } else {
        // Controlled: Fredkin.  PRD v2 §6 — must NOT become 3 CCNOTs.
        lib_c_SWAP(s, mgr, ctrl, a_idxs, b_idxs, n);
    }
}

} // namespace v2
} // namespace sturm
