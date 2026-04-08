// promotion.cpp — M17: Partial operand promotion (implementation).
//
// See promotion.hpp for the full contract.
//
// Implementation notes:
//   - Pool allocation uses ctx.pool.acquire() (cap-enforcing, per M18).
//   - X emission uses execute_gate() (C++ helper) which also increments the
//     gate counter unconditionally (PRD §8 / §5).
//   - The loop iterates over bit positions 0 .. (QINT_BASE_MAX_WIDTH - 1).
//     Only positions within reg.width and within bit_mask are visited.
//   - Idempotency is guaranteed by the (bit_mask & ~reg.super_mask) filter.
//
// LOC budget: <180 (this file alone).

#include "sturm/dispatch/promotion.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>
#include <cassert>

namespace sturm {

void promote_bits(qint_base&      reg,
                  uint64_t        bit_mask,
                  BackendContext& ctx) {
    // Only promote bits that are requested AND not already quantum.
    uint64_t to_promote = bit_mask & ~reg.super_mask;

    // Iterate over each bit position that needs promotion.
    for (uint8_t i = 0; i < QINT_BASE_MAX_WIDTH; ++i) {
        if (!((to_promote >> i) & 1u)) {
            continue;  // not requested or already quantum
        }

        // 1. Allocate a physical qubit (cap-enforcing).
        int qidx = ctx.pool.acquire();
        assert(qidx >= 0 && "promote_bits: pool returned invalid qubit index");
        reg.qubits[i] = static_cast<uint32_t>(qidx);

        // 2. If the classical bit value is 1, emit X to set |0⟩ → |1⟩.
        bool bit_is_one = ((reg.value >> i) & 1) != 0;
        if (bit_is_one) {
            uint32_t q = reg.qubits[i];
            execute_gate(ctx, STURM_GATE_X, &q, 1u, 0.0);

            // 5. Mark in promotion_mask: this qubit was |1⟩ at promotion time.
            reg.promotion_mask |= (uint64_t(1) << i);
        }

        // 4. Mark the bit as quantum.
        reg.super_mask |= (uint64_t(1) << i);
    }
}

} // namespace sturm
