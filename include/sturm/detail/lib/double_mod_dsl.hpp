// double_mod_dsl.hpp -- Beat B (sturm-wdas) lib_double_mod_dsl forward primitive.
//
// In-place modular doubling: x := (2x) mod n, all unsigned, where x is a
// (W+1)-bit register whose bit W is the overflow slot (must enter |0>) and
// bits 0..W-1 hold the value x in [0, n).  Built strictly on top of the
// existing lib_add_dsl + detail_div::lib_add_adj pair (no direct gate
// emission, no new arithmetic kernel — see PRD §4 layering rule).
//
// Why a separate primitive?  Plan §4.2 / Beat C (sturm-7cix) wants in-place
// `shifted_reg := (2 · shifted_reg) mod n` in the rewrite of
// lib_mul_mod_dsl.  Calling `lib_add_mod_dsl(x, x, n, W, ...)` is blocked by
// the inner `lib_add_dsl(b, s_low, ...)`: the `s_low` register would alias
// the addend `b` because both reads of x point at the same physical qubits.
// `lib_double_mod_dsl` sidesteps the alias by reformulating doubling as a
// "structural shift + conditional subtract":
//   1. Logical left-shift of x by one position — implemented as a qubit
//      relabeling (no gates emitted): the (W+1)-bit shifted view uses
//      x_bits[W] (= |0>) as the new LSB and the original x_bits[i-1] as
//      bit i for i in 1..W.  The original x_bits[W-1] becomes the overflow
//      slot at bit W of the (W+1)-bit shifted view.  Value of shifted view
//      = 2x as a (W+1)-bit unsigned integer.
//   2. Conditional subtract n if the result is ≥ n.  Reuse the trial-
//      subtract / conditional-add-back idiom from lib_add_mod_dsl steps
//      6–11: `lib_add_adj(n_ext, s_view, lt_flag, W+1)` (gate-reverse of
//      unconditional add — flips lt_flag if the subtract underflows),
//      then `lift_under(lt_flag) { lib_add_dsl(n_ext, s_view, carry_anc,
//      W+1); }` to add n back when the subtract underflowed.  carry_anc
//      cleanup: `carry_anc ^= lt_flag` returns it to |0>.  After this the
//      (W+1)-bit s_view holds (2x mod n) with bit W = 0 (since
//      (2x mod n) ∈ [0, n) ⊂ [0, 2^W)).
//   3. Rotate the physical x_bits to standard layout via a chain of W
//      SWAPs (each SWAP = three CNOTs `a^=b; b^=a; a^=b`).  After the
//      rotation: x_bits[i] holds bit i of (2x mod n) for i in 0..W-1, and
//      x_bits[W] = 0 (the original x_bits[W-1], which became bit W of the
//      shifted view, is now zeroed because (2x mod n) < 2^W).
//   4. Uncompute lt_flag using x_bits[0] (the LSB of the result).  This
//      step requires n to be ODD: when n is odd, (2x mod n) is even iff
//      lt_flag = 1 (no reduction was applied) and odd iff lt_flag = 0
//      (reduction was applied; 2x - n inherits LSB = LSB(n) = 1 since n
//      is odd).  So `lt_flag ^= x_bits[0]; lt_flag.flip();` clears
//      lt_flag back to |0>.  PRECONDITION: n must be odd.
//
// Crucially, this primitive does NOT call lib_add_dsl with aliased operands
// at any point — the only adder use is the unconditional subtract of n
// during reduction, which has distinct (n_extended, s_view) operands (the
// (W+1)-bit n_ext lives in n_bits + a fresh n_pad ancilla; s_view is the
// relabel of x_bits).  This is exactly the property the doubling primitive
// is designed to provide for Beat C's lib_mul_mod_dsl rewrite.
//
// Adjoint precondition:
//   `__lib_double_mod_dsl_adj` is the gate-reverse of the forward; it is
//   NOT a literal "halve mod n" operation.  The (n+1)/2 counterexample
//   (x = (n+1)/2 → 2x mod n = 1, LSB = 1) shows that the unconditional
//   "halve via shift-right" claim is FALSE in general.  The adjoint is
//   correct ONLY when paired with its forward call (used inside Beat C's
//   uncompute pass): given x_bits = (2x mod n) — exactly the output of a
//   paired forward call — the adjoint runs the gate-reverse and restores
//   x_bits = x_orig.  See `__lib_double_mod_dsl_adj` in the sibling
//   double_mod_dsl_adj.hpp header for the documented precondition and
//   the per-step gate-reverse trace.
//
// Why no `lib_ge_dsl` ancilla?  Same rationale as lib_add_mod_dsl: routing
// the comparison through a dedicated `ge_flag` would push the live qubit
// count past the simulator cap.  The lib_add_adj route used here folds
// the comparison and the conditional subtract into a single primitive
// call by reading the post-call carry bit as the `s_old < n` flag, paired
// with a controlled add-back.  See add_mod_dsl.hpp's preamble for the
// full rationale.
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

