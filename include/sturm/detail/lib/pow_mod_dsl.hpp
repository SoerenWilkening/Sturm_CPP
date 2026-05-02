// pow_mod_dsl.hpp -- Beat D-C (sturm-3sfl.3) lib_pow_mod_dsl forward primitive,
//                     rewritten on top of Beat D-A (lib_mul_mod_inplace_dsl)
//                     and Beat D-B (lib_square_mod_dsl).
//
// Out-of-place modular exponentiation: r = (base ^ exp) mod n, all unsigned,
// all W bits wide.  Built strictly on top of `lib_mul_mod_inplace_dsl` /
// `__lib_mul_mod_inplace_dsl_adj` (Beat D-A, sturm-3sfl.1) and
// `lib_square_mod_dsl` / `__lib_square_mod_dsl_adj` (Beat D-B, sturm-3sfl.2)
// plus per-bit XOR copies — no direct gate emission, no new arithmetic
// kernel (PRD §4 layering rule).
//
// History: the pre-Beat-D-C implementation (sturm-a5te) used chain-style
// repeated squaring with `sq_chain[W][W]` + `acc_chain[W+1][W]` and a
// chain-style `lib_mul_mod_dsl`.  Beat D-C replaces that with a single
// accumulator `acc_reg` and single squaring register `sq_reg`, updated in
// place by the Beat D-A / Beat D-B primitives; the chain machinery is gone,
// leaving only per-step witness registers required by the in-place
// primitives' non-injectivity contracts.
//
// Two complexities -- "Beat D O(W)" vs measured peak (sturm-vf2c):
//   The headline "Beat D = O(W) modular exponentiation" refers strictly to
//   the REGISTER TOPOLOGY: a single live `acc_reg[W]` plus a single live
//   `sq_reg[W]`, replacing the chain-style `O(W^2)` topology.  PEAK
//   TRANSIENT ANCILLA, however, is `O(W^2)` -- specifically `2W^2 + 4W + 11`
//   above the 4·W input registers, pinned in
//   `tests/lib/test_pow_mod_dsl_ancilla.cpp`.  The residual `W^2` term comes
//   from the per-iteration witness registers required by Beat D-A
//   (`dest_copy_out_bits`) and Beat D-B (`x_copy_out_bits`): each forward
//   call needs a W-bit witness that must stay live until its paired adjoint
//   runs, and the forward loop runs to completion (W mul-inplace +
//   (W-1) square) before the reverse loop begins, so all per-iteration
//   witnesses are simultaneously live at the loop boundary.  This is the
//   documented bound, not a defect: see sturm-vf2c's investigation note in
//   `docs/01_principles.md` and the "Witness accounting" comment below.
//
// Algorithm — square-and-multiply on top of in-place primitives:
//   precond: base ∈ [0, n_value), n_value ≥ 1 (PRD §5).  output: r_bits =
//   (base ^ exp) mod n.  Convention 0^0 = 1 (matches lib_pow_dsl).
//
//   1. Allocate acc_reg[W] (|0>); flip bit 0 → acc_reg = 1.
//   2. Allocate sq_reg[W] (|0>); XOR-copy from base_bits → sq_reg = base.
//   3. Forward loop, i = 0..W-1:
//      3a. Allocate mul_witness_i[W] (|0>) and call
//          lib_mul_mod_inplace_dsl(sq_reg, acc_reg, n_bits, n, mul_witness_i)
//          under `lift_under(exp_bits[i])`.  When exp_bits[i] = 1 this
//          updates acc_reg := (sq_reg * acc_reg) mod n and stores the
//          mul-inplace witness (acc_old) in mul_witness_i; when exp_bits[i]
//          = 0 the body emits all gates controlled-off, so acc_reg and the
//          witness register stay unchanged at their pre-call values
//          (acc_reg = pre, mul_witness_i = |0>).  The witness is allocated
//          unconditionally so the adjoint sees the same layout.
//      3b. If i < W-1: allocate sq_witness_i[W] (|0>) and call
//          lib_square_mod_dsl(sq_reg, n_bits, n, sq_witness_i),
//          unconditionally.  After: sq_reg := sq_reg^2 mod n; sq_witness_i
//          holds sq_old (the squaring's non-injectivity witness).
//   4. r_bits ^= acc_reg                                  (write the answer)
//   5. Reverse loop, i = W-1..0: __lib_square_mod_dsl_adj (if i < W-1) and
//      __lib_mul_mod_inplace_dsl_adj under lift_under(exp_bits[i]) consume
//      each witness back to |0>; release LIFO.
//   6. sq_reg ^= base_bits → sq_reg = |0>.   (uncomputes step 2)
//   7. acc_reg[0].flip()   → acc_reg = |0>.  (uncomputes step 1)
//   8. Release sq_reg, acc_reg LIFO.
//
// On exit only r_bits is changed (= (base^exp) mod n); base_bits, exp_bits,
// n_bits are preserved.  0^0 = 1 follows because with exp = 0 every step-3a
// body is fully gated off, leaving acc_reg = 1 (its initial value) which
// step 4 XOR-copies into r_bits.
//
// lt_flags batching decision (per the issue brief, option (i) chosen for V1;
// re-confirmed under sturm-vf2c):
//   Beat D-A and Beat D-B each emit per-call (W-1)-bit `lt_flags` registers
//   from the inner `lib_mul_mod_dsl_oneshot` cascade; those registers are
//   entirely managed inside each primitive (each forward+adjoint pair clears
//   its own).  Beat D-C does NOT batch them at this layer — the (W * (W-1))
//   savings of option (ii) are modest compared to the live witness chain
//   discussed below (the W^2 caller-owned witness floor dominates the W *
//   (W-1) lt_flags term and the asymptotic peak stays O(W^2) either way),
//   and option (i) keeps each per-iteration call entirely self-contained.
//   sturm-vf2c re-considered this and concluded option (i) is still the
//   right call: the simulator-qubit budget overshoot is dictated by the
//   2W^2 - W caller-owned witness floor, not by the lt_flags term.
//
// Witness accounting / peak ancilla (sturm-vf2c documented bound):
//   The issue brief's qualitative target was "~5W + O(1)" peak, derived
//   under the assumption that Beat D-A / Beat D-B were witness-less.  Both
//   primitives in fact require a caller-owned W-bit witness (D-A's
//   `dest_copy_out_bits`, D-B's `x_copy_out_bits`) per call, and because the
//   forward loop runs to completion before the reverse loop begins, every
//   per-iteration witness must stay live across all subsequent forward
//   iterations until its paired adjoint consumes it.  Total caller-owned
//   witnesses across the forward loop: W mul-inplace + (W-1) square = O(W²).
//   Adding acc_reg (W) + sq_reg (W) + the active D-A or D-B internal
//   (~3W + O(1)) gives a peak of O(W²) above the 4W input registers — not
//   the ~5W of the brief, but still strictly less than the chain-style
//   implementation's `4·W² + 2W + 8` peak.
//
//   This O(W^2) peak ancilla is the DOCUMENTED BOUND for the current
//   implementation (sturm-vf2c).  It coexists with the O(W) register-
//   topology claim from the Beat D headline: data registers are O(W); peak
//   transient ancilla is O(W^2).  Reaching ~5W peak would require either
//   witness-less D-A/D-B variants (restrict to gcd(a, n) = 1 or require an
//   a^-1 round-trip — see the Beat D-A header preamble) or Bennett-style
//   pebbling that interleaves forward/adjoint phases inside the
//   square-and-multiply loop (~2× more gates).  Both are out of scope for
//   Beat D-C / sturm-vf2c.  The regression test in
//   test_pow_mod_dsl_ancilla.cpp pins the actual achieved bound
//   (`2W^2 + 4W + 11`); see also the "pow_mod ancilla bound" section in
//   docs/01_principles.md.
//
// Sibling adjoint header is auto-included at the bottom.
//
// Target: ≤ 300 LoC.

