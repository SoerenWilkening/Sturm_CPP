// pow_mod_dsl_adj.hpp -- Beat D-C (sturm-3sfl.3) lib_pow_mod_dsl adjoint
//                          sibling, paired with the rewritten forward.
//
// `__lib_pow_mod_dsl_adj` is the gate-reverse of `lib_pow_mod_dsl`: starting
// from `r = (base ^ exp) mod n` (with the original `base, exp, n` preserved
// by the paired forward call), it returns `r` to |0> while leaving
// `base, exp, n` unchanged.  Implementation runs the forward gate sequence
// in reverse, swapping each `lib_mul_mod_inplace_dsl` for
// `__lib_mul_mod_inplace_dsl_adj` and each `lib_square_mod_dsl` for
// `__lib_square_mod_dsl_adj`; self-inverse XOR / `flip()` steps stay as-is.
//
// Allocation order for `acc_reg`, `sq_reg`, and the per-iteration witness
// registers matches the forward exactly so that, under TDD harnesses calling
// `reset_for_testing()` between cases, the QubitPool's LIFO reuses the same
// indices.  Even when the adjoint runs back-to-back with the forward (no
// reset), the operations only touch ancillas the adjoint owns, so the order
// matters only for parity with the forward (mirrors `square_mod_dsl_adj.hpp`'s
// "re-allocate in forward order" pattern).
//
// Algorithm — gate-reverse of pow_mod_dsl.hpp's steps 1-8.  The forward's
// numbered steps (F1..F8) become adjoint steps 1'..8' executed in reverse:
//
//   1'.  alloc acc_reg[W] (forward order); flip bit 0 → acc_reg = 1.
//        (Inverse of F8's release; F1 self-inverse re-stages acc_reg = 1.)
//   2'.  alloc sq_reg[W]; sq_reg ^= base_bits → sq_reg = base.
//        (Inverse of F8's sq release; F7 self-inverse XOR re-loads base.)
//   3'.  Re-build acc_reg / sq_reg / witnesses by running forward step (3)
//        end-to-end: for i = 0..W-1, allocate mul_witness_i, run the
//        controlled `lib_mul_mod_inplace_dsl`; if i < W-1 allocate
//        sq_witness_i and run `lib_square_mod_dsl`.  After this: acc_reg
//        holds (base^exp) mod n, sq_reg holds base^(2^W) mod n.
//        (Inverse of F5's reverse loop.)
//   4'.  r_bits ^= acc_reg.  Self-inverse XOR — since r_bits enters holding
//        (base^exp) mod n and acc_reg holds the same value, r_bits returns
//        to |0>.  (Inverse of F4.)
//   5'.  Reverse loop, i = W-1..0: __lib_square_mod_dsl_adj then
//        __lib_mul_mod_inplace_dsl_adj (under control of exp_bits[i]),
//        releasing the per-iteration witnesses LIFO.  (Inverse of F3.)
//   6'.  sq_reg ^= base_bits → sq_reg = |0>.  (Inverse of F2.)
//   7'.  acc_reg[0].flip() → acc_reg = |0>.  (Inverse of F1.)
//   8'.  Release sq_reg, acc_reg LIFO.
//
// STURM_REGISTER_ADJOINT at the bottom hooks
// `invert<&lib_pow_mod_dsl<BitProxy>>()` to this adjoint, mirroring
// mul_mod_inplace_dsl_adj.hpp / square_mod_dsl_adj.hpp.
//
// Auto-included from pow_mod_dsl.hpp.
//
// Target: ≤ 220 LoC.

#pragma once

