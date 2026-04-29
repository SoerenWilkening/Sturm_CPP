// mul_mod_dsl_oneshot.hpp -- sturm-7cix Beat C: O(W) lib_mul_mod_dsl helper
//                              ("oneshot" = single-shifted-register, single
//                              accumulator; replaces the chain-style W^2 ancilla
//                              footprint of mul_mod_dsl.hpp).
//
// Out-of-place modular multiplication: r = (a * b) mod n, all unsigned, all
// W bits wide.  Built strictly on top of `lib_add_mod_inplace_dsl` (Beat A,
// sturm-8lnp) + `lib_double_mod_dsl` (Beat B, sturm-wdas) + per-bit XOR
// copies (no direct gate emission, no new arithmetic kernel — see PRD §4
// layering rule).
//
// Algorithm — single-shifted-register / single-accumulator (issue brief):
//
//   inputs : a_bits[W], b_bits[W], n_bits[W], r_bits[W]   (r = |0>)
//   precond: a, b in [0, n_value), n_value odd, n_value >= 1
//   output : r_bits = (a * b) mod n_value
//
//   1. Allocate shifted_reg[0..W] (W+1 qubits).  All start in |0>.
//   2. shifted_reg[0..W-1] ^= a_bits[0..W-1]  (XOR-copy of a; bit W stays |0>)
//      Now shifted_reg holds a in [0, n) with overflow slot at bit W = |0>.
//   3. Allocate acc_reg[0..W] (W+1 qubits).  All in |0>.  Bits 0..W-1 = the
//      running accumulator value `acc` in [0, n_value); bit W is the overflow
//      slot expected by lib_add_mod_inplace_dsl.
//   3b. Allocate lt_flags[0..W-2] (W-1 qubits, all |0>).  One bit per inner
//       doubling, held across the forward pass and the reverse uncompute
//       pass to feed each `__lib_double_mod_dsl_adj`.  Pool allocation
//       guarantees the |0> entry condition; see precondition note below.
//   4. for i = 0..W-1:
//        4a. lift_under(b_bits[i]) {
//              lib_add_mod_inplace_dsl(shifted_reg, acc_reg, n_bits, W);
//            }
//            // controlled on b[i]: acc := (acc + shifted) mod n
//            // shifted_reg's bit W must be |0> at entry; lib_add_mod_inplace_dsl
//            // restores it.  acc_reg's bit W is the (W+1)-bit dest's overflow
//            // slot per Beat A's contract.
//        4b. if i < W-1:
//              lib_double_mod_dsl(shifted_reg, n_bits, W, lt_flags[i]);
//              // shifted := (2 * shifted) mod n; the witness
//              //   `(2·shifted_orig < n_value)` is XOR-written into
//              //   lt_flags[i] and held across to the matching adjoint in
//              //   step (6a).  As of sturm-4oot.1 the inner doubling is
//              //   parity-agnostic.
//   5. for j in 0..W-1: r_bits[j] ^= acc_reg[j]      (write the answer)
//
//   6. Uncompute (gate-reverse of step 4):
//      for i = W-1..0:
//        6a. if i < W-1:
//              __lib_double_mod_dsl_adj(shifted_reg, n_bits, W, lt_flags[i]);
//              // consumes lt_flags[i] back to |0>.
//        6b. lift_under(b_bits[i]) {
//              __lib_add_mod_inplace_dsl_adj(shifted_reg, acc_reg, n_bits, W);
//            }
//      After the loop: shifted_reg = a, acc_reg = |0>, every lt_flags[i]
//      = |0> (consumed in reverse order by the matched adjoint calls).
//   6c. Release lt_flags LIFO.
//   7. shifted_reg[0..W-1] ^= a_bits[0..W-1]   (XOR-uncopy; zeros shifted_reg)
//   8. Release acc_reg, then shifted_reg LIFO.
//
// Total internal qubits at peak (sturm-4oot.3) ≈ (W+1) [shifted] + (W+1)
// [acc] + (W−1) [lt_flags] + 5 [lib_add_mod_inplace_dsl interior peak]
// + 1 [lift_under(b[i]) AND ancilla] = 3W + 7.  Pre-fix peak (Beat C as
// originally landed) was 2W + 8; sturm-4oot.1 + sturm-4oot.3 added
// (W−1) for `lt_flags` here and removed 1 from the inner double_mod's
// internal `lt_flag`, net + (W − 2) qubits across both layers in
// exchange for a parity-agnostic doubling primitive.  See
// docs/design_even_n_double_mod.md §8 row 2; the pinned bound lives in
// `test_mul_mod_dsl_oneshot_ancilla.cpp`.  Compared to the chain
// implementation's `2W² + W + 7` peak, savings at W=14 are 413 − 49 =
// 364 qubits — same asymptotic class change (O(W²) → O(W)).
//
// Precondition — see @pre below.  This layer still requires `n_value`
// odd; sturm-4oot.1 made the doubling primitive itself parity-agnostic,
// but lifting this layer's odd-n restriction is sturm-4oot.4's
// explicit job (which extends the tests to even n).
//
// Aliasing — the XOR-copy `shifted_reg ^= a_bits` happens before any
// inner add/double call, so the algorithm naturally handles the
// `a_bits == b_bits` (squaring) case used by `lib_pow_mod_dsl`.  See
// `test_mul_mod_dsl_oneshot.cpp`'s squaring-aliasing test for the
// regression pin.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// add_mod_inplace_dsl.hpp / add_mod_inplace_dsl_adj.hpp pattern).
//
// Target: <=300 LoC.

