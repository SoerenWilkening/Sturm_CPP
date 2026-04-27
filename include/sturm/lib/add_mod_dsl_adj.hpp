// add_mod_dsl_adj.hpp -- P1 (sturm-yh3d.5) lib_add_mod_dsl adjoint sibling.
//
// __lib_add_mod_dsl_adj is the gate-reverse of lib_add_mod_dsl: starting
// from `r = (a + b) mod n` (and the original `a, b, n` preserved), it
// returns `r` to |0> while leaving `a, b, n` unchanged.  Implementation
// runs the forward gate sequence in reverse, swapping each lib_add_dsl
// for lib_add_adj (and vice-versa); self-inverse XOR steps stay as-is.
//
// Re-allocation order for the auxiliary ancillas (`s`, `n_pad`, `lt_flag`,
// `carry_anc`) matches the forward exactly so that, under TDD harnesses
// that call `reset_for_testing()` between cases, the QubitPool's LIFO
// reuses the same indices.  Even when the adjoint runs back-to-back with
// the forward (no reset), the operations only touch ancillas the adjoint
// owns, so the order matters only for LoC parity with the forward
// (mirrors `mod_dsl_adj.hpp`'s "re-allocate in forward order" pattern).
//
// Algorithm (reverse of add_mod_dsl.hpp's steps 1–15):
//   1'. allocate s (W+1), n_pad, lt_flag, carry_anc in forward order.
//   2'. s_low ^= a                                   (reverse of step 14)
//   3'. lib_add_dsl(b, s_low, s_high, n)             (reverse of step 13)
//   4'. lib_add_adj(n_ext, s_full, lt_flag, n+1)     (reverse of step 12)
//   5'. push(lt_flag); lib_add_dsl(n_ext, s_full,
//                                 carry_anc, n+1); pop  (reverse of 11)
//   6'. carry_anc ^= lt_flag                         (reverse of step 10)
//   7'. r ^= s_low                                   (reverse of step 9;
//                                                     zeros r since r==s_low)
//   8'. carry_anc ^= lt_flag                         (reverse of step 8)
//   9'. push(lt_flag); lib_add_adj(n_ext, s_full,
//                                 carry_anc, n+1); pop  (reverse of 7)
//  10'. lib_add_dsl(n_ext, s_full, lt_flag, n+1)     (reverse of step 6)
//  11'. lib_add_adj(b, s_low, s_high, n)             (reverse of step 3)
//  12'. s_low ^= a                                   (reverse of step 2)
//  13'. release carry_anc, lt_flag, n_pad, s LIFO    (reverse of step 1)
//
// Sibling registration (`STURM_REGISTER_ADJOINT`) at the bottom hooks
// `invert<&lib_add_mod_dsl<BitProxy>>()` to this adjoint, mirroring the
// pattern used by `div_dsl_adj.hpp`/`mod_dsl_adj.hpp`.
//
// Auto-included from add_mod_dsl.hpp.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"          // detail_div::lib_add_adj
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/control/lift.hpp"          // sturm-k8f2: shared lift_under
#include "sturm/routines/invert.hpp"

#include <cstddef>
#include <cassert>
#include <type_traits>

// Forward-declare BitProxy for the LO-1b adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

