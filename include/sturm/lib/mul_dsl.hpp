// mul_dsl.hpp — M16 (PRD v3): shift-and-add multiplication in DSL style.
//
// lib_mul_dsl(a_bits, a_width, b_bits, b_width, result_bits, result_width):
//   Out-of-place multiplication: result = a * b.
//
//   result_bits must point to (a_width + b_width) qubits all in |0> state.
//   a and b are left unchanged.
//
// Algorithm: shift-and-add.
//   For each bit i of b (i = 0..b_width-1):
//     WHEN b[i]: result += (a << i)
//   "a << i" means: the a_width-bit value a occupies positions [i .. i+a_width-1]
//   of the result register.
//
// Implementation:
//   - Push b[i] onto the control stack (simulate WHEN(b[i])).
//   - Call lib_add_dsl on the shifted window of result.
//   - Pop b[i] from the control stack.
//   - The carry output of each partial add goes to result[i + a_width].
//
// Gate cost: b_width × cost(a_width-bit controlled Cuccaro ADD).
// Ancilla: 1 QubitPool qubit per ADD call (Cuccaro carry_anc).
//
// No explicit BackendContext parameter — operators read TLS context internally.
// WHEN lifting is automatic via qbool operators and the control stack.
//
// Target: <150 LoC.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <cstddef>
#include <cassert>

namespace sturm {

// ── lib_mul_dsl ───────────────────────────────────────────────────────────────
//
// Out-of-place multiplication: result = a * b.
//
// Parameters:
//   a_bits       — pointer to a_width qbool objects for a (LSB = [0]). Unchanged.
//   a_width      — number of bits in a.
//   b_bits       — pointer to b_width qbool objects for b (LSB = [0]). Unchanged.
//   b_width      — number of bits in b.
//   result_bits  — pointer to (a_width + b_width) qbool objects for the result
//                  (all must start |0>). Written with the product a * b.
//   result_width — must equal (a_width + b_width); checked by assert.
//
// n == 0 (either width zero): no-op.
inline void lib_mul_dsl(qbool* a_bits, size_t a_width,
                        qbool* b_bits, size_t b_width,
                        qbool* result_bits, size_t result_width) {
    if (a_width == 0u || b_width == 0u) return;
    assert(result_width == a_width + b_width
           && "lib_mul_dsl: result_width must equal a_width + b_width");

    // Get context for control stack access.
    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_mul_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;

    // For each bit i of b, conditionally add (a << i) into the result.
    // (a << i) maps to result positions [i .. i + a_width - 1].
    // The carry output of the ADD goes to result[i + a_width].
    //
    // We use result[i + a_width] as the carry_out qubit, which is always
    // within the (a_width + b_width)-bit result register.
    //
    // Note: result[i + a_width] may be non-zero from a previous partial add.
    // We use a separate carry ancilla to hold overflow and XOR it into
    // result[i + a_width] to accumulate correctly.
    //
    // Simpler approach (matching lib_mul.hpp):
    //   carry_out slot = result[i + a_width]
    //   window        = result[i .. i + a_width - 1]
    // Push b[i] as control; call lib_add_dsl; pop b[i].

    for (size_t i = 0; i < b_width; ++i) {
        // Window of the result register where (a << i) will be added.
        qbool* window = result_bits + i;           // a_width qubits [i..i+a_width-1]
        qbool& carry  = result_bits[i + a_width];  // carry output slot

        // Push b[i] as control.
        uint32_t b_qubit = static_cast<uint32_t>(b_bits[i].qubits[0]);
        ctx.control_stack.push_control(b_qubit);

        // WHEN b[i]: result[i..i+a_width-1] += a (adds a into the shifted window).
        lib_add_dsl(a_bits, window, carry, a_width);

        // Pop b[i].
        ctx.control_stack.pop_control();
    }
}

} // namespace sturm
