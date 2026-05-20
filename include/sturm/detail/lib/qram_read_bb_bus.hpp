// qram_read_bb_bus.hpp — sturm-44bt.2 (Beat BB2).
//
// Bucket-brigade Phase 2: bus traversal + leaf XOR + reverse walk
// (PRD §4.2; plan §5 Beat BB2).
//
// Surface (under sturm::detail_qram_bb):
//   template <std::size_t Nprime, std::size_t W>
//   inline void bb_bus_traverse(
//       const std::array<BBRouter, Nprime - 1>& routers,
//       std::array<std::array<qbool, W>, Nprime - 2>& transits,
//       std::array<qbool, W>& bus,
//       const qint_t<W>* a, qint_t<W>& b);
//
// Caller invariants: `routers` already set up by bb_setup_routers;
// `transits` / `bus` have allocated qubits = |0⟩; `b` has W qubits.
// `a[k]` may be quantum (qubits[j]>=0) OR fully classical (qubits[j]==-1):
//   - Both target and operand quantum → standard CSWAP shape
//     `CNOT(t,a); WHEN(ctrl){CNOT(a,t)}; CNOT(t,a)` = 2 CNOT + 1 CCX
//     per scalar. Satisfies §4.2 closed form `2W CCX + 4W CNOT` per
//     internal node.
//   - a[k] classical → for each set bit of `a[k].value`, emit a single
//     CNOT under WHEN(ctrl) (controlled-X-classical load). Gate count
//     is data-dependent; the simulate test fixtures use this path to
//     fit inside orkan's kMaxQubits cap.
//
// Algorithm: forward walk = POST-order (leaves load first, data
// percolates up to bus at root). Then `b ^= bus` (W CNOTs). Reverse
// walk = PRE-order mirror — re-emits the same gates in reverse order;
// each cluster is self-inverse so the round trip restores bus / transit
// / a[k] to entry state.
//
// LoC budget: ≤ 250 (plan §1 / Beat BB2).

#pragma once

