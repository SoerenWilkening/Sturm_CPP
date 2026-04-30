// add_mod_inplace_dsl_adj.hpp -- sturm-8lnp Beat A adjoint sibling.
//
// `__lib_add_mod_inplace_dsl_adj` is the gate-reverse of
// `lib_add_mod_inplace_dsl`: starting from `dest = (dest_old + a) mod n`
// (with the original `a_bits`, `n_bits` preserved by the forward call),
// it returns `dest_bits[0..W-1]` to `dest_old` while leaving `n_bits` and
// `a_bits` unchanged.  When viewed as an algebraic operation on the dest
// value, the adjoint computes `dest := (dest - a) mod n` -- the modular
// inverse of the forward's `dest := (dest + a) mod n`.
//
// Implementation runs the forward gate sequence in reverse, swapping each
// `lib_add_dsl` for `lib_add_adj` (and vice-versa); self-inverse XOR /
// `flip` / `lift_under` steps stay as-is.
//
// Re-allocation order for the auxiliary ancillas (`n_pad`, `lt_flag`,
// `carry_anc`) matches the forward exactly so that, under TDD harnesses
// that call `reset_for_testing()` between cases, the QubitPool's LIFO
// reuses the same indices.  Even when the adjoint runs back-to-back with
// the forward (no reset), the operations only touch ancillas the adjoint
// owns, so the order matters only for LoC parity with the forward
// (mirrors `add_mod_dsl_adj.hpp`'s "re-allocate in forward order" pattern).
//
// Algorithm (reverse of add_mod_inplace_dsl.hpp's steps 1-10):
//   1'. allocate n_pad, lt_flag, carry_anc in forward order.  Build n_ext.
//   2'. Reverse forward step 9 (lib_add_dsl(a, dest_low, dest_high, W)):
//          lib_add_adj(a, dest_low, dest_high, W);
//   3'. Reverse forward step 8 (lt_flag uncompute):
//          lt_flag.flip();
//          lt_flag ^= dest_high;
//       Restores lt_flag to the (a+b<n) state it held after forward step 6.
//   4'. Reverse forward step 7 (lib_add_adj(a, dest_low, dest_high, W)):
//          lib_add_dsl(a, dest_low, dest_high, W);
//   5'. Reverse forward step 6 (carry_anc ^= lt_flag, self-inverse):
//          carry_anc ^= lt_flag;
//   6'. Reverse forward step 5 (controlled lib_add_dsl):
//          lift_under(lt_flag) { lib_add_adj(n_ext, dest_bits, carry_anc,
//                                            n+1); }
//   7'. Reverse forward step 4 (lib_add_adj on dest_bits):
//          lib_add_dsl(n_ext, dest_bits, lt_flag, n+1);
//       Restores dest to (dest_old + a) in (W+1)-bit storage and clears
//       lt_flag back to |0>.
//   8'. Reverse forward step 1 (lib_add_dsl(a, dest_low, dest_high, W)):
//          lib_add_adj(a, dest_low, dest_high, W);
//       Subtracts a from dest_low and clears dest_high back to |0>.
//   9'. Release ancillas LIFO (carry_anc, lt_flag, n_pad).
//
// Step trace (input dest = (b+a) mod n where b = dest_old):
//   - State after 2': branch (b+a<n): dest_low = b, dest_high = 0;
//                     branch (b+a>=n): dest_low = b-n+2^W, dest_high = 1.
//   - State after 3': lt_flag = 1 in both branches (flip of 0, XOR with
//                     dest_high), then XOR'd with dest_high yields:
//       branch (b+a<n) (dest_high=0): lt_flag = 1 XOR 0 = 1.
//       branch (b+a>=n) (dest_high=1): lt_flag = 1 XOR 1 = 0.
//     I.e. lt_flag = (b+a < n), restored to its forward step-6 state.
//   - State after 4' (add a back): dest_low = b+a in branch 1 (no carry,
//                                  dest_high stays 0), or dest_low = b+a-n
//                                  in branch 2 (carry overflows, dest_high
//                                  flips 1 -> 0).  In both branches:
//                                  dest_low = (b+a) mod n -> WAIT, branch 1
//                                  gives dest_low = b+a (not (b+a) mod n).
//
// Re-checking the trace: after 4', dest_low = b+a in branch 1 (which is in
// [0, n) since b+a < n), and dest_low = b+a-n in branch 2 (which is in
// [0, n) since b+a >= n).  In both branches dest_low = (b+a) mod n -- the
// branches give different physical values but both equal to (b+a) mod n.
// HOWEVER, this is the WRONG INTERMEDIATE state for the adjoint's gate
// trace: we need dest = (a+b) mod 2^(W+1) at this point (so that step 7'
// can clear lt_flag via the lib_add_dsl carry).  The trace continues:
//   - State after 5' (carry_anc ^= lt_flag): carry_anc = lt_flag.
//   - State after 6' (controlled lib_add_adj):
//       branch 1 (lt_flag=1, dest = b+a < n): subtract n.  dest = b+a-n,
//          which is negative; wrap to b+a-n+2^(W+1).  dest[W] flips 0->1.
//          carry_anc XOR 1 (borrow) = lt_flag XOR 1 = 0.
//       branch 2 (lt_flag=0): no-op.  dest still b+a-n.  dest[W] still 0.
//          carry_anc still 0.
//   - State after 7' (lib_add_dsl n_ext to dest, lt_flag): adds n.
//       branch 1: dest = (b+a-n+2^(W+1)) + n = b+a+2^(W+1) -> wraps to
//          b+a in (W+1)-bit storage.  Carry: 1.  lt_flag XOR 1 = 0.
//          dest[W] = (b+a) >= 2^W ? 1 : 0.
//       branch 2: dest = (b+a-n) + n = b+a.  Carry: 0 (no overflow).
//          lt_flag XOR 0 = 0.  dest[W] = (b+a) >= 2^W ? 1 : 0.
//     Both branches converge: dest = b+a in (W+1)-bit storage; lt_flag = 0.
//   - State after 8' (lib_add_adj(a, dest_low, dest_high, W)): subtract a.
//       In (W+1)-bit storage dest = b+a.  Subtracting a from low W bits:
//          if b+a < 2^W (so dest_high = 0): dest_low = b, no borrow.
//             dest_high stays 0.
//          if b+a >= 2^W (so dest_high = 1): dest_low = (b+a-2^W) - a =
//             b-2^W mod 2^W = b.  Borrow occurs (since b+a-2^W < a), so
//             dest_high flips 1 -> 0.
//       Both branches: dest_low = b, dest_high = 0.
//
// So the adjoint correctly restores dest to its pre-forward value `b`,
// and all ancillas (lt_flag, n_pad, carry_anc) are released back to |0>.
//
// STURM_REGISTER_ADJOINT at the bottom hooks
// `invert<&lib_add_mod_inplace_dsl<BitProxy>>()` to this adjoint, mirroring
// the add_mod_dsl_adj.hpp / double_mod_dsl_adj.hpp pattern.
//
// Auto-included from add_mod_inplace_dsl.hpp.

