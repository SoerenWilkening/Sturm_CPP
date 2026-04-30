// mul_mod_dsl.hpp -- P2 (sturm-kubb.2) lib_mul_mod_dsl forward primitive.
//
// Out-of-place modular multiplication: r = (a * b) mod n, all unsigned, all
// W bits wide.  Built strictly on top of `lib_add_mod_inplace_dsl` (Beat A,
// sturm-8lnp) and `lib_double_mod_dsl` (Beat B, sturm-wdas) via the
// O(W)-ancilla `lib_mul_mod_dsl_oneshot` helper — no direct gate emission,
// no new arithmetic kernel.  PRD §4 layering rule.
//
// Single-path implementation (sturm-4oot.5):
//
//   `lib_mul_mod_dsl` is a thin wrapper that delegates unconditionally to
//   `lib_mul_mod_dsl_oneshot`.  No runtime dispatch on n's parity; the
//   oneshot helper became parity-agnostic in sturm-4oot.4 once the inner
//   `lib_double_mod_dsl` had been rewritten to externalise its `< n`
//   witness (sturm-4oot.1) and the (W-1)-bit `lt_flags` register had been
//   threaded through this layer (sturm-4oot.3).
//
//   Pre-fix history (sturm-7cix Beat C, now removed): a runtime dispatcher
//   inspected `n_bits[0]`'s classical hint and routed odd-n callers to the
//   O(W) oneshot path while falling back to a chain-style O(W²) helper
//   (`lib_mul_mod_dsl_chain`) for even n or unknown-parity cases.  After
//   sturm-4oot.4 the chain helper became dead code; sturm-4oot.5 removed
//   both the dispatcher and the chain helper.  Consult git history at
//   commit 644a5f8 ("rewrite lib_double_mod_dsl + adjoint with lt_flag_out")
//   or earlier for the chain narrative if needed.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// add_mod_inplace_dsl.hpp / add_mod_inplace_dsl_adj.hpp pairing pattern).

#pragma once

#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"

#include <cstddef>

namespace sturm {

// ── lib_mul_mod_dsl (single-path wrapper) ────────────────────────────────────

/**
 * @brief Out-of-place W-bit modular multiplication primitive:
 *        `r_bits = (a_bits * b_bits) mod n_bits`.
 *
 * Single-path implementation (sturm-4oot.5): delegates unconditionally to
 * `lib_mul_mod_dsl_oneshot`, the O(W)-ancilla helper built on
 * `lib_add_mod_inplace_dsl` + `lib_double_mod_dsl`.  Parity-agnostic in
 * `n_value` since sturm-4oot.4.
 *
 * @param a_bits Left factor register (W qubits, read but restored).
 * @param b_bits Right factor register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width (NOT the modulus value).  `n == 0`
 *               short-circuits (handled inside the oneshot helper).
 * @param r_bits Result register (W qubits).  Must start in |0>.
 *               On exit, holds `(a * b) mod n_value`.
 *
 * @pre `a, b ∈ [0, n_value)` and `n_value ≥ 1` (when `n == 0`, the call
 *      is a no-op).  `r_bits` must enter in |0>.  `a_bits`, `n_bits`,
 *      and `r_bits` must refer to physically distinct qubit registers;
 *      `a_bits` and `b_bits` may alias (squaring case used by
 *      `lib_pow_mod_dsl`).
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_mul_mod_dsl` with operands outside `[0, n_value)` is **undefined
 * behavior**.
 *
 * @sa __lib_mul_mod_dsl_adj, lib_mul_mod_dsl_oneshot, sturm::mul_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_mul_mod_dsl(Bit* a_bits, Bit* b_bits,
                            Bit* n_bits, std::size_t n,
                            Bit* r_bits) {
    lib_mul_mod_dsl_oneshot(a_bits, b_bits, n_bits, n, r_bits);
}

} // namespace sturm

#include "sturm/detail/lib/mul_mod_dsl_adj.hpp"
