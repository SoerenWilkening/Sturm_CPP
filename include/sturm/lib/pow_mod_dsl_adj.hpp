// pow_mod_dsl_adj.hpp -- P3 (sturm-a5te.6) lib_pow_mod_dsl adjoint sibling.
//
// __lib_pow_mod_dsl_adj is the gate-reverse of lib_pow_mod_dsl: starting
// from `r = (base ^ exp) mod n` (with the original `base, exp, n`
// preserved), it returns `r` to |0> while leaving `base, exp, n` unchanged.
// Implementation runs the forward gate sequence in reverse, swapping each
// `lib_mul_mod_dsl` for `__lib_mul_mod_dsl_adj` (and vice-versa);
// self-inverse XOR / `flip()` steps stay as-is, and control-stack
// pushes/pops mirror the forward.
//
// Re-allocation order for sq_chain[0..W-1] and acc_chain[0..W] matches the
// forward exactly so that under TDD harnesses calling reset_for_testing()
// between cases the QubitPool's LIFO reuses the same indices.  Even when
// the adjoint runs back-to-back with the forward (no reset), the operations
// only touch ancillas the adjoint owns, so the order matters only for LoC
// parity — mirrors `mul_mod_dsl_adj.hpp`'s pattern.
//
// Algorithm — gate-reverse of pow_mod_dsl.hpp (forward steps F1..F13 are
// numbered there).  Adjoint executes (Fk^-1) in reverse order k=13..1; we
// label adjoint steps 1'..13' to mirror the forward numbering.
//
//   1'.  alloc sq_chain[0..W-1] in forward order (F13 was LIFO-release;
//        adjoint's first gate is the inverse of forward's last alloc).
//   2'.  sq[0] ^= base  (F12 is XOR self-inverse → restores sq[0] = base).
//   3'.  i=1..W-1: lib_mul_mod_dsl(sq[i-1], sq[i-1], n, W, sq[i])
//        (F11 was __lib_mul_mod_dsl_adj uncompute; adjoint runs forward
//         lib_mul_mod_dsl to re-build sq[i] = (base^(2^i)) mod n).
//   4'.  alloc acc_chain[0..W] in forward order.
//   5'.  acc[0][0].flip()  (F9 self-inverse → re-builds acc[0] = 1).
//   6'.  i=0..W-1: F6a (push(exp[i]); lib_mul_mod_dsl(acc[i], sq[i], n, W,
//        acc[i+1]); pop) then F6b (flip(exp[i]); push(exp[i]); XOR-copy
//        acc[i] into acc[i+1]; pop; flip(exp[i])).  Re-builds acc_chain
//        end-to-end; acc[W] now holds (base^exp) mod n.
//   7'.  r_bits ^= acc[W]  (F7 self-inverse → zeros r_bits since they
//        currently hold the answer).
//   8'.  i=W-1..0: F6b_inv (flip(exp[i]); push(exp[i]); XOR-copy acc[i]
//        into acc[i+1]; pop; flip(exp[i])) then F6a_inv (push(exp[i]);
//        __lib_mul_mod_dsl_adj(acc[i], sq[i], n, W, acc[i+1]); pop).  Zeros
//        acc[1..W].
//   9'.  acc[0][0].flip()  (F5 self-inverse → zeros acc[0]).
//  10'.  release acc_chain LIFO.
//  11'.  i=W-1..1: __lib_mul_mod_dsl_adj(sq[i-1], sq[i-1], n, W, sq[i]).
//        Zeros sq[1..W-1].
//  12'.  sq[0] ^= base  (F2 self-inverse → zeros sq[0]).
//  13'.  release sq_chain LIFO.
//
// STURM_REGISTER_ADJOINT at the bottom hooks invert<&lib_pow_mod_dsl<
// BitProxy>>() to this adjoint, mirroring mul_mod_dsl_adj.hpp.
//
// LoC budget: target ≤ 220 (plan §5.2).  Reuses the
// detail_pow_mod::make_ancilla_view / flag_qbool_view helpers from
// pow_mod_dsl.hpp so this header stays a thin gate-reverse shell.
//
// Auto-included from pow_mod_dsl.hpp.

