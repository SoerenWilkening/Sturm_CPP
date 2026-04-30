// double_mod_dsl_adj.hpp -- Beat B (sturm-wdas) lib_double_mod_dsl adjoint sibling.
//
// `__lib_double_mod_dsl_adj` is the gate-reverse of `lib_double_mod_dsl`:
// starting from `x_bits = (2 · x_orig mod n_value)` and `lt_flag_out` =
// the value the paired forward wrote (= `(2x_orig < n_value)`), it returns
// `x_bits` to `x_orig` and consumes `lt_flag_out` back to |0>, leaving
// `n_bits` unchanged.  Implementation runs the forward gate sequence in
// reverse, swapping each `lib_add_dsl` for `lib_add_adj` (and vice-versa);
// the SWAP chain is reversed in iteration order; self-inverse XOR /
// control-stack steps stay as-is.
//
// Adjoint precondition (TRICKY — see double_mod_dsl.hpp preamble):
//   This routine is NOT a literal "halve mod n" operation.  The
//   counterexample x_orig = (n+1)/2 → 2 · x_orig mod n = 1 (LSB = 1)
//   means `(2 · x mod n)` is NOT unconditionally even, so a literal
//   "shift right by 1" cannot be a sound forward halver.  The adjoint
//   here is the gate-reverse of the forward primitive, so it correctly
//   restores `x_orig` ONLY when `(x_bits, lt_flag_out)` enters holding
//   the value pair produced by a paired `lib_double_mod_dsl` call on
//   the same `n_bits` operands.  Calling the adjoint with any other
//   `(x_bits, lt_flag_out)` produces undefined output (the routine still
//   emits well-formed gates, but `x_bits` will be scrambled, `lt_flag_out`
//   will not return to |0>, and `n_bits` may be corrupted).  This matches
//   the trust model documented in the forward header and the PRD §5
//   contract; it is the resolution to issue sturm-wdas's design note #3.
//
// Re-allocation order for the auxiliary ancillas (`n_pad`, `carry_anc`)
// matches the forward exactly so that, under TDD harnesses that call
// `reset_for_testing()` between cases, the QubitPool's LIFO reuses the
// same indices.  Even when the adjoint runs back-to-back with the
// forward (no reset), the operations only touch ancillas the adjoint
// owns, so the order matters only for LoC parity with the forward
// (mirrors `add_mod_dsl_adj.hpp`'s "re-allocate in forward order"
// pattern).  Note: the LSB-trick lt_flag is no longer internal as of
// sturm-4oot.1 — only n_pad and carry_anc are re-allocated here.
//
// Algorithm (reverse of double_mod_dsl.hpp's steps 1–9):
//   1'. allocate n_pad, carry_anc in forward order.  Build n_ext.
//   2'. Build s_view (gate-free relabel; same as forward step 3).
//   3'. [REMOVED in sturm-4oot.1] Forward step 8 was the LSB-trick
//        uncompute, which is gone.  No corresponding reverse step.
//   4'. Reverse forward step 7 (SWAP chain) — apply SWAPs in REVERSE
//       iteration order: for i = n-1 down to 0:
//          x_bits[i] ^= x_bits[n];
//          x_bits[n] ^= x_bits[i];
//          x_bits[i] ^= x_bits[n];
//       (Each SWAP is self-inverse, so the inner XOR triplet stays the
//       same; only the outer iteration order reverses.)
//   5'. Reverse forward step 6 (carry_anc ^= lt_flag_out, self-inverse):
//          carry_anc ^= lt_flag_out;
//   6'. Reverse forward step 5 (controlled lib_add_dsl):
//          lift_under(lt_flag_out) { lib_add_adj(n_ext, s_view, carry_anc, n+1); }
//   7'. Reverse forward step 4 (lib_add_adj on s_view):
//          lib_add_dsl(n_ext, s_view, lt_flag_out, n+1);
//       This restores s_view to the original 2x and clears lt_flag_out
//       back to |0> (XOR-undo of the forward's `lt_flag_out ^=
//       (s_view_old < n_ext)`).
//   8'. Release ancillas LIFO.
//
// STURM_REGISTER_ADJOINT at the bottom hooks
// `invert<&lib_double_mod_dsl<BitProxy>>()` to this adjoint, mirroring the
// add_mod_dsl_adj.hpp / mul_mod_dsl_adj.hpp pattern.
//
// Auto-included from double_mod_dsl.hpp.

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
 * @brief Gate-reverse adjoint of @ref lib_double_mod_dsl.
 *
 * Starting from `x_bits = (2 · x_orig) mod n_value` and `lt_flag_out` =
 * the value the paired forward wrote (= `(2x_orig < n_value)`), this
 * routine returns `x_bits` to `x_orig`, consumes `lt_flag_out` back to
 * |0>, and leaves `n_bits` unchanged.  Implementation runs the forward
 * gate sequence in reverse, swapping each `lib_add_dsl` for
 * `lib_add_adj` (and vice-versa); self-inverse XOR / SWAP-XOR steps
 * stay as-is, with the SWAP chain reversed in iteration order.
 *
 * Built strictly on top of `lib_add_dsl` + `detail_div::lib_add_adj`
 * per the PRD §4 layering rule — emits no gates of its own.
 *
 * @param x_bits      Input/output register of length `n + 1` qubits.
 *                    On entry holds `(2 · x_orig) mod n_value` in bits
 *                    0..n-1 and 0 in bit n.  On exit holds `x_orig` in
 *                    bits 0..n-1 and 0 in bit n.
 * @param n_bits      Modulus register (n qubits, read but restored).
 * @param n           Register width.  `n == 0` short-circuits to a
 *                    no-op.
 * @param lt_flag_out Caller-owned 1-qubit register; on entry must hold
 *                    the value the paired forward wrote (=
 *                    `(2x_orig < n_value)`).  The adjoint XORs this bit
 *                    back out (via the inner `lib_add_dsl` of step 7'),
 *                    leaving `lt_flag_out = 0` on exit.
 *
 * @pre `x_bits[0..n-1]` and `lt_flag_out` must enter holding the value
 *      pair produced by a paired `lib_double_mod_dsl` call on the same
 *      `n_bits` operands.  `x_bits[n]` must be |0>.  No parity
 *      restriction on `n_value`: this routine handles even and odd
 *      moduli uniformly via the caller-owned `lt_flag_out` (see
 *      `docs/design_even_n_double_mod.md`).  `x_bits`, `n_bits`, and
 *      `lt_flag_out` must refer to physically distinct qubit registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_double_mod_dsl_adj` with `(x_bits, lt_flag_out)` not equal to
 * the paired forward output, or with aliased registers, is **undefined
 * behavior** — the routine still emits a well-formed gate sequence,
 * but `x_bits` will not be restored to the original value,
 * `lt_flag_out` may not return to |0>, and `n_bits` may be corrupted.
 * This matches the trust model shared with the forward (PRD §5).
 *
 * @sa lib_double_mod_dsl, __lib_add_mod_dsl_adj, __lib_mul_mod_dsl_adj
 * @see PRD §5 (Trust model and precondition contract).
 * @see docs/design_even_n_double_mod.md (sturm-fya1 / sturm-4oot.1).
 */
template <typename Bit>
inline void __lib_double_mod_dsl_adj(Bit* x_bits, Bit* n_bits, std::size_t n,
                                     Bit& lt_flag_out) {
    if (n == 0u) return;

    // sturm-8n73: kMaxN bumped 32 → 64 to match the forward primitive.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN && "__lib_double_mod_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_double_mod_dsl_adj: no BackendContext installed");
    (void)raw;  // control_stack is driven by WHEN/WhenGuard through the
                // lift pattern; ctx is no longer poked directly.

    // (1') Re-allocate n_pad in forward order; build n_ext.
    int   n_pad_idx = QubitPool::instance().allocate();
    qbool n_pad_own = qbool::make_non_owning(n_pad_idx);
    Bit   n_pad     = detail_double_mod::make_ancilla_view<Bit>(n_pad_own);
    Bit   n_ext[kMaxN + 1u];
    for (std::size_t i = 0u; i < n; ++i) n_ext[i] = n_bits[i];
    n_ext[n] = n_pad;

    // (1') Re-allocate carry_anc in forward order.  lt_flag is now
    //      caller-owned (sturm-4oot.1) and is NOT re-allocated here.
    int   carry_anc_idx   = QubitPool::instance().allocate();
    qbool carry_anc_own   = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc       = detail_double_mod::make_ancilla_view<Bit>(carry_anc_own);

    // (2') Build s_view (same gate-free relabel as forward step 3).
    Bit s_view[kMaxN + 1u];
    s_view[0] = x_bits[n];
    for (std::size_t i = 1u; i <= n; ++i) s_view[i] = x_bits[i - 1u];

    // (3') [REMOVED in sturm-4oot.1] Forward step 8 (the LSB-trick
    //      uncompute) is gone, so its reverse step is gone too.

    // (4') Reverse forward step 7 (SWAP chain): apply the SWAP triplets
    //      in REVERSE iteration order (i = n-1 down to 0).  Each SWAP
    //      itself is self-inverse, so the inner three CNOTs stay the
    //      same; only the outer index sequence reverses.
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;
        x_bits[i] ^= x_bits[n];
        x_bits[n] ^= x_bits[i];
        x_bits[i] ^= x_bits[n];
    }

    // (5') Reverse forward step 6 (carry_anc ^= lt_flag_out,
    //      self-inverse).
    carry_anc ^= lt_flag_out;

    // (6') Reverse forward step 5 (controlled lib_add_dsl):
    //      controlled lib_add_adj on the same operands.  The shared
    //      `lift_under(Bit&, body)` overload promotes lt_flag_out and
    //      forces super_mask=1.
    sturm::lift_under(lt_flag_out, [&]() {
        detail_div::lib_add_adj(n_ext, s_view, carry_anc, n + 1u);
    });

    // (7') Reverse forward step 4 (lib_add_adj on s_view):
    //      lib_add_dsl on the same operands restores s_view to the
    //      original 2x and clears lt_flag_out back to |0> (XOR-undo of
    //      the forward's `lt_flag_out ^= (s_view_old < n_ext)`).
    lib_add_dsl(n_ext, s_view, lt_flag_out, n + 1u);

    // (8') Release ancillas LIFO (carry_anc, n_pad).  lt_flag_out is
    //      caller-owned and is NOT released here.
    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(n_pad_idx);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_double_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_double_mod_dsl_adj<sturm::BitProxy>)
#endif
