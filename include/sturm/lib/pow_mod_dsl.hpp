// pow_mod_dsl.hpp -- P3 (sturm-a5te.2) lib_pow_mod_dsl forward primitive.
//
// Out-of-place modular exponentiation: r = (base ^ exp) mod n, all unsigned,
// all W bits wide.  Built strictly on top of `lib_mul_mod_dsl` and per-bit
// XOR copies (no direct gate emission, no new arithmetic kernel — see PRD §4
// layering rule).
//
// Algorithm — chain-style repeated squaring (plan §5.1, distilled into the
// gate-reversible chain shape that side-steps `lib_add_dsl`'s
// distinct-operand precondition the same way mul_mod does):
//
//   inputs : base_bits[W], exp_bits[W], n_bits[W], r_bits[W]   (r = |0>)
//   precond: base, exp ∈ [0, n)                                 (PRD §5)
//   output : r_bits = (base ^ exp) mod n
//   convention (matches lib_pow_dsl): 0^0 = 1
//
//   1. Build the squaring chain:
//      sq_chain[0]   := XOR-copy of base                         (base · base^0 = base^1)
//      sq_chain[i]   := mul_mod(sq_chain[i-1], sq_chain[i-1], n)  for i = 1..W-1
//                      ("base ^ (2^i)" mod n)
//
//   2. Build the accumulator chain.  acc_chain[0] := 1 (set bit 0 only).
//      For i = 0..W-1:
//        acc_chain[i+1] := if exp[i] then mul_mod(acc_chain[i], sq_chain[i], n)
//                                    else copy(acc_chain[i])
//
//      The "if exp[i] then mul_mod else copy" is realised with the same
//      flip-then-control idiom mul_mod uses for `b[i]` (mul_mod_dsl §2b):
//        push(exp[i]); mul_mod(acc_chain[i], sq_chain[i], n,
//                              acc_chain[i+1]); pop;
//        flip(exp[i]); push(exp[i]); for j: acc_chain[i+1][j] ^=
//                                            acc_chain[i][j]; pop;
//                                            flip(exp[i]);
//      Trace:
//        original exp[i]=1 → first call writes mul_mod, second no-ops
//                            (exp[i]=0 after flip).
//        original exp[i]=0 → first call no-ops, second XOR-copies acc[i]
//                            into acc[i+1] (exp[i]=1 after flip).
//
//   3. r_bits ^= acc_chain[W]                                    (write the answer)
//
//   4. Uncompute acc_chain[1..W] in reverse (gate-reverse of step 2):
//      For i = W-1 down to 0:
//        flip(exp[i]); push(exp[i]); for j: acc_chain[i+1][j] ^=
//                                            acc_chain[i][j]; pop;
//                                            flip(exp[i]);
//        push(exp[i]); __lib_mul_mod_dsl_adj(acc_chain[i], sq_chain[i],
//                                             n, W, acc_chain[i+1]); pop;
//
//   5. Reset acc_chain[0]: flip bit 0 (uncomputes step 2 init).
//
//   6. Uncompute sq_chain[1..W-1] in reverse via __lib_mul_mod_dsl_adj.
//      sq_chain[0] ^= base (XOR-uncopy).
//
//   7. Release acc_chain[0..W] and sq_chain[0..W-1] LIFO.
//
// Why a chain?  Plan §5.1 envisions a single in-place `acc` register and a
// single `sq` register, updated by `acc := mul_mod(acc, sq, n)` and
// `sq := mul_mod(sq, sq, n)`.  In-place `acc := mul_mod(acc, sq, n)` is
// blocked by mul_mod's interior `lib_add_dsl` operand-aliasing rule
// (the same reason mul_mod itself uses chain-style r_chain); in-place
// `sq := mul_mod(sq, sq, n)` is blocked by the same rule applied to the
// destination.  Allocating a fresh chain register at each step keeps each
// `lib_*_dsl` call on physically distinct operands, at the cost of W² extra
// qubits beyond the long-term `O(W)` target — the `O(W)` budget is reachable
// once `mul_mod` switches to the deferred Karatsuba design (PRD §8 #1).
//
// 0^0 == 1 convention (beat 3.2): with base=0 and exp=0 the loop body's
// `if exp[i]` is always false, so every acc_chain step XOR-copies the
// previous accumulator.  acc_chain[0] starts at 1, so acc_chain[W] = 1
// and r ^= acc_chain[W] writes 1 into r.  Matches lib_pow_dsl.
//
// Sibling adjoint header is auto-included at the bottom.
//
// Target: <=280 LoC.

