// square_mod_dsl_adj.hpp -- Beat D-B (sturm-3sfl.2) lib_square_mod_dsl
//                             adjoint sibling.
//
// `__lib_square_mod_dsl_adj` is the gate-reverse of `lib_square_mod_dsl`:
// starting from `x_bits = (x_orig^2) mod n_value` and `x_copy_out_bits` =
// the value the paired forward wrote (= `x_orig` XOR-into the caller's
// pre-state), it returns `x_bits` to `x_orig` and consumes
// `x_copy_out_bits` back to its pre-state (or |0> if the caller
// pre-zeroed), leaving `n_bits` unchanged.  Implementation runs the
// forward gate sequence in reverse, swapping each
// `lib_mul_mod_dsl_oneshot` for `__lib_mul_mod_dsl_oneshot_adj`;
// self-inverse XOR / SWAP-XOR steps stay as-is.
//
// Adjoint precondition (TRICKY — see square_mod_dsl.hpp preamble):
//   This routine is NOT a literal "modular square root" operation.
//   Squaring x → x^2 mod n is **2-to-1** for n > 1 (x and n−x both
//   map to the same x^2), so no algebraic inverse exists in general.
//   The adjoint here is the gate-reverse of the forward primitive, so
//   it correctly restores `x_orig` ONLY when `(x_bits, x_copy_out_bits)`
//   enters holding the value pair produced by a paired
//   `lib_square_mod_dsl` call on the same `n_bits` operands.  Calling
//   the adjoint with any other `(x_bits, x_copy_out_bits)` produces
//   undefined output (the routine still emits well-formed gates, but
//   `x_bits` will be scrambled, `x_copy_out_bits` will not return to
//   its pre-state, and `n_bits` may be corrupted).  This matches the
//   trust model documented in the forward header and the PRD §5
//   contract; it mirrors the sturm-wdas Beat B doubling adjoint
//   precondition pattern.
//
// Re-allocation order for the auxiliary `r_reg` scratch matches the
// forward exactly so that, under TDD harnesses calling
// `reset_for_testing()` between cases, the QubitPool's LIFO reuses the
// same indices.  Even when the adjoint runs back-to-back with the
// forward (no reset), the operations only touch ancillas the adjoint
// owns, so the order matters only for LoC parity (mirrors the
// `mul_mod_dsl_oneshot_adj.hpp` "re-allocate in forward order" pattern).
//
// Algorithm — gate-reverse of square_mod_dsl.hpp's steps 1–6:
//   1'. Re-allocate r_reg[W] (all |0>) in forward order.
//   2'. Reverse forward step (5): r_bits[i] ^= x_copy_out_bits[i].
//       Self-inverse.  After this step r_bits = x_orig (since
//       x_copy_out_bits = x_orig and the XOR re-loads it).
//   3'. Reverse forward step (4) (SWAP chain): apply the SWAP triplets
//       in REVERSE iteration order (i = W-1 down to 0).  Each SWAP is
//       self-inverse, so the inner three CNOTs stay the same; only
//       the outer index sequence reverses.  After this step
//       x_bits = x_orig and r_bits = x_orig^2 mod n (same as the
//       state after the forward's step 3 / before step 4).
//   4'. Reverse forward step (3) (lib_mul_mod_dsl_oneshot):
//       __lib_mul_mod_dsl_oneshot_adj(x_bits, x_bits, n_bits, n,
//                                     r_bits).  Adjoint precondition:
//       r_bits = x_bits * x_bits mod n = x_orig^2 mod n (matches the
//       state after step 3').  Post: r_bits cleared to |0>;
//       x_bits, x_copy_out_bits, n_bits unchanged.
//   5'. Release r_reg LIFO (every slot is |0> after step 4').
//   6'. Reverse forward step (1): x_copy_out_bits[i] ^= x_bits[i].
//       Self-inverse XOR-uncopy.  Since x_bits = x_orig and
//       x_copy_out_bits = (pre XOR x_orig) post-forward = x_orig when
//       the caller pre-zeroed, this leaves x_copy_out_bits at the
//       caller's pre-forward state (= 0 in the clean-write case).
//
// STURM_REGISTER_ADJOINT at the bottom hooks
// `invert<&lib_square_mod_dsl<BitProxy>>()` to this adjoint, mirroring
// the double_mod_dsl_adj.hpp / mul_mod_dsl_oneshot_adj.hpp pattern.
//
// Auto-included from square_mod_dsl.hpp.

#pragma once

