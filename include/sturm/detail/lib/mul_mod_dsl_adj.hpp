// mul_mod_dsl_adj.hpp -- P2 (sturm-kubb.5) lib_mul_mod_dsl adjoint sibling.
//
// `__lib_mul_mod_dsl_adj` is the gate-reverse of `lib_mul_mod_dsl`: starting
// from `r = (a * b) mod n` (with the original `a, b, n` preserved), it
// returns `r` to |0> while leaving `a, b, n` unchanged.
//
// Single-path implementation (sturm-4oot.5): delegates unconditionally to
// `__lib_mul_mod_dsl_oneshot_adj`, mirroring the forward
// (`mul_mod_dsl.hpp`).  Pre-fix history — the symmetric chain-adjoint
// dispatch and `__lib_mul_mod_dsl_chain_adj` helper were removed once
// sturm-4oot.4 made the oneshot path parity-agnostic; see git history
// commit 644a5f8 or earlier for the chain narrative.
//
// STURM_REGISTER_ADJOINT at the bottom hooks invert<&lib_mul_mod_dsl<
// BitProxy>>() to this adjoint, mirroring add_mod_dsl_adj.hpp.

#pragma once

#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"  // pulls oneshot adj
#include "sturm/routines/invert.hpp"

#include <cstddef>

// Forward-declare BitProxy for the LO-1b adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

// ── __lib_mul_mod_dsl_adj (single-path wrapper) ──────────────────────────────

/**
 * @brief Gate-reverse adjoint of @ref lib_mul_mod_dsl.
 *
 * Single-path wrapper (sturm-4oot.5): delegates unconditionally to
 * `__lib_mul_mod_dsl_oneshot_adj`.
 *
 * @pre `r_bits` must enter holding `(a * b) mod n_value` (the paired
 *      forward's output); exits |0>.  `a, b, n` preserved across call.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `__lib_mul_mod_dsl_adj` with `r_bits` not equal to the paired
 * forward's output is **undefined behavior**.
 *
 * @sa lib_mul_mod_dsl, __lib_mul_mod_dsl_oneshot_adj, sturm::mul_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void __lib_mul_mod_dsl_adj(Bit* a_bits, Bit* b_bits,
                                  Bit* n_bits, std::size_t n,
                                  Bit* r_bits) {
    __lib_mul_mod_dsl_oneshot_adj(a_bits, b_bits, n_bits, n, r_bits);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_mul_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_mul_mod_dsl_adj<sturm::BitProxy>)
#endif
