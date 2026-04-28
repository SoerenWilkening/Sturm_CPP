// mul_mod_dsl_oneshot_adj.hpp -- sturm-7cix Beat C: oneshot adjoint sibling.
//
// `__lib_mul_mod_dsl_oneshot_adj` is the gate-reverse of
// `lib_mul_mod_dsl_oneshot`: starting from `r = (a * b) mod n` (with the
// original `a, b, n` preserved), it returns `r` to |0> while leaving the
// input registers unchanged.  Implementation runs the forward gate
// sequence in reverse, swapping each `lib_add_mod_inplace_dsl` for
// `__lib_add_mod_inplace_dsl_adj` (and vice-versa) and each
// `lib_double_mod_dsl` for `__lib_double_mod_dsl_adj`; self-inverse XOR
// steps stay as-is.
//
// Re-allocation order for `shifted_reg` and `acc_reg` matches the forward
// exactly so that, under TDD harnesses calling `reset_for_testing()`
// between cases, the QubitPool's LIFO reuses the same indices.  Even when
// the adjoint runs back-to-back with the forward (no reset), the
// operations only touch ancillas the adjoint owns, so the order matters
// only for LoC parity (mirrors `add_mod_dsl_adj.hpp`'s pattern).
//
// Algorithm — gate-reverse of mul_mod_dsl_oneshot.hpp's steps 1-8:
//   1'. Re-allocate shifted_reg[0..W] and acc_reg[0..W] in forward order.
//   2'. shifted_reg[0..W-1] ^= a_bits[0..W-1]  (XOR self-inverse: now
//       shifted_reg[0..W-1] = a, shifted_reg[W] = 0).
//   3'. Forward step (6) was the uncompute loop; its reverse rebuilds
//       shifted_reg/acc_reg from |0>+a back to the post-loop state.
//       Iterate i = 0..W-1 (the forward uncompute went W-1..0):
//         3a. Run lift_under(b[i]) { lib_add_mod_inplace_dsl(...); }
//             — gate-reverse of forward step 6b.
//         3b. If i < W-1: lib_double_mod_dsl(shifted_reg, n)
//             — gate-reverse of forward step 6a (which was
//             __lib_double_mod_dsl_adj).
//       After the loop: shifted_reg = (a · 2^(W-1)) mod n with bit W = 0,
//       and acc_reg[0..W-1] = (a*b) mod n.
//   4'. r_bits[j] ^= acc_reg[j]   (XOR self-inverse: zeros r_bits).
//   5'. Forward loop step (4) reverse: zero acc_reg back to |0> and
//       shifted_reg back to a.  Iterate i = W-1..0:
//         5a. If i < W-1: __lib_double_mod_dsl_adj(shifted_reg, n).
//         5b. lift_under(b[i]) { __lib_add_mod_inplace_dsl_adj(...); }
//   6'. shifted_reg[0..W-1] ^= a_bits   (XOR self-inverse: zeros shifted_reg).
//   7'. Release acc_reg, then shifted_reg LIFO.
//
// STURM_REGISTER_ADJOINT at the bottom hooks
// `invert<&lib_mul_mod_dsl_oneshot<BitProxy>>()` to this adjoint, mirroring
// add_mod_dsl_adj.hpp's pattern.
//
// Auto-included from mul_mod_dsl_oneshot.hpp.

#pragma once

