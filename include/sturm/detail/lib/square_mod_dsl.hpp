// square_mod_dsl.hpp -- Beat D-B (sturm-3sfl.2) lib_square_mod_dsl forward
//                         primitive: in-place modular squaring.
//
// In-place modular squaring: x := (x * x) mod n, all unsigned, where x is a
// W-bit register.  Built strictly on top of `lib_mul_mod_dsl_oneshot`
// (Beat C, sturm-7cix; parity-agnostic post sturm-4oot.4) plus per-bit
// XOR copies and a CNOT-based register swap (no direct gate emission, no
// new arithmetic kernel — see PRD §4 layering rule).
//
// Why a separate primitive?  Squaring is the inner loop of
// `lib_pow_mod_dsl` (Beat D-C, sturm-3sfl.3); each pow_mod step does
// `base := base^2 mod n`.  This is NOT
// `lib_mul_mod_inplace_dsl(x, x, n, ...)` — squaring is fundamentally
// **non-injective** over [0, n) for n > 1 (x and n−x both map to x^2),
// so an in-place square cannot be reversible without exporting a witness
// that distinguishes the two pre-images.  See `docs/design_even_n_double_mod.md`
// §6 for the design analysis (squaring's "lt_flag-equivalent" output).
//
// Strategy (a) per the issue brief: "out-of-place compute via
// `lib_mul_mod_dsl_oneshot` (which permits a == b aliasing for the
// squaring case), swap into the in-place register, then XOR-uncopy the
// scratch using the caller-owned witness register."  Total internal
// qubit cost ≈ W (the scratch r_reg) on top of the inner oneshot's
// 3W + 7 peak; net peak ≈ 4W + 7 above the W-bit caller-owned
// `x_copy_out_bits` register.  Strategy (b) (a dedicated reversible
// squaring kernel — HRS schoolbook specialised for a==b) was deferred
// per the brief's recommendation to "Pick (a) for V1 unless (b) is
// clearly cheaper".
//
// `x_copy_out_bits` contract (the squaring's endemic non-injectivity
// witness):
//   The forward XOR-loads the caller-owned `x_copy_out_bits` register
//   with `x_orig` (so callers can pre-zero it for a clean write or
//   accumulate into a pre-existing bit-vector).  This W-bit register is
//   the witness that distinguishes the two pre-images of `x^2 mod n`
//   when `n_value > 1` (the map x → x^2 mod n collapses x and n − x to
//   the same image), and is therefore necessary for reversibility of
//   the in-place squaring.  The witness is "x_orig itself" rather than
//   a more compact 1-bit "(x < n/2)" because for non-prime moduli the
//   square root can have more than two values (e.g. for n = 8,
//   sqrt(1) = {1, 3, 5, 7}), so a 1-bit witness is insufficient in
//   general.  The full W-bit copy is the simplest faithful witness and
//   matches the "expose comparison/borrow bits at the enclosing scope"
//   convention surveyed in `docs/design_even_n_double_mod.md` §App.
//
// Inherited lt_flags from inner `lib_mul_mod_dsl_oneshot` (per design
// doc §6 source #1):
//   The inner oneshot calls (forward and adjoint) each allocate, use,
//   and release their own (W-1)-bit `lt_flags` register; these lt_flags
//   are entirely managed inside the oneshot primitive and do NOT escape
//   to this layer.  We rely on the inner forward+adjoint pair to keep
//   the lt_flags balanced (each oneshot teardown returns its slots to
//   |0>).  This is the "tear them down inside the call" option from
//   §6's (a)/(b) choice: we pay the inner adjoint cost twice (once for
//   the forward squaring's oneshot teardown, once for the squaring's
//   own adjoint when paired by the caller), accepting the constant
//   overhead in exchange for not needing a new "no-tear-down" oneshot
//   variant.  pow_mod (Beat D-C) can layer its own batching on top of
//   this primitive without observing internal lt_flags.
//
// Endemic comparison bits from the squaring's own reduction (per
// design doc §6 source #2): strategy (a) inherits Beat C's reduction
// wholesale via the oneshot call, so there are NO new endemic bits at
// this layer beyond `x_copy_out_bits`.
//
// Adjoint precondition (mirrors Beat B / sturm-wdas):
//   `__lib_square_mod_dsl_adj` is the gate-reverse of the forward; it
//   is NOT a literal "modular square root" operation (squaring is
//   2-to-1 for n > 1, so no algebraic inverse exists in general).  The
//   adjoint is correct ONLY when paired with its forward call: given
//   `x_bits = (x_orig^2) mod n` and `x_copy_out_bits` = the value the
//   paired forward wrote (= x_orig), the adjoint runs the gate-reverse
//   and restores `x_bits = x_orig` with `x_copy_out_bits = 0` on exit.
//   See `__lib_square_mod_dsl_adj` in the sibling square_mod_dsl_adj.hpp
//   header for the documented precondition and the per-step gate-reverse
//   trace.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// double_mod_dsl.hpp / double_mod_dsl_adj.hpp pairing pattern).
//
// Target: <=300 LoC.