#pragma once

#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/control/when.hpp"          // sturm-a3t4.3: WHEN + WhenGuard::active_control
#include "sturm/uncompute/uncompute_api.hpp" // sturm-a3t4.3: uncompute_and
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
 * @brief Gate-reverse adjoint of @ref lib_pow_mod_dsl.
 *
 * Starting from `r_bits = (base_bits ^ exp_bits) mod n_bits` (with the
 * original `base_bits`, `exp_bits`, `n_bits` preserved by the forward
 * call), this routine returns `r_bits` to |0> while leaving the input
 * registers unchanged.  Implementation runs the forward gate sequence
 * in reverse, swapping each `lib_mul_mod_dsl` for
 * `__lib_mul_mod_dsl_adj` (and vice-versa); self-inverse XOR /
 * `flip()` / control-stack steps stay as-is.
 *
 * Built strictly on top of `lib_mul_mod_dsl` + `__lib_mul_mod_dsl_adj`
 * per the PRD §4 layering rule — emits no gates of its own.  Honors
 * the same `0^0 == 1` convention as the forward primitive.
 *
 * @param base_bits Base register (W qubits, read but restored).
 * @param exp_bits  Exponent register (W qubits, read but restored).
 * @param n_bits    Modulus register (W qubits, read but restored).
 * @param n         Register width.  `n == 0` short-circuits to a no-op.
 * @param r_bits    Result register (W qubits).  Must enter holding
 *                  `(base ^ exp) mod n`; exits in |0>.
 *
 * @pre `base ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the call is a
 *      no-op).  The exponent `exp` is unconstrained beyond fitting in
 *      W bits.  `r_bits` must enter the routine holding the value
 *      produced by a paired `lib_pow_mod_dsl` call on the same
 *      `(base_bits, exp_bits, n_bits)` operands.  `base_bits`,
 *      `exp_bits`, `n_bits`, and `r_bits` must refer to physically
 *      distinct qubit registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_pow_mod_dsl_adj` with `base` outside `[0, n)`, with
 * `r_bits` not equal to the paired forward output, or with aliased
 * registers is **undefined behavior** — the routine still emits a
 * well-formed gate sequence, but `r_bits` will not return to |0> and
 * the input registers may be corrupted.  This matches the trust model
 * shared with the public `pow_mod` wrapper (PRD §5).
 *
 * @sa lib_pow_mod_dsl, __lib_mul_mod_dsl_adj, sturm::pow_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_pow_mod_dsl_adj(Bit* base_bits, Bit* exp_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 8u;
    assert(n <= kMaxN && "__lib_pow_mod_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_pow_mod_dsl_adj: no BackendContext installed");
    (void)raw;  // sturm-a3t4.3: control_stack is now driven by WHEN/WhenGuard
                // through the lift pattern; ctx is no longer poked directly.

    // (1') Re-allocate sq_chain[0..W-1] in forward order.
    int   sq_idx[kMaxN][kMaxN];
    qbool sq_own[kMaxN][kMaxN];
    Bit   sq_bits[kMaxN][kMaxN];
    for (std::size_t i = 0u; i < n; ++i) {
        for (std::size_t j = 0u; j < n; ++j) {
            sq_idx[i][j]  = QubitPool::instance().allocate();
            sq_own[i][j]  = qbool::make_non_owning(sq_idx[i][j]);
            sq_bits[i][j] =
                detail_pow_mod::make_ancilla_view<Bit>(sq_own[i][j]);
        }
    }

    // (2') sq[0] ^= base (XOR self-inverse): sq[0] = base.
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[0][j] ^= base_bits[j];

    // (3') Re-build squaring chain: sq[i] = (base^(2^i)) mod n for i=1..W-1.
    for (std::size_t i = 1u; i < n; ++i) {
        lib_mul_mod_dsl(sq_bits[i - 1u], sq_bits[i - 1u],
                        n_bits, n, sq_bits[i]);
    }

    // (4') Re-allocate acc_chain[0..W] in forward order.
    int   acc_idx[kMaxN + 1u][kMaxN];
    qbool acc_own[kMaxN + 1u][kMaxN];
    Bit   acc_bits[kMaxN + 1u][kMaxN];
    for (std::size_t i = 0u; i <= n; ++i) {
        for (std::size_t j = 0u; j < n; ++j) {
            acc_idx[i][j]  = QubitPool::instance().allocate();
            acc_own[i][j]  = qbool::make_non_owning(acc_idx[i][j]);
            acc_bits[i][j] =
                detail_pow_mod::make_ancilla_view<Bit>(acc_own[i][j]);
        }
    }

    // (5') acc[0][0].flip() (self-inverse): acc[0] = 1.
    acc_bits[0][0].flip();

    // (6') Re-build acc_chain[i+1] for i=0..W-1 — gate-reverse of forward
    //      step 8's adjoint loop is the forward step 6's build loop.
    //      sturm-a3t4.3: depth-1 lift via detail_pow_mod::lift_under_flag.
    for (std::size_t i = 0u; i < n; ++i) {
        // F6a: under control(exp[i]), write mul_mod into acc_chain[i+1].
        detail_pow_mod::lift_under_flag(exp_bits[i], [&]() {
            lib_mul_mod_dsl(acc_bits[i], sq_bits[i],
                            n_bits, n, acc_bits[i + 1u]);
        });

        // F6b: flip exp[i], lift, XOR-copy acc[i] into acc[i+1], unflip.
        exp_bits[i].flip();
        detail_pow_mod::lift_under_flag(exp_bits[i], [&]() {
            for (std::size_t j = 0u; j < n; ++j)
                acc_bits[i + 1u][j] ^= acc_bits[i][j];
        });
        exp_bits[i].flip();
    }

    // (7') r_bits ^= acc[W] (self-inverse): r_bits == acc[W] now (the
    //      answer), so XOR zeros r_bits.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[n][j];

    // (8') Zero acc_chain[1..W] — gate-reverse of forward step 6's build
    //      loop is forward step 8's uncompute loop (i=W-1..0: F6b_inv then
    //      F6a_inv).
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i goes W-1, W-2, ..., 0.

        // F6b_inv: XOR-copy under control(flipped exp[i]).
        exp_bits[i].flip();
        detail_pow_mod::lift_under_flag(exp_bits[i], [&]() {
            for (std::size_t j = 0u; j < n; ++j)
                acc_bits[i + 1u][j] ^= acc_bits[i][j];
        });
        exp_bits[i].flip();

        // F6a_inv: __lib_mul_mod_dsl_adj under control(exp[i]).
        detail_pow_mod::lift_under_flag(exp_bits[i], [&]() {
            __lib_mul_mod_dsl_adj(acc_bits[i], sq_bits[i],
                                  n_bits, n, acc_bits[i + 1u]);
        });
    }

    // (9') acc[0][0].flip() (self-inverse): acc[0] = 0.
    acc_bits[0][0].flip();

    // (10') Release acc_chain[0..W] LIFO.
    for (std::size_t i = n + 1u; i-- > 0u;) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(acc_idx[i][j]);
    }

    // (11') Zero sq_chain[1..W-1] — gate-reverse of forward step 3's
    //       squaring-build loop is forward step 11's uncompute loop
    //       (i=W-1..1: __lib_mul_mod_dsl_adj).
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;  // i goes W-1, W-2, ..., 1.
        __lib_mul_mod_dsl_adj(sq_bits[i - 1u], sq_bits[i - 1u],
                              n_bits, n, sq_bits[i]);
    }

    // (12') sq[0] ^= base (self-inverse): sq[0] = 0.
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[0][j] ^= base_bits[j];

    // (13') Release sq_chain[0..W-1] LIFO.
    for (std::size_t i = n; i-- > 0u;) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(sq_idx[i][j]);
    }
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_pow_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_pow_mod_dsl_adj<sturm::BitProxy>)
#endif
