// mul_mod_dsl_adj.hpp -- P2 (sturm-kubb.5) lib_mul_mod_dsl adjoint sibling.
//
// __lib_mul_mod_dsl_adj is the gate-reverse of lib_mul_mod_dsl: starting
// from `r = (a * b) mod n` (with the original `a, b, n` preserved), it
// returns `r` to |0> while leaving `a, b, n` unchanged.  Implementation
// runs the forward gate sequence in reverse, swapping each lib_add_mod_dsl
// for __lib_add_mod_dsl_adj (and vice-versa); self-inverse XOR steps and
// control-stack pushes/pops mirror the forward.
//
// Re-allocation order for shifted_chain[0..W-1] and r_chain[1..W] matches
// the forward exactly so that under TDD harnesses calling reset_for_testing()
// between cases the QubitPool's LIFO reuses the same indices.  Even when
// the adjoint runs back-to-back with the forward (no reset), the operations
// only touch ancillas the adjoint owns, so the order matters only for LoC
// parity (mirrors `add_mod_dsl_adj.hpp`'s pattern).
//
// Algorithm — gate-reverse of mul_mod_dsl.hpp:
//   1'.  alloc shifted_chain[0..W-1] and r_chain[1..W] in forward order.
//   2'.  shifted[0] ^= a (XOR self-inverse → restores a into shifted[0]).
//   3'.  i=1..W-1: lib_add_mod_dsl(shifted[i-1], shifted[i-1], n, W,
//        shifted[i]) — gate-reverse of forward step 6's adjoint loop.
//        Re-builds shifted_chain[i] = (a·2^i) mod n.
//   4'.  push(b[0]); r_chain[1] ^= shifted[0]; pop — re-builds r_chain[1].
//   5'.  i=1..W-1: F8b_inv (push(b[i]); lib_add_mod_dsl; pop) then F8a_inv
//        (XOR-copy under flipped b[i]).  Re-builds r_chain[i+1] for all i,
//        ending with r_chain[W] = (a*b) mod n.
//   6'.  r_bits ^= r_chain[W] (XOR self-inverse → zeros r_bits).
//   7'.  i=W-1..1: F6b_inv (XOR-copy under flipped b[i]) then F6a_inv
//        (push(b[i]); __lib_add_mod_dsl_adj; pop).  Zeros r_chain[2..W].
//   8'.  push(b[0]); r_chain[1] ^= shifted[0]; pop — zeros r_chain[1].
//   9'.  i=W-1..1: __lib_add_mod_dsl_adj(shifted[i-1], shifted[i-1], n, W,
//        shifted[i]).  Zeros shifted_chain[1..W-1].
//  10'.  shifted[0] ^= a (zeros shifted_chain[0]).
//  11'.  release r_chain LIFO, then shifted_chain LIFO.
//
// STURM_REGISTER_ADJOINT at the bottom hooks invert<&lib_mul_mod_dsl<
// BitProxy>>() to this adjoint, mirroring add_mod_dsl_adj.hpp.

#pragma once