#include "sturm/detail/lib/mul_mod_inplace_dsl.hpp"
#include "sturm/detail/lib/square_mod_dsl.hpp"
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
 * @brief Gate-reverse adjoint of @ref lib_pow_mod_dsl.
 *
 * Starting from `r_bits = (base_bits ^ exp_bits) mod n_bits` (with the
 * original `base_bits`, `exp_bits`, `n_bits` preserved by the forward
 * call), this routine returns `r_bits` to |0> while leaving the input
 * registers unchanged.  Implementation runs the forward gate sequence in
 * reverse, swapping each `lib_mul_mod_inplace_dsl` for its adjoint and
 * each `lib_square_mod_dsl` for its adjoint; self-inverse XOR / `flip()`
 * steps stay as-is.
 *
 * Built strictly on top of `lib_mul_mod_inplace_dsl` (Beat D-A) and
 * `lib_square_mod_dsl` (Beat D-B) and their adjoints per the PRD §4
 * layering rule — emits no gates of its own.  Honors the same
 * `0^0 == 1` convention as the forward primitive.
 *
 * @param base_bits Base register (W qubits, read but restored).
 * @param exp_bits  Exponent register (W qubits, read but restored).
 * @param n_bits    Modulus register (W qubits, read but restored).
 * @param n         Register width.  `n == 0` short-circuits to a no-op.
 * @param r_bits    Result register (W qubits).  Must enter holding
 *                  `(base ^ exp) mod n`; exits in |0>.
 *
 * @pre `base ∈ [0, n_value)` and `n_value ≥ 1` (when `n == 0`, the call
 *      is a no-op).  The exponent `exp` is unconstrained beyond fitting
 *      in W bits.  `r_bits` must enter the routine holding the value
 *      produced by a paired `lib_pow_mod_dsl` call on the same
 *      `(base_bits, exp_bits, n_bits)` operands.  `base_bits`,
 *      `exp_bits`, `n_bits`, and `r_bits` must refer to physically
 *      distinct qubit registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_pow_mod_dsl_adj` with `base` outside `[0, n_value)`, with
 * `r_bits` not equal to the paired forward output, or with aliased
 * registers is **undefined behavior** — the routine still emits a
 * well-formed gate sequence, but `r_bits` will not return to |0> and
 * the input registers may be corrupted.  Matches the trust model
 * shared with the public `pow_mod` wrapper.
 *
 * @sa lib_pow_mod_dsl, __lib_mul_mod_inplace_dsl_adj,
 *     __lib_square_mod_dsl_adj, sturm::pow_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_pow_mod_dsl_adj(Bit* base_bits, Bit* exp_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    if (n == 0u) return;

    // Mirrors the forward's internal W cap (Beat D-D will lift it).
    static constexpr std::size_t kMaxN = 8u;
    assert(n <= kMaxN && "__lib_pow_mod_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_pow_mod_dsl_adj: no BackendContext installed");
    (void)raw;

    // (1') Re-allocate acc_reg[W]; flip bit 0 → acc_reg = 1.
    int   acc_idx[kMaxN];
    qbool acc_own[kMaxN];
    Bit   acc_bits[kMaxN];
    for (std::size_t j = 0u; j < n; ++j) {
        acc_idx[j]  = QubitPool::instance().allocate();
        acc_own[j]  = qbool::make_non_owning(acc_idx[j]);
        acc_bits[j] = detail_pow_mod::make_ancilla_view<Bit>(acc_own[j]);
    }
    acc_bits[0].flip();

    // (2') Re-allocate sq_reg[W]; sq_reg ^= base_bits → sq_reg = base.
    int   sq_idx[kMaxN];
    qbool sq_own[kMaxN];
    Bit   sq_bits[kMaxN];
    for (std::size_t j = 0u; j < n; ++j) {
        sq_idx[j]  = QubitPool::instance().allocate();
        sq_own[j]  = qbool::make_non_owning(sq_idx[j]);
        sq_bits[j] = detail_pow_mod::make_ancilla_view<Bit>(sq_own[j]);
    }
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[j] ^= base_bits[j];

    // (3') Re-run the forward loop end-to-end.  Witnesses must persist
    //      across all forward iterations until consumed by the reverse
    //      pass below — same accounting as the forward primitive.
    int   mul_w_idx[kMaxN][kMaxN];
    qbool mul_w_own[kMaxN][kMaxN];
    Bit   mul_w_bits[kMaxN][kMaxN];
    int   sq_w_idx[kMaxN][kMaxN];
    qbool sq_w_own[kMaxN][kMaxN];
    Bit   sq_w_bits[kMaxN][kMaxN];

    for (std::size_t i = 0u; i < n; ++i) {
        for (std::size_t j = 0u; j < n; ++j) {
            mul_w_idx[i][j]  = QubitPool::instance().allocate();
            mul_w_own[i][j]  = qbool::make_non_owning(mul_w_idx[i][j]);
            mul_w_bits[i][j] =
                detail_pow_mod::make_ancilla_view<Bit>(mul_w_own[i][j]);
        }

        sturm::lift_under(exp_bits[i], [&]() {
            lib_mul_mod_inplace_dsl(sq_bits, acc_bits, n_bits, n,
                                    mul_w_bits[i]);
        });

        if (i + 1u < n) {
            for (std::size_t j = 0u; j < n; ++j) {
                sq_w_idx[i][j]  = QubitPool::instance().allocate();
                sq_w_own[i][j]  = qbool::make_non_owning(sq_w_idx[i][j]);
                sq_w_bits[i][j] =
                    detail_pow_mod::make_ancilla_view<Bit>(sq_w_own[i][j]);
            }
            lib_square_mod_dsl(sq_bits, n_bits, n, sq_w_bits[i]);
        }
    }

    // (4') r_bits ^= acc_reg.  Self-inverse: r_bits enters holding
    //      (base^exp) mod n and acc_reg now holds the same value, so
    //      r_bits returns to |0>.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[j];

    // (5') Reverse loop: gate-reverse of step (3').  Each adjoint call
    //      consumes its paired witness back to |0>; release LIFO.
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i goes W-1, W-2, ..., 0.

        if (i + 1u < n) {
            __lib_square_mod_dsl_adj(sq_bits, n_bits, n, sq_w_bits[i]);
            for (std::size_t j = n; j-- > 0u;)
                QubitPool::instance().release(sq_w_idx[i][j]);
        }

        sturm::lift_under(exp_bits[i], [&]() {
            __lib_mul_mod_inplace_dsl_adj(sq_bits, acc_bits, n_bits, n,
                                          mul_w_bits[i]);
        });
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(mul_w_idx[i][j]);
    }

    // (6') sq_reg ^= base_bits → sq_reg = |0>.  (Inverse of F2.)
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[j] ^= base_bits[j];

    // (7') acc_reg[0].flip() → acc_reg = |0>.  (Inverse of F1.)
    acc_bits[0].flip();

    // (8') Release sq_reg, acc_reg LIFO.
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(sq_idx[j]);
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(acc_idx[j]);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_pow_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_pow_mod_dsl_adj<sturm::BitProxy>)
#endif