#pragma once

#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace sturm {

namespace detail_pow_mod {

// Bit-view helper mirroring detail_mul_mod::make_ancilla_view.  Local so
// pow_mod_dsl can be included without dragging in the mul_mod helper
// namespace from mul_mod_dsl.hpp.
template <typename Bit>
inline Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

// Push the qubit underlying `flag` onto the active control stack.
template <typename Bit>
inline void push_flag(BackendContext& ctx, Bit& flag) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        ctx.control_stack.push_control(static_cast<uint32_t>(flag.qubits[0]));
    } else {
        flag.ensure_quantum();
        ctx.control_stack.push_control(
            static_cast<uint32_t>(flag.qubit_index()));
    }
}

}  // namespace detail_pow_mod

/**
 * @brief Out-of-place W-bit modular exponentiation primitive:
 *        `r_bits = (base_bits ^ exp_bits) mod n_bits`.
 *
 * This is the lib-level primitive backing the public `sturm::pow_mod`
 * free function (see `include/sturm/ops/qint_modular.hpp`).  Built
 * strictly on top of `lib_mul_mod_dsl` + per-bit XOR copies per the
 * PRD §4 layering rule — emits no gates of its own.  See the header
 * preamble for the chain-style repeated-squaring algorithm.
 *
 * Convention: `0^0 == 1` (matches `lib_pow_dsl` and Python's
 * three-argument `pow`).
 *
 * @param base_bits Base register (W qubits, read but restored).
 * @param exp_bits  Exponent register (W qubits, read but restored).
 * @param n_bits    Modulus register (W qubits, read but restored).
 * @param n         Register width (NOT the modulus; the modulus is
 *                  encoded in `n_bits[0..n-1]`).  `n == 0`
 *                  short-circuits to a no-op (matches PRD §5 / §8 #3).
 * @param r_bits    Result register (W qubits).  Must start in |0>.
 *                  On exit, holds `(base ^ exp) mod n`.
 *
 * @pre `base ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the call is a
 *      no-op — see PRD §5 / §8 #3).  The exponent `exp` is
 *      unconstrained beyond fitting in W bits.  `r_bits` must enter
 *      the routine in |0>.  `base_bits`, `exp_bits`, `n_bits`, and
 *      `r_bits` must refer to physically distinct qubit registers.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition.  Calling
 * `lib_pow_mod_dsl` with `base` outside `[0, n)` is **undefined
 * behavior** — the routine still emits a well-formed gate sequence,
 * but the resulting `r_bits` are not the mathematical answer and the
 * input registers may not be restored.  This matches the trust model
 * of the public `pow_mod` wrapper and of classical `pow(a, b, c)` /
 * GMP / OpenSSL (PRD §5).
 *
 * @sa __lib_pow_mod_dsl_adj, lib_mul_mod_dsl, sturm::pow_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
template <typename Bit>
inline void lib_pow_mod_dsl(Bit* base_bits, Bit* exp_bits,
                            Bit* n_bits, std::size_t n,
                            Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 8u;
    assert(n <= kMaxN && "lib_pow_mod_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_pow_mod_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;

    // (1) Allocate sq_chain[0..W-1], each W bits.  sq_chain[i] = base^(2^i)
    //     mod n once built.
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

    // sq_chain[0] := base (XOR copy).
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[0][j] ^= base_bits[j];

    // sq_chain[i] := (sq_chain[i-1]^2) mod n via lib_mul_mod_dsl.  mul_mod
    // accepts a==b at the algorithmic level — it allocates a fresh shifted
    // register internally so the inner lib_add_dsl never sees aliased
    // operands (see mul_mod_dsl.hpp preamble).
    for (std::size_t i = 1u; i < n; ++i) {
        lib_mul_mod_dsl(sq_bits[i - 1u], sq_bits[i - 1u],
                        n_bits, n, sq_bits[i]);
    }

    // (2) Allocate acc_chain[0..W], each W bits.
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

    // (2a) acc_chain[0] := 1 (flip bit 0 only).
    acc_bits[0][0].flip();

    // (2b) Build acc_chain[i+1] for i=0..W-1 using the flip-then-control
    //      idiom that mul_mod uses for r_chain.
    for (std::size_t i = 0u; i < n; ++i) {
        // First call: under push(exp[i]), write mul_mod into acc_chain[i+1].
        detail_pow_mod::push_flag(ctx, exp_bits[i]);
        lib_mul_mod_dsl(acc_bits[i], sq_bits[i],
                        n_bits, n, acc_bits[i + 1u]);
        ctx.control_stack.pop_control();

        // Second call: flip exp[i], push, XOR-copy acc_chain[i] into
        // acc_chain[i+1] (so when original exp[i]=0 we propagate
        // acc_chain[i] forward), then unflip.
        exp_bits[i].flip();
        detail_pow_mod::push_flag(ctx, exp_bits[i]);
        for (std::size_t j = 0u; j < n; ++j)
            acc_bits[i + 1u][j] ^= acc_bits[i][j];
        ctx.control_stack.pop_control();
        exp_bits[i].flip();
    }

    // (3) Write the result into r_bits via per-bit XOR.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= acc_bits[n][j];

    // (4) Uncompute acc_chain[1..W] in reverse — gate-reverse of step 2b.
    for (std::size_t step = 0u; step < n; ++step) {
        std::size_t i = n - 1u - step;  // i goes W-1, W-2, ..., 0.

        // Reverse the second call (XOR-copy under flipped control).
        exp_bits[i].flip();
        detail_pow_mod::push_flag(ctx, exp_bits[i]);
        for (std::size_t j = 0u; j < n; ++j)
            acc_bits[i + 1u][j] ^= acc_bits[i][j];
        ctx.control_stack.pop_control();
        exp_bits[i].flip();

        // Reverse the first call (lib_mul_mod_dsl) with __lib_mul_mod_dsl_adj.
        detail_pow_mod::push_flag(ctx, exp_bits[i]);
        __lib_mul_mod_dsl_adj(acc_bits[i], sq_bits[i],
                              n_bits, n, acc_bits[i + 1u]);
        ctx.control_stack.pop_control();
    }

    // (5) Reset acc_chain[0] to |0>: flip bit 0 (uncomputes step 2a).
    acc_bits[0][0].flip();

    // (6) Release acc_chain[0..W] LIFO.
    for (std::size_t i = n + 1u; i-- > 0u;) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(acc_idx[i][j]);
    }

    // (7) Uncompute sq_chain[1..W-1] in reverse (gate-reverse of step 1's
    //     squaring loop).
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;  // i goes W-1, W-2, ..., 1.
        __lib_mul_mod_dsl_adj(sq_bits[i - 1u], sq_bits[i - 1u],
                              n_bits, n, sq_bits[i]);
    }

    // (7a) Reverse sq_chain[0] := XOR-copy of base (self-inverse).
    for (std::size_t j = 0u; j < n; ++j)
        sq_bits[0][j] ^= base_bits[j];

    // (8) Release sq_chain[0..W-1] LIFO.
    for (std::size_t i = n; i-- > 0u;) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(sq_idx[i][j]);
    }
}

} // namespace sturm

#include "sturm/lib/pow_mod_dsl_adj.hpp"
