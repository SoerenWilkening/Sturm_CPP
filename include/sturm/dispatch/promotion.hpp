// promotion.hpp — M17: Partial operand promotion + promotion_mask.
//
// Provides:
//   void promote_bits(qint_base&, uint64_t bit_mask, BackendContext&)
//       Allocates pool qubits for each bit in (bit_mask & ~super_mask),
//       emits X through Layer B for bits whose classical value is 1,
//       then sets the corresponding bits in super_mask and promotion_mask.
//
// Partial-promotion invariant:
//   Only bits in bit_mask that are not already quantum are promoted.
//   Bits outside bit_mask remain classical (super_mask bit stays clear).
//
// const correctness:
//   No const overload is declared; promotion mutates the register.
//   Passing a const qint_base& is a compile-time error (no matching overload).
//
// LOC budget: <180 (header + .cpp counted separately).

#pragma once

#include "sturm/uncompute/qint_base.hpp"  // qint_base, QINT_BASE_MAX_WIDTH
#include "sturm/core/context.hpp"         // BackendContext
#include "sturm/core/gate_kind.h"         // sturm_gate_kind_t

#include <cstdint>

namespace sturm {

// ── promote_bits ──────────────────────────────────────────────────────────────
//
// Promote a subset of bits in reg to quantum.
//
// Parameters:
//   reg       — quantum register to promote in place (must be non-const).
//   bit_mask  — bitmask identifying which bit positions to promote.
//               Bit i set ⇒ reg bit-position i should become quantum.
//   ctx       — active BackendContext; X gates are emitted through it.
//
// Steps for each bit position i where (bit_mask >> i) & 1 is set
// and (reg.super_mask >> i) & 1 is NOT set (i.e. currently classical):
//   1. Allocate a physical qubit index from ctx's pool via acquire().
//   2. Store the index in reg.qubits[i].
//   3. If (reg.value >> i) & 1 == 1, emit X(qubit) through execute_gate to
//      initialise the |0⟩ qubit to |1⟩.  This counts as a gate.
//   4. Set bit i in reg.super_mask.
//   5. If the classical bit was 1, also set bit i in reg.promotion_mask
//      (so the uncompute runner knows to flip it back on release).
//
// Bits already set in reg.super_mask are silently skipped (idempotent).
// Bits not in bit_mask are untouched.

void promote_bits(qint_base&      reg,
                  uint64_t        bit_mask,
                  BackendContext& ctx);

} // namespace sturm