#pragma once

#include "sturm/detail/lib/mul_mod_inplace_dsl.hpp"
#include "sturm/detail/lib/square_mod_dsl.hpp"
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

namespace detail_pow_mod {

// Bit-view helper, kept local so pow_mod_dsl can be included independently
// of Beat D-A / Beat D-B's helper namespaces.  Mirrors the same idiom the
// sibling primitives use.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

}  // namespace detail_pow_mod

/**
 * @brief Out-of-place W-bit modular exponentiation primitive:
 *        `r_bits = (base_bits ^ exp_bits) mod n_bits`.
 *
 * Beat D-C rewrite (sturm-3sfl.3): square-and-multiply on top of the in-place
 * primitives `lib_mul_mod_inplace_dsl` (Beat D-A) and `lib_square_mod_dsl`
 * (Beat D-B).  Built strictly on those primitives plus per-bit XOR copies per
 * the PRD §4 layering rule — emits no gates of its own.  See the header
 * preamble for the algorithm and the witness-register accounting.
 *
 * Convention: `0^0 == 1` (matches `lib_pow_dsl` and Python's three-argument
 * `pow`).
 *
 * @param base_bits Base register (W qubits, read but restored).
 * @param exp_bits  Exponent register (W qubits, read but restored).
 * @param n_bits    Modulus register (W qubits, read but restored).
 * @param n         Register width (NOT the modulus value; the modulus is
 *                  encoded in `n_bits[0..n-1]`).  `n == 0` short-circuits
 *                  to a no-op (matches PRD §5 / §8 #3).
 * @param r_bits    Result register (W qubits).  Must start in |0>.
 *                  On exit, holds `(base ^ exp) mod n`.
 *
 * @pre `base ∈ [0, n_value)` and `n_value ≥ 1` (when `n == 0`, the call is
 *      a no-op).  The exponent `exp` is unconstrained beyond fitting in W
 *      bits.  `r_bits` must enter the routine in |0>.  `base_bits`,
 *      `exp_bits`, `n_bits`, and `r_bits` must refer to physically
 *      distinct qubit registers.  No parity restriction on `n_value` (the
 *      inner Beat D-A / Beat D-B primitives are parity-agnostic).
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_pow_mod_dsl` with `base` outside `[0, n_value)` is **undefined
 * behavior** — the routine still emits a well-formed gate sequence, but
 * the resulting `r_bits` are not the mathematical answer and the input
 * registers may not be restored.  Matches the trust model of the public
 * `pow_mod` wrapper.
 *
 * @sa __lib_pow_mod_dsl_adj, lib_mul_mod_inplace_dsl, lib_square_mod_dsl,
 *     sturm::pow_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_pow_mod_dsl(Bit* base_bits, Bit* exp_bits,
                            Bit* n_bits, std::size_t n,
                            Bit* r_bits) {
    if (n == 0u) return;

    // Internal W cap.  Beat D-A / Beat D-B were both lifted to kMaxN = 64
    // under sturm-8n73 for circuit-generation use cases; Beat D-D
    // (sturm-3sfl.4) lifts pow_mod's cap to match.  The cap is governed by
    // stack budget — pow_mod's per-iteration witness arrays scale as
    // kMaxN² qbool/BitProxy slots — and by the underlying primitives'
    // own kMaxN.  At kMaxN = 64 the worst-case stack frame is:
    //   • acc/sq registers       :   2·64 · (sizeof(int) + sizeof(qbool)
    //                                        + sizeof(BitProxy))
    //   • mul/sq witness matrices : 2·64·64 · (sizeof(int) + sizeof(qbool)
    //                                          + sizeof(BitProxy))
    // ≈ 500 KiB, well under the default 8 MiB thread stack.
    //
    // Note on simulator runnability and the O(W^2) peak (sturm-vf2c):
    // pow_mod's peak ancilla is 2W^2 + 4W + 11 above the 4W input registers
    // (see tests/lib/test_pow_mod_dsl_ancilla.cpp).  This is O(W^2) in
    // peak transient ancilla, even though the *register topology* is O(W)
    // (single acc_reg + single sq_reg) — the W^2 term is the caller-owned
    // witness floor required by the Beat D-A / Beat D-B in-place primitives'
    // non-injectivity contracts, held live across the full forward loop
    // until the reverse pass consumes them.  This is the documented bound
    // (sturm-vf2c) -- see the "Witness accounting" comment in the header
    // preamble and the "pow_mod ancilla bound" section in
    // docs/01_principles.md.  The peak exceeds the orkan simulator's
    // 17-qubit budget at every W >= 1.  Public-wrapper (`sturm::pow_mod`)
    // tests and the lib_pow_mod_dsl test suite use APPEND-mode capture +
    // classical replay rather than statevector simulation, so the cap-lift
    // is bounded only by stack budget and the underlying primitives' caps,
    // not by simulator capacity.
    static constexpr std::size_t kMaxN = 64u;
    assert(n <= kMaxN && "lib_pow_mod_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_pow_mod_dsl: no BackendContext installed");
    (void)raw;  // control_stack is driven by WHEN/WhenGuard via lift_under;
                // ctx is no longer poked directly.

    // (1) Allocate acc_reg[W] (|0>), flip bit 0 → acc_reg = 1.
    int   acc_idx[kMaxN];
    qbool acc_own[kMaxN];
    Bit   acc_bits[kMaxN];
    for (std::size_t j = 0u; j < n; ++j) {
        acc_idx[j]  = QubitPool::instance().allocate();
        acc_own[j]  = qbool::make_non_owning(acc_idx[j]);
        acc_bits[j] = detail_pow_mod::make_ancilla_view<Bit>(acc_own[j]);
    }
    acc_bits[0].flip();

    // (2) Allocate sq_reg[W] (|0>) and XOR-copy from base_bits → sq_reg = base.
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

    // (3) Forward loop: per-iteration witness registers held live across
    //     the loop until consumed by the reverse pass.  See the preamble's
    //     "Witness accounting" comment.
    int   mul_w_idx[kMaxN][kMaxN];
    qbool mul_w_own[kMaxN][kMaxN];
    Bit   mul_w_bits[kMaxN][kMaxN];
    int   sq_w_idx[kMaxN][kMaxN];
    qbool sq_w_own[kMaxN][kMaxN];
    Bit   sq_w_bits[kMaxN][kMaxN];

    for (std::size_t i = 0u; i < n; ++i) {
        // (3a) Per-iteration mul-inplace witness: allocate W qubits |0>.
        //      Allocated unconditionally (independent of exp_bits[i]) so the
        //      adjoint pass sees the same register layout under classical
        //      replay.  When exp_bits[i] = 0 the lift_under body emits no
        //      gates and the witness stays at |0>; the matched adjoint
        //      no-ops on it symmetrically.
        for (std::size_t j = 0u; j < n; ++j) {
            mul_w_idx[i][j]  = QubitPool::instance().allocate();
            mul_w_own[i][j]  = qbool::make_non_owning(mul_w_idx[i][j]);
            mul_w_bits[i][j] =
                detail_pow_mod::make_ancilla_view<Bit>(mul_w_own[i][j]);
        }

        // Conditional in-place modular multiplication: under control of
        // exp_bits[i], compute acc_reg := (sq_reg * acc_reg) mod n.
        sturm::lift_under(exp_bits[i], [&]() {
            lib_mul_mod_inplace_dsl(sq_bits, acc_bits, n_bits, n,
                                    mul_w_bits[i]);
        });

        // (3b) Squaring step (skipped on the final iteration).
        if (i + 1u < n) {
            // Per-iteration squaring witness.
            for (std::size_t j = 0u; j < n; ++j) {
                sq_w_idx[i][j]  = QubitPool::instance().allocate();
                sq_w_own[i][j]  = qbool::make_non_owning(sq_w_idx[i][j]);
                sq_w_bits[i][j] =
                    detail_pow_mod::make_ancilla_view<Bit>(sq_w_own[i][j]);
            }
            // Unconditional squaring: sq_reg := sq_reg^2 mod n.  The square
            // step is independent of exp_bits[i] — every iteration advances
            // sq_reg to base^(2^(i+1)) mod n regardless of the exponent's
            // current bit (the conditional accumulation in step 3a already
            // gated the contribution to acc_reg).
            lib_square_mod_dsl(sq_bits, n_bits, n, sq_w_bits[i]);
        }
    }

    // (4) Write the result into r_bits via per-bit XOR.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[j];

    // (5) Reverse loop — gate-reverse of step 3.  Releases per-iteration
    //     witnesses LIFO after each consumes-back-to-|0> adjoint call.
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i goes W-1, W-2, ..., 0.

        // (5a) Reverse the squaring step (skipped on the original final
        //      iteration where step 3b was also skipped).
        if (i + 1u < n) {
            __lib_square_mod_dsl_adj(sq_bits, n_bits, n, sq_w_bits[i]);
            // Release sq_witness_i LIFO (every slot is |0> after the adjoint).
            for (std::size_t j = n; j-- > 0u;)
                QubitPool::instance().release(sq_w_idx[i][j]);
        }

        // (5b) Reverse the conditional in-place mul under control of exp_bits[i].
        sturm::lift_under(exp_bits[i], [&]() {
            __lib_mul_mod_inplace_dsl_adj(sq_bits, acc_bits, n_bits, n,
                                          mul_w_bits[i]);
        });
        // Release mul_witness_i LIFO.
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(mul_w_idx[i][j]);
    }

    // (6) Uncompute step (2): sq_reg ^= base_bits → sq_reg = |0>.
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[j] ^= base_bits[j];

    // (7) Uncompute step (1): acc_reg[0].flip() → acc_reg = |0>.
    acc_bits[0].flip();

    // (8) Release sq_reg, acc_reg LIFO.
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(sq_idx[j]);
    for (std::size_t j = n; j-- > 0u;)
        QubitPool::instance().release(acc_idx[j]);
}

} // namespace sturm

#include "sturm/detail/lib/pow_mod_dsl_adj.hpp"