#include "sturm/detail/lib/add_mod_inplace_dsl.hpp"
#include "sturm/detail/lib/double_mod_dsl.hpp"
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
 * @brief Gate-reverse adjoint of @ref lib_mul_mod_dsl_oneshot.
 *
 * Starting from `r_bits = (a_bits * b_bits) mod n_bits` (with the
 * original `a_bits`, `b_bits`, `n_bits` preserved by the forward call),
 * this routine returns `r_bits` to |0> while leaving the input
 * registers unchanged.  Implementation runs the forward gate sequence
 * in reverse, swapping each `lib_add_mod_inplace_dsl` for its adjoint
 * (and `lib_double_mod_dsl` for its adjoint).
 *
 * Built strictly on top of `lib_add_mod_inplace_dsl` and
 * `lib_double_mod_dsl` (and their adjoints) per the PRD §4 layering
 * rule — emits no gates of its own.
 *
 * @param a_bits Left factor register (W qubits, read but restored).
 * @param b_bits Right factor register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width.  `n == 0` short-circuits to a no-op.
 * @param r_bits Result register (W qubits).  Must enter holding
 *               `(a * b) mod n`; exits in |0>.
 *
 * @pre `a, b ∈ [0, n_value)`, `n_value ≥ 1`, **AND `n_value` MUST be odd**
 *      (matches the forward's precondition; inherited from
 *      `lib_double_mod_dsl`).  `r_bits` must enter holding the value
 *      produced by a paired `lib_mul_mod_dsl_oneshot` call on the same
 *      `(a_bits, b_bits, n_bits)` operands.
 *
 * @sa lib_mul_mod_dsl_oneshot, __lib_add_mod_inplace_dsl_adj,
 *     __lib_double_mod_dsl_adj
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_mul_mod_dsl_oneshot_adj(Bit* a_bits, Bit* b_bits,
                                          Bit* n_bits, std::size_t n,
                                          Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 32u;
    assert(n <= kMaxN && "__lib_mul_mod_dsl_oneshot_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_mul_mod_dsl_oneshot_adj: no BackendContext installed");
    (void)raw;

    // (1') Re-allocate shifted_reg[0..W] in forward order.
    int   shifted_idx[kMaxN + 1u];
    qbool shifted_own[kMaxN + 1u];
    Bit   shifted_bits[kMaxN + 1u];
    for (std::size_t j = 0u; j <= n; ++j) {
        shifted_idx[j]  = QubitPool::instance().allocate();
        shifted_own[j]  = qbool::make_non_owning(shifted_idx[j]);
        shifted_bits[j] =
            detail_mul_mod_oneshot::make_ancilla_view<Bit>(shifted_own[j]);
    }

    // (1') Re-allocate acc_reg[0..W] in forward order.
    int   acc_idx[kMaxN + 1u];
    qbool acc_own[kMaxN + 1u];
    Bit   acc_bits[kMaxN + 1u];
    for (std::size_t j = 0u; j <= n; ++j) {
        acc_idx[j]  = QubitPool::instance().allocate();
        acc_own[j]  = qbool::make_non_owning(acc_idx[j]);
        acc_bits[j] =
            detail_mul_mod_oneshot::make_ancilla_view<Bit>(acc_own[j]);
    }

    // (2') shifted_reg[0..W-1] ^= a (XOR self-inverse): shifted_reg = a.
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[j] ^= a_bits[j];

    // (3') Re-build the forward post-loop state: rebuild shifted_reg
    //      doubling chain and acc_reg sums.  The forward step (6)
    //      iterated i = W-1..0 and ran (undo_double, undo_add).  Reverse
    //      iterates i = 0..W-1 and runs (do_add, do_double).
    for (std::size_t i = 0u; i < n; ++i) {
        // (3a) Reverse forward step 6b: do the controlled add.
        sturm::lift_under(b_bits[i], [&]() {
            lib_add_mod_inplace_dsl(shifted_bits, acc_bits, n_bits, n);
        });

        // (3b) Reverse forward step 6a: do the doubling (only at i < W-1).
        if (i + 1u < n) {
            lib_double_mod_dsl(shifted_bits, n_bits, n);
        }
    }

    // (4') r_bits[j] ^= acc_reg[j]  (XOR self-inverse: zeros r_bits).
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[j];

    // (5') Forward step (4) reverse: zero acc_reg, restore shifted_reg
    //      back to a.  For i = W-1..0:
    //        if i < W-1: undo doubling
    //        undo controlled add
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i = W-1, W-2, ..., 0.

        if (i + 1u < n) {
            __lib_double_mod_dsl_adj(shifted_bits, n_bits, n);
        }

        sturm::lift_under(b_bits[i], [&]() {
            __lib_add_mod_inplace_dsl_adj(shifted_bits, acc_bits, n_bits, n);
        });
    }

    // (6') shifted_reg[0..W-1] ^= a (XOR self-inverse: zeros shifted_reg).
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[j] ^= a_bits[j];

    // (7') Release acc_reg, then shifted_reg LIFO.
    for (std::size_t j = n + 1u; j-- > 0u;)
        QubitPool::instance().release(acc_idx[j]);
    for (std::size_t j = n + 1u; j-- > 0u;)
        QubitPool::instance().release(shifted_idx[j]);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>,
                       sturm::__lib_mul_mod_dsl_oneshot_adj<sturm::BitProxy>)
#endif