#pragma once

#include "sturm/detail/lib/add_mod_inplace_dsl.hpp"
#include "sturm/detail/lib/double_mod_dsl.hpp"
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

namespace detail_mul_mod_oneshot {

// Bit-view helper mirroring detail_mul_mod::make_ancilla_view, kept local
// so this header can be included independently of mul_mod_dsl.hpp.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_mul_mod_oneshot

/**
 * @brief O(W)-ancilla helper for `lib_mul_mod_dsl` when `n` is ODD.
 *
 * This helper is the "oneshot" implementation of modular multiplication
 * referenced by sturm-7cix Beat C: a single (W+1)-bit shifted register,
 * a single (W+1)-bit accumulator, and a (W-1)-bit `lt_flags` register
 * (one bit per inner doubling), updated in-place via Beat A
 * (`lib_add_mod_inplace_dsl`) and Beat B (`lib_double_mod_dsl`) primitives.
 * Total internal qubits at peak ≈ `3W + 7` (post sturm-4oot.3), an O(W)
 * bound that replaces the chain implementation's `2W² + W + 7` peak.
 *
 * Built strictly on top of `lib_add_mod_inplace_dsl` and
 * `lib_double_mod_dsl` per the PRD §4 layering rule — emits no gates
 * of its own (only the in-place primitives, plus per-bit XOR copies and
 * the standard `sturm::lift_under` depth-1 control idiom).
 *
 * @param a_bits Left factor register (W qubits, read but restored).
 * @param b_bits Right factor register (W qubits, read but restored).
 * @param n_bits Modulus register (W qubits, read but restored).
 * @param n      Register width (NOT the modulus value).  `n == 0`
 *               short-circuits to a no-op.
 * @param r_bits Result register (W qubits).  Must start in |0>.
 *               On exit, holds `(a * b) mod n_value`.
 *
 * @pre `a, b ∈ [0, n_value)`, `n_value ≥ 1`, **AND `n_value` MUST be odd**
 *      (inherited from this layer's existing odd-n precondition; the
 *      inner `lib_double_mod_dsl` is itself parity-agnostic as of
 *      sturm-4oot.1, so lifting this layer's restriction is the
 *      explicit subject of sturm-4oot.4).  `a_bits`, `n_bits`, and
 *      `r_bits` must refer to physically distinct qubit registers.
 *      `a_bits` and `b_bits` may alias (the XOR-copy step makes a
 *      separate physical `shifted_reg`, naturally handling the squaring
 *      case used by `lib_pow_mod_dsl`).  The internal (W-1)-bit
 *      `lt_flags` register is allocated from the qubit pool and is
 *      guaranteed to enter the routine in |0> by the pool contract; it
 *      is consumed back to |0> and released LIFO before exit.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_mul_mod_dsl_oneshot` with `n_value` even, with operands outside
 * `[0, n_value)`, or with aliased registers (other than `a_bits == b_bits`)
 * is **undefined behavior** — the routine still emits a well-formed
 * gate sequence, but `r_bits` will not be the mathematical answer and
 * the input registers may not be restored.
 *
 * @sa __lib_mul_mod_dsl_oneshot_adj, lib_add_mod_inplace_dsl,
 *     lib_double_mod_dsl, lib_mul_mod_dsl
 * @see PRD §5 (Trust model and precondition contract).
 * @see docs/design_even_n_double_mod.md §8 row 2 (sturm-4oot.3).
 */
template <typename Bit>
inline void lib_mul_mod_dsl_oneshot(Bit* a_bits, Bit* b_bits,
                                    Bit* n_bits, std::size_t n,
                                    Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 32u;
    assert(n <= kMaxN && "lib_mul_mod_dsl_oneshot: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_mul_mod_dsl_oneshot: no BackendContext installed");
    (void)raw;

    // (1) Allocate shifted_reg[0..W] (W+1 qubits, all |0>).  Bits 0..W-1
    //     hold the running shifted value; bit W is the overflow slot
    //     (|0> on entry to / exit from each lib_double_mod_dsl /
    //     lib_add_mod_inplace_dsl call).
    int   shifted_idx[kMaxN + 1u];
    qbool shifted_own[kMaxN + 1u];
    Bit   shifted_bits[kMaxN + 1u];
    for (std::size_t j = 0u; j <= n; ++j) {
        shifted_idx[j]  = QubitPool::instance().allocate();
        shifted_own[j]  = qbool::make_non_owning(shifted_idx[j]);
        shifted_bits[j] =
            detail_mul_mod_oneshot::make_ancilla_view<Bit>(shifted_own[j]);
    }

    // (2) shifted_reg[0..W-1] ^= a_bits[0..W-1]; shifted_reg[W] stays |0>.
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[j] ^= a_bits[j];

    // (3) Allocate acc_reg[0..W] (W+1 qubits, all |0>).
    int   acc_idx[kMaxN + 1u];
    qbool acc_own[kMaxN + 1u];
    Bit   acc_bits[kMaxN + 1u];
    for (std::size_t j = 0u; j <= n; ++j) {
        acc_idx[j]  = QubitPool::instance().allocate();
        acc_own[j]  = qbool::make_non_owning(acc_idx[j]);
        acc_bits[j] =
            detail_mul_mod_oneshot::make_ancilla_view<Bit>(acc_own[j]);
    }

    // (3b) Allocate the (W-1)-bit lt_flags register.  Each
    //      lib_double_mod_dsl call in step (4b) writes its `(2·shifted_orig
    //      < n_value)` witness into lt_flags[i], held across to the
    //      matched __lib_double_mod_dsl_adj in step (6a).  Pool allocation
    //      guarantees the |0> entry condition required by the doubling
    //      primitive's caller-owned-XOR-into contract — see
    //      double_mod_dsl.hpp / docs/design_even_n_double_mod.md §3 + §8
    //      row 2.  The register is released LIFO in step (6c) after
    //      every slot has been consumed back to |0>.
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

    // (4) For each bit i of b: optionally add the shifted value, then
    //     double the shifted register (except on the last iteration).

    for (std::size_t i = 0u; i < n; ++i) {
        // (4a) Controlled add: acc := (acc + shifted) mod n if b[i] = 1.
        //      `shifted_bits` is W+1 wide but lib_add_mod_inplace_dsl
        //      reads only the low W bits as the addend (its `a_bits`
        //      param), and shifted_bits[W] is currently |0> per the
        //      Beat B contract.
        sturm::lift_under(b_bits[i], [&]() {
            lib_add_mod_inplace_dsl(shifted_bits, acc_bits, n_bits, n);
        });

        // (4b) Double the shifted register modulo n (except on i = W-1).
        //      The (2x_orig < n_value) witness goes into lt_flag_bits[i],
        //      held across to the matching adjoint in step (6a).
        if (i + 1u < n) {
            lib_double_mod_dsl(shifted_bits, n_bits, n, lt_flag_bits[i]);
        }
    }

    // (5) Write the answer into r_bits via per-bit XOR.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[j];

    // (6) Uncompute the loop: gate-reverse of step (4).  For step 4b's
    //     doubling we apply the adjoint; for step 4a's controlled add
    //     we apply the controlled adjoint.  Iterate i from W-1 down
    //     to 0; within each iteration reverse-order 4b then 4a.
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i = W-1, W-2, ..., 0.

        // (6a) Reverse step 4b: undo the doubling (only present at i < W-1).
        //      Consumes lt_flag_bits[i] back to |0>.
        if (i + 1u < n) {
            __lib_double_mod_dsl_adj(shifted_bits, n_bits, n,
                                     lt_flag_bits[i]);
        }

        // (6b) Reverse step 4a: undo the controlled add.
        sturm::lift_under(b_bits[i], [&]() {
            __lib_add_mod_inplace_dsl_adj(shifted_bits, acc_bits, n_bits, n);
        });
    }

    // (6c) Release the lt_flags scaffolding LIFO.  All slots are |0>
    //      after step (6a) consumed each in reverse order.
    if (n > 0u) {
        for (std::size_t step = 0u; step + 1u < n; ++step) {
            std::size_t i = n - 2u - step;  // i = W-2, W-3, ..., 0.
            QubitPool::instance().release(lt_flag_idx[i]);
        }
    }

    // (7) shifted_reg[0..W-1] ^= a_bits[0..W-1]: zeros shifted_reg.
    for (std::size_t j = 0u; j < n; ++j)
        shifted_bits[j] ^= a_bits[j];

    // (8) Release acc_reg, then shifted_reg LIFO.
    for (std::size_t j = n + 1u; j-- > 0u;)
        QubitPool::instance().release(acc_idx[j]);
    for (std::size_t j = n + 1u; j-- > 0u;)
        QubitPool::instance().release(shifted_idx[j]);
}

} // namespace sturm

#include "sturm/detail/lib/mul_mod_dsl_oneshot_adj.hpp"