/**
 * @brief Gate-reverse adjoint of @ref lib_add_mod_dsl.
 *
 * Starting from `r_bits = (a_bits + b_bits) mod n_bits` (with the
 * original `a_bits`, `b_bits`, `n_bits` preserved by the forward call),
 * this routine returns `r_bits` to |0> while leaving the input
 * registers unchanged.  Implementation runs the forward gate sequence
 * in reverse, swapping each `lib_add_dsl` for `lib_add_adj` (and
 * vice-versa); self-inverse XOR / control-stack steps stay as-is.
 *
 * Built strictly on top of `lib_add_dsl` + `detail_div::lib_add_adj`
 * per the PRD §4 layering rule — emits no gates of its own.
 *
 * @param a_bits Left addend register (W qubits, read but restored).
 * @param b_bits Right addend register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width.  `n == 0` short-circuits to a no-op.
 * @param r_bits Result register (W qubits).  Must enter holding
 *               `(a + b) mod n`; exits in |0>.
 *
 * @pre `a, b ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the call is a
 *      no-op).  `r_bits` must enter the routine holding the value
 *      produced by a paired `lib_add_mod_dsl` call on the same
 *      `(a_bits, b_bits, n_bits)` operands.  `a_bits`, `b_bits`,
 *      `n_bits`, and `r_bits` must refer to physically distinct qubit
 *      registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_add_mod_dsl_adj` with operands outside `[0, n)`, with
 * `r_bits` not equal to the paired forward output, or with aliased
 * registers is **undefined behavior** — the routine still emits a
 * well-formed gate sequence, but `r_bits` will not return to |0> and
 * the input registers may be corrupted.  This matches the trust model
 * shared with the public `add_mod` wrapper (PRD §5).
 *
 * @sa lib_add_mod_dsl, __lib_mul_mod_dsl_adj, sturm::add_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_add_mod_dsl_adj(Bit* a_bits, Bit* b_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 32u;
    assert(n <= kMaxN && "__lib_add_mod_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_add_mod_dsl_adj: no BackendContext installed");
    (void)raw;  // sturm-a3t4.2: control_stack is now driven by WHEN/WhenGuard
                // through the lift pattern; ctx is no longer poked directly.

    // (1') Re-allocate the (W+1)-bit sum register `s` in forward order.
    int   s_idx[kMaxN + 1u];
    qbool s_own[kMaxN + 1u];
    Bit   s_bits[kMaxN + 1u];
    for (std::size_t i = 0u; i < n + 1u; ++i) {
        s_idx[i]  = QubitPool::instance().allocate();
        s_own[i]  = qbool::make_non_owning(s_idx[i]);
        s_bits[i] = detail_add_mod::make_ancilla_view<Bit>(s_own[i]);
    }
    Bit* s_low  = s_bits;
    Bit& s_high = s_bits[n];

    // (1') Allocate n_pad qubit (always |0>); form n_extended view.
    int   n_pad_idx = QubitPool::instance().allocate();
    qbool n_pad_own = qbool::make_non_owning(n_pad_idx);
    Bit   n_pad     = detail_add_mod::make_ancilla_view<Bit>(n_pad_own);
    Bit   n_ext[kMaxN + 1u];
    for (std::size_t i = 0u; i < n; ++i) n_ext[i] = n_bits[i];
    n_ext[n] = n_pad;

    // (1') Allocate lt_flag and carry_anc (both |0>).
    int   lt_flag_idx     = QubitPool::instance().allocate();
    qbool lt_flag_own     = qbool::make_non_owning(lt_flag_idx);
    // sturm-a3t4.2: lt_flag receives a data-dependent comparison bit at
    // step (4'); mark its qbool anchor as superposed so the lift pattern's
    // `(*outer) & lt_flag_own` and `WHEN(lt_flag_own)` take the quantum
    // branch instead of being short-circuited as classical |0>.
    lt_flag_own.super_mask = 1ULL;
    Bit   lt_flag         = detail_add_mod::make_ancilla_view<Bit>(lt_flag_own);
    int   carry_anc_idx   = QubitPool::instance().allocate();
    qbool carry_anc_own   = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc       = detail_add_mod::make_ancilla_view<Bit>(carry_anc_own);

    // (2') Reverse forward step 14 (s_low ^= a is self-inverse): s = a.
    for (std::size_t i = 0u; i < n; ++i) s_low[i] ^= a_bits[i];

    // (3') Reverse forward step 13 (lib_add_adj on (b, s_low, s_high)):
    //      run lib_add_dsl on the same operands.  s = a + b.
    lib_add_dsl(b_bits, s_low, s_high, n);

    // (4') Reverse forward step 12 (lib_add_dsl(n_ext, s_full, lt_flag, n+1)):
    //      run lib_add_adj — restores s to s - n mod 2^(W+1) and flips
    //      lt_flag to (s_old < n).
    detail_div::lib_add_adj(n_ext, s_bits, lt_flag, n + 1u);

    // (5') Reverse forward step 11 (controlled lib_add_adj):
    //      controlled lib_add_dsl on the same operands.
    //
    // sturm-k8f2: same shared sturm::lift_under as the forward; the qbool
    // anchor `lt_flag_own` (not the Bit view) is the operand so the helper
    // drives operator& / uncompute_and on qbool inputs.
    sturm::lift_under(lt_flag_own, [&]() {
        lib_add_dsl(n_ext, s_bits, carry_anc, n + 1u);
    });

    // (6') Reverse forward step 10 (carry_anc ^= lt_flag is self-inverse).
    carry_anc ^= lt_flag;

    // (7') Reverse forward step 9 (r ^= s_low is self-inverse): zeros r,
    //      since r currently equals s_low == (a+b) mod n.
    for (std::size_t i = 0u; i < n; ++i) r_bits[i] ^= s_low[i];

    // (8') Reverse forward step 8 (carry_anc ^= lt_flag is self-inverse).
    carry_anc ^= lt_flag;

    // (9') Reverse forward step 7 (controlled lib_add_dsl):
    //      controlled lib_add_adj on the same operands.  Same shared lift.
    sturm::lift_under(lt_flag_own, [&]() {
        detail_div::lib_add_adj(n_ext, s_bits, carry_anc, n + 1u);
    });

    // (10') Reverse forward step 6 (lib_add_adj(n_ext, s_full, lt_flag, n+1)):
    //       lib_add_dsl on the same operands restores s to s + n and clears
    //       lt_flag back to |0>.
    lib_add_dsl(n_ext, s_bits, lt_flag, n + 1u);

    // (11') Reverse forward step 3 (lib_add_dsl(b, s_low, s_high, n)):
    //       lib_add_adj on the same operands.  s = a.
    detail_div::lib_add_adj(b_bits, s_low, s_high, n);

    // (12') Reverse forward step 2 (s_low ^= a is self-inverse): s = 0.
    for (std::size_t i = 0u; i < n; ++i) s_low[i] ^= a_bits[i];

    // (13') Release ancillas LIFO (carry_anc, lt_flag, n_pad, s).
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(lt_flag_idx);
    QubitPool::instance().release(n_pad_idx);
    for (std::size_t i = n + 1u; i-- > 0u;) {
        QubitPool::instance().release(s_idx[i]);
    }
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_add_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_add_mod_dsl_adj<sturm::BitProxy>)
#endif