#pragma once

#include "sturm/detail/lib/adder_dsl.hpp"
#include "sturm/detail/lib/div_dsl.hpp"          // detail_div::lib_add_adj
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

// Forward-declare BitProxy for the adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

/**
 * @brief Gate-reverse adjoint of @ref lib_add_mod_inplace_dsl.
 *
 * Starting from `dest_bits[0..n-1] = (dest_old + a) mod n_value` (with
 * the original `a_bits` and `n_bits` preserved by the forward call),
 * this routine returns `dest_bits[0..n-1]` to `dest_old` while leaving
 * `n_bits` and `a_bits` unchanged.  Implementation runs the forward gate
 * sequence in reverse, swapping each `lib_add_dsl` for `lib_add_adj` (and
 * vice-versa); self-inverse XOR / `flip` / `lift_under` steps stay as-is.
 *
 * Algebraically, the adjoint computes `dest := (dest - a) mod n_value`
 * -- the modular inverse of the forward's `dest := (dest + a) mod n_value`.
 *
 * Built strictly on top of `lib_add_dsl` + `detail_div::lib_add_adj`
 * per the PRD §4 layering rule -- emits no gates of its own.
 *
 * @param a_bits   Addend register (W qubits, read but restored).
 * @param dest_bits Destination register of length `n + 1` qubits.  On
 *                 entry holds `(dest_old + a) mod n_value` in bits 0..n-1
 *                 and 0 in bit n.  On exit holds `dest_old` in bits 0..n-1
 *                 and 0 in bit n.
 * @param n_bits   Modulus register (n qubits, read but restored).
 * @param n        Register width.  `n == 0` short-circuits to a no-op.
 *
 * @pre `dest_bits[0..n-1]` must enter holding the value produced by a
 *      paired `lib_add_mod_inplace_dsl` call on the same `(a_bits, n_bits)`
 *      operands -- equivalently, any value `c in [0, n_value)`.  (Because
 *      modular subtraction is well-defined for any `c in [0, n)`, the
 *      adjoint is correct as a free-standing "subtract a mod n" primitive
 *      so long as the precondition `dest_old, a in [0, n)` is met.)
 *      `dest_bits[n]` must be |0>.  `a_bits`, `dest_bits`, and `n_bits`
 *      must refer to **physically distinct** qubit registers (inherited
 *      from `lib_add_dsl`'s aliasing rule).
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_add_mod_inplace_dsl_adj` with operands outside `[0, n_value)`,
 * with `dest_bits[n]` not in |0>, or with aliased registers is **undefined
 * behavior** -- the routine still emits a well-formed gate sequence, but
 * `dest_bits` will not be the modular subtraction result and the input
 * registers may be corrupted.  This matches the trust model shared with
 * the forward (PRD §5).
 *
 * @sa lib_add_mod_inplace_dsl, __lib_add_mod_dsl_adj, __lib_double_mod_dsl_adj
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_add_mod_inplace_dsl_adj(Bit* a_bits, Bit* dest_bits,
                                          Bit* n_bits, std::size_t n) {
    if (n == 0u) return;

    // sturm-8n73: kMaxN bumped 32 → 64 to match the forward primitive.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN
           && "__lib_add_mod_inplace_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_add_mod_inplace_dsl_adj: no BackendContext installed");
    (void)raw;  // control_stack is driven by WHEN/WhenGuard through the
                // lift pattern; ctx is no longer poked directly.

    // Convenience aliases for the dest register's low (W) and high (1) parts.
    Bit* dest_low  = dest_bits;
    Bit& dest_high = dest_bits[n];

    // (1') Re-allocate n_pad in forward order; build n_ext.
    int   n_pad_idx = QubitPool::instance().allocate();
    qbool n_pad_own = qbool::make_non_owning(n_pad_idx);
    Bit   n_pad     =
        detail_add_mod_inplace::make_ancilla_view<Bit>(n_pad_own);
    Bit   n_ext[kMaxN + 1u];
    for (std::size_t i = 0u; i < n; ++i) n_ext[i] = n_bits[i];
    n_ext[n] = n_pad;

    // (1') Re-allocate lt_flag and carry_anc in forward order (lt_flag
    //      receives a data-dependent comparison bit at step 3'; mark its
    //      qbool anchor superposed so the lift pattern takes the quantum
    //      branch instead of short-circuiting as classical |0>).
    int   lt_flag_idx     = QubitPool::instance().allocate();
    qbool lt_flag_own     = qbool::make_non_owning(lt_flag_idx);
    lt_flag_own.super_mask = 1ULL;
    Bit   lt_flag         =
        detail_add_mod_inplace::make_ancilla_view<Bit>(lt_flag_own);
    int   carry_anc_idx   = QubitPool::instance().allocate();
    qbool carry_anc_own   = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc       =
        detail_add_mod_inplace::make_ancilla_view<Bit>(carry_anc_own);

    // (2') Reverse forward step 9 (lib_add_dsl(a, dest_low, dest_high, W)):
    //      lib_add_adj on the same operands.
    detail_div::lib_add_adj(a_bits, dest_low, dest_high, n);

    // (3') Reverse forward step 8 (`lt_flag ^= dest_high; lt_flag.flip();`):
    //      reverse runs `lt_flag.flip(); lt_flag ^= dest_high;`.
    //      Restores lt_flag to (b+a < n).
    lt_flag.flip();
    lt_flag ^= dest_high;

    // (4') Reverse forward step 7 (lib_add_adj(a, dest_low, dest_high, W)):
    //      lib_add_dsl on the same operands.
    lib_add_dsl(a_bits, dest_low, dest_high, n);

    // (5') Reverse forward step 6 (carry_anc ^= lt_flag, self-inverse).
    carry_anc ^= lt_flag;

    // (6') Reverse forward step 5 (controlled lib_add_dsl):
    //      controlled lib_add_adj on the same operands.
    sturm::lift_under(lt_flag_own, [&]() {
        detail_div::lib_add_adj(n_ext, dest_bits, carry_anc, n + 1u);
    });

    // (7') Reverse forward step 4 (lib_add_adj on dest_bits):
    //      lib_add_dsl on the same operands restores dest to (b+a) in
    //      (W+1)-bit storage and clears lt_flag back to |0>.
    lib_add_dsl(n_ext, dest_bits, lt_flag, n + 1u);

    // (8') Reverse forward step 1 (lib_add_dsl(a, dest_low, dest_high, W)):
    //      lib_add_adj on the same operands.  Subtracts a from dest_low
    //      and clears dest_high back to |0>.
    detail_div::lib_add_adj(a_bits, dest_low, dest_high, n);

    // (9') Release ancillas LIFO (carry_anc, lt_flag, n_pad).
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(lt_flag_idx);
    QubitPool::instance().release(n_pad_idx);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>,
                       sturm::__lib_add_mod_inplace_dsl_adj<sturm::BitProxy>)
#endif
