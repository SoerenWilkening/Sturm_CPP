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
// Re-allocation order for `shifted_reg`, `acc_reg`, and the `lt_flags`
// register matches the forward exactly so that, under TDD harnesses
// calling `reset_for_testing()` between cases, the QubitPool's LIFO
// reuses the same indices.  Even when the adjoint runs back-to-back
// with the forward (no reset), the operations only touch ancillas the
// adjoint owns, so the order matters only for LoC parity (mirrors
// `add_mod_dsl_adj.hpp`'s pattern).
//
// Ancilla peak ≈ 3W + 7 (sturm-4oot.3): same accounting as the forward
// — see mul_mod_dsl_oneshot.hpp's preamble and
// `test_mul_mod_dsl_oneshot_ancilla.cpp` for the pinned bound.
//
// Algorithm — gate-reverse of mul_mod_dsl_oneshot.hpp's steps 1-8:
//   1'. Re-allocate shifted_reg[0..W] and acc_reg[0..W] in forward order.
//   2'. shifted_reg[0..W-1] ^= a_bits[0..W-1]  (XOR self-inverse: now
//       shifted_reg[0..W-1] = a, shifted_reg[W] = 0).
//   2'b. Re-allocate lt_flags[0..W-2] in forward order (one bit per
//        inner doubling, all |0> by pool contract — same precondition
//        as the forward).
//   3'. Forward step (6) was the uncompute loop; its reverse rebuilds
//       shifted_reg/acc_reg from |0>+a back to the post-loop state.
//       Iterate i = 0..W-1 (the forward uncompute went W-1..0):
//         3a. Run lift_under(b[i]) { lib_add_mod_inplace_dsl(...); }
//             — gate-reverse of forward step 6b.
//         3b. If i < W-1: lib_double_mod_dsl(shifted_reg, n, lt_flags[i])
//             — gate-reverse of forward step 6a (which was
//             __lib_double_mod_dsl_adj).  Writes the (2·shifted_orig
//             < n_value) witness back into lt_flags[i] for the matched
//             consumption in step (5'a).
//       After the loop: shifted_reg = (a · 2^(W-1)) mod n with bit W = 0,
//       acc_reg[0..W-1] = (a*b) mod n, and each lt_flags[i] holds the
//       same witness the forward's pre-uncompute pass had stored.
//   4'. r_bits[j] ^= acc_reg[j]   (XOR self-inverse: zeros r_bits).
//   5'. Forward loop step (4) reverse: zero acc_reg back to |0> and
//       shifted_reg back to a, consuming each lt_flags[i] back to |0>.
//       Iterate i = W-1..0:
//         5a. If i < W-1: __lib_double_mod_dsl_adj(shifted_reg, n,
//                                                  lt_flags[i]).
//         5b. lift_under(b[i]) { __lib_add_mod_inplace_dsl_adj(...); }
//   6'. shifted_reg[0..W-1] ^= a_bits   (XOR self-inverse: zeros shifted_reg).
//   6'b. Release lt_flags LIFO (every slot is back to |0>).
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
 * rule — emits no gates of its own.  Re-allocates the same internal
 * (W-1)-bit `lt_flags` register the forward used (also from the qubit
 * pool, also guaranteed |0> at entry by the pool contract); each slot
 * is written by the inner `lib_double_mod_dsl` of step (3'b) and
 * consumed back to |0> by the matching `__lib_double_mod_dsl_adj` of
 * step (5'a).  See preamble for the full step trace.
 *
 * @param a_bits Left factor register (W qubits, read but restored).
 * @param b_bits Right factor register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width.  `n == 0` short-circuits to a no-op.
 * @param r_bits Result register (W qubits).  Must enter holding
 *               `(a * b) mod n`; exits in |0>.
 *
 * @pre `a, b ∈ [0, n_value)`, `n_value ≥ 1`.  As of sturm-4oot.4 the
 *      odd-n restriction is **lifted**: `n_value` may be even or odd
 *      (matches the forward's precondition; sturm-4oot.1 made the inner
 *      doubling primitive parity-agnostic, sturm-4oot.3 threaded the
 *      (W-1)-bit `lt_flags` register through forward and adjoint, and
 *      sturm-4oot.4 dropped this layer's odd-n precondition).
 *      `r_bits` must enter holding the value produced by a paired
 *      `lib_mul_mod_dsl_oneshot` call on the same
 *      `(a_bits, b_bits, n_bits)` operands.  The internal (W-1)-bit
 *      `lt_flags` register is re-allocated from the qubit pool and is
 *      guaranteed to enter the routine in |0> by the pool contract
 *      (matching the forward's entry-time precondition); each slot is
 *      written by step (3'b)'s `lib_double_mod_dsl` call and consumed
 *      back to |0> by step (5'a)'s `__lib_double_mod_dsl_adj`, with
 *      the register released LIFO before exit.
 *
 * @sa lib_mul_mod_dsl_oneshot, __lib_add_mod_inplace_dsl_adj,
 *     __lib_double_mod_dsl_adj
 * @see PRD §5 (Trust model and precondition contract).
 * @see docs/design_even_n_double_mod.md §8 row 2 (sturm-4oot.3).
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

    // (2'b) Re-allocate the (W-1)-bit lt_flags register in forward
    //       order.  Each slot enters in |0> by the qubit-pool
    //       contract, matching the forward's entry-time precondition
    //       (sturm-4oot.3).  Slot i is written by the
    //       lib_double_mod_dsl call inside step (3'b), held across to
    //       step (5'a)'s matched __lib_double_mod_dsl_adj which
    //       consumes it back to |0>.  Released LIFO in step (6'b).
    int   lt_flag_idx[kMaxN];
    qbool lt_flag_own[kMaxN];
    Bit   lt_flag_bits[kMaxN];
    if (n > 0u) {
        for (std::size_t i = 0u; i + 1u < n; ++i) {
            lt_flag_idx[i]  = QubitPool::instance().allocate();
            lt_flag_own[i]  = qbool::make_non_owning(lt_flag_idx[i]);
            lt_flag_own[i].super_mask = 1ULL;
            lt_flag_bits[i] =
                detail_mul_mod_oneshot::make_ancilla_view<Bit>(lt_flag_own[i]);
        }
    }

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
        //      Forward post-loop had each lt_flag_bits[i] = 0 (consumed),
        //      so the doubling here writes the witness back into the
        //      same slot for the matching consumption in step (5b).
        if (i + 1u < n) {
            lib_double_mod_dsl(shifted_bits, n_bits, n, lt_flag_bits[i]);
        }
    }

    // (4') r_bits[j] ^= acc_reg[j]  (XOR self-inverse: zeros r_bits).
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[j];

    // (5') Forward step (4) reverse: zero acc_reg, restore shifted_reg
    //      back to a.  For i = W-1..0:
    //        if i < W-1: undo doubling (consumes lt_flag_bits[i])
    //        undo controlled add
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i = W-1, W-2, ..., 0.

        if (i + 1u < n) {
            __lib_double_mod_dsl_adj(shifted_bits, n_bits, n,
                                     lt_flag_bits[i]);
        }

        sturm::lift_under(b_bits[i], [&]() {
            __lib_add_mod_inplace_dsl_adj(shifted_bits, acc_bits, n_bits, n);
        });
    }

    // (6') shifted_reg[0..W-1] ^= a (XOR self-inverse: zeros shifted_reg).
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[j] ^= a_bits[j];

    // (6'b) Release lt_flags LIFO (all slots are back to |0>).
    if (n > 0u) {
        for (std::size_t step = 0u; step + 1u < n; ++step) {
            std::size_t i = n - 2u - step;  // i = W-2, W-3, ..., 0.
            QubitPool::instance().release(lt_flag_idx[i]);
        }
    }

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
