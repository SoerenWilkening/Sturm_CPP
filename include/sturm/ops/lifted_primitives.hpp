// lifted_primitives.hpp — sturm-8x3: emit_RY_lifted / emit_RZ_lifted.
//
// Rotation-gate emitters that respect the active WHEN control stack:
//   depth 0  → emit bare RY / RZ (1-qubit)
//   depth 1  → emit CRY / CRZ (2-qubit, ctrl = control_stack.controls()[0])
//   depth >= 2 → assertion failure (not yet supported)
//
// Pattern mirrors emit_X_lifted in qtypes/qbool_ops.hpp.
// Target: ~40 LoC.

#pragma once

#include "sturm/core/context.hpp"   // BackendContext, execute_gate, control_stack
#include "sturm/core/gate_kind.h"   // STURM_GATE_RY(8), STURM_GATE_RZ(9),
                                    // STURM_GATE_CRY(14), STURM_GATE_CRZ(15)
#include <cassert>
#include <cstdint>

namespace sturm {

// ── emit_RY_lifted ────────────────────────────────────────────────────────────
// Depth-1 invariant (sturm-a3t4): library code must lift via outer flag + WHEN.
// 0 controls → RY(theta, target)
// 1 control  → CRY(theta, ctrl, target)

inline void emit_RY_lifted(BackendContext& ctx, uint32_t target, double theta) {
    const auto     ctrls = ctx.control_stack.controls();
    const uint32_t depth = static_cast<uint32_t>(ctrls.size());
    assert(depth <= 1u && "depth-1 invariant violated; library code must lift via outer & flag + WHEN");
    if (depth == 0u) {
        execute_gate(ctx, STURM_GATE_RY, &target, 1u, theta);
    } else {
        uint32_t qs[2] = {ctrls[0], target};
        execute_gate(ctx, STURM_GATE_CRY, qs, 2u, theta);
    }
}

// ── emit_RZ_lifted ────────────────────────────────────────────────────────────
// Depth-1 invariant (sturm-a3t4): library code must lift via outer flag + WHEN.
// 0 controls → RZ(theta, target)
// 1 control  → CRZ(theta, ctrl, target)

inline void emit_RZ_lifted(BackendContext& ctx, uint32_t target, double theta) {
    const auto     ctrls = ctx.control_stack.controls();
    const uint32_t depth = static_cast<uint32_t>(ctrls.size());
    assert(depth <= 1u && "depth-1 invariant violated; library code must lift via outer & flag + WHEN");
    if (depth == 0u) {
        execute_gate(ctx, STURM_GATE_RZ, &target, 1u, theta);
    } else {
        uint32_t qs[2] = {ctrls[0], target};
        execute_gate(ctx, STURM_GATE_CRZ, qs, 2u, theta);
    }
}

} // namespace sturm
