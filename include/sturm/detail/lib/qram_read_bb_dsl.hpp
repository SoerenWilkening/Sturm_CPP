// qram_read_bb_dsl.hpp — sturm-44bt.3 (Beat BB3).
//
// Top-level bucket-brigade DSL composing BB1 (router setup / teardown,
// `qram_read_bb_routers.hpp`) and BB2 (bus traversal, `qram_read_bb_bus.hpp`)
// into the public `lib_qram_read_bb_dsl<W, N>` template (PRD
// `docs/prd_qram_backend_bb.md` §2, §4; plan `docs/plan_qram_backend_bb.md`
// §5 Beat BB3). First beat where PRD §2 G1 / G3 / G5 / G6 are testable
// end-to-end at the DSL layer; BB4 then wires it into `QRAM_read`.
//
// Surface (under `sturm::`):
//
//   template <std::size_t W, std::size_t N>
//   inline void lib_qram_read_bb_dsl(const qint_t<W>* a,
//                                    qint_t<W>& i,
//                                    qint_t<W>& b);
//
// Algorithm (PRD §4):
//   1. Phase 1: bb_setup_routers<N>(i, routers) — sets up Nprime-1 router
//      states encoding the address path through the binary tree.
//   2. Phase 2: bb_bus_traverse<N, W>(routers, transits, bus, a, b) —
//      walks the bus through the tree (forward POST-order CSWAP chain),
//      XORs `b ^= bus` at the root, then walks back (reverse PRE-order
//      mirror) — restoring the bus / transit qubits to |0⟩.
//   3. Phase 3: bb_teardown_routers<N>(i, routers) — inverse of Phase 1.
//
// Self-adjoint: the entire body is self-inverse (PRD §4.4 / `_adj`).
//
// Scope (BB3): asserts `is_pow2(N)` — BB5 (sturm-44bt.5) lifts the gate
// and adds phantom-leaf padding for non-pow2 N.
//
// RAII: routers / transits / bus are stack-allocated `std::array`s.
// Each `qbool` is lazily promoted to a quantum qubit by the BB1/BB2
// helpers when a backend context is live; the helper releases each
// allocated qubit back to `QubitPool` at scope exit (mirrors
// `qram_read_dsl.hpp:188-194` for `eq_k`). Per-call ancilla cost from
// PRD §4.5: `2(N - 1) + (N - 2) * W` (router pairs + transit blocks +
// bus). Frontend-only callers (no backend ctx) skip the qubit alloc /
// release path entirely.
//
// Counter-mode bumps (umbrella `qram_read` + split `qrom_read` /
// `qreg_read`) live on the public `QRAM_read` site (BB4) — NOT in this
// DSL helper. The DSL is a gate-emission body; telemetry is the
// dispatcher's responsibility.
//
// Threading: header-only inline templates; no thread-locals introduced.
// LoC budget: ≤ 200 (plan §1, §5 / Beat BB3).

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/detail/lib/qram_read_bb_routers.hpp"
#include "sturm/detail/lib/qram_read_bb_bus.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/core/context.hpp"
#  include "sturm/core/qubit_pool.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace sturm {

namespace detail_qram_bb {

// ── Compile-time helpers ─────────────────────────────────────────────
// `is_pow2(N)` — N is a positive power of 2 (N == 0 → false). Used by
// the BB3 `static_assert` gate; BB5 will lift the precondition by
// computing `next_pow2(N)`.
constexpr bool is_pow2(std::size_t n) noexcept {
    return (n != 0u) && ((n & (n - 1u)) == 0u);
}

// `next_pow2(N)` — smallest power of 2 ≥ N. `next_pow2(0) == 1`
// (degenerate; BB3 forbids N == 0). Used by BB5 once the pow2 gate
// lifts; BB3 also calls it to keep `Nprime` named consistently.
constexpr std::size_t next_pow2(std::size_t n) noexcept {
    if (n <= 1u) return 1u;
    std::size_t p = 1u;
    while (p < n) p <<= 1u;
    return p;
}

// Release every qubit owned by a `qbool` back to the pool; clear the
// state to "classical false" (qubits[0] = -1, super_mask = 0). Called
// for every router / transit / bus qubit at scope exit (B6 RAII).
inline void release_qbool(qbool& q) noexcept {
#ifdef STURM_BACKEND_ENABLED
    if (q.qubits[0] >= 0) {
        QubitPool::instance().release(q.qubits[0]);
        q.qubits[0]  = -1;
        q.super_mask = 0u;
    }
#else
    (void)q;
#endif
}

}  // namespace detail_qram_bb

