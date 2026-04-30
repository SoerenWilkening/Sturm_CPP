// add_mod_inplace_dsl.hpp -- sturm-8lnp Beat A: lib_add_mod_inplace_dsl
//                              forward primitive (in-place modular addition).
//
// In-place modular addition: dest := (dest + a) mod n, all unsigned.  The
// `dest_bits` register is W+1 qubits wide -- bits 0..W-1 hold the running
// value `dest_old` (entering in [0, n_value)) and bit W is the overflow slot
// (must enter |0>).  The forward computes `dest_new = (dest_old + a) mod n`
// in-place, leaving dest_bits[W] back at |0>.  Built strictly on top of the
// existing lib_add_dsl + detail_div::lib_add_adj pair (no direct gate
// emission, no new arithmetic kernel -- see PRD §4 layering rule).
//
// Why a separate primitive (vs. the out-of-place lib_add_mod_dsl)?  Beat C
// (sturm-7cix) rewrites lib_mul_mod_dsl onto a single accumulator + single
// shifted register without the chain-style O(W^2) qubit cost.  The
// accumulator is updated via `acc := (acc + shifted) mod n`, which an
// out-of-place primitive cannot express without an extra W-bit copy slot
// per chain step.  Pairing this in-place add-mod with the in-place
// lib_double_mod_dsl (Beat B, sturm-wdas) keeps the rewrite's running ancilla
// footprint to O(W) instead of O(W^2).
//
// Algorithm (12 steps; mirrors add_mod_dsl.hpp's trial-subtract /
// conditional-add-back idiom from steps 6-11 of that file's preamble, with
// dest_bits playing the role of the (W+1)-bit `s` register):
//
//   1. lib_add_dsl(a_bits, dest_low, dest_high, W)
//      dest_low (= dest_bits[0..W-1]) holds dest_old = b in [0, n).
//      dest_high (= dest_bits[W]) is the overflow slot, |0> on entry.
//      After this call: dest = a + b in (W+1)-bit storage.  dest_high
//      may be 1 if a + b >= 2^W; dest still < 2n <= 2^(W+1).
//   2. allocate n_pad qubit (always |0>); form n_extended = [n_bits, n_pad].
//   3. allocate lt_flag and carry_anc (both |0>).
//   4. lib_add_adj(n_ext, dest_bits, lt_flag, W+1)
//      gate-reverse of an unconditional add of n_ext: subtracts n from dest.
//      Side-effect: lt_flag ^= ((a+b) < n).  After call:
//        - branch (a+b < n): dest = (a+b)-n+2^(W+1) (wrap), dest[W] = 1,
//          lt_flag = 1.
//        - branch (a+b >= n): dest = (a+b)-n in [0, n) ⊂ [0, 2^W),
//          dest[W] = 0, lt_flag = 0.
//      I.e. dest[W] == lt_flag after this step.
//   5. lift_under(lt_flag) { lib_add_dsl(n_ext, dest_bits, carry_anc, W+1); }
//      Conditional add-back of n controlled on lt_flag.  After call:
//        - lt_flag=1 branch: dest = a + b (carry_anc = 1, the (W+1)-bit
//          add overflowed because dest+ n in this branch wraps from
//          2^(W+1)-(n-(a+b)) past 2^(W+1)).
//        - lt_flag=0 branch: dest = (a+b) - n (no-op; carry_anc = 0).
//      In both branches: dest_low (low W bits) = (a+b) mod n in [0, n),
//      dest[W] = 0, carry_anc = lt_flag.
//   6. carry_anc ^= lt_flag      ->  carry_anc returns to 0 in both branches.
//      State: dest_low = (a+b) mod n, dest[W] = 0, lt_flag still set,
//      carry_anc = 0.
//   7. lib_add_adj(a_bits, dest_low, dest_high, W)
//      W-bit subtract of a from dest_low; dest_high (= dest[W]) gets XOR'd
//      with the borrow bit (1 iff dest_low < a).
//        - lt_flag=1 branch: dest_low = a+b in [a, n), so a <= dest_low,
//          no borrow.  dest_low becomes b.  dest[W] still 0.
//        - lt_flag=0 branch: dest_low = (a+b)-n in [0, n) and we need
//          dest_low - a; (a+b)-n - a = b-n which is negative since b < n,
//          so borrow = 1.  dest_low becomes b-n mod 2^W = b-n+2^W.
//          dest[W] flips from 0 to 1.
//      So after step 7: dest[W] = NOT(lt_flag).
//   8. lt_flag ^= dest[W]; lt_flag.flip()
//      Uncompute lt_flag using dest[W] as the witness.
//        - lt_flag=1, dest[W]=0: lt_flag = 1 XOR 0 = 1; flip -> 0.
//        - lt_flag=0, dest[W]=1: lt_flag = 0 XOR 1 = 1; flip -> 0.
//      Both branches: lt_flag = 0.  dest[W] unchanged by this step.
//   9. lib_add_dsl(a_bits, dest_low, dest_high, W)
//      Add a back to dest_low in W bits; dest_high XOR'd with the carry.
//        - lt_flag=1 branch (dest_low = b, dest[W] = 0): dest_low = b+a
//          = a+b in [0, n) ⊂ [0, 2^W), no carry.  dest[W] XOR 0 = 0.
//        - lt_flag=0 branch (dest_low = b-n+2^W, dest[W] = 1): dest_low =
//          (b-n+2^W) + a = a+b-n+2^W.  mod 2^W = a+b-n in [0, n).  Carry =
//          1 (since the sum a+b-n+2^W >= 2^W).  dest[W] XOR 1 = 1 XOR 1 = 0.
//      Both branches: dest_low = (a+b) mod n, dest[W] = 0.
//  10. release lt_flag, n_pad, carry_anc LIFO.  Final state: dest_low =
//      (dest_old + a) mod n in [0, n); dest[W] = 0; lt_flag = 0; n_pad = 0.
//
// Why this lt_flag-uncomputation trick works:  After step 6, dest_low =
// (a+b) mod n in both branches, but the path to that state was different.
// In the lt_flag=1 branch dest still holds a+b (no reduction); subtract a
// to recover the original b (which is dest_old).  In the lt_flag=0 branch
// dest holds (a+b)-n, which is < a (since b<n => (a+b)-n < a); subtracting
// a underflows and sets dest[W] = 1.  This makes dest[W] the gate-reverse
// witness of lt_flag.  After cleaning lt_flag with `^= dest[W]; flip()`,
// adding a back via lib_add_dsl restores dest_low to (a+b) mod n and
// zeroes dest[W] via the carry XOR.  Crucially, steps 7-9 do NOT depend on
// lt_flag (no controls), so they execute unconditionally on both branches.
//
// Why no separate `lib_ge_dsl` ancilla?  Same rationale as lib_add_mod_dsl
// (see add_mod_dsl.hpp preamble): routing the comparison through a
// dedicated `ge_flag` would push the live qubit count past the simulator
// cap and require a new arithmetic kernel.  The lib_add_adj route folds
// the comparison and the conditional subtract into a single primitive call.
//
// Aliasing precondition: a_bits and dest_bits MUST refer to physically
// distinct qubits (inherited from lib_add_dsl's rule -- see adder_dsl.hpp).
// The internal lib_add_dsl/lib_add_adj calls take (a_bits, dest_low) or
// (n_ext, dest_bits) as their (a, b) operand pair; aliasing a with dest
// would corrupt the addend during the ripple-carry pass.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// add_mod_dsl.hpp / add_mod_dsl_adj.hpp pairing pattern).
//
// Target: <=300 LoC.

