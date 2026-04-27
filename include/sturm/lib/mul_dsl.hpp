// mul_dsl.hpp — M16 / LO-1a (sturm-735v): shift-and-add multiplication in DSL.
//
// lib_mul_dsl(a, aw, b, bw, result, rw): out-of-place result = a * b; rw must
//   equal aw + bw; result must start |0>; a and b are unchanged.
//
// __lib_mul_dsl_adj (LO-1a, sturm-735v): gate-reverse of lib_mul_dsl.  Given
//   state where result = a * b, zeros result.  Registered via
//   STURM_REGISTER_ADJOINT so `invert<&lib_mul_dsl>()(…)` resolves at the LO
//   rewrite's scope-exit cleanup.
//
// Forward: i = 0..bw-1: lift on b[i]; lib_add_dsl on shifted window.
// Adjoint: i = bw-1..0: lift on b[i]; detail_div::lib_add_adj on the same
//   window.  Per-bit body is otherwise identical, so direction toggles only
//   the loop order and the add/add_adj choice.  Shared via a `Forward`
//   template kernel.
//
// sturm-a3t4.4 P3: per-call push_b_i / pop_control triple replaced with the
// depth-1 lift pattern adopted under sturm-a3t4.[1-3].  sturm-k8f2 (P7):
// the duplicated lift idiom moved into the shared `sturm::lift_under` helper
// in `sturm/control/lift.hpp`, so this header drops back below its 150-LoC
// budget.
//
// Target: <150 LoC.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"           // detail_div::lib_add_adj
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/routines/invert.hpp"
#include "sturm/control/lift.hpp"          // sturm-k8f2: shared lift_under

#include <cstddef>
#include <cassert>
#include <type_traits>

// Forward-declare BitProxy for the LO-1a adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

namespace detail_mul {

// Shared body for forward + adjoint multiplication.  `Forward` toggles the
// b-bit loop order and the add (forward Cuccaro) vs. add_adj (gate-reversed
// Cuccaro) call.  The control lift is self-inverse so it appears unchanged
// in both directions.
template <typename Bit, bool Forward>
inline void mul_kernel(Bit* a_bits, size_t a_width,
                       Bit* b_bits, size_t b_width,
                       Bit* result_bits) {
    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_mul_dsl: no BackendContext installed");
    (void)raw;  // sturm-a3t4.4: control_stack is now driven by WHEN/WhenGuard
                // through the lift pattern; ctx is no longer poked directly.

    for (size_t step = 0u; step < b_width; ++step) {
        size_t i      = Forward ? step : (b_width - 1u - step);
        Bit*   window = result_bits + i;            // [i..i+a_width-1]
        Bit&   carry  = result_bits[i + a_width];   // carry output slot

        // sturm-k8f2: depth-1 lift collapsed into shared sturm::lift_under.
        sturm::lift_under(b_bits[i], [&]() {
            if constexpr (Forward) {
                lib_add_dsl(a_bits, window, carry, a_width);
            } else {
                detail_div::lib_add_adj(a_bits, window, carry, a_width);
            }
        });
    }
}

}  // namespace detail_mul

// ── lib_mul_dsl ───────────────────────────────────────────────────────────────
//
// Out-of-place multiplication: result = a * b.  result_bits must point to
// (a_width + b_width) qubits all in |0>; a and b are unchanged.
// Either width zero: no-op.
template <typename Bit>
inline void lib_mul_dsl(Bit* a_bits, size_t a_width,
                        Bit* b_bits, size_t b_width,
                        Bit* result_bits, size_t result_width) {
    if (a_width == 0u || b_width == 0u) return;
    assert(result_width == a_width + b_width
           && "lib_mul_dsl: result_width must equal a_width + b_width");
    detail_mul::mul_kernel<Bit, true>(a_bits, a_width, b_bits, b_width,
                                      result_bits);
}

// ── __lib_mul_dsl_adj (LO-1a, sturm-735v) ────────────────────────────────────
// Gate-reverse of lib_mul_dsl.  Precondition: result_bits == a * b.
// Postcondition: result_bits all |0>; a and b unchanged.
template <typename Bit>
inline void __lib_mul_dsl_adj(Bit* a_bits, size_t a_width,
                              Bit* b_bits, size_t b_width,
                              Bit* result_bits, size_t result_width) {
    if (a_width == 0u || b_width == 0u) return;
    assert(result_width == a_width + b_width
           && "__lib_mul_dsl_adj: result_width must equal a_width + b_width");
    detail_mul::mul_kernel<Bit, false>(a_bits, a_width, b_bits, b_width,
                                       result_bits);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_mul_dsl<sturm::BitProxy>,
                       sturm::__lib_mul_dsl_adj<sturm::BitProxy>)
#endif
