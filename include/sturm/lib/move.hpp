// move.hpp — M5 (PRD v2): move semantics for out-of-place-with-move operations.
//
// move_result(dest_idxs, src_idxs, n, s, mgr, wl):
//
//   Uncontrolled context (wl == nullptr or wl->control_depth() == 0):
//     Index relabel — swap the qubit-index arrays of dest and src in place.
//     Zero gates emitted.  The displaced register (old dest) carries
//     entangled history and must be handed to the GarbageManager for later
//     uncomputing.
//
//   Controlled context (wl != nullptr && wl->control_depth() > 0):
//     Per-qubit Fredkin cascade — emit lib_c_SWAP for each qubit pair.
//     Gate cost: n * (2 XOR + 1 AND).  Index arrays are NOT modified.
//
// PRD v2 §7: "Step 3 has two regimes" — this file implements those regimes.
// The caller (assignment-operator lowering) decides which regime applies.
//
// Target: <200 LoC (impl plan M5).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/backend/when_lift.hpp"
#include "sturm/lib/swap.hpp"

#include <cstdint>

namespace sturm {
namespace v2 {

// ── move_result ────────────────────────────────────────────────────────────────
//
// Move the result from src register into the dest register slot.
//
// Parameters:
//   dest_idxs — qubit-index array for the destination register (n elements).
//               Modified in-place during an uncontrolled relabel; unchanged
//               during a controlled Fredkin cascade.
//   src_idxs  — qubit-index array for the source register (n elements).
//               Modified in-place during an uncontrolled relabel (dest and src
//               are swapped); unchanged during a controlled Fredkin cascade.
//   n         — number of qubits in each register (must match).
//   s         — simulator state (gates emitted here in controlled case).
//   mgr       — ancilla manager (passed through to lib_c_SWAP; not used in
//               uncontrolled case).
//   wl        — WhenLift context pointer.  nullptr or control_depth() == 0
//               means uncontrolled (index relabel).  control_depth() > 0 means
//               controlled (Fredkin cascade).
//
// Uncontrolled path:
//   Calls lib_SWAP — swaps dest_idxs[i] and src_idxs[i] for each i.
//   After the call dest_idxs holds the source qubit indices (the result).
//   src_idxs holds the old destination qubit indices (the displaced register).
//   No gates emitted.
//
// Controlled path:
//   Extracts wl->top_control() from the WhenLift stack, then calls lib_c_SWAP
//   for each qubit pair.  The physical swap is conditional on the control qubit;
//   index arrays are left unchanged (physical state carries the result).
//
// n == 0 is a no-op in both paths.
inline void move_result(uint32_t* dest_idxs, uint32_t* src_idxs, uint32_t n,
                        SimState& s, AncillaManager& mgr, WhenLift* wl) {
    if (wl == nullptr || wl->control_depth() == 0u) {
        // Uncontrolled: pure index relabel, zero gates.
        lib_SWAP(s, mgr, dest_idxs, src_idxs, n);
    } else {
        // Controlled: per-qubit Fredkin cascade.
        // Extract the top-of-stack control qubit from the WhenLift context.
        uint32_t ctrl = wl->top_control();
        lib_c_SWAP(s, mgr, ctrl, dest_idxs, src_idxs, n);
    }
}

// ── move_result_ctrl ──────────────────────────────────────────────────────────
//
// Explicit-control variant: always performs the Fredkin cascade regardless of
// wl state.  Used by assignment-operator lowering when it knows a WHEN context
// is active and already has the control qubit at hand.
//
// Parameters:
//   dest_idxs — qubit-index array for destination (n elements, unchanged).
//   src_idxs  — qubit-index array for source     (n elements, unchanged).
//   n         — number of qubit pairs.
//   s         — simulator state.
//   mgr       — ancilla manager.
//   ctrl      — control qubit index for the Fredkin gate.
//
// Gate cost: n × (2 XOR + 1 AND).
inline void move_result_ctrl(uint32_t* dest_idxs, uint32_t* src_idxs, uint32_t n,
                             SimState& s, AncillaManager& mgr,
                             uint32_t ctrl) {
    lib_c_SWAP(s, mgr, ctrl, dest_idxs, src_idxs, n);
}

// ── move_result_when ──────────────────────────────────────────────────────────
//
// Context-aware dispatch (the primary API for assignment-operator lowering):
//
//   - wl == nullptr or wl->control_depth() == 0:
//       Uncontrolled: index relabel via lib_SWAP.  Zero gates.
//
//   - wl->control_depth() > 0:
//       Controlled: Fredkin cascade via lib_c_SWAP.
//       The ctrl qubit is provided explicitly (the caller extracts it from the
//       WHEN block's condition register before entering this function).
//
// Parameters:
//   dest_idxs — destination qubit-index array (modified in uncontrolled case).
//   src_idxs  — source qubit-index array (modified in uncontrolled case).
//   n         — number of qubits.
//   s         — simulator state.
//   mgr       — ancilla manager.
//   wl        — WhenLift context (may be nullptr).
//   ctrl      — control qubit for Fredkin path; only used when
//               wl != nullptr && wl->control_depth() > 0.
inline void move_result_when(uint32_t* dest_idxs, uint32_t* src_idxs, uint32_t n,
                             SimState& s, AncillaManager& mgr,
                             WhenLift* wl, uint32_t ctrl) {
    if (wl == nullptr || wl->control_depth() == 0u) {
        // Uncontrolled: zero gates, index relabel.
        lib_SWAP(s, mgr, dest_idxs, src_idxs, n);
    } else {
        // Controlled: per-qubit Fredkin, gate cost n*(2 XOR + 1 AND).
        lib_c_SWAP(s, mgr, ctrl, dest_idxs, src_idxs, n);
    }
}

} // namespace v2
} // namespace sturm
