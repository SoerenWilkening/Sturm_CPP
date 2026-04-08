// dispatch_gate.hpp — M15/M16: Layer A entry point.
//
// Provides the all-classical dispatch path (M15):
//   - Inspect operand super_mask to decide classicality.
//   - NONE  → skip (no Layer B call, no mutation).
//   - FLIP  → mutate classical values in-place via permutation helpers.
//   - BRANCH → promote operand (via M17 promotion), then forward to Layer B.
//
// M16 (mixed/all-quantum paths) extends this file with two new entry points:
//   dispatch_gate_mixed(kind, ops, n, param)
//       — Mixed path: some operands are classical controls, some are quantum.
//         Consults the reduction table; if any classical control is 0, returns
//         immediately (no-op). Otherwise reduces the gate and calls Layer B
//         with the quantum operand's physical qubit indices.
//
//   dispatch_gate_quantum(kind, ops, n, param)
//       — All-quantum path: all operands are quantum registers. Resolves
//         physical qubit indices directly and calls Layer B.
//
// Public types:
//   ClassicalOperand        — carries (int64_t value, uint8_t bit_pos) for one
//                             operand bit that the caller has confirmed is classical.
//   PromotionResult         — returned by dispatch_gate_branch_classical(); records
//                             which bits were promoted (bit mask).
//   GateOperand             — tagged union for mixed dispatch; carries either a
//                             classical value or a quantum physical qubit index.
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

// ── GateOperand ───────────────────────────────────────────────────────────────
//
// Tagged union representing one gate operand in the mixed/all-quantum paths.
// Either a classical operand (with an integer value 0 or 1) or a quantum
// operand (with a physical qubit index already resolved from qint.qubits[i]).
//
// Constructors are provided as static factory methods so the tag is always set.

struct GateOperand {
    enum class Tag : uint8_t { Classical = 0, Quantum = 1 };

    Tag      tag       = Tag::Classical;
    int64_t  value     = 0;   // Classical: bit value (0 or 1).
    uint32_t qubit_idx = 0u;  // Quantum: physical qubit index.

    /// Build a classical operand with the given bit value (0 or 1).
    static GateOperand classical(int64_t val) noexcept {
        GateOperand o;
        o.tag   = Tag::Classical;
        o.value = val & 1;
        return o;
    }

    /// Build a quantum operand with the given physical qubit index.
    static GateOperand quantum(uint32_t qidx) noexcept {
        GateOperand o;
        o.tag       = Tag::Quantum;
        o.qubit_idx = qidx;
        return o;
    }

    bool is_classical() const noexcept { return tag == Tag::Classical; }
    bool is_quantum()   const noexcept { return tag == Tag::Quantum;   }
};

// ── Mixed dispatch ────────────────────────────────────────────────────────────
//
// Handles gates where some operands are classical controls and at least one
// operand is quantum.  Steps:
//   1. For each operand, if it is a classical control with value 0, return
//      immediately (gate cannot fire; no Layer B call).
//   2. Build a classical_pattern bitmask: bit i = 1 iff ops[i] is classical
//      AND has value 1.
//   3. If pattern == 0 (all operands are quantum), fall through to all-quantum
//      handling (direct Layer B call without table lookup).
//   4. Look up (kind, pattern) in the reduction table to get the reduced gate
//      and the subset of operand indices that remain quantum.
//   5. Resolve physical qubit indices for the remaining operands and call
//      sturm_execute_gate with the reduced gate kind.
//
// Precondition: at least one operand must be quantum (ops[i].is_quantum()).

void dispatch_gate_mixed(sturm_gate_kind_t kind,
                         GateOperand*      ops,
                         uint8_t           n,
                         double            param);

// ── All-quantum dispatch ──────────────────────────────────────────────────────
//
// Handles gates where all operands are quantum.  Steps:
//   1. For each operand, extract the physical qubit index from ops[i].qubit_idx.
//   2. Call sturm_execute_gate(kind, qubits, n, param) directly.
//
// Precondition: all operands must be quantum (ops[i].is_quantum()).

void dispatch_gate_quantum(sturm_gate_kind_t kind,
                           const GateOperand* ops,
                           uint8_t            n,
                           double             param);

} // namespace sturm