#include "sturm/lib/add_mod_dsl.hpp"
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
 * @brief Gate-reverse adjoint of @ref lib_mul_mod_dsl.
 *
 * Starting from `r_bits = (a_bits * b_bits) mod n_bits` (with the
 * original `a_bits`, `b_bits`, `n_bits` preserved by the forward call),
 * this routine returns `r_bits` to |0> while leaving the input
 * registers unchanged.  Implementation runs the forward gate sequence
 * in reverse, swapping each `lib_add_mod_dsl` for
 * `__lib_add_mod_dsl_adj` (and vice-versa); self-inverse XOR /
 * control-stack steps stay as-is.
 *
 * Built strictly on top of `lib_add_mod_dsl` + `__lib_add_mod_dsl_adj`
 * per the PRD §4 layering rule — emits no gates of its own.
 *
 * @param a_bits Left factor register (W qubits, read but restored).
 * @param b_bits Right factor register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width.  `n == 0` short-circuits to a no-op.
 * @param r_bits Result register (W qubits).  Must enter holding
 *               `(a * b) mod n`; exits in |0>.
 *
 * @pre `a, b ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the call is a
 *      no-op).  `r_bits` must enter the routine holding the value
 *      produced by a paired `lib_mul_mod_dsl` call on the same
 *      `(a_bits, b_bits, n_bits)` operands.  `a_bits`, `b_bits`,
 *      `n_bits`, and `r_bits` must refer to physically distinct qubit
 *      registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_mul_mod_dsl_adj` with operands outside `[0, n)`, with
 * `r_bits` not equal to the paired forward output, or with aliased
 * registers is **undefined behavior** — the routine still emits a
 * well-formed gate sequence, but `r_bits` will not return to |0> and
 * the input registers may be corrupted.  This matches the trust model
 * shared with the public `mul_mod` wrapper (PRD §5).
 *
 * @sa lib_mul_mod_dsl, __lib_add_mod_dsl_adj, sturm::mul_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_mul_mod_dsl_adj(Bit* a_bits, Bit* b_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 8u;
    assert(n <= kMaxN && "__lib_mul_mod_dsl_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_mul_mod_dsl_adj: no BackendContext installed");
    (void)raw;  // sturm-a3t4.3: control_stack is now driven by WHEN/WhenGuard
                // through the lift pattern; ctx is no longer poked directly.

    // (1') Re-allocate shifted_chain[0..W-1] in forward order.
    int   shifted_idx[kMaxN][kMaxN];
    qbool shifted_own[kMaxN][kMaxN];
    Bit   shifted_bits[kMaxN][kMaxN];
    for (std::size_t i = 0u; i < n; ++i) {
        for (std::size_t j = 0u; j < n; ++j) {
            shifted_idx[i][j]  = QubitPool::instance().allocate();
            shifted_own[i][j]  = qbool::make_non_owning(shifted_idx[i][j]);
            shifted_bits[i][j] =
                detail_mul_mod::make_ancilla_view<Bit>(shifted_own[i][j]);
        }
    }

    // (1') Re-allocate r_chain[1..W] in forward order.
    int   r_chain_idx[kMaxN + 1u][kMaxN];
    qbool r_chain_own[kMaxN + 1u][kMaxN];
    Bit   r_chain_bits[kMaxN + 1u][kMaxN];
    for (std::size_t i = 1u; i <= n; ++i) {
        for (std::size_t j = 0u; j < n; ++j) {
            r_chain_idx[i][j]  = QubitPool::instance().allocate();
            r_chain_own[i][j]  = qbool::make_non_owning(r_chain_idx[i][j]);
            r_chain_bits[i][j] =
                detail_mul_mod::make_ancilla_view<Bit>(r_chain_own[i][j]);
        }
    }

    // (2') shifted[0] ^= a (XOR self-inverse): shifted[0] = a.
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[0][j] ^= a_bits[j];

    // (3') Re-build doubling chain: shifted[i] = (a·2^i) mod n for i=1..W-1.
    for (std::size_t i = 1u; i < n; ++i) {
        lib_add_mod_dsl(shifted_bits[i - 1u], shifted_bits[i - 1u],
                        n_bits, n, shifted_bits[i]);
    }

    // (4') Re-build r_chain[1] = b[0] ? shifted[0] : 0 (XOR self-inverse).
    //      sturm-a3t4.3: depth-1 lift over b[0] flag (see mul_mod_dsl.hpp's
    //      detail_mul_mod::lift_under_flag for the idiom).
    detail_mul_mod::lift_under_flag(b_bits[0], [&]() {
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[1][j] ^= shifted_bits[0][j];
    });

    // (5') Re-build r_chain[i+1] for i=1..W-1 (reverse of forward step 4's
    //      r_chain-uncompute loop).  Forward did F8a then F8b in i=W-1..1
    //      order; reverse runs F8b_inv then F8a_inv in i=1..W-1 order.
    for (std::size_t i = 1u; i < n; ++i) {
        // F8b_inv: lib_add_mod_dsl under control(b[i]).
        detail_mul_mod::lift_under_flag(b_bits[i], [&]() {
            lib_add_mod_dsl(r_chain_bits[i], shifted_bits[i],
                            n_bits, n, r_chain_bits[i + 1u]);
        });

        // F8a_inv: XOR-copy under control(flipped b[i]).
        b_bits[i].flip();
        detail_mul_mod::lift_under_flag(b_bits[i], [&]() {
            for (std::size_t j = 0u; j < n; ++j)
                r_chain_bits[i + 1u][j] ^= r_chain_bits[i][j];
        });
        b_bits[i].flip();
    }

    // (6') r_bits ^= r_chain[W] (self-inverse): r_bits == r_chain[W] now,
    //      so XOR zeros r_bits.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= r_chain_bits[n][j];

    // (7') Zero r_chain[2..W] (reverse of forward step 2b's build loop).
    //      Forward did F6a then F6b in i=1..W-1 order; reverse runs
    //      F6b_inv then F6a_inv in i=W-1..1 order.
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;  // i goes W-1, W-2, ..., 1.

        // F6b_inv: XOR-copy under control(flipped b[i]).
        b_bits[i].flip();
        detail_mul_mod::lift_under_flag(b_bits[i], [&]() {
            for (std::size_t j = 0u; j < n; ++j)
                r_chain_bits[i + 1u][j] ^= r_chain_bits[i][j];
        });
        b_bits[i].flip();

        // F6a_inv: __lib_add_mod_dsl_adj under control(b[i]).
        detail_mul_mod::lift_under_flag(b_bits[i], [&]() {
            __lib_add_mod_dsl_adj(r_chain_bits[i], shifted_bits[i],
                                  n_bits, n, r_chain_bits[i + 1u]);
        });
    }

    // (8') Zero r_chain[1] (reverse of forward step 2a, XOR self-inverse).
    detail_mul_mod::lift_under_flag(b_bits[0], [&]() {
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[1][j] ^= shifted_bits[0][j];
    });

    // (9') Zero shifted_chain[1..W-1] (reverse of forward doubling-build
    //      loop): for i=W-1..1 run __lib_add_mod_dsl_adj.
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;  // i goes W-1, W-2, ..., 1.
        __lib_add_mod_dsl_adj(shifted_bits[i - 1u], shifted_bits[i - 1u],
                              n_bits, n, shifted_bits[i]);
    }

    // (10') shifted[0] ^= a (XOR self-inverse): shifted[0] = 0.
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[0][j] ^= a_bits[j];

    // (11') Release r_chain[1..W] then shifted_chain[0..W-1] LIFO.
    for (std::size_t i = n; i >= 1u; --i) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(r_chain_idx[i][j]);
    }
    for (std::size_t i = n; i-- > 0u;) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(shifted_idx[i][j]);
    }
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_mul_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_mul_mod_dsl_adj<sturm::BitProxy>)
#endif
