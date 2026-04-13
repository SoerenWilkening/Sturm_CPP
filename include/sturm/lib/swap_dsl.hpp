// swap_dsl.hpp — M16 (PRD v3): SWAP / Fredkin in DSL style using qbool operators.
//
// lib_swap_dsl(a, b):
//   Uncontrolled (control stack depth == 0):
//     Zero gates. Pure index relabel: std::swap(a.qubits[0], b.qubits[0]).
//   Controlled (control stack depth >= 1):
//     Fredkin via three auto-lifted CNOT ops:
//       a ^= b;   // CX → CCX under 1 control
//       b ^= a;   // CX → CCX under 1 control
//       a ^= b;   // CX → CCX under 1 control
//     Each CX is automatically lifted to CCX because the control stack is
//     consulted inside qbool::operator^=.
//     Under 1 control: 3 CCX gates = Fredkin.
//
// No explicit BackendContext parameter — operators read TLS context internally.
// No _when variant needed: control stack determines the path automatically.
//
// Gate cost:
//   Uncontrolled: 0 gates.
//   Controlled (1 ctrl): 3 CCX gates (Fredkin decomposition).
//
// Target: <100 LoC.

#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <utility>   // std::swap
#include <cassert>
#include <type_traits>

namespace sturm {

// ── lib_swap_dsl ──────────────────────────────────────────────────────────────
//
// SWAP two qbool objects in DSL style.
//
// Behavior depends on the current control stack depth:
//
//   depth == 0 (uncontrolled):
//     Zero gates emitted. The qubit indices (and associated state) in a and b
//     are swapped in place via std::swap on qubits[0]. The classical values,
//     superposition flag, and ownership flag are also swapped. This is a pure
//     compile-time index relabel — no gate touches the quantum state.
//
//   depth >= 1 (controlled / WHEN context):
//     Fredkin decomposition via three qbool ^= operations:
//       a ^= b;   // emit_CX_lifted: under depth-1 controls → CCX etc.
//       b ^= a;   // emit_CX_lifted
//       a ^= b;   // emit_CX_lifted
//     When depth == 1, each ^= becomes CCX (Toffoli), giving 3 CCX = Fredkin.
//
// Parameters:
//   a — first qbool (must have a valid qubit: qubits[0] >= 0).
//   b — second qbool (must have a valid qubit: qubits[0] >= 0).
//
// Precondition: a.qubits[0] != b.qubits[0] (distinct qubits).
template <typename Bit>
inline void lib_swap_dsl(Bit& a, Bit& b) {
    // Check if a context exists (required for gate emission and stack check).
    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_swap_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;

    if constexpr (std::is_same_v<Bit, qbool>) {
        // qbool path: uncontrolled swap is a zero-gate index relabel.
        assert(a.qubits[0] >= 0 && b.qubits[0] >= 0
               && "lib_swap_dsl: both qbools must have allocated qubits");
        if (ctx.control_stack.depth() == 0u) {
            std::swap(a.qubits[0], b.qubits[0]);
            std::swap(a.value,      b.value);
            std::swap(a.super_mask, b.super_mask);
            return;
        }
    }
    // Controlled (Fredkin) or non-qbool Bit: 3-CNOT swap decomposition.
    a ^= b; b ^= a; a ^= b;
}

} // namespace sturm
