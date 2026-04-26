// mul_mod_dsl.hpp -- P2 (sturm-kubb.2) lib_mul_mod_dsl forward primitive.
//
// Out-of-place modular multiplication: r = (a * b) mod n, all unsigned, all
// W bits wide.  Built strictly on top of `lib_add_mod_dsl` and per-bit XOR
// copies (no direct gate emission, no new arithmetic kernel — see PRD §4
// layering rule).
//
// Algorithm — chain-style shift-and-add (plan §4.1):
//
//   inputs : a_bits[W], b_bits[W], n_bits[W], r_bits[W]   (r = |0>)
//   precond: a, b ∈ [0, n)                                 (PRD §5)
//   output : r_bits = (a * b) mod n
//
//   1. Build the doubling chain:
//      shifted_chain[0]   := XOR-copy of a                 (a · 2^0 mod n = a)
//      shifted_chain[i]   := add_mod(shifted_chain[i-1], shifted_chain[i-1], n)
//                            for i = 1..W-1                (a · 2^i mod n)
//
//   2. Build the partial-sum chain:
//      r_chain[1]         := b[0] ? shifted_chain[0] : 0    (XOR-copy via push)
//      r_chain[i+1]       := b[i] ? (r_chain[i] + shifted_chain[i]) mod n
//                                 : r_chain[i]
//                            for i = 1..W-1
//
//      The "if b[i] then add_mod else copy" is realised with the same
//      flip-then-control idiom used by add_mod (plan §3.1):
//        push(b[i]); add_mod(r_chain[i], shifted_chain[i], r_chain[i+1]); pop;
//        flip(b[i]); push(b[i]); for j: r_chain[i+1][j] ^= r_chain[i][j];
//        pop; flip(b[i]);
//      Trace:
//        original b[i]=1 → first call writes (sum) mod n, second call no-ops
//                          (b[i]=0 after flip), final r_chain[i+1] = sum mod n.
//        original b[i]=0 → first call no-ops (control off), second call
//                          XOR-copies r_chain[i] in (b[i]=1 after flip),
//                          final r_chain[i+1] = r_chain[i].
//
//   3. r_bits ^= r_chain[W]                                 (write the answer)
//
//   4. Uncompute r_chain[1..W] in reverse:
//      For i = W-1 down to 1:
//        flip(b[i]); push(b[i]); for j: r_chain[i+1][j] ^= r_chain[i][j];
//        pop; flip(b[i]);
//        push(b[i]); __lib_add_mod_dsl_adj(r_chain[i], shifted_chain[i],
//                                           n, W, r_chain[i+1]); pop;
//      For i = 0:
//        push(b[0]); for j: r_chain[1][j] ^= shifted_chain[0][j]; pop;
//
//   5. Uncompute shifted_chain in reverse (W doubling steps in adjoint
//      order, per the issue brief):
//      For i = W-1 down to 1:
//        __lib_add_mod_dsl_adj(shifted_chain[i-1], shifted_chain[i-1],
//                              n, W, shifted_chain[i]);
//      shifted_chain[0] ^= a (XOR-uncopy).
//
//   6. Release shifted_chain[0..W-1] and r_chain[1..W] LIFO.
//
// Why a chain?  Plan §4.1 envisions a single in-place `shifted` register
// updated by `shifted := add_mod(shifted, shifted, n)`.  In-place add_mod
// with a == b violates `lib_add_mod_dsl`'s precondition that the two
// addend registers are physically distinct (the inner `lib_add_dsl(b,
// s_low, ...)` call would alias the addend onto its own destination).
// The chain pattern preserves the algorithm's spirit while sidestepping
// the aliasing issue, at the cost of W·W extra qubits beyond the long-
// term `2W + O(1)` target — beat 2.6 will tighten the budget; beat 2.2
// just needs the single-classical-case correctness.
//
// `r_chain` is also chained for the same reason: an in-place modular add
// would otherwise need a `(n - shifted)`-style helper register to undo
// the swap-and-uncompute pattern.  Keeping the chain alive lets the
// adjoint of step 2 zero each `r_chain[i+1]` directly using the original
// `(r_chain[i], shifted_chain[i])` pair as the (a, b) inputs to
// `__lib_add_mod_dsl_adj`.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// add_mod_dsl.hpp / add_mod_dsl_adj.hpp pairing pattern).
//
// Target: <=280 LoC.

#pragma once

#include "sturm/lib/add_mod_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

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

}  // namespace detail_mul_mod