#pragma once

#include "sturm/detail/lib/adder_dsl.hpp"
#include "sturm/detail/lib/div_dsl.hpp"          // detail_div::lib_add_adj
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

namespace detail_add_mod_inplace {

// Bit-view helper mirroring detail_add_mod::make_ancilla_view, kept local
// so add_mod_inplace_dsl can be included without dragging the out-of-place
// add-mod helper namespace into headers that only need the in-place form.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_add_mod_inplace

/**
 * @brief In-place W-bit modular addition primitive:
 *        `dest_bits := (dest_bits + a_bits) mod n_bits`.
 *
 * This is the lib-level primitive Beat C (sturm-7cix) uses to rewrite
 * `lib_mul_mod_dsl` onto an O(W) accumulator update.  Built strictly on
 * top of `lib_add_dsl` + `detail_div::lib_add_adj` per the PRD §4 layering
 * rule -- emits no gates of its own (only the lib_add_dsl / lib_add_adj
 * primitives, plus self-inverse XOR / flip / `lift_under` lift-pattern).
 * See the header preamble for the full algorithm.
 *
 * @param a_bits   Addend register (W qubits, read but restored).
 * @param dest_bits Destination register of length `n + 1` qubits.  On
 *                 entry `dest_bits[0..n-1]` holds the value `dest_old` in
 *                 [0, n_value) and `dest_bits[n]` (the overflow slot) is
 *                 |0>.  On exit `dest_bits[0..n-1]` holds `(dest_old + a)
 *                 mod n_value` in [0, n_value) and `dest_bits[n]` is |0>.
 * @param n_bits   Modulus register (n qubits, read but restored).
 *                 Encodes the modulus value `n_value` in [1, 2^n).
 * @param n        Register width (NOT the modulus value; the modulus is
 *                 encoded in `n_bits[0..n-1]`).  `n == 0` short-circuits
 *                 to a no-op (matches PRD §5 / §8 #3).
 *
 * @pre `a, dest_old in [0, n_value)` and `n_value >= 1` (when `n == 0`,
 *      the call is a no-op).  `dest_bits[n]` must enter the routine in
 *      |0>.  `a_bits` and `dest_bits` must refer to **physically distinct**
 *      qubit registers (inherited from `lib_add_dsl`'s aliasing rule -- see
 *      adder_dsl.hpp).  `n_bits` must also be distinct from both `a_bits`
 *      and `dest_bits`.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_add_mod_inplace_dsl` with operands outside `[0, n_value)`, with
 * `dest_bits[n]` not in |0>, or with aliased registers is **undefined
 * behavior** -- the routine still emits a well-formed gate sequence, but
 * the resulting `dest_bits` are not the mathematical answer and the
 * input registers may not be restored.  This matches the trust model of
 * the sibling `lib_add_mod_dsl` and `lib_double_mod_dsl` primitives.
 *
 * @sa __lib_add_mod_inplace_dsl_adj, lib_add_mod_dsl, lib_double_mod_dsl
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_add_mod_inplace_dsl(Bit* a_bits, Bit* dest_bits,
                                    Bit* n_bits, std::size_t n) {
    if (n == 0u) return;

    // sturm-8n73: kMaxN bumped 32 → 64 for circuit-generation use cases.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN && "lib_add_mod_inplace_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_add_mod_inplace_dsl: no BackendContext installed");
    (void)raw;  // control_stack is driven by WHEN/WhenGuard through the
                // lift pattern; ctx is no longer poked directly.

    // Convenience aliases for the dest register's low (W) and high (1) parts.
    Bit* dest_low  = dest_bits;
    Bit& dest_high = dest_bits[n];

    // (1) lib_add_dsl(a, dest_low, dest_high, W).  dest = a + b in (W+1)-bit
    //     storage.  dest_high may be 1 if a+b >= 2^W; dest still < 2n <=
    //     2^(W+1).
    lib_add_dsl(a_bits, dest_low, dest_high, n);

    // (2) Allocate n_pad qubit (always |0>); form n_extended view.
    int   n_pad_idx = QubitPool::instance().allocate();
    qbool n_pad_own = qbool::make_non_owning(n_pad_idx);
    Bit   n_pad     =
        detail_add_mod_inplace::make_ancilla_view<Bit>(n_pad_own);
    Bit   n_ext[kMaxN + 1u];
    for (std::size_t i = 0u; i < n; ++i) n_ext[i] = n_bits[i];
    n_ext[n] = n_pad;

    // (3) Allocate lt_flag and carry_anc (both |0>).
    int   lt_flag_idx     = QubitPool::instance().allocate();
    qbool lt_flag_own     = qbool::make_non_owning(lt_flag_idx);
    // lt_flag receives a data-dependent comparison bit at step (4); mark
    // its qbool anchor as superposed so the lift pattern's
    // `(*outer) & lt_flag_own` and `WHEN(lt_flag_own)` take the quantum
    // branch instead of being short-circuited as classical |0>.
    lt_flag_own.super_mask = 1ULL;
    Bit   lt_flag         =
        detail_add_mod_inplace::make_ancilla_view<Bit>(lt_flag_own);
    int   carry_anc_idx   = QubitPool::instance().allocate();
    qbool carry_anc_own   = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc       =
        detail_add_mod_inplace::make_ancilla_view<Bit>(carry_anc_own);

    // (4) Gate-reverse of unconditional add: subtract n_ext from dest.
    //     Side-effect: lt_flag ^= ((a+b) < n).  After: dest[W] = lt_flag.
    detail_div::lib_add_adj(n_ext, dest_bits, lt_flag, n + 1u);

    // (5) Conditional add-back of n_ext controlled on lt_flag.  In both
    //     branches: dest_low (low W bits) = (a+b) mod n; dest[W] = 0;
    //     carry_anc = lt_flag.
    sturm::lift_under(lt_flag_own, [&]() {
        lib_add_dsl(n_ext, dest_bits, carry_anc, n + 1u);
    });

    // (6) carry_anc ^= lt_flag  ->  carry_anc = 0 in both branches.
    carry_anc ^= lt_flag;

    // (7) Subtract a from dest_low in W bits; dest_high gets XOR'd with
    //     the borrow bit.  Effect: dest[W] = NOT(lt_flag).
    detail_div::lib_add_adj(a_bits, dest_low, dest_high, n);

    // (8) Uncompute lt_flag using dest[W] as the witness.  Both branches
    //     converge: lt_flag = 1 (after XOR) then flipped -> 0.
    lt_flag ^= dest_high;
    lt_flag.flip();

    // (9) Add a back to dest_low; dest[W] XOR'd with the carry.  Both
    //     branches: dest_low = (a+b) mod n; dest[W] returns to 0.
    lib_add_dsl(a_bits, dest_low, dest_high, n);

    // (10) Release ancillas LIFO (carry_anc, lt_flag, n_pad).
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(lt_flag_idx);
    QubitPool::instance().release(n_pad_idx);
}

} // namespace sturm

#include "sturm/detail/lib/add_mod_inplace_dsl_adj.hpp"