#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
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
 * @brief Gate-reverse adjoint of @ref lib_square_mod_dsl.
 *
 * Starting from `x_bits = (x_orig^2) mod n_value` and `x_copy_out_bits`
 * = the value the paired forward wrote (= `x_orig` XOR-into the
 * caller's pre-state), this routine returns `x_bits` to `x_orig`,
 * consumes `x_copy_out_bits` back to its pre-forward state (= |0> in
 * the clean-write case), and leaves `n_bits` unchanged.  Implementation
 * runs the forward gate sequence in reverse, swapping each
 * `lib_mul_mod_dsl_oneshot` for its adjoint; self-inverse XOR / SWAP
 * steps stay as-is, with the SWAP chain reversed in iteration order.
 *
 * Built strictly on top of `lib_mul_mod_dsl_oneshot` (and its adjoint)
 * per the PRD §4 layering rule — emits no gates of its own.  The inner
 * oneshot adjoint manages its own (W-1)-bit `lt_flags` register
 * entirely internally (matching the forward's choice — see
 * square_mod_dsl.hpp's preamble for the rationale).
 *
 * @param x_bits           Input/output register of W qubits.  On entry
 *                         holds `(x_orig^2) mod n_value`; on exit holds
 *                         `x_orig`.
 * @param n_bits           Modulus register (W qubits, read but
 *                         restored).
 * @param n                Register width.  `n == 0` short-circuits to
 *                         a no-op.
 * @param x_copy_out_bits  Caller-owned W-qubit register; on entry must
 *                         hold the value the paired forward wrote (=
 *                         `x_orig` XOR-into the caller's pre-state).
 *                         The adjoint XORs `x_bits` (= `x_orig` after
 *                         step 4') back out via step 6', leaving
 *                         `x_copy_out_bits` at the caller's pre-forward
 *                         state on exit.
 *
 * @pre `(x_bits, x_copy_out_bits)` must enter holding the value pair
 *      produced by a paired `lib_square_mod_dsl` call on the same
 *      `n_bits` operands.  No parity restriction on `n_value` (the
 *      inner oneshot is parity-agnostic post sturm-4oot.4).  `x_bits`,
 *      `n_bits`, and `x_copy_out_bits` must refer to physically
 *      distinct qubit registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_square_mod_dsl_adj` with `(x_bits, x_copy_out_bits)` not
 * equal to the paired forward output, or with aliased registers, is
 * **undefined behavior** — the routine still emits a well-formed
 * gate sequence, but `x_bits` will not be restored,
 * `x_copy_out_bits` may not return to its pre-state, and `n_bits` may
 * be corrupted.  This matches the trust model shared with the forward
 * (PRD §5).
 *
 * @sa lib_square_mod_dsl, __lib_mul_mod_dsl_oneshot_adj,
 *     __lib_double_mod_dsl_adj
 * @see PRD §5 (Trust model and precondition contract).
 * @see docs/design_even_n_double_mod.md §6 (Beat D-B implications).
 */
template <typename Bit>
inline void __lib_square_mod_dsl_adj(Bit* x_bits, Bit* n_bits, std::size_t n,
                                     Bit* x_copy_out_bits) {
    if (n == 0u) return;

    // sturm-8n73: kMaxN bumped to 64 to match the forward primitive.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN && "__lib_square_mod_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_square_mod_dsl_adj: no BackendContext installed");
    (void)raw;

    // (1') Re-allocate r_reg[W] in forward order.  Pool contract
    //      guarantees |0> entry, matching the forward's step (2).
    int   r_idx[kMaxN];
    qbool r_own[kMaxN];
    Bit   r_bits[kMaxN];
    for (std::size_t j = 0u; j < n; ++j) {
        r_idx[j]  = QubitPool::instance().allocate();
        r_own[j]  = qbool::make_non_owning(r_idx[j]);
        r_bits[j] =
            detail_square_mod::make_ancilla_view<Bit>(r_own[j]);
    }

    // (2') Reverse forward step (5): r_bits[i] ^= x_copy_out_bits[i].
    //      Self-inverse.  After: r_bits[i] = x_copy_out_bits[i] = x_orig
    //      (assuming the caller pre-zeroed x_copy_out_bits; otherwise
    //      r_bits[i] = x_orig regardless because r_bits started |0>
    //      and x_copy_out_bits encodes x_orig in its delta from the
    //      pre-state).
    for (std::size_t i = 0u; i < n; ++i)
        r_bits[i] ^= x_copy_out_bits[i];

    // (3') Reverse forward step (4) (SWAP chain): apply the SWAP
    //      triplets in REVERSE iteration order (i = W-1 down to 0).
    //      Each SWAP is self-inverse, so the inner XOR-triplet stays
    //      the same; only the outer index sequence reverses.  After:
    //      x_bits = x_orig, r_bits = x_orig^2 mod n (same as the
    //      state after forward's step 3 / before step 4).
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;
        x_bits[i] ^= r_bits[i];
        r_bits[i] ^= x_bits[i];
        x_bits[i] ^= r_bits[i];
    }

    // (4') Reverse forward step (3): __lib_mul_mod_dsl_oneshot_adj.
    //      Adjoint precondition: r_bits = x_bits * x_bits mod n =
    //      x_orig^2 mod n (matches state after step 3').  Post:
    //      r_bits cleared to |0>; x_bits and n_bits preserved.
    __lib_mul_mod_dsl_oneshot_adj(x_bits, x_bits, n_bits, n, r_bits);

    // (5') Release r_reg LIFO (every slot is |0> after step 4').
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(r_idx[j]);

    // (6') Reverse forward step (1): x_copy_out_bits[i] ^= x_bits[i].
    //      Self-inverse XOR-uncopy.  Since x_bits = x_orig (restored
    //      by step 4') and x_copy_out_bits = (pre XOR x_orig)
    //      post-forward, this leaves x_copy_out_bits at the caller's
    //      pre-forward state (= 0 in the clean-write case).
    for (std::size_t i = 0u; i < n; ++i)
        x_copy_out_bits[i] ^= x_bits[i];
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_square_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_square_mod_dsl_adj<sturm::BitProxy>)
#endif
