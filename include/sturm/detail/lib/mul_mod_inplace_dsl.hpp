// mul_mod_inplace_dsl.hpp -- Beat D-A (sturm-3sfl.1) lib_mul_mod_inplace_dsl
//                              forward primitive: in-place modular
//                              multiplication.
//
// In-place modular multiplication: dest := (dest * a) mod n, all unsigned,
// where dest is a W-bit register holding `dest_old in [0, n_value)` and
// `a_bits` is a W-bit factor register also holding a value in [0, n_value).
// Built strictly on top of `lib_mul_mod_dsl_oneshot` (Beat C, sturm-7cix;
// parity-agnostic post sturm-4oot.4) plus per-bit XOR copies and a
// CNOT-based register swap (no direct gate emission, no new arithmetic
// kernel — see PRD §4 layering rule).
//
// Why a separate primitive?  Beat D-C (sturm-3sfl.3) rewrites
// `lib_pow_mod_dsl` onto `acc := (acc * sq) mod n` (one in-place
// multiplication per exponent bit) plus `sq := sq^2 mod n` (handled by
// Beat D-B / lib_square_mod_dsl).  An out-of-place multiplication
// requires an extra W-bit copy slot per chain step; the in-place form
// keeps the running ancilla footprint to O(W).
//
// Why a witness register (`dest_copy_out_bits`)?  The map
// `dest -> (a * dest) mod n` is bijective on [0, n_value) **only when**
// `gcd(a, n_value) = 1`.  When `a` and `n` share a factor (e.g. n=4,
// a=2: 0->0, 1->2, 2->0, 3->2) the map is non-injective, so an in-place
// modular multiplication is **not unitary on `dest_bits` alone**.
// Reversibility therefore requires exporting a witness register that
// distinguishes pre-images of the same image.  The issue brief
// (sturm-3sfl.1, design block §1) outlined a swap+oneshot-adjoint
// scheme without a witness; that scheme is correct only when an
// inverse `a^{-1}` exists (Beauregard 2003 c-MUL pattern, which needs
// a second oneshot call with `a^{-1}` as the multiplier).  In our
// setting `a` is passed as a quantum register pointer, and the only
// available primitive (`lib_mul_mod_dsl_oneshot`) takes a single
// multiplier — so a Beauregard-style `a^{-1}` round-trip would require
// either an extra `a_inv_bits` argument or a precondition
// `gcd(a, n_value) = 1`.  This implementation chose the witness route
// (option B) instead, mirroring Beat D-B's `lib_square_mod_dsl` pattern:
// the W-bit `dest_copy_out_bits` register XOR-loads `dest_old`, the
// out-of-place oneshot writes `(a * dest_old) mod n` into a scratch
// `r_reg`, dest is swapped with `r_reg`, and `r_reg` is XOR-uncopied
// against `dest_copy_out_bits` to clear it.  See `square_mod_dsl.hpp`'s
// preamble for the analogous design rationale on the squaring side
// and `docs/design_even_n_double_mod.md` §6 for the broader Beat D
// guidance.  Net peak: oneshot's ~3W+7 plus the local W-bit `r_reg`,
// total ~4W+7 internal qubits above the 3W caller-owned registers
// (a, n, dest) — same order of cost as Beat D-B.
//
// `dest_copy_out_bits` contract (the in-place mul's reversibility
// witness):
//   The forward XOR-loads `dest_old` into the caller-owned
//   `dest_copy_out_bits` register, so callers can pre-zero (clean
//   write) or accumulate into a pre-existing bit-vector.  This W-bit
//   register is the witness held across to the matched adjoint call;
//   the adjoint XOR-uncopies it back to its pre-forward state.  The
//   semantic mirrors sturm-3sfl.2's `x_copy_out_bits` for the
//   squaring's non-injectivity witness; both follow the convention
//   surveyed in `docs/design_even_n_double_mod.md` §App ("expose
//   comparison/borrow bits at the enclosing scope").
//
// Inherited lt_flags from inner `lib_mul_mod_dsl_oneshot` (per design
// doc §6 source #1):
//   The inner oneshot calls (forward and the matching adjoint in the
//   sibling `__lib_mul_mod_inplace_dsl_adj`) each allocate, use, and
//   release their own (W-1)-bit `lt_flags` register; these lt_flags
//   are entirely managed inside the oneshot primitive and do NOT
//   escape to this layer.  This is the "tear them down inside the
//   call" option from §6's (a)/(b) choice: we pay the inner adjoint
//   cost twice (once for the forward in-place mul's oneshot teardown,
//   once for the in-place mul's own adjoint when paired by the
//   caller), accepting the constant overhead in exchange for not
//   needing a new "no-tear-down" oneshot variant — exposing the
//   lt_flags as an output here would violate PRD §4 'no new
//   arithmetic kernel beyond combining existing primitives'.  Beat
//   D-C (sturm-3sfl.3) layers its own batching on top of this
//   primitive without observing internal lt_flags.
//
// Squaring case forbidden:
//   The aliased call `lib_mul_mod_inplace_dsl(x, x, n, n, x_copy)`
//   (i.e. `a_bits == dest_bits`) is **NOT supported** by this
//   primitive.  In-place squaring is the dedicated job of
//   `lib_square_mod_dsl` (Beat D-B / sturm-3sfl.2), which uses
//   `lib_mul_mod_dsl_oneshot`'s documented `a == b` aliasing
//   allowance to compute `x^2 mod n` directly into the scratch.
//   Routing through this primitive's `a_bits == dest_bits` would
//   double-XOR the `dest_old` factor onto itself in the inner
//   oneshot's shifted-register XOR-copy step (oneshot.hpp step 2)
//   before any value swap, leaving the scratch holding zero rather
//   than `x^2 mod n`.  Callers must use `lib_square_mod_dsl` for
//   the squaring case.  No runtime check is added (matches the
//   trust model of the sibling Beat A / Beat B / Beat C
//   primitives — see PRD §5).
//
// Adjoint precondition (mirrors Beat D-B / sturm-3sfl.2 and the
// sturm-wdas Beat B doubling adjoint precondition pattern):
//   `__lib_mul_mod_inplace_dsl_adj` is the gate-reverse of the
//   forward; it is NOT a literal "modular division by a" operation
//   (the forward map is non-injective for `gcd(a, n) > 1`, so no
//   algebraic inverse exists in general).  The adjoint is correct
//   ONLY when paired with its forward call: given `dest_bits =
//   (a * dest_old) mod n` and `dest_copy_out_bits` = the value the
//   paired forward wrote (= `dest_old` XOR-into the caller's
//   pre-state), the adjoint runs the gate-reverse and restores
//   `dest_bits = dest_old` with `dest_copy_out_bits` consumed back
//   to its pre-forward state on exit.  See
//   `__lib_mul_mod_inplace_dsl_adj` in the sibling
//   mul_mod_inplace_dsl_adj.hpp header for the documented
//   precondition and the per-step gate-reverse trace.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// add_mod_inplace_dsl.hpp / square_mod_dsl.hpp pairing pattern).
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