// lib_mul_mod_dsl: out-of-place W-bit modular multiplication.  See header
// preamble for the algorithm.  `n` is the register width (NOT the modulus);
// the modulus is encoded in n_bits[0..n-1].  r_bits must start in |0>.
// Inputs a_bits, b_bits, n_bits are unchanged.  n == 0 is a no-op.
template <typename Bit>
inline void lib_mul_mod_dsl(Bit* a_bits, Bit* b_bits,
                            Bit* n_bits, std::size_t n,
                            Bit* r_bits) {
    if (n == 0u) return;

    static constexpr std::size_t kMaxN = 8u;
    assert(n <= kMaxN && "lib_mul_mod_dsl: register too wide");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_mul_mod_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;

    // (1) Allocate shifted_chain[0..W-1], each W bits.  Built lazily as
    //     shifted_chain[i] = (a · 2^i) mod n.
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

    // shifted_chain[i] := (2 · shifted_chain[i-1]) mod n, via lib_add_mod_dsl
    // with the same register on both sides of the sum.  Two distinct chain
    // registers means the inner `lib_add_dsl(b, s_low, ...)` call sees
    // physically distinct operands.
    for (std::size_t i = 1u; i < n; ++i) {
        lib_add_mod_dsl(shifted_bits[i - 1u], shifted_bits[i - 1u],
                        n_bits, n, shifted_bits[i]);
    }

    // (2) Allocate r_chain[1..W], each W bits.  Index 0 is unused (would
    //     be the input r_bits, but we keep r_bits at |0> until step 3 to
    //     keep the uncompute symmetric).
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

    // (2a) i = 0 special case: r_chain[1] := b[0] ? shifted_chain[0] : 0.
    //      Plain XOR-copy under push(b[0]) — no modular reduction needed
    //      because shifted_chain[0] in [0, n) and r_chain[0] = 0.
    detail_mul_mod::push_flag(ctx, b_bits[0]);
    for (std::size_t j = 0u; j < n; ++j)
        r_chain_bits[1][j] ^= shifted_bits[0][j];
    ctx.control_stack.pop_control();

    // (2b) i = 1..W-1: r_chain[i+1] := b[i] ? (r_chain[i] + shifted_chain[i])
    //                                       : r_chain[i].
    for (std::size_t i = 1u; i < n; ++i) {
        // First call: when b[i]=1, write the modular sum to r_chain[i+1].
        detail_mul_mod::push_flag(ctx, b_bits[i]);
        lib_add_mod_dsl(r_chain_bits[i], shifted_bits[i],
                        n_bits, n, r_chain_bits[i + 1u]);
        ctx.control_stack.pop_control();

        // Second call: when b[i]=0, XOR-copy r_chain[i] into r_chain[i+1].
        // flip(b[i]) inverts the control polarity locally; unflipped at end.
        b_bits[i].flip();
        detail_mul_mod::push_flag(ctx, b_bits[i]);
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[i + 1u][j] ^= r_chain_bits[i][j];
        ctx.control_stack.pop_control();
        b_bits[i].flip();
    }

    // (3) Write the result into r_bits via per-bit XOR.
    for (std::size_t j = 0u; j < n; ++j)
        r_bits[j] ^= r_chain_bits[n][j];

    // (4) Uncompute r_chain[1..W] in reverse — gate-reverse of step 2.
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;  // i goes W-1, W-2, ..., 1.

        // Reverse the second call (XOR-copy under flipped control).
        b_bits[i].flip();
        detail_mul_mod::push_flag(ctx, b_bits[i]);
        for (std::size_t j = 0u; j < n; ++j)
            r_chain_bits[i + 1u][j] ^= r_chain_bits[i][j];
        ctx.control_stack.pop_control();
        b_bits[i].flip();

        // Reverse the first call (lib_add_mod_dsl) with __lib_add_mod_dsl_adj.
        detail_mul_mod::push_flag(ctx, b_bits[i]);
        __lib_add_mod_dsl_adj(r_chain_bits[i], shifted_bits[i],
                              n_bits, n, r_chain_bits[i + 1u]);
        ctx.control_stack.pop_control();
    }

    // (4a) Reverse the i=0 XOR-copy (self-inverse).
    detail_mul_mod::push_flag(ctx, b_bits[0]);
    for (std::size_t j = 0u; j < n; ++j)
        r_chain_bits[1][j] ^= shifted_bits[0][j];
    ctx.control_stack.pop_control();

    // (5) Release r_chain[1..W] LIFO.
    for (std::size_t i = n; i >= 1u; --i) {
        for (std::size_t j = n; j-- > 0u;)
            QubitPool::instance().release(r_chain_idx[i][j]);
    }

    // (6) Uncompute shifted_chain in reverse (W doublings in adjoint order).
    for (std::size_t step = 1u; step < n; ++step) {
        std::size_t i = n - step;  // i goes W-1, W-2, ..., 1.
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

} // namespace sturm

#include "sturm/lib/mul_mod_dsl_adj.hpp"