#pragma once

#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace sturm {

namespace detail_square_mod {

// Bit-view helper mirroring detail_mul_mod_oneshot::make_ancilla_view,
// kept local so square_mod_dsl can be included without dragging the
// oneshot helper namespace into headers that only need the squaring
// form.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_square_mod

/**
 * @brief In-place W-bit modular squaring primitive: `x_bits := (x * x) mod n`,
 *        with the original `x` exported via `x_copy_out_bits` as the
 *        non-injectivity witness.
 *
 * This is the lib-level primitive used by Beat D-C (sturm-3sfl.3) to
 * rewrite `lib_pow_mod_dsl` onto an O(W)-ancilla square-and-multiply
 * structure.  Built strictly on top of `lib_mul_mod_dsl_oneshot` per
 * the PRD §4 layering rule — emits no gates of its own (only the
 * oneshot primitive plus per-bit XOR copies and a CNOT-based register
 * swap).  See the header preamble for the full algorithm.
 *
 * @param x_bits           Input/output register of W qubits.  On entry
 *                         holds `x_orig` in [0, n_value).  On exit
 *                         holds `(x_orig * x_orig) mod n_value` in
 *                         [0, n_value).
 * @param n_bits           Modulus register (W qubits, read but
 *                         restored).  Encodes `n_value` in [1, 2^W).
 * @param n                Register width (NOT the modulus value;
 *                         the modulus is encoded in `n_bits[0..n-1]`).
 *                         `n == 0` short-circuits to a no-op.
 * @param x_copy_out_bits  Caller-owned W-qubit register.  The forward
 *                         XOR-loads `x_orig` into this register
 *                         (`x_copy_out_bits[i] ^= x_bits[i]` for i in
 *                         0..W-1), so callers can pre-zero (clean
 *                         write) or accumulate into an existing
 *                         bit-vector.  This register is the
 *                         non-injectivity witness for the squaring map
 *                         and must be preserved by the caller until the
 *                         paired `__lib_square_mod_dsl_adj` consumes
 *                         it.
 *
 * @pre `x ∈ [0, n_value)` and `n_value ≥ 1` (when `n == 0`, the call is
 *      a no-op).  `x_bits`, `n_bits`, and `x_copy_out_bits` must refer
 *      to physically distinct qubit registers.  No parity restriction
 *      on `n_value`: the inner `lib_mul_mod_dsl_oneshot` is
 *      parity-agnostic (post sturm-4oot.4) and so is this primitive.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_square_mod_dsl` with `x` outside `[0, n_value)` or with aliased
 * registers is **undefined behavior** — the routine still emits a
 * well-formed gate sequence, but `x_bits` is not the mathematical
 * answer and the input registers may not be restored.  This matches
 * the trust model of the sibling Beat B / Beat C primitives.
 *
 * @sa __lib_square_mod_dsl_adj, lib_mul_mod_dsl_oneshot,
 *     lib_double_mod_dsl
 * @see PRD §5 (Trust model and precondition contract).
 * @see docs/design_even_n_double_mod.md §6 (Beat D-B implications).
 */
template <typename Bit>
inline void lib_square_mod_dsl(Bit* x_bits, Bit* n_bits, std::size_t n,
                               Bit* x_copy_out_bits) {
    if (n == 0u) return;

    // sturm-8n73: kMaxN bumped to 64 for circuit-generation use cases.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN && "lib_square_mod_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_square_mod_dsl: no BackendContext installed");
    (void)raw;

    // (1) XOR-load x_orig into the caller-owned x_copy_out_bits register.
    //     After this step: x_copy_out_bits = x_copy_out_bits_pre XOR
    //     x_orig.  When the caller pre-zeros, x_copy_out_bits becomes
    //     a faithful copy of x_orig — the non-injectivity witness held
    //     across to the matched adjoint call.
    for (std::size_t i = 0u; i < n; ++i)
        x_copy_out_bits[i] ^= x_bits[i];

    // (2) Allocate scratch r_reg[W] (all |0>).  This holds the squaring
    //     result before it is swapped into x_bits.
    int   r_idx[kMaxN];
    qbool r_own[kMaxN];
    Bit   r_bits[kMaxN];
    for (std::size_t j = 0u; j < n; ++j) {
        r_idx[j]  = QubitPool::instance().allocate();
        r_own[j]  = qbool::make_non_owning(r_idx[j]);
        r_bits[j] =
            detail_square_mod::make_ancilla_view<Bit>(r_own[j]);
    }

    // (3) Out-of-place modular multiplication using x_bits as both
    //     factors via lib_mul_mod_dsl_oneshot's documented a==b aliasing
    //     allowance (see mul_mod_dsl_oneshot.hpp's "Aliasing" section
    //     and the regression-pin test in test_mul_mod_dsl_oneshot.cpp).
    //     After this call: r_bits = (x_orig * x_orig) mod n_value;
    //     x_bits and x_copy_out_bits are preserved.  The inner oneshot
    //     manages its own (W-1)-bit lt_flags register entirely
    //     internally — see preamble for the choice rationale.
    lib_mul_mod_dsl_oneshot(x_bits, x_bits, n_bits, n, r_bits);

    // (4) Swap x_bits ↔ r_bits via a chain of W per-bit XOR-triplets
    //     (each triplet is a SWAP = three CNOTs `a^=b; b^=a; a^=b`).
    //     After: x_bits[i] = r_bits[i]_pre = (x_orig^2) mod n;
    //            r_bits[i] = x_bits[i]_pre = x_orig.
    for (std::size_t i = 0u; i < n; ++i) {
        x_bits[i] ^= r_bits[i];
        r_bits[i] ^= x_bits[i];
        x_bits[i] ^= r_bits[i];
    }

    // (5) XOR-uncopy r_bits using x_copy_out_bits.  Both registers now
    //     hold x_orig (r_bits was just swapped in, x_copy_out_bits was
    //     XOR-loaded in step 1), so r_bits[i] ^= x_copy_out_bits[i]
    //     returns r_bits to |0> in every branch.  This is the same
    //     "XOR-uncopy via the witness register" idiom Beat C uses in
    //     mul_mod_dsl_oneshot's step (7) for the shifted_reg cleanup,
    //     adapted to the swapped-out scratch.
    for (std::size_t i = 0u; i < n; ++i)
        r_bits[i] ^= x_copy_out_bits[i];

    // (6) Release the scratch r_reg LIFO.  All slots are |0> after
    //     step (5) consumed them via the witness.
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(r_idx[j]);
}

} // namespace sturm

#include "sturm/detail/lib/square_mod_dsl_adj.hpp"