namespace detail_mul_mod_inplace {

// Bit-view helper mirroring detail_square_mod::make_ancilla_view, kept
// local so this header can be included independently of the squaring
// primitive's helper namespace.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_mul_mod_inplace

/**
 * @brief In-place W-bit modular multiplication primitive:
 *        `dest_bits := (a * dest) mod n`, with the original `dest`
 *        exported via `dest_copy_out_bits` as the non-injectivity
 *        witness.
 *
 * This is the lib-level primitive used by Beat D-C (sturm-3sfl.3) to
 * rewrite `lib_pow_mod_dsl` onto an O(W)-ancilla square-and-multiply
 * structure.  Built strictly on top of `lib_mul_mod_dsl_oneshot` per
 * the PRD §4 layering rule — emits no gates of its own (only the
 * oneshot primitive plus per-bit XOR copies and a CNOT-based register
 * swap).  See the header preamble for the full algorithm and the
 * witness-register design rationale.
 *
 * @param a_bits           Factor register (W qubits, read but
 *                         restored).  Encodes `a` in [0, n_value).
 * @param dest_bits        Input/output register of W qubits.  On
 *                         entry holds `dest_old` in [0, n_value); on
 *                         exit holds `(a * dest_old) mod n_value` in
 *                         [0, n_value).
 * @param n_bits           Modulus register (W qubits, read but
 *                         restored).  Encodes `n_value` in
 *                         [1, 2^W).
 * @param n                Register width (NOT the modulus value).
 *                         `n == 0` short-circuits to a no-op.
 * @param dest_copy_out_bits
 *                         Caller-owned W-qubit register.  The
 *                         forward XOR-loads `dest_old` into this
 *                         register (`dest_copy_out_bits[i] ^=
 *                         dest_bits[i]` for i in 0..W-1), so callers
 *                         can pre-zero (clean write) or accumulate
 *                         into an existing bit-vector.  This
 *                         register is the non-injectivity witness
 *                         (when `gcd(a, n_value) > 1` the map
 *                         `dest -> a*dest mod n` collapses multiple
 *                         pre-images to the same image) and must be
 *                         preserved by the caller until the paired
 *                         `__lib_mul_mod_inplace_dsl_adj` consumes
 *                         it.
 *
 * @pre `a, dest_old in [0, n_value)` and `n_value >= 1` (when
 *      `n == 0`, the call is a no-op).  `a_bits`, `dest_bits`,
 *      `n_bits`, and `dest_copy_out_bits` must refer to physically
 *      **distinct** qubit registers — in particular, `a_bits ==
 *      dest_bits` (the squaring case) is **forbidden**; callers
 *      must use `lib_square_mod_dsl` (Beat D-B) for that case.
 *      No parity restriction on `n_value`: the inner
 *      `lib_mul_mod_dsl_oneshot` is parity-agnostic (post
 *      sturm-4oot.4) and so is this primitive.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_mul_mod_inplace_dsl` with operands outside `[0, n_value)`,
 * with `a_bits == dest_bits`, or with otherwise aliased registers
 * is **undefined behavior** — the routine still emits a well-formed
 * gate sequence, but `dest_bits` is not the mathematical answer
 * and the input registers may not be restored.  This matches the
 * trust model of the sibling Beat A / Beat B / Beat C / Beat D-B
 * primitives.
 *
 * @sa __lib_mul_mod_inplace_dsl_adj, lib_mul_mod_dsl_oneshot,
 *     lib_square_mod_dsl, lib_add_mod_inplace_dsl
 * @see PRD §5 (Trust model and precondition contract).
 * @see docs/design_even_n_double_mod.md §6 (Beat D implications).
 */
template <typename Bit>
inline void lib_mul_mod_inplace_dsl(Bit* a_bits, Bit* dest_bits,
                                    Bit* n_bits, std::size_t n,
                                    Bit* dest_copy_out_bits) {
    if (n == 0u) return;

    // sturm-8n73: kMaxN bumped to 64 for circuit-generation use cases.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN && "lib_mul_mod_inplace_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_mul_mod_inplace_dsl: no BackendContext installed");
    (void)raw;

    // (1) XOR-load dest_old into the caller-owned dest_copy_out_bits
    //     register.  After this step: dest_copy_out_bits =
    //     dest_copy_out_bits_pre XOR dest_old.  When the caller pre-
    //     zeros, dest_copy_out_bits becomes a faithful copy of
    //     dest_old — the non-injectivity witness held across to the
    //     matched adjoint call.
    for (std::size_t i = 0u; i < n; ++i)
        dest_copy_out_bits[i] ^= dest_bits[i];

    // (2) Allocate scratch r_reg[W] (all |0>).  This holds the
    //     out-of-place product before it is swapped into dest_bits.
    int   r_idx[kMaxN];
    qbool r_own[kMaxN];
    Bit   r_bits[kMaxN];
    for (std::size_t j = 0u; j < n; ++j) {
        r_idx[j]  = QubitPool::instance().allocate();
        r_own[j]  = qbool::make_non_owning(r_idx[j]);
        r_bits[j] =
            detail_mul_mod_inplace::make_ancilla_view<Bit>(r_own[j]);
    }

    // (3) Out-of-place modular multiplication via lib_mul_mod_dsl_oneshot.
    //     After this call: r_bits = (a * dest_old) mod n_value;
    //     a_bits, dest_bits, dest_copy_out_bits, n_bits are preserved.
    //     The inner oneshot manages its own (W-1)-bit lt_flags
    //     register entirely internally — see preamble for the choice
    //     rationale.
    lib_mul_mod_dsl_oneshot(a_bits, dest_bits, n_bits, n, r_bits);

    // (4) Swap dest_bits ↔ r_bits via a chain of W per-bit XOR-triplets
    //     (each triplet is a SWAP = three CNOTs `a^=b; b^=a; a^=b`).
    //     After: dest_bits[i] = r_bits[i]_pre = (a * dest_old) mod n;
    //            r_bits[i]    = dest_bits[i]_pre = dest_old.
    for (std::size_t i = 0u; i < n; ++i) {
        dest_bits[i] ^= r_bits[i];
        r_bits[i]    ^= dest_bits[i];
        dest_bits[i] ^= r_bits[i];
    }

    // (5) XOR-uncopy r_bits using dest_copy_out_bits.  Both registers
    //     now hold dest_old (r_bits was just swapped in,
    //     dest_copy_out_bits was XOR-loaded in step 1), so
    //     r_bits[i] ^= dest_copy_out_bits[i] returns r_bits to |0>
    //     in every branch.  This is the same "XOR-uncopy via the
    //     witness register" idiom Beat D-B's lib_square_mod_dsl uses
    //     for r_reg cleanup, adapted to the in-place mul case.
    for (std::size_t i = 0u; i < n; ++i)
        r_bits[i] ^= dest_copy_out_bits[i];

    // (6) Release the scratch r_reg LIFO.  All slots are |0> after
    //     step (5) consumed them via the witness.
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(r_idx[j]);
}

} // namespace sturm

#include "sturm/detail/lib/mul_mod_inplace_dsl_adj.hpp"
