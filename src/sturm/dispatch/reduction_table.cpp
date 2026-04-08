// reduction_table.cpp — M14: Classical-control reduction table (implementation).
//
// Static flat array of all (gate_kind, pattern) → (reduced_kind, operands)
// entries.  The table is a plain C-style array so it has no construction order
// issues and zero overhead.
//
// Operand index conventions (match the arity layout in gate_kind.h):
//   CX, CY, CZ, CRx, CRy, CRz  operands[0]=ctrl,  operands[1]=target
//   CCX                          operands[0]=ctrl0, operands[1]=ctrl1, operands[2]=target
//
// Pattern encoding:
//   bit 0 set → operand[0] (ctrl / ctrl0) is classical-1
//   bit 1 set → operand[1] (ctrl1 for CCX) is classical-1
//
// CCX:
//   pattern 0b01 (ctrl0=1, ctrl1=quantum) → CX(ctrl1[idx=1], tgt[idx=2])
//   pattern 0b10 (ctrl0=quantum, ctrl1=1) → CX(ctrl0[idx=0], tgt[idx=2])
//   pattern 0b11 (ctrl0=1, ctrl1=1)       → X(tgt[idx=2])
//
// Single-control gates (CX/CY/CZ/CRx/CRy/CRz):
//   pattern 0b1 (ctrl=1) → bare base gate on target[idx=1]

#include "sturm/dispatch/reduction_table.hpp"

#include <cassert>
#include <cstring>

namespace sturm {

// ── Static table ──────────────────────────────────────────────────────────────

// Helper macro to build a single-control entry (ctrl at operand 0, target at 1).
// Pattern 0x1 means ctrl is classical-1 → reduce to base_gate(target[1]).
#define CTRL1_ENTRY(ctl_gate, base_gate)                        \
    {                                                            \
        /* key */   { (ctl_gate), 0x1u },                       \
        /* result*/ { false, (base_gate), 1u, {1u, 0u, 0u} }   \
    }

static const ReductionTableEntry kTable[] = {
    // ── Single-control gates ─────────────────────────────────────────────────
    CTRL1_ENTRY(STURM_GATE_CX,  STURM_GATE_X),
    CTRL1_ENTRY(STURM_GATE_CY,  STURM_GATE_Y),
    CTRL1_ENTRY(STURM_GATE_CZ,  STURM_GATE_Z),
    CTRL1_ENTRY(STURM_GATE_CRX, STURM_GATE_RX),
    CTRL1_ENTRY(STURM_GATE_CRY, STURM_GATE_RY),
    CTRL1_ENTRY(STURM_GATE_CRZ, STURM_GATE_RZ),

    // ── CCX: ctrl0=1, ctrl1=quantum → CX(ctrl1[1], tgt[2]) ──────────────────
    {
        /* key    */ { STURM_GATE_CCX, 0b01u },
        /* result */ { false, STURM_GATE_CX, 2u, {1u, 2u, 0u} }
    },

    // ── CCX: ctrl0=quantum, ctrl1=1 → CX(ctrl0[0], tgt[2]) ──────────────────
    {
        /* key    */ { STURM_GATE_CCX, 0b10u },
        /* result */ { false, STURM_GATE_CX, 2u, {0u, 2u, 0u} }
    },

    // ── CCX: ctrl0=1, ctrl1=1 → X(tgt[2]) ───────────────────────────────────
    {
        /* key    */ { STURM_GATE_CCX, 0b11u },
        /* result */ { false, STURM_GATE_X, 1u, {2u, 0u, 0u} }
    },
};

#undef CTRL1_ENTRY

static constexpr std::size_t kTableSize =
    sizeof(kTable) / sizeof(kTable[0]);

// ── Public API ────────────────────────────────────────────────────────────────

const ReductionTableEntry* reduction_table_entries() noexcept {
    return kTable;
}

std::size_t reduction_table_size() noexcept {
    return kTableSize;
}

std::optional<ReductionResult> lookup_reduce(const ReductionKey& key) noexcept {
    for (std::size_t i = 0; i < kTableSize; ++i) {
        if (kTable[i].key.gate    == key.gate &&
            kTable[i].key.pattern == key.pattern) {
            return kTable[i].result;
        }
    }
    return std::nullopt;
}

ReductionResult reduce(const ReductionKey& key) noexcept {
    auto opt = lookup_reduce(key);
    assert(opt.has_value() && "reduce(): key not found in reduction table");
    return *opt;
}

} // namespace sturm
