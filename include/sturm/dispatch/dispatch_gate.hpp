// dispatch_gate.hpp — M15/M16: Layer A entry point.
//
// Provides the all-classical dispatch path (M15):
//   - Inspect operand super_mask to decide classicality.
//   - NONE  → skip (no Layer B call, no mutation).
//   - FLIP  → mutate classical values in-place via permutation helpers.
//   - BRANCH → promote operand (via M17 promotion), then forward to Layer B.
//
// M16 (mixed/all-quantum paths) extends this file; stubs are left with
// TODO(backend) markers.
//
// Public types:
//   ClassicalOperand        — carries (int64_t value, uint8_t bit_pos) for one
//                             operand bit that the caller has confirmed is classical.
//   PromotionResult         — returned by dispatch_gate_branch_classical(); records
//                             which bits were promoted (bit mask).
//
// Public functions:
//   dispatch_gate_classical(kind, ops, n, param)
//       — All-classical FLIP/NONE path.  ops must all have super_mask==0.
//         Mutates ops[i].value in-place for FLIP gates; is a no-op for NONE.
//         Returns immediately without calling Layer B for both cases.
//
//   dispatch_gate_branch_classical(kind, ops, n, param) → PromotionResult
//       — All-classical BRANCH path.  Promotes each operand's bit (allocates a
//         qubit, emits X if classical value was 1), sets promoted_mask, and
//         forwards the gate to Layer B on the newly-allocated physical qubit.
//         Returns a PromotionResult describing which bits were promoted.
//
// LOC budget: <150 (header + .cpp counted separately).

#pragma once

#include "sturm/core/gate_kind.h"
#include "sturm/core/qubit_pool.hpp"

#include <cstdint>

namespace sturm {

// ── ClassicalOperand ──────────────────────────────────────────────────────────
//
// A lightweight view of a single classical bit extracted from a qint/qbool for
// use by the all-classical dispatch helpers.
//
// Fields:
//   value     — current bit value (0 or 1); mutated in-place by FLIP helpers.
//   bit_pos   — bit position within the parent register (informational; used to
//               build the promoted_mask in BRANCH dispatch).
//   qubit_idx — physical qubit index after promotion (-1 means not yet promoted).

struct ClassicalOperand {
    int64_t value    = 0;   // 0 or 1 — the classical bit value
    uint8_t bit_pos  = 0;   // bit position in the parent register
    int     qubit_idx = -1; // physical qubit index; -1 = not promoted
};

// ── PromotionResult ───────────────────────────────────────────────────────────
//
// Returned by dispatch_gate_branch_classical().  Records which bits were
// promoted by this call (one bit per bit_pos of the corresponding operand).

struct PromotionResult {
    uint64_t promoted_mask = 0; // bit i set ⇒ bit at bit_pos i was promoted
};

// ── All-classical FLIP/NONE dispatch ─────────────────────────────────────────
//
// Handles the case where all operands are classical (super_mask == 0 for all).
//
// For NONE gates: returns immediately, no mutation, no Layer B call.
// For FLIP gates: applies the gate's permutation to the classical values
//                 in-place without calling Layer B.
// For BRANCH gates: this overload must NOT be used; call
//                   dispatch_gate_branch_classical() instead.
//
// Parameters:
//   kind  — gate kind (must have effect NONE or FLIP).
//   ops   — array of ClassicalOperand, one per gate operand.
//   n     — number of operands (must equal gate arity).
//   param — rotation angle (unused for NONE/FLIP gates; passed for symmetry).

void dispatch_gate_classical(sturm_gate_kind_t  kind,
                              ClassicalOperand*  ops,
                              uint8_t            n,
                              double             param) noexcept;

// ── All-classical BRANCH dispatch ────────────────────────────────────────────
//
// Handles a BRANCH gate (H, Rx, Ry, CRx, CRy) whose operands are currently
// all classical.  Steps (per PRD §7):
//   1. For each operand bit, allocate a physical qubit from the global QubitPool.
//   2. If the classical value of that bit is 1, emit X(qubit) through Layer B
//      to initialise the statevector qubit to |1⟩.
//   3. Set the corresponding bit in the returned PromotionResult.promoted_mask.
//   4. Forward the original gate to Layer B using the newly-allocated qubit
//      indices so the BRANCH operation is actually executed.
//
// The caller is responsible for updating the parent qint/qbool super_mask and
// storing the qubit indices returned in ops[i].qubit_idx.
//
// Parameters and return value as for dispatch_gate_classical.

PromotionResult dispatch_gate_branch_classical(sturm_gate_kind_t  kind,
                                               ClassicalOperand*  ops,
                                               uint8_t            n,
                                               double             param);

} // namespace sturm
