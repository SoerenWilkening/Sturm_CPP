// qram_read_bb_routers.hpp — sturm-44bt.1 (Beat BB1).
//
// Bucket-brigade router-state primitive + Phase 1 (setup) and Phase 3
// (teardown) of the BB algorithm (PRD `docs/prd_qram_backend_bb.md`
// §4.1, §4.3; plan `docs/plan_qram_backend_bb.md` §5 Beat BB1).
//
// Surface (under `sturm::detail_qram_bb`):
//
//   struct BBRouter { qbool is_left; qbool is_right; };
//
//   template <std::size_t Nprime, std::size_t W>
//   inline void bb_setup_routers(qint_t<W>& addr,
//                                std::array<BBRouter, Nprime - 1>& routers);
//
//   template <std::size_t Nprime, std::size_t W>
//   inline void bb_teardown_routers(qint_t<W>& addr,
//                                   std::array<BBRouter, Nprime - 1>& routers);
//
// Algorithm (PRD §4.1):
//   - Root (level 0): root.is_right ^= addr[0]; root.is_left ^= addr[0];
//     X(root.is_left) — two CNOTs + one bare X.
//   - Non-root r at level ℓ with parent p: WHEN(p.is_X) { r.is_right ^=
//     addr[ℓ]; r.is_left ^= addr[ℓ]; X(r.is_left); } — two CCXs + one
//     CNOT (p.is_X = p.is_left for left children, p.is_right for right).
//     B5a depth-1 invariant: one control bit live (the parent state).
//
// Closed-form Phase 1 gate count (plan §4.1):
//   2·(N′ − 2) CCX + N′ CNOT + 1 X.
// Phase 3 = Phase 1 reversed (each gate self-inverse); round trip
// balanced gate-by-gate.
//
// Recursive-unrolling — the walk dispatches at compile time via `if
// constexpr` on (Level, NodeIdx). Each instantiation emits one node's
// gate cluster. Router layout — level-order, flat index:
//   flat_idx(level, idx) = (2^level − 1) + idx.
// Left child of (level, idx) = (level+1, 2·idx); right = (level+1, 2·idx+1).
//
// LoC budget: ≤ 250 (plan §1, §5 / Beat BB1).

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/control/when.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qbool_ops.hpp"
#  include "sturm/detail/qtypes/bit_proxy.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace sturm {
namespace detail_qram_bb {

// PRD §4.1 encoded states: (0,0)=|wait⟩, (1,0)=|L⟩, (0,1)=|R⟩, (1,1)
// forbidden. Both qbools lazily promoted to quantum by promote_router().
// Caller owns the storage (B6 / B10).
struct BBRouter {
    qbool is_left;
    qbool is_right;
};

constexpr std::size_t ct_log2(std::size_t n) noexcept {
    std::size_t r = 0u;
    while ((std::size_t{1} << r) < n) ++r;
    return r;
}

inline void promote_router(BBRouter& r) noexcept {
#ifdef STURM_BACKEND_ENABLED
    if (sturm_get_thread_context() == nullptr) return;
    if (r.is_left.qubits[0]  < 0) {
        r.is_left.qubits[0]   = QubitPool::instance().allocate();
        r.is_left.super_mask  = 1ULL;
    }
    if (r.is_right.qubits[0] < 0) {
        r.is_right.qubits[0]  = QubitPool::instance().allocate();
        r.is_right.super_mask = 1ULL;
    }
#else
    (void)r;
#endif
}

// One node's three-gate cluster (Phase 1 emission order):
//   r.is_right ^= addr_bit;  r.is_left ^= addr_bit;  X(r.is_left);
// Under a single WHEN: 2 CCX + 1 CNOT. Bare: 2 CNOT + 1 X.
template <std::size_t W>
inline void setup_node_body(BBRouter& r, qint_t<W>& addr, std::size_t level) {
    promote_router(r);
    qbool addr_bit = static_cast<const qint_t<W>&>(addr)[level];
    r.is_right ^= addr_bit;
    r.is_left  ^= addr_bit;
    r.is_left.value ^= int64_t{1};
#ifdef STURM_BACKEND_ENABLED
    if (sturm_get_thread_context() != nullptr) {
        emit_X_lifted(get_ctx(),
                      static_cast<uint32_t>(r.is_left.qubits[0]));
    }
#endif
}

// Phase 3 = Phase 1 in reverse: X → is_left ^= → is_right ^=.
template <std::size_t W>
inline void teardown_node_body(BBRouter& r, qint_t<W>& addr,
                               std::size_t level) {
    promote_router(r);
    r.is_left.value ^= int64_t{1};
#ifdef STURM_BACKEND_ENABLED
    if (sturm_get_thread_context() != nullptr) {
        emit_X_lifted(get_ctx(),
                      static_cast<uint32_t>(r.is_left.qubits[0]));
    }
#endif
    qbool addr_bit = static_cast<const qint_t<W>&>(addr)[level];
    r.is_left  ^= addr_bit;
    r.is_right ^= addr_bit;
}

// Compile-time pre-order walk (Phase 1) and post-order walk (Phase 3).
template <std::size_t Nprime, std::size_t Level, std::size_t NodeIdx,
          std::size_t W>
inline void setup_walk(qint_t<W>& addr,
                       std::array<BBRouter, Nprime - 1u>& routers);

template <std::size_t Nprime, std::size_t Level, std::size_t NodeIdx,
          std::size_t W>
inline void teardown_walk(qint_t<W>& addr,
                          std::array<BBRouter, Nprime - 1u>& routers);

template <std::size_t Nprime, std::size_t Level, std::size_t NodeIdx,
          std::size_t W>
inline void setup_walk(qint_t<W>& addr,
                       std::array<BBRouter, Nprime - 1u>& routers) {
    constexpr std::size_t flat_idx = ((std::size_t{1} << Level) - 1u) + NodeIdx;
    static_assert(flat_idx < (Nprime - 1u),
                  "setup_walk: flat_idx out of range");
    constexpr std::size_t depth = ct_log2(Nprime);

    if constexpr (Level == 0u) {
        setup_node_body<W>(routers[flat_idx], addr, /*level=*/0u);
    } else {
        constexpr std::size_t parent_flat =
            ((std::size_t{1} << (Level - 1u)) - 1u) + (NodeIdx / 2u);
        constexpr bool is_right_child = ((NodeIdx & 1u) != 0u);
        BBRouter& parent = routers[parent_flat];
        promote_router(parent);
        if constexpr (is_right_child) {
            WHEN(parent.is_right) {
                setup_node_body<W>(routers[flat_idx], addr, /*level=*/Level);
            }
        } else {
            WHEN(parent.is_left)  {
                setup_node_body<W>(routers[flat_idx], addr, /*level=*/Level);
            }
        }
    }

    if constexpr (Level + 1u < depth) {
        setup_walk<Nprime, Level + 1u, 2u * NodeIdx, W>(addr, routers);
        setup_walk<Nprime, Level + 1u, 2u * NodeIdx + 1u, W>(addr, routers);
    }
}

template <std::size_t Nprime, std::size_t Level, std::size_t NodeIdx,
          std::size_t W>
inline void teardown_walk(qint_t<W>& addr,
                          std::array<BBRouter, Nprime - 1u>& routers) {
    constexpr std::size_t flat_idx = ((std::size_t{1} << Level) - 1u) + NodeIdx;
    static_assert(flat_idx < (Nprime - 1u),
                  "teardown_walk: flat_idx out of range");
    constexpr std::size_t depth = ct_log2(Nprime);

    if constexpr (Level + 1u < depth) {
        // Reverse-order descent: right subtree, then left.
        teardown_walk<Nprime, Level + 1u, 2u * NodeIdx + 1u, W>(addr, routers);
        teardown_walk<Nprime, Level + 1u, 2u * NodeIdx, W>(addr, routers);
    }

    if constexpr (Level == 0u) {
        teardown_node_body<W>(routers[flat_idx], addr, /*level=*/0u);
    } else {
        constexpr std::size_t parent_flat =
            ((std::size_t{1} << (Level - 1u)) - 1u) + (NodeIdx / 2u);
        constexpr bool is_right_child = ((NodeIdx & 1u) != 0u);
        BBRouter& parent = routers[parent_flat];
        promote_router(parent);
        if constexpr (is_right_child) {
            WHEN(parent.is_right) {
                teardown_node_body<W>(routers[flat_idx], addr,
                                      /*level=*/Level);
            }
        } else {
            WHEN(parent.is_left)  {
                teardown_node_body<W>(routers[flat_idx], addr,
                                      /*level=*/Level);
            }
        }
    }
}

// ── Public entry points ──────────────────────────────────────────────────────

template <std::size_t Nprime, std::size_t W>
inline void bb_setup_routers(qint_t<W>& addr,
                             std::array<BBRouter, Nprime - 1u>& routers) {
    static_assert(Nprime >= 2u, "bb_setup_routers: Nprime must be >= 2");
    static_assert((Nprime & (Nprime - 1u)) == 0u,
                  "bb_setup_routers: Nprime must be a power of 2");
    static_assert(W >= ct_log2(Nprime),
                  "bb_setup_routers: addr width must cover Nprime");
    setup_walk<Nprime, 0u, 0u, W>(addr, routers);
}

template <std::size_t Nprime, std::size_t W>
inline void bb_teardown_routers(qint_t<W>& addr,
                                std::array<BBRouter, Nprime - 1u>& routers) {
    static_assert(Nprime >= 2u, "bb_teardown_routers: Nprime must be >= 2");
    static_assert((Nprime & (Nprime - 1u)) == 0u,
                  "bb_teardown_routers: Nprime must be a power of 2");
    static_assert(W >= ct_log2(Nprime),
                  "bb_teardown_routers: addr width must cover Nprime");
    teardown_walk<Nprime, 0u, 0u, W>(addr, routers);
}

}  // namespace detail_qram_bb
}  // namespace sturm