// ── lib_qram_read_bb_dsl ─────────────────────────────────────────────
// BB top-level DSL — composes BB1 setup → BB2 bus → BB1 teardown into
// a single self-adjoint gate-emission body. See file header for the
// per-phase algorithm + RAII / pool discipline.
//
// `i` is taken by mutable reference because BB1's `bb_setup_routers` /
// `bb_teardown_routers` build router-driving qbools off `i.bit(level)`
// proxies — those proxies need a mutable qint. Net change on `i` is
// zero (setup + teardown are mutual inverses; `i.super_mask` and
// `i.value` are unchanged across the helper).
template <std::size_t W, std::size_t N>
inline void lib_qram_read_bb_dsl(const qint_t<W>* a,
                                 qint_t<W>& i,
                                 qint_t<W>& b) {
    static_assert(W >= 1u, "lib_qram_read_bb_dsl: W must be >= 1");
    static_assert(N >= 2u,
                  "lib_qram_read_bb_dsl: N must be >= 2 (BB3 minimum)");
    static_assert(detail_qram_bb::is_pow2(N),
                  "lib_qram_read_bb_dsl: BB3 requires N to be a power of 2; "
                  "BB5 (sturm-44bt.5) lifts this gate with phantom-leaf padding");
    static_assert(W >= detail_qram_bb::ct_log2(N),
                  "lib_qram_read_bb_dsl: address width W must cover N");

    // BB3: Nprime == N (the pow2 gate above guarantees this). BB5 will
    // replace this with `next_pow2(N)` once phantom-leaf padding lands.
    constexpr std::size_t Nprime = N;

    // ── RAII ancilla allocation (BB-side) ─────────────────────────
    // routers: Nprime - 1 BBRouter pairs (is_left, is_right qbools).
    // transits: (Nprime - 2) W-wide qbool blocks (internal-node bus
    //   buffers). Nprime == 2 has zero transits — leaf swap exchanges
    //   `bus` directly with `a[]`.
    // bus: one W-wide qbool block (root-side carrier).
    // Routers are promoted lazily by BB1's promote_router on first
    // write; transits / bus we promote up front so BB2 sees live qubits.
    std::array<detail_qram_bb::BBRouter, (Nprime >= 1u) ? (Nprime - 1u) : 0u>
        routers{};
    std::array<std::array<qbool, W>, (Nprime >= 2u) ? (Nprime - 2u) : 0u>
        transits{};
    std::array<qbool, W> bus{};

#ifdef STURM_BACKEND_ENABLED
    if (sturm_get_thread_context() != nullptr) {
        for (auto& blk : transits)
            for (auto& q : blk)
                if (q.qubits[0] < 0) {
                    q.qubits[0]  = QubitPool::instance().allocate();
                    q.super_mask = 1ULL;
                }
        for (auto& q : bus)
            if (q.qubits[0] < 0) {
                q.qubits[0]  = QubitPool::instance().allocate();
                q.super_mask = 1ULL;
            }
    }
#endif

    // ── Phase 1 → Phase 2 → Phase 3 ───────────────────────────────
    detail_qram_bb::bb_setup_routers<Nprime, W>(i, routers);
    detail_qram_bb::bb_bus_traverse<Nprime, W>(routers, transits, bus, a, b);
    detail_qram_bb::bb_teardown_routers<Nprime, W>(i, routers);

    // ── Release ancillae back to the pool ────────────────────────
    // After Phase 3 every ancilla is in |0⟩ (B6 / B10). Release in the
    // EXACT REVERSE of allocation order so a subsequent call recycles
    // the same indices (LIFO; QubitPool's `free_.back()`). This pins
    // the recording-sink round-trip balance: forward + adjoint emit
    // SAME (kind, qubit-group) tuples instead of allocation-keyed
    // disjoint sets. Alloc order: transits[t][j], bus[j], routers
    // (lazy, BB1 pre-order; promote_router does is_left then is_right).
#ifdef STURM_BACKEND_ENABLED
    for (std::size_t k = routers.size(); k-- > 0u; ) {
        detail_qram_bb::release_qbool(routers[k].is_right);
        detail_qram_bb::release_qbool(routers[k].is_left);
    }
    for (std::size_t j = bus.size(); j-- > 0u; )
        detail_qram_bb::release_qbool(bus[j]);
    for (std::size_t t = transits.size(); t-- > 0u; )
        for (std::size_t j = transits[t].size(); j-- > 0u; )
            detail_qram_bb::release_qbool(transits[t][j]);
#endif
}

}  // namespace sturm

// Sibling header carries `__lib_qram_read_bb_dsl_adj` +
// `STURM_REGISTER_ADJOINT`. Auto-included so direct callers of
// `lib_qram_read_bb_dsl` pick up the registration without an extra
// `#include` (matches `qram_read_dsl.hpp` ↔ `qram_read_dsl_adj.hpp`).
#include "sturm/detail/lib/qram_read_bb_dsl_adj.hpp"
