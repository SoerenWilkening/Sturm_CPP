// dispatch_gate.cpp — M15/M16: Layer A entry point (implementation).
//
// Implements the all-classical FLIP/NONE path and the all-classical BRANCH
// promotion path described in dispatch_gate.hpp.
//
// FLIP permutation helpers:
//   flip_x(op)      — flips op.value (XOR 1).
//   flip_cx(ctrl, tgt) — if ctrl.value==1, flips tgt.value.
//   flip_cy(ctrl, tgt) — same as CX for classical purposes.
//   flip_ccx(c0, c1, tgt) — flips tgt if both controls are 1.
//   flip_swap(a, b)  — swaps a.value and b.value.
//
// BRANCH path:
//   Allocates one qubit per operand from QubitPool::instance(), emits X
//   through Layer B for bits whose classical value is 1, then forwards the
//   gate through execute_gate.
//
// LOC budget: <150 (this file alone).

#include "sturm/dispatch/dispatch_gate.hpp"
#include "sturm/dispatch/reduction_table.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdint>

namespace sturm {

// ── Internal permutation helpers ──────────────────────────────────────────────

// X: flip single bit.
static inline void perm_x(ClassicalOperand* op) {
    op->value ^= 1;
}

// Y: same classical effect as X — flips target.
static inline void perm_y(ClassicalOperand* op) {
    op->value ^= 1;
}

// CX: if ctrl==1, flip target.
static inline void perm_cx(ClassicalOperand* ctrl, ClassicalOperand* tgt) {
    if (ctrl->value & 1) {
        tgt->value ^= 1;
    }
}

// CY: same classical effect as CX.
static inline void perm_cy(ClassicalOperand* ctrl, ClassicalOperand* tgt) {
    if (ctrl->value & 1) {
        tgt->value ^= 1;
    }
}

// SWAP: exchange values.
static inline void perm_swap(ClassicalOperand* a, ClassicalOperand* b) {
    int64_t tmp = a->value;
    a->value    = b->value;
    b->value    = tmp;
}

// CCX (Toffoli): if both controls are 1, flip target.
static inline void perm_ccx(ClassicalOperand* c0,
                             ClassicalOperand* c1,
                             ClassicalOperand* tgt) {
    if ((c0->value & 1) && (c1->value & 1)) {
        tgt->value ^= 1;
    }
}

// ── dispatch_gate_classical ───────────────────────────────────────────────────

void dispatch_gate_classical(sturm_gate_kind_t  kind,
                              ClassicalOperand*  ops,
                              uint8_t            n,
                              double             /*param*/) noexcept {
    const sturm_gate_info_t* info = sturm_gate_info_of(kind);
    assert(info && "dispatch_gate_classical: unknown gate kind");

    // NONE effect: phase-only gate. No mutation, no Layer B call.
    if (info->effect == STURM_CE_NONE) {
        return;
    }

    // BRANCH effect: caller should use dispatch_gate_branch_classical().
    // This overload is for FLIP/NONE only.
    assert(info->effect == STURM_CE_FLIP &&
           "dispatch_gate_classical: BRANCH gate must use branch overload");
    (void)n; // n is checked implicitly by the switch below.

    // FLIP effect: apply permutation in-place.
    switch (kind) {
    case STURM_GATE_X:
        assert(n == 1u);
        perm_x(&ops[0]);
        break;

    case STURM_GATE_Y:
        assert(n == 1u);
        perm_y(&ops[0]);
        break;

    case STURM_GATE_CX:
        assert(n == 2u);
        perm_cx(&ops[0], &ops[1]);
        break;

    case STURM_GATE_CY:
        assert(n == 2u);
        perm_cy(&ops[0], &ops[1]);
        break;

    case STURM_GATE_SWAP:
        assert(n == 2u);
        perm_swap(&ops[0], &ops[1]);
        break;

    case STURM_GATE_CCX:
        assert(n == 3u);
        perm_ccx(&ops[0], &ops[1], &ops[2]);
        break;

    default:
        // Unexpected FLIP gate — should not occur if gate_kind taxonomy is correct.
        assert(false && "dispatch_gate_classical: unhandled FLIP gate kind");
        break;
    }
}

// ── dispatch_gate_branch_classical ───────────────────────────────────────────

PromotionResult dispatch_gate_branch_classical(sturm_gate_kind_t  kind,
                                               ClassicalOperand*  ops,
                                               uint8_t            n,
                                               double             param) {
    // Sanity: only BRANCH gates should reach here.
    const sturm_gate_info_t* info = sturm_gate_info_of(kind);
    assert(info && "dispatch_gate_branch_classical: unknown gate kind");
    assert(info->effect == STURM_CE_BRANCH &&
           "dispatch_gate_branch_classical: gate must have BRANCH effect");

    PromotionResult result{};

    // Allocate a qubit for each operand bit that does not yet have one.
    // Emit X through Layer B for bits whose classical value is 1.
    for (uint8_t i = 0; i < n; ++i) {
        if (ops[i].qubit_idx < 0) {
            // Allocate from the global singleton QubitPool.
            // TODO(backend): route through the per-context pool when wired (M-future).
            ops[i].qubit_idx = QubitPool::instance().allocate();

            // Record promotion in the result mask using bit_pos.
            result.promoted_mask |= (uint64_t(1) << ops[i].bit_pos);

            // If the classical bit is 1, initialise the |0⟩ qubit to |1⟩ via X.
            if (ops[i].value & 1) {
                uint32_t q = static_cast<uint32_t>(ops[i].qubit_idx);
                sturm_execute_gate(STURM_GATE_X, &q, 1u, 0.0);
            }
        }
    }

    // Forward the BRANCH gate to Layer B using the promoted qubit indices.
    // Build a small array of physical qubit indices.
    uint32_t qubits[3] = {0u, 0u, 0u};
    for (uint8_t i = 0; i < n && i < 3u; ++i) {
        qubits[i] = static_cast<uint32_t>(ops[i].qubit_idx);
    }
    sturm_execute_gate(kind, qubits, n, param);

    return result;
}

// ── dispatch_gate_mixed ───────────────────────────────────────────────────────
//
// Mixed path: some operands are classical controls, some are quantum.
// Implements:
//   - Any classical control with value 0 → immediate return (no Layer B call).
//   - Classical-1 controls → build pattern mask, look up reduction table,
//     resolve remaining quantum operand indices, call execute_gate.
//   - All-quantum (pattern == 0) → forward directly to dispatch_gate_quantum.

void dispatch_gate_mixed(sturm_gate_kind_t kind,
                         GateOperand*      ops,
                         uint8_t           n,
                         double            param) {
    // Pass 1: if any classical operand is 0 → gate cannot fire; short-circuit.
    for (uint8_t i = 0; i < n; ++i) {
        if (ops[i].is_classical() && (ops[i].value & 1) == 0) {
            return; // no-op
        }
    }

    // Build the classical-1 pattern bitmask.
    uint8_t pattern = 0u;
    for (uint8_t i = 0; i < n; ++i) {
        if (ops[i].is_classical()) {
            pattern |= static_cast<uint8_t>(1u << i);
        }
    }

    if (pattern == 0u) {
        // All operands are quantum: delegate to the all-quantum path.
        dispatch_gate_quantum(kind, ops, n, param);
        return;
    }

    // Look up the reduction table.
    const sturm::ReductionResult rr = sturm::reduce({kind, pattern});

    // Build the physical qubit index array from the remaining quantum operands.
    uint32_t qubits[3] = {0u, 0u, 0u};
    assert(rr.n_remaining <= 3u && "reduction: too many remaining operands");
    for (uint8_t i = 0; i < rr.n_remaining; ++i) {
        uint8_t src_idx = rr.remaining_operand_indices[i];
        assert(src_idx < n && "reduction: operand index out of range");
        assert(ops[src_idx].is_quantum() && "reduction: remaining operand must be quantum");
        qubits[i] = ops[src_idx].qubit_idx;
    }

    // Call Layer B with the reduced gate kind.
    sturm_execute_gate(rr.reduced_kind, qubits, rr.n_remaining, param);
}

// ── dispatch_gate_quantum ─────────────────────────────────────────────────────
//
// All-quantum path: all operands are quantum; resolve physical qubit indices
// and call Layer B directly.

void dispatch_gate_quantum(sturm_gate_kind_t  kind,
                           const GateOperand* ops,
                           uint8_t            n,
                           double             param) {
    uint32_t qubits[3] = {0u, 0u, 0u};
    assert(n <= 3u && "dispatch_gate_quantum: arity > 3 unsupported");
    for (uint8_t i = 0; i < n; ++i) {
        assert(ops[i].is_quantum() && "dispatch_gate_quantum: all operands must be quantum");
        qubits[i] = ops[i].qubit_idx;
    }
    sturm_execute_gate(kind, qubits, n, param);
}

} // namespace sturm