#include "sturm/detail/lib/qram_read_bb_routers.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qbool_ops.hpp"
#  include "sturm/backend/primitives.hpp"
#  include "sturm/core/context.hpp"
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace sturm {
namespace detail_qram_bb {

// Standard CSWAP scalar form when both target and operand are quantum:
//   CNOT(t,a) | WHEN(ctrl) { CNOT(a,t) } | CNOT(t,a)  = 2 CNOT + 1 CCX
// Classical-operand fallback: under WHEN(ctrl), for each set bit of
// `a.value` at bit 0, emit CNOT(ctrl, t) — the controlled-X-classical
// load of a's classical bit pattern into t. q1/q2 are duck-typed views
// reading only .qubits[0] / .value (qbool or fields-compatible).
template <typename Q1, typename Q2>
inline void emit_csswap_one_bit(Q1& t, Q2& a, qbool& ctrl) {
#ifdef STURM_BACKEND_ENABLED
    BackendContext& ctx = get_ctx();
    const bool t_q = (t.qubits[0] >= 0);
    const bool a_q = (a.qubits[0] >= 0);
    if (t_q && a_q) {
        const auto tq = static_cast<uint32_t>(t.qubits[0]);
        const auto aq = static_cast<uint32_t>(a.qubits[0]);
        primitive_XOR(ctx, tq, aq);                       // outer CNOT(t,a)
        WHEN(ctrl) { emit_CX_lifted(ctx, aq, tq); }       // → CCX(ctrl,a,t)
        primitive_XOR(ctx, tq, aq);                       // outer CNOT(t,a)
    } else if (t_q && !a_q) {
        if ((a.value & int64_t{1}) != 0) {
            WHEN(ctrl) {
                emit_X_lifted(ctx, static_cast<uint32_t>(t.qubits[0]));
            }
        }
    }
#else
    (void)t; (void)a; (void)ctrl;
#endif
}

// W-wide CSWAP arm — a is a qint_t<W> leaf slot (per-bit view built lazily).
template <std::size_t W, typename Q1>
inline void emit_csswap_w_arm_qint(std::array<Q1, W>& t, qint_t<W>& a, qbool& ctrl) {
    for (std::size_t j = 0; j < W; ++j) {
        qbool a_j;
        a_j.qubits[0]  = a.qubits[j];
        a_j.value      = (a.value >> j) & int64_t{1};
        a_j.super_mask = (a.super_mask >> j) & 1ULL;
        a_j.owning_    = false;
        emit_csswap_one_bit(t[j], a_j, ctrl);
    }
}

// W-wide CSWAP arm — both operands are std::array<qbool, W> (bus or transit).
template <std::size_t W, typename Q1, typename Q2>
inline void emit_csswap_w_arm_arr(std::array<Q1, W>& t,
                                  std::array<Q2, W>& a, qbool& ctrl) {
    for (std::size_t j = 0; j < W; ++j) emit_csswap_one_bit(t[j], a[j], ctrl);
}

// b ^= bus payload: W uncontrolled CNOTs from bus[j] to b.qubits[j].
template <std::size_t W>
inline void emit_b_xor_bus(std::array<qbool, W>& bus, qint_t<W>& b) {
#ifdef STURM_BACKEND_ENABLED
    BackendContext& ctx = get_ctx();
    for (std::size_t j = 0; j < W; ++j) {
        assert(bus[j].qubits[0] >= 0 && b.qubits[j] >= 0
               && "bb_bus_traverse: bus and b must have allocated qubits");
        primitive_XOR(ctx,
                      static_cast<uint32_t>(bus[j].qubits[0]),
                      static_cast<uint32_t>(b.qubits[j]));
    }
#else
    (void)bus; (void)b;
#endif
}

// Level of node at FlatIdx (root=0). Leaf-side routers live at depth-1.
constexpr std::size_t level_of(std::size_t flat) noexcept {
    std::size_t L = 0u, span = 1u, sum = 0u;
    while (sum + span <= flat) { sum += span; ++L; span <<= 1u; }
    return L;
}

// Reverse the low `bits` bits of `x`. BB1's setup uses `addr[level]`
// with bit 0 at root, so leaves' tree-positions (MSB-first) map to
// a[] indices (LSB-first via addr-value) by bit-reversal.
constexpr std::size_t bit_reverse(std::size_t x, std::size_t bits) noexcept {
    std::size_t r = 0u;
    for (std::size_t i = 0; i < bits; ++i) r |= ((x >> i) & 1u) << (bits - 1u - i);
    return r;
}

// Forward walk — POST-order: recurse into children first, then emit
// at this internal node (left arm then right arm).
template <std::size_t Nprime, std::size_t FlatIdx, std::size_t W>
inline void forward_walk(
    const std::array<BBRouter, Nprime - 1u>& routers,
    std::array<std::array<qbool, W>, (Nprime >= 2u) ? (Nprime - 2u) : 0u>& transits,
    std::array<qbool, W>& bus, const qint_t<W>* a) {
    constexpr std::size_t depth = ct_log2(Nprime);
    constexpr std::size_t Level = level_of(FlatIdx);
    constexpr bool is_leaf_side = (Level + 1u == depth);
    constexpr std::size_t LeftChild  = 2u * FlatIdx + 1u;
    constexpr std::size_t RightChild = 2u * FlatIdx + 2u;
    if constexpr (!is_leaf_side) {
        forward_walk<Nprime, LeftChild,  W>(routers, transits, bus, a);
        forward_walk<Nprime, RightChild, W>(routers, transits, bus, a);
    }
    BBRouter& r = const_cast<BBRouter&>(routers[FlatIdx]);
    if constexpr (is_leaf_side) {
        constexpr std::size_t LeafStart = (Nprime / 2u) - 1u;
        constexpr std::size_t LocalIdx  = FlatIdx - LeafStart;
        constexpr std::size_t LeafL = bit_reverse(2u * LocalIdx,      depth);
        constexpr std::size_t LeafR = bit_reverse(2u * LocalIdx + 1u, depth);
        if constexpr (FlatIdx == 0u) {
            emit_csswap_w_arm_qint<W>(bus, const_cast<qint_t<W>&>(a[LeafL]), r.is_left);
            emit_csswap_w_arm_qint<W>(bus, const_cast<qint_t<W>&>(a[LeafR]), r.is_right);
        } else {
            auto& in = transits[FlatIdx - 1u];
            emit_csswap_w_arm_qint<W>(in, const_cast<qint_t<W>&>(a[LeafL]), r.is_left);
            emit_csswap_w_arm_qint<W>(in, const_cast<qint_t<W>&>(a[LeafR]), r.is_right);
        }
    } else {
        auto& tl = transits[LeftChild  - 1u];
        auto& tr = transits[RightChild - 1u];
        if constexpr (FlatIdx == 0u) {
            emit_csswap_w_arm_arr<W>(bus, tl, r.is_left);
            emit_csswap_w_arm_arr<W>(bus, tr, r.is_right);
        } else {
            auto& in = transits[FlatIdx - 1u];
            emit_csswap_w_arm_arr<W>(in, tl, r.is_left);
            emit_csswap_w_arm_arr<W>(in, tr, r.is_right);
        }
    }
}

// Reverse walk — PRE-order mirror of forward: emit at this node first
// (right arm then left arm — reversed from forward), then recurse.
template <std::size_t Nprime, std::size_t FlatIdx, std::size_t W>
inline void reverse_walk(
    const std::array<BBRouter, Nprime - 1u>& routers,
    std::array<std::array<qbool, W>, (Nprime >= 2u) ? (Nprime - 2u) : 0u>& transits,
    std::array<qbool, W>& bus, const qint_t<W>* a) {
    constexpr std::size_t depth = ct_log2(Nprime);
    constexpr std::size_t Level = level_of(FlatIdx);
    constexpr bool is_leaf_side = (Level + 1u == depth);
    constexpr std::size_t LeftChild  = 2u * FlatIdx + 1u;
    constexpr std::size_t RightChild = 2u * FlatIdx + 2u;
    BBRouter& r = const_cast<BBRouter&>(routers[FlatIdx]);
    if constexpr (is_leaf_side) {
        constexpr std::size_t LeafStart = (Nprime / 2u) - 1u;
        constexpr std::size_t LocalIdx  = FlatIdx - LeafStart;
        constexpr std::size_t LeafL = bit_reverse(2u * LocalIdx,      depth);
        constexpr std::size_t LeafR = bit_reverse(2u * LocalIdx + 1u, depth);
        if constexpr (FlatIdx == 0u) {
            emit_csswap_w_arm_qint<W>(bus, const_cast<qint_t<W>&>(a[LeafR]), r.is_right);
            emit_csswap_w_arm_qint<W>(bus, const_cast<qint_t<W>&>(a[LeafL]), r.is_left);
        } else {
            auto& in = transits[FlatIdx - 1u];
            emit_csswap_w_arm_qint<W>(in, const_cast<qint_t<W>&>(a[LeafR]), r.is_right);
            emit_csswap_w_arm_qint<W>(in, const_cast<qint_t<W>&>(a[LeafL]), r.is_left);
        }
    } else {
        auto& tl = transits[LeftChild  - 1u];
        auto& tr = transits[RightChild - 1u];
        if constexpr (FlatIdx == 0u) {
            emit_csswap_w_arm_arr<W>(bus, tr, r.is_right);
            emit_csswap_w_arm_arr<W>(bus, tl, r.is_left);
        } else {
            auto& in = transits[FlatIdx - 1u];
            emit_csswap_w_arm_arr<W>(in, tr, r.is_right);
            emit_csswap_w_arm_arr<W>(in, tl, r.is_left);
        }
        reverse_walk<Nprime, LeftChild,  W>(routers, transits, bus, a);
        reverse_walk<Nprime, RightChild, W>(routers, transits, bus, a);
    }
}

// ── Public entry point ───────────────────────────────────────────────────────
template <std::size_t Nprime, std::size_t W>
inline void bb_bus_traverse(
    const std::array<BBRouter, Nprime - 1u>& routers,
    std::array<std::array<qbool, W>, (Nprime >= 2u) ? (Nprime - 2u) : 0u>& transits,
    std::array<qbool, W>& bus,
    const qint_t<W>* a,
    qint_t<W>& b) {
    static_assert(Nprime >= 2u, "bb_bus_traverse: Nprime must be >= 2");
    static_assert((Nprime & (Nprime - 1u)) == 0u,
                  "bb_bus_traverse: Nprime must be a power of 2");
    static_assert(W >= 1u, "bb_bus_traverse: W must be >= 1");
    forward_walk<Nprime, 0u, W>(routers, transits, bus, a);
    emit_b_xor_bus<W>(bus, b);
    reverse_walk<Nprime, 0u, W>(routers, transits, bus, a);
}

}  // namespace detail_qram_bb
}  // namespace sturm
