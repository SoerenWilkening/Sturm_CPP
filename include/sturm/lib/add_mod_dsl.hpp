// add_mod_dsl.hpp -- P1 (sturm-yh3d) lib_add_mod_dsl forward primitive.
//
// Out-of-place modular addition: r = (a + b) mod n, all unsigned, all W bits
// wide.  Built strictly on top of the existing lib_add_dsl + the gate-reverse
// detail_div::lib_add_adj (no direct gate emission, no new arithmetic kernel
// — see PRD §4 layering rule).
//
// Algorithm (plan §3.1, distilled into the smallest gate-reversible shape
// that survives all branches of the input space, not just the (1,1,3) demo):
//
//   1. allocate (W+1)-bit sum register `s` (s_low = s[0..W-1], s_high = s[W])
//   2. copy a -> s_low via per-bit XOR (s = a)
//   3. lib_add_dsl(b, s_low, s_high, W)              ->  s = a + b
//   4. allocate n_pad qubit (always |0>); n_extended = [n_bits..., n_pad]
//   5. allocate lt_flag, carry_anc_for_add (both |0>)
//   6. lib_add_adj(n_extended, s_full, lt_flag, W+1)
//      this is the gate-reverse of an unconditional add of n; its side-effect
//      is exactly lt_flag = (s_old < n) (s_old < n => carry_out flips to 1
//      because the inverse-add saw a "borrow"); s becomes s_old - n mod 2^(W+1)
//   7. push_control(lt_flag); lib_add_dsl(n_extended, s_full,
//      carry_anc_for_add, W+1); pop_control
//      conditional add-back: when lt_flag=1 we restore s to s_old (the original
//      a+b); when lt_flag=0 (s_old >= n) the controlled call is a no-op.
//      The carry_anc_for_add picks up exactly lt_flag's value (1 in the
//      add-back branch, 0 otherwise).
//   8. carry_anc_for_add ^= lt_flag                   ->  carry_anc_for_add = 0
//   9. r ^= s_low                                     ->  r = (a+b) mod n
//  10. carry_anc_for_add ^= lt_flag                   ->  carry_anc_for_add = lt_flag
//  11. push_control(lt_flag); lib_add_adj(n_extended, s_full,
//      carry_anc_for_add, W+1); pop_control          ->  reverses step 7
//  12. lib_add_dsl(n_extended, s_full, lt_flag, W+1)  ->  reverses step 6;
//                                                         lt_flag returns to 0
//  13. lib_add_adj(b, s_low, s_high, W)               ->  reverses step 3 (s = a)
//  14. s_low ^= a                                     ->  reverses step 2 (s = 0)
//  15. release lt_flag, n_pad, carry_anc_for_add, s LIFO.
//
// Why lib_add_adj and not lib_sub_dsl?  PRD §4 says we must layer on the
// existing primitives; lib_sub_dsl exists but its gate-reverse pair is the
// unhandled half of the catalogue, whereas lib_add_dsl <-> lib_add_adj is the
// already-registered LO-1a pair (used inside mul_dsl and div_dsl).  Keeping
// the algorithm in that pair lets the adjoint sibling (sturm-yh3d.5) reuse
// the same kernel.
//
// Why no `lib_ge_dsl` ancilla?  Plan §3.1 sketches a separate `ge_flag`
// computed via `lib_ge_dsl(s, n_extended, W+1, ge_flag)` followed by a
// controlled subtract.  That shape is correct but exceeds STURM's
// kMaxQubits=17 simulator cap at W=2: the (W+1)-bit `lib_ge_dsl` allocates
// ~7 transient ancillas (lt_idx + a_copy[W+1] + borrow + carry_scratch + an
// internal sub/add carry), pushing the live qubit count past the cap.  The
// `lib_add_adj` route used here folds the comparison and the conditional
// subtract into a single primitive call by reading the post-call carry bit
// as the `s_old < n` flag, then pairs that with a controlled add-back to
// land on `(a+b) mod n`.  This preserves the spirit of plan §3.1 — paired
// compute/uncompute via existing `lib_*_dsl` primitives — while keeping the
// peak live-ancilla count inside the cap.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// mod_dsl.hpp / mod_dsl_adj.hpp pairing pattern).
//
// Target: <=250 LoC.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"          // detail_div::lib_add_adj
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/control/lift.hpp"          // sturm-k8f2: shared lift_under

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace sturm {

namespace detail_add_mod {

// Bit-view helper mirroring detail_adder::make_ancilla_view, kept local so
// add_mod_dsl can be included without dragging the adder helper namespace
// into headers that only need the modular form.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_add_mod

/**
 * @brief Out-of-place W-bit modular addition primitive:
 *        `r_bits = (a_bits + b_bits) mod n_bits`.
 *
 * This is the lib-level primitive backing the public `sturm::add_mod`
 * free function (see `include/sturm/ops/qint_modular.hpp`).  Built
 * strictly on top of `lib_add_dsl` + `detail_div::lib_add_adj` per the
 * PRD §4 layering rule — emits no gates of its own.  See the header
 * preamble for the full algorithm.
 *
 * @param a_bits Left addend register (W qubits, read but restored).
 * @param b_bits Right addend register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width (NOT the modulus; the modulus is encoded
 *               in `n_bits[0..n-1]`).  `n == 0` short-circuits to a
 *               no-op (matches PRD §5 / §8 #3).
 * @param r_bits Result register (W qubits).  Must start in |0>.
 *               On exit, holds `(a + b) mod n`.
 *
 * @pre `a, b ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the call is a
 *      no-op — see PRD §5 / §8 #3).  `r_bits` must enter the routine
 *      in |0>.  `a_bits`, `b_bits`, `n_bits`, and `r_bits` must refer
 *      to physically distinct qubit registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_add_mod_dsl` with operands outside `[0, n)` is **undefined
 * behavior** — the routine still emits a well-formed gate sequence,
 * but the resulting `r_bits` are not the mathematical answer and the
 * input registers may not be restored.  This matches the trust model
 * of the public `add_mod` wrapper and of classical `pow(a, b, c)` /
 * GMP / OpenSSL (PRD §5).
 *
 * @sa __lib_add_mod_dsl_adj, lib_mul_mod_dsl, sturm::add_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_add_mod_dsl(Bit* a_bits, Bit* b_bits,
                            Bit* n_bits, std::size_t n,
                            Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 32u;
    assert(n <= kMaxN && "lib_add_mod_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_add_mod_dsl: no BackendContext installed");
    (void)raw;  // sturm-a3t4.2: control_stack is now driven by WHEN/WhenGuard
                // through the lift pattern; ctx is no longer poked directly.

    // (1) Allocate the (W+1)-bit sum register `s`.
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

    // (2) Copy a -> s_low via per-bit XOR.  s = a.
    for (std::size_t i = 0u; i < n; ++i) s_low[i] ^= a_bits[i];

    // (3) lib_add_dsl(b, s_low, s_high, n).  s = a + b.
    lib_add_dsl(b_bits, s_low, s_high, n);

    // (4) Allocate n_pad qubit (always |0>); form n_extended view.
    int   n_pad_idx = QubitPool::instance().allocate();
    qbool n_pad_own = qbool::make_non_owning(n_pad_idx);
    Bit   n_pad     = detail_add_mod::make_ancilla_view<Bit>(n_pad_own);
    Bit   n_ext[kMaxN + 1u];
    for (std::size_t i = 0u; i < n; ++i) n_ext[i] = n_bits[i];
    n_ext[n] = n_pad;

    // (5) Allocate lt_flag and carry_anc_for_add (both |0>).
    int   lt_flag_idx     = QubitPool::instance().allocate();
    qbool lt_flag_own     = qbool::make_non_owning(lt_flag_idx);
    // sturm-a3t4.2: lt_flag receives a data-dependent comparison bit at
    // step (6); mark its qbool anchor as superposed so the lift pattern's
    // `(*outer) & lt_flag_own` and `WHEN(lt_flag_own)` take the quantum
    // branch instead of being short-circuited as classical |0>.
    lt_flag_own.super_mask = 1ULL;
    Bit   lt_flag         = detail_add_mod::make_ancilla_view<Bit>(lt_flag_own);
    int   carry_anc_idx   = QubitPool::instance().allocate();
    qbool carry_anc_own   = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc       = detail_add_mod::make_ancilla_view<Bit>(carry_anc_own);

    // (6) Gate-reverse of unconditional add: subtract n_extended from s.
    //     Side-effect: lt_flag ^= (s_old < n_extended).
    detail_div::lib_add_adj(n_ext, s_bits, lt_flag, n + 1u);

    // (7) Conditional add-back of n_extended controlled on lt_flag.
    //     When lt_flag=1, restores s to s_old (= a+b) and pushes overflow=1
    //     into carry_anc_for_add.  When lt_flag=0, no-op.
    //
    // sturm-k8f2: collapse outer-control + lt_flag into the shared
    // sturm::lift_under helper.  Use lt_flag_own (the qbool anchor), not the
    // Bit view, so the helper drives operator& / uncompute_and on qbool
    // inputs and forces super_mask=1 on the view internally.
    sturm::lift_under(lt_flag_own, [&]() {
        lib_add_dsl(n_ext, s_bits, carry_anc, n + 1u);
    });

    // (8) carry_anc_for_add ^= lt_flag  ->  carry_anc_for_add returns to 0
    //     (in both branches) so that the controlled add is paired with a
    //     fresh sink for its overflow bit.
    carry_anc ^= lt_flag;

    // (9) Copy s_low (= (a+b) mod n) into r via per-bit XOR.
    for (std::size_t i = 0u; i < n; ++i) r_bits[i] ^= s_low[i];

    // (10) Re-XOR carry_anc_for_add with lt_flag (reverse of step 8) so the
    //      following gate-reverse of step 7 sees the post-step-7 state.
    carry_anc ^= lt_flag;

    // (11) Reverse step 7: controlled (gate-reverse) of the add-back.
    //      Same shared lift as step (7).
    sturm::lift_under(lt_flag_own, [&]() {
        detail_div::lib_add_adj(n_ext, s_bits, carry_anc, n + 1u);
    });

    // (12) Reverse step 6: unconditional add restores s to a+b and clears
    //      lt_flag back to |0>.
    lib_add_dsl(n_ext, s_bits, lt_flag, n + 1u);

    // (13) Reverse step 3: s = a.
    detail_div::lib_add_adj(b_bits, s_low, s_high, n);

    // (14) Reverse step 2: s = 0.
    for (std::size_t i = 0u; i < n; ++i) s_low[i] ^= a_bits[i];

    // (15) Release ancillas LIFO (carry_anc, lt_flag, n_pad, s).
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(lt_flag_idx);
    QubitPool::instance().release(n_pad_idx);
    for (std::size_t i = n + 1u; i-- > 0u;) {
        QubitPool::instance().release(s_idx[i]);
    }
}

} // namespace sturm

#include "sturm/lib/add_mod_dsl_adj.hpp"