namespace detail_double_mod {

// Bit-view helper mirroring detail_add_mod::make_ancilla_view, kept local
// so double_mod_dsl can be included without dragging the add-mod helper
// namespace into headers that only need the doubling form.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_double_mod

/**
 * @brief In-place W-bit modular doubling primitive: `x_bits := (2 · x) mod n`.
 *
 * This is the lib-level primitive used by Beat C (sturm-7cix) to rewrite
 * `lib_mul_mod_dsl` onto a single accumulator + single shifted register
 * without the chain-style O(W^2) qubit cost.  Built strictly on top of
 * `lib_add_dsl` + `detail_div::lib_add_adj` per the PRD §4 layering rule
 * — emits no gates of its own (only XOR / SWAP CNOTs and the
 * lib_add_dsl/lib_add_adj primitives).  See the header preamble for the
 * full algorithm.
 *
 * @param x_bits Input/output register of length `n + 1` qubits.  On
 *               entry `x_bits[0..n-1]` holds the value `x` in [0, n_value)
 *               and `x_bits[n]` (the overflow slot) is |0>.  On exit
 *               `x_bits[0..n-1]` holds `(2 · x) mod n_value` in [0,
 *               n_value) and `x_bits[n]` is |0>.
 * @param n_bits Modulus register (n qubits, read but restored).  Encodes
 *               the modulus value `n_value` in [1, 2^n).
 * @param n      Register width (NOT the modulus value; the modulus is
 *               encoded in `n_bits[0..n-1]`).  `n == 0` short-circuits
 *               to a no-op (matches PRD §5 / §8 #3).
 *
 * @pre `x ∈ [0, n_value)` and `n_value ≥ 1` (when `n == 0`, the call is a
 *      no-op).  `x_bits[n]` must enter the routine in |0>.  `x_bits` and
 *      `n_bits` must refer to physically distinct qubit registers.
 * @pre `n_value` must be **odd** — the lt_flag uncomputation step uses
 *      `x_bits[0]` (the result LSB) which is well-defined only when n is
 *      odd; for even n the LSB is always 0 and lt_flag cannot be cleaned
 *      up via a local XOR.  The Shor's-algorithm / ECC use cases have
 *      odd n (the integer to factor with factors of 2 removed; the curve
 *      order, typically prime).  For even n the call leaves lt_flag in
 *      a data-dependent state and the function does not return cleanly.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_double_mod_dsl` with `x` outside `[0, n_value)`, with `x_bits[n]`
 * not in |0>, or with `n_value` even is **undefined behavior** — the
 * routine still emits a well-formed gate sequence, but `x_bits` is not
 * the mathematical answer and the input registers may not be restored.
 * This matches the trust model of the sibling `lib_add_mod_dsl`
 * primitive.
 *
 * @sa __lib_double_mod_dsl_adj, lib_add_mod_dsl, lib_mul_mod_dsl
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_double_mod_dsl(Bit* x_bits, Bit* n_bits, std::size_t n) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 32u;
    assert(n <= kMaxN && "lib_double_mod_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_double_mod_dsl: no BackendContext installed");
    (void)raw;  // control_stack is driven by WHEN/WhenGuard through the
                // lift pattern; ctx is no longer poked directly.

    // (1) Allocate n_pad qubit (always |0>); form n_extended = [n_bits, n_pad].
    int   n_pad_idx = QubitPool::instance().allocate();
    qbool n_pad_own = qbool::make_non_owning(n_pad_idx);
    Bit   n_pad     = detail_double_mod::make_ancilla_view<Bit>(n_pad_own);
    Bit   n_ext[kMaxN + 1u];
    for (std::size_t i = 0u; i < n; ++i) n_ext[i] = n_bits[i];
    n_ext[n] = n_pad;

    // (2) Allocate lt_flag and carry_anc (both |0>).
    int   lt_flag_idx     = QubitPool::instance().allocate();
    qbool lt_flag_own     = qbool::make_non_owning(lt_flag_idx);
    // lt_flag receives a data-dependent comparison bit at step (4); mark
    // its qbool anchor as superposed so the lift pattern's
    // `(*outer) & lt_flag_own` and `WHEN(lt_flag_own)` take the quantum
    // branch instead of being short-circuited as classical |0>.
    lt_flag_own.super_mask = 1ULL;
    Bit   lt_flag         = detail_double_mod::make_ancilla_view<Bit>(lt_flag_own);
    int   carry_anc_idx   = QubitPool::instance().allocate();
    qbool carry_anc_own   = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc       = detail_double_mod::make_ancilla_view<Bit>(carry_anc_own);

    // (3) Build s_view = [x_bits[n], x_bits[0..n-1]] — the (n+1)-bit
    //     left-shift view of x_bits, with x_bits[n] (= |0>) as the new
    //     LSB.  Pure relabeling: no gates emitted.  Value of s_view
    //     = 2 * x as a (n+1)-bit unsigned integer.
    Bit s_view[kMaxN + 1u];
    s_view[0] = x_bits[n];
    for (std::size_t i = 1u; i <= n; ++i) s_view[i] = x_bits[i - 1u];

    // (4) Gate-reverse of unconditional add: subtract n_ext from s_view.
    //     Side-effect: lt_flag ^= (s_view_old < n_ext) = (2x < n).
    detail_div::lib_add_adj(n_ext, s_view, lt_flag, n + 1u);

    // (5) Conditional add-back of n_ext controlled on lt_flag.  When
    //     lt_flag=1, restores s_view to s_view_old (= 2x mod 2^(n+1) = 2x);
    //     when lt_flag=0, no-op and s_view stays at (2x - n).  In both
    //     cases the low n bits of s_view = (2x mod n).
    sturm::lift_under(lt_flag_own, [&]() {
        lib_add_dsl(n_ext, s_view, carry_anc, n + 1u);
    });

    // (6) carry_anc ^= lt_flag  ->  carry_anc returns to 0 in both
    //     branches.  When lt_flag=1, the controlled add-back caused a
    //     (n+1)-bit overflow (s_view wrapped from 2x - n + 2^(n+1) back
    //     to 2x), so carry_anc was set to 1 by the add; XORing with
    //     lt_flag = 1 clears it.  When lt_flag=0, both sides are 0.
    carry_anc ^= lt_flag;

    // (7) Rotate physical x_bits to standard layout via a chain of n
    //     SWAPs (each SWAP = three CNOTs `a^=b; b^=a; a^=b`).  Pre-
    //     rotation: x_bits[n] = bit_0, x_bits[i-1] = bit_i for i in
    //     1..n (so x_bits[0..n-2] = bits 1..n-1 and x_bits[n-1] = bit_n
    //     = 0).  Post-rotation: x_bits[i] = bit_i for i in 0..n-1 and
    //     x_bits[n] = 0 (was bit_n = 0).
    for (std::size_t i = 0u; i < n; ++i) {
        x_bits[i] ^= x_bits[n];
        x_bits[n] ^= x_bits[i];
        x_bits[i] ^= x_bits[n];
    }

    // (8) Uncompute lt_flag using x_bits[0] = bit_0 of (2x mod n).
    //     For odd n: bit_0 of (2x mod n) = 0 iff lt_flag = 1 (no
    //     reduction; 2x is always even), and bit_0 = 1 iff lt_flag = 0
    //     (reduction; 2x - n inherits LSB = LSB(n) = 1 since n odd).
    //     So `lt_flag ^= x_bits[0]; lt_flag.flip();` always clears
    //     lt_flag.  PRECONDITION: n_value is odd (see header doxygen).
    lt_flag ^= x_bits[0];
    lt_flag.flip();

    // (9) Release ancillas LIFO (carry_anc, lt_flag, n_pad).
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(lt_flag_idx);
    QubitPool::instance().release(n_pad_idx);
}

} // namespace sturm

#include "sturm/detail/lib/double_mod_dsl_adj.hpp"
