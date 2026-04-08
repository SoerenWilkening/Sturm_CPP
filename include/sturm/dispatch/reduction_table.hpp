// reduction_table.hpp — M14: Classical-control reduction table.
//
// Static map keyed by (gate_kind, classical_pattern_bits) →
// (reduced_gate_kind, remaining_operand_indices).
//
// Covers every gate that has one or more control operands:
//   CX, CY, CZ, CRx, CRy, CRz  (single control, operands[0]=ctrl, [1]=target)
//   CCX                          (two controls, operands[0]=ctrl0, [1]=ctrl1, [2]=target)
//
// Pattern encoding:
//   Bit i in classical_pattern_bits means operand i is a classical control with
//   value 1.  Patterns where any control has value 0 are handled by the caller
//   (gate short-circuits); those patterns do NOT appear in this table.
//   Pattern 0 (all operands quantum) does NOT appear in this table.
//
// CCX 9 combinations across two control slots:
//   The table stores the 3 non-trivial patterns:
//     0b01 (ctrl0=1, ctrl1=quantum) → CX(ctrl1, tgt)
//     0b10 (ctrl0=quantum, ctrl1=1) → CX(ctrl0, tgt)
//     0b11 (ctrl0=1, ctrl1=1)       → X(tgt)
//   The remaining 6 patterns (0b00 and any with a 0-valued control) are
//   handled upstream and are NOT in this table.
//
// LOC budget: <200 (header + .cpp counted separately).

#pragma once

#include "sturm/core/gate_kind.h"

#include <array>
#include <cstdint>
#include <optional>

namespace sturm {

// ── ReductionKey ──────────────────────────────────────────────────────────────

struct ReductionKey {
    sturm_gate_kind_t gate;     ///< Incoming gate kind
    uint8_t           pattern;  ///< Bitmask: bit i=1 ⇒ operand i is classical-1 control
};

// ── ReductionResult ───────────────────────────────────────────────────────────

struct ReductionResult {
    bool              is_noop;   ///< True when the gate fires but has no quantum effect
                                  ///< (reserved; currently always false in the table).
    sturm_gate_kind_t reduced_kind;  ///< Gate kind after stripping classical controls
    uint8_t           n_remaining;   ///< Number of remaining quantum operand indices
    uint8_t           remaining_operand_indices[3]; ///< Indices into the original operand array
};

// ── ReductionTableEntry ───────────────────────────────────────────────────────

struct ReductionTableEntry {
    ReductionKey    key;
    ReductionResult result;
};

// ── Public API ────────────────────────────────────────────────────────────────

/// Return a pointer to the static flat array of all reduction table entries.
/// The lifetime of the returned pointer is the lifetime of the program.
const ReductionTableEntry* reduction_table_entries() noexcept;

/// Return the number of entries in the reduction table.
std::size_t reduction_table_size() noexcept;

/// Look up (key.gate, key.pattern) in the table.
/// Returns the ReductionResult if the key is present, or std::nullopt if not.
/// O(N) linear scan on a tiny static table; negligible in practice.
std::optional<ReductionResult> lookup_reduce(const ReductionKey& key) noexcept;

/// Same as lookup_reduce but asserts the key exists.
/// Prefer this inside Layer A where an absent key is a programming error.
ReductionResult reduce(const ReductionKey& key) noexcept;

} // namespace sturm
