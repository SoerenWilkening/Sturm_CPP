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
// sturm-a3t4.4 P3: per-call push_b_i / pop_control triple replaced with
// the depth-1 lift pattern adopted under sturm-a3t4.[1-3].  When wrapped
// in an outer WHEN(c), each iteration allocates+uncomputes its own AND
// ancilla; otherwise we drop straight into WHEN(b[i]).
//
// Target: <250 LoC.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"           // detail_div::lib_add_adj
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/routines/invert.hpp"
#include "sturm/control/when.hpp"          // sturm-a3t4.4: WHEN + WhenGuard::active_control

#include <cstddef>
#include <cassert>
#include <type_traits>

// sturm-a3t4.4: forward-declare `uncompute_and` rather than pulling the
// full uncompute_api.hpp.  The full header includes <qint.hpp>, which
// transitively re-includes mul_dsl.hpp via qint_arith_v3.hpp.  Pragma-
// once skips the second pass, so the recursive expansion would parse
// `sturm::uncompute_and` calls below before its declaration is reached.
// A bare forward declaration is sufficient: qbool is already a complete
// type (qbool.hpp is included above via div_dsl.hpp); the implementation
// lives in src/sturm/uncompute/uncompute_api.cpp.
namespace sturm {
void uncompute_and(qbool& r, const qbool& a, const qbool& b);
}  // namespace sturm

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

    // sturm-a3t4.4: build a non-owning qbool view of b[i] for use as the
    // AND/WHEN operand in the depth-1 lift pattern.  Mirrors
    // detail_mul_mod::flag_qbool_view.
    auto flag_qbool_view = [](Bit& flag) -> qbool {
        if constexpr (std::is_same_v<Bit, qbool>) {
            return qbool::make_non_owning(flag.qubits[0],
                                           flag.value, /*mask=*/1ULL);
        } else {
            flag.ensure_quantum();
            return qbool::make_non_owning(flag.qubit_index(),
                                           /*val=*/0,
                                           /*mask=*/1ULL);
        }
    };

    for (size_t step = 0u; step < b_width; ++step) {
        size_t i      = Forward ? step : (b_width - 1u - step);
        Bit*   window = result_bits + i;            // [i..i+a_width-1]
        Bit&   carry  = result_bits[i + a_width];   // carry output slot

        // sturm-a3t4.4: depth-1 lift around the controlled add.  Mirrors
        // the lift idiom in add_mod_dsl.hpp / mul_mod_dsl.hpp verbatim.
        qbool flag_q = flag_qbool_view(b_bits[i]);
        if (qbool* outer = WhenGuard::active_control()) {
            qbool tmp = (*outer) & flag_q;
            WHEN(tmp) {
                if constexpr (Forward) {
                    lib_add_dsl(a_bits, window, carry, a_width);
                } else {
                    detail_div::lib_add_adj(a_bits, window, carry, a_width);
                }
            }
            sturm::uncompute_and(tmp, *outer, flag_q);
        } else {
            WHEN(flag_q) {
                if constexpr (Forward) {
                    lib_add_dsl(a_bits, window, carry, a_width);
                } else {
                    detail_div::lib_add_adj(a_bits, window, carry, a_width);
                }
            }
        }
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
