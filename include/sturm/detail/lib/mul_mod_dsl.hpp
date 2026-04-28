// mul_mod_dsl.hpp -- P2 (sturm-kubb.2) lib_mul_mod_dsl forward primitive.
// sturm-7cix Beat C: dispatcher between O(W) oneshot (for odd-n moduli)
// and the original chain-style fallback (for even-n moduli).
//
// Out-of-place modular multiplication: r = (a * b) mod n, all unsigned, all
// W bits wide.  Built strictly on top of `lib_add_mod_inplace_dsl` /
// `lib_double_mod_dsl` (Beats A & B; new O(W) path) OR the original
// `lib_add_mod_dsl` chain (legacy O(W^2) path).  PRD §4 layering rule —
// no direct gate emission, no new arithmetic kernel.
//
// sturm-7cix Beat C — odd-n / even-n dispatch (resolves the issue's
// design-question on `lib_double_mod_dsl`'s odd-n precondition):
//
//   `lib_mul_mod_dsl` reads `n_bits[0]` classically.  When the BitProxy's
//   tracked `.value` is 1 AND `super_mask` is 0 (classical hint that
//   n_value is odd), we dispatch to `lib_mul_mod_dsl_oneshot` which is
//   the new O(W) implementation built on the in-place add-mod and
//   doubling primitives (Beats A & B).
//
//   In every other case (n_bits[0] in superposition, or classically 0)
//   we fall back to the original chain-style `lib_mul_mod_dsl_chain`
//   helper, which uses the out-of-place `lib_add_mod_dsl` and works
//   for any n.  The chain keeps its `2W^2 + W + 7` peak ancilla footprint;
//   this is the path the existing tests
//   (`test_mul_mod_dsl{,_adjoint,_ancilla,_pool_drain}.cpp`) hit because
//   they use the 1-arg `qbool::make_non_owning(idx)` factory which leaves
//   `.value = 0`.
//
// In production usage via the `sturm::mul_mod` wrapper (which threads
// the classical `n.value` through `make_non_owning(idx, n.value, ...)`),
// the oneshot path is taken when n is classically odd — covering the
// common Shor's-algorithm / ECC moduli.  Even-n callers keep working
// through the chain fallback.
//
// Chain algorithm (preserved as `lib_mul_mod_dsl_chain` private helper):
// see the original preamble below for the doubling-and-conditional-add
// shift-and-add chain.  Peak ancilla = 2W^2 + W + 7 above 4W inputs.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// add_mod_dsl.hpp / add_mod_dsl_adj.hpp pairing pattern).
//
// ─────────────────────────────────────────────────────────────────────────
// Original chain algorithm (now `lib_mul_mod_dsl_chain`, sturm-kubb.2):
//   inputs : a_bits[W], b_bits[W], n_bits[W], r_bits[W]   (r = |0>)
//   precond: a, b ∈ [0, n)                                 (PRD §5)
//   output : r_bits = (a * b) mod n
//
//   1. Build the doubling chain:
//      shifted_chain[0]   := XOR-copy of a
//      shifted_chain[i]   := add_mod(shifted_chain[i-1], shifted_chain[i-1], n)
//
//   2. Build the partial-sum chain via the flip-then-control idiom:
//      r_chain[1]   := b[0] ? shifted_chain[0] : 0
//      r_chain[i+1] := b[i] ? (r_chain[i] + shifted_chain[i]) mod n
//                            : r_chain[i]
//
//   3. r_bits ^= r_chain[W]                                 (write the answer)
//
//   4. Uncompute r_chain[1..W] in reverse via __lib_add_mod_dsl_adj.
//   5. Uncompute shifted_chain in reverse (W doublings in adjoint order).
//
// See git history for the chain's full step-by-step rationale.
// ─────────────────────────────────────────────────────────────────────────

#pragma once

#include "sturm/detail/lib/add_mod_dsl.hpp"
#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"
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

namespace detail_mul_mod {

// Bit-view helper mirroring detail_add_mod::make_ancilla_view, kept local
// so mul_mod_dsl can be included without dragging in the add_mod helper
// namespace from add_mod_dsl.hpp.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

// Detect whether the caller has classically hinted that the modulus is
// odd.  The hint is the BitProxy's `.bit_value() == 1` together with
// `super_mask` bit 0 == 0 (classical, not in superposition).  This
// exactly matches the `qbool::make_non_owning(idx, val, mask)`-seeded
// callers (e.g. `sturm::mul_mod` via `qint_modular.hpp`).  The 1-arg
// `make_non_owning(idx)` factory used by the lib-level tests leaves
// `.value = 0`, which yields `is_classical_odd_n_hint == false` and
// routes those tests to the chain fallback — preserving their pre-Beat-C
// behavior exactly.
template <typename Bit>
inline bool is_classical_odd_n_hint(const Bit& n_bit_low) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        // qbool overload: read the bool value bit and super_mask LSB.
        const bool classical = (n_bit_low.super_mask & 1ULL) == 0ULL;
        const bool lsb_one   = (n_bit_low.value & 1) != 0;
        return classical && lsb_one;
    } else {
        // BitProxy / detail-bit overload: bit_value() reads the parent's
        // tracked classical value at this bit_pos, and the super_mask
        // bit must also be 0 to assert "classical".
        const qbool view = static_cast<qbool>(n_bit_low);
        const bool classical = (view.super_mask & 1ULL) == 0ULL;
        return classical && view.get_bool_value();
    }
}

}  // namespace detail_mul_mod

