// mul_mod_dsl_adj.hpp -- P2 (sturm-kubb.5) lib_mul_mod_dsl adjoint sibling.
// sturm-7cix Beat C: dispatcher between the new oneshot adjoint and the
// original chain adjoint.  Mirrors mul_mod_dsl.hpp's forward dispatcher.
//
// `__lib_mul_mod_dsl_adj` is the gate-reverse of `lib_mul_mod_dsl`: starting
// from `r = (a * b) mod n` (with the original `a, b, n` preserved), it
// returns `r` to |0> while leaving `a, b, n` unchanged.
//
// Dispatch logic exactly mirrors the forward (mul_mod_dsl.hpp): when
// `n_bits[0]` is classically hinted odd (`bit_value() == 1` AND
// `super_mask == 0`), route to `__lib_mul_mod_dsl_oneshot_adj`.  Otherwise
// fall back to `__lib_mul_mod_dsl_chain_adj` (gate-reverse of the chain
// helper).  This guarantees `forward + adjoint` round-trips on the same
// implementation path for any single call, regardless of the modulus.
//
// Re-allocation order for shifted_chain[0..W-1] and r_chain[1..W] in the
// chain adjoint matches the forward exactly so that under TDD harnesses
// calling reset_for_testing() between cases the QubitPool's LIFO reuses
// the same indices.
//
// STURM_REGISTER_ADJOINT at the bottom hooks invert<&lib_mul_mod_dsl<
// BitProxy>>() to this adjoint, mirroring add_mod_dsl_adj.hpp.

#pragma once

#include "sturm/detail/lib/add_mod_dsl.hpp"
#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"  // pulls oneshot adj
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

// ── __lib_mul_mod_dsl_chain_adj (legacy O(W^2) path) ─────────────────────────

/**
 * @brief Gate-reverse of @ref lib_mul_mod_dsl_chain.
 *
 * Mirrors the forward chain helper's allocation pattern (re-alloc
 * shifted_chain + r_chain in forward order so the LIFO recycles indices),
 * then runs the forward gate sequence in reverse.  Used as the fallback
 * when n is not classically hinted odd; preserves the legacy behavior
 * exactly so the existing `test_mul_mod_dsl_*` tests continue to pass.
 *
 * @pre `r_bits` must enter holding `(a * b) mod n` (the forward's output);
 *      exits |0>.  `a, b, n` preserved across the call.
 */
template <typename Bit>
inline void __lib_mul_mod_dsl_chain_adj(Bit* a_bits, Bit* b_bits,
                                        Bit* n_bits, std::size_t n,
                                        Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 14u;
    assert(n <= kMaxN && "__lib_mul_mod_dsl_chain_adj: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "__lib_mul_mod_dsl_chain_adj: no BackendContext installed");
    (void)raw;

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

    // (3') Re-build doubling chain.
    for (std::size_t i = 1u; i < n; ++i) {
        lib_add_mod_dsl(shifted_bits[i - 1u], shifted_bits[i - 1u],
                        n_bits, n, shifted_bits[i]);
    }

    // (4') Re-build r_chain[1] = b[0] ? shifted[0] : 0.
    sturm::lift_under(b_bits[0], [&]() {
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[1][j] ^= shifted_bits[0][j];
    });

    // (5') Re-build r_chain[i+1] for i=1..W-1.
    for (std::size_t i = 1u; i < n; ++i) {
        sturm::lift_under(b_bits[i], [&]() {
            lib_add_mod_dsl(r_chain_bits[i], shifted_bits[i],
                            n_bits, n, r_chain_bits[i + 1u]);
        });
        b_bits[i].flip();
        sturm::lift_under(b_bits[i], [&]() {
            for (std::size_t j = 0u; j < n; ++j)
                r_chain_bits[i + 1u][j] ^= r_chain_bits[i][j];
        });
        b_bits[i].flip();
    }

    // (6') r_bits ^= r_chain[W] (XOR self-inverse: zeros r_bits).
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= r_chain_bits[n][j];

    // (7') Zero r_chain[2..W].
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;
        b_bits[i].flip();
        sturm::lift_under(b_bits[i], [&]() {
            for (std::size_t j = 0u; j < n; ++j)
                r_chain_bits[i + 1u][j] ^= r_chain_bits[i][j];
        });
        b_bits[i].flip();
        sturm::lift_under(b_bits[i], [&]() {
            __lib_add_mod_dsl_adj(r_chain_bits[i], shifted_bits[i],
                                  n_bits, n, r_chain_bits[i + 1u]);
        });
    }

    // (8') Zero r_chain[1].
    sturm::lift_under(b_bits[0], [&]() {
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[1][j] ^= shifted_bits[0][j];
    });

    // (9') Zero shifted_chain[1..W-1].
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;
        __lib_add_mod_dsl_adj(shifted_bits[i - 1u], shifted_bits[i - 1u],
                              n_bits, n, shifted_bits[i]);
    }

    // (10') shifted[0] ^= a (XOR self-inverse: zeros shifted[0]).
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

// ── __lib_mul_mod_dsl_adj (public dispatcher) ────────────────────────────────

/**
 * @brief Gate-reverse adjoint of @ref lib_mul_mod_dsl.
 *
 * Public dispatcher mirroring the forward: routes to the oneshot adjoint
 * for classically-odd-hinted moduli, otherwise to the chain adjoint.
 *
 * @pre `r_bits` must enter holding `(a * b) mod n_value` (the paired
 *      forward's output); exits |0>.  `a, b, n` preserved across call.
 *      The dispatch decision MUST match the forward call's: when
 *      `__lib_mul_mod_dsl_adj` is invoked back-to-back with
 *      `lib_mul_mod_dsl` on the same `n_bits[0]` classical hint, both
 *      take the same path.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_mul_mod_dsl_adj` with `r_bits` not equal to the paired
 * forward's output, or under a dispatch hint that disagrees with the
 * forward's, is **undefined behavior**.
 *
 * @sa lib_mul_mod_dsl, __lib_mul_mod_dsl_oneshot_adj,
 *     __lib_mul_mod_dsl_chain_adj, sturm::mul_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_mul_mod_dsl_adj(Bit* a_bits, Bit* b_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    if (n == 0u) return;

    if (detail_mul_mod::is_classical_odd_n_hint(n_bits[0])) {
        __lib_mul_mod_dsl_oneshot_adj(a_bits, b_bits, n_bits, n, r_bits);
    } else {
        __lib_mul_mod_dsl_chain_adj(a_bits, b_bits, n_bits, n, r_bits);
    }
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_mul_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_mul_mod_dsl_adj<sturm::BitProxy>)
#endif