// ── lib_mul_mod_dsl_chain (legacy O(W^2) path) ───────────────────────────────

/**
 * @brief Original chain-style modular multiplication helper.
 *
 * Allocates `shifted_chain[0..W-1]` and `r_chain[1..W]` (each W bits) for
 * an O(W^2) ancilla footprint.  Used as the fallback path when
 * `lib_mul_mod_dsl`'s caller has not classically hinted that n is odd
 * (mostly the lib-level tests using the 1-arg `make_non_owning` factory).
 * See file preamble for the algorithm summary; see git history pre-Beat-C
 * for the exhaustive step-by-step rationale.
 *
 * Built strictly on top of `lib_add_mod_dsl` + per-bit XOR copies per
 * the PRD §4 layering rule.
 *
 * @pre `a, b ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the call is a no-op).
 *      `r_bits` must enter in |0>.  `a_bits` and `b_bits` may alias.
 */
template <typename Bit>
inline void lib_mul_mod_dsl_chain(Bit* a_bits, Bit* b_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 14u;
    assert(n <= kMaxN && "lib_mul_mod_dsl_chain: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_mul_mod_dsl_chain: no BackendContext installed");
    (void)raw;

    // (1) Allocate shifted_chain[0..W-1], each W bits.
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

    // shifted_chain[0] := a (XOR copy).
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[0][j] ^= a_bits[j];

    // shifted_chain[i] := (2 · shifted_chain[i-1]) mod n via lib_add_mod_dsl.
    for (std::size_t i = 1u; i < n; ++i) {
        lib_add_mod_dsl(shifted_bits[i - 1u], shifted_bits[i - 1u],
                        n_bits, n, shifted_bits[i]);
    }

    // (2) Allocate r_chain[1..W], each W bits.
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

    // (2a) r_chain[1] := b[0] ? shifted_chain[0] : 0.
    sturm::lift_under(b_bits[0], [&]() {
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[1][j] ^= shifted_bits[0][j];
    });

    // (2b) i = 1..W-1: r_chain[i+1] := b[i] ? (r_chain[i] + shifted_chain[i])
    //                                       : r_chain[i].
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

    // (3) Write the result into r_bits via per-bit XOR.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= r_chain_bits[n][j];

    // (4) Uncompute r_chain[1..W] in reverse — gate-reverse of step 2.
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

    // (4a) Reverse the i=0 XOR-copy (self-inverse).
    sturm::lift_under(b_bits[0], [&]() {
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[1][j] ^= shifted_bits[0][j];
    });

    // (5) Release r_chain[1..W] LIFO.
    for (std::size_t i = n; i >= 1u; --i) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(r_chain_idx[i][j]);
    }

    // (6) Uncompute shifted_chain in reverse.
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;
        __lib_add_mod_dsl_adj(shifted_bits[i - 1u], shifted_bits[i - 1u],
                              n_bits, n, shifted_bits[i]);
    }
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[0][j] ^= a_bits[j];

    // (7) Release shifted_chain LIFO.
    for (std::size_t i = n; i-- > 0u;) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(shifted_idx[i][j]);
    }
}

// ── lib_mul_mod_dsl (public dispatcher) ──────────────────────────────────────

/**
 * @brief Out-of-place W-bit modular multiplication primitive:
 *        `r_bits = (a_bits * b_bits) mod n_bits`.
 *
 * Public dispatcher for the two implementation paths after sturm-7cix
 * Beat C: when `n_bits[0]`'s classical-tracked LSB is 1 (super_mask=0),
 * route to `lib_mul_mod_dsl_oneshot` (O(W) ancilla via Beat A's
 * in-place add-mod + Beat B's in-place doubling); otherwise fall back
 * to `lib_mul_mod_dsl_chain` (O(W^2) chain), which works for any modulus.
 *
 * @param a_bits Left factor register (W qubits, read but restored).
 * @param b_bits Right factor register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width (NOT the modulus value).  `n == 0`
 *               short-circuits.
 * @param r_bits Result register (W qubits).  Must start in |0>.
 *               On exit, holds `(a * b) mod n_value`.
 *
 * @pre `a, b ∈ [0, n_value)` and `n_value ≥ 1` (when `n == 0`, the call
 *      is a no-op).  `r_bits` must enter in |0>.  `a_bits`, `b_bits`,
 *      `n_bits`, and `r_bits` must refer to physically distinct qubit
 *      registers, except `a_bits` and `b_bits` may alias (squaring case).
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_mul_mod_dsl` with operands outside `[0, n_value)` is **undefined
 * behavior**.
 *
 * @sa __lib_mul_mod_dsl_adj, lib_mul_mod_dsl_oneshot,
 *     lib_mul_mod_dsl_chain, sturm::mul_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_mul_mod_dsl(Bit* a_bits, Bit* b_bits,
                            Bit* n_bits, std::size_t n,
                            Bit* r_bits) {
    if (n == 0u) return;

    if (detail_mul_mod::is_classical_odd_n_hint(n_bits[0])) {
        lib_mul_mod_dsl_oneshot(a_bits, b_bits, n_bits, n, r_bits);
    } else {
        lib_mul_mod_dsl_chain(a_bits, b_bits, n_bits, n, r_bits);
    }
}

} // namespace sturm

#include "sturm/detail/lib/mul_mod_dsl_adj.hpp"
