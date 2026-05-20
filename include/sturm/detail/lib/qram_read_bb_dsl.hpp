// qram_read_bb_dsl.hpp — sturm-44bt.3 (BB3) + sturm-44bt.5 (BB5).
//
// Top-level bucket-brigade DSL composing BB1 (router setup / teardown,
// `qram_read_bb_routers.hpp`) and BB2 (bus traversal, `qram_read_bb_bus.hpp`)
// into the public `lib_qram_read_bb_dsl<W, N>` template (PRD
// `docs/prd_qram_backend_bb.md` §2, §4, §6; plan §5 Beats BB3, BB5).
//
// Surface (under `sturm::`):
//
//   template <std::size_t W, std::size_t N>
//   inline void lib_qram_read_bb_dsl(const qint_t<W>* a,
//                                    qint_t<W>& i,
//                                    qint_t<W>& b);
//
// Algorithm (PRD §4):
//   1. Phase 1: bb_setup_routers<Nprime>(i, routers) — Nprime-1 router
//      states encoding the address path through the binary tree.
//   2. Phase 2: bb_bus_traverse<Nprime, W>(routers, transits, bus,
//      padded_a, b) — bus walks the tree (POST-order CSWAP chain),
//      XORs `b ^= bus`, then walks back (PRE-order mirror) — restoring
//      the bus / transit qubits to |0⟩.
//   3. Phase 3: bb_teardown_routers<Nprime>(i, routers) — inverse of (1).
//
// Self-adjoint: the entire body is self-inverse (PRD §4.4 / `_adj`).
//
// BB5 padding (PRD §6): for N not a power of 2, Nprime = next_pow2(N).
// Phantom leaves at slots [N, Nprime) are realised as W-qubit transit
// ancilla blocks initialised to |0⟩ at call entry. The padded leaf
// view `padded_a` has length Nprime — slots [0, N) are non-owning
// aliases of the user-provided `a[k]`, slots [N, Nprime) are owning
// phantom blocks whose qubits are released at scope exit (B6 RAII).
// At the BB2 leaf-side CSWAP layer, phantom slots emit the SAME gate
// stream as real quantum leaves (their qubits[j] >= 0 takes the
// standard 2-CNOT + 1-CCX scalar path) — so the gate budget recomputes
// from (Nprime, W) and matches the plan §4.4 closed form at Nprime,
// not N.
//
// RAII: routers / transits / bus / phantoms are stack-allocated. Each
// `qbool` is lazily promoted to a quantum qubit by the BB1/BB2 helpers
// when a backend context is live; the helper releases each allocated
// qubit back to `QubitPool` at scope exit. Per-call ancilla cost from
// PRD §4.5: `2(Nprime - 1) +
// (Nprime - 2) * W` (routers + transit blocks + bus), plus the BB5
// phantom (Nprime - N) * W. Frontend-only callers (no backend ctx)
// skip the qubit alloc / release path entirely.
//
// Counter-mode bumps (umbrella `qram_read` + split `qrom_read` /
// `qreg_read`) live on the public `QRAM_read` site (BB4) — NOT in
// this DSL helper.
//
// LoC budget: ≤ 250 (plan §1, §5 / Beat BB5).

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
// per-phase algorithm + RAII / pool discipline + BB5 padding.
//
// `i` is taken by mutable reference because BB1's `bb_setup_routers` /
// `bb_teardown_routers` build router-driving qbools off `i.bit(level)`
// proxies — those proxies need a mutable qint. Net change on `i` is
// zero (setup + teardown are mutual inverses).
template <std::size_t W, std::size_t N>
inline void lib_qram_read_bb_dsl(const qint_t<W>* a,
                                 qint_t<W>& i,
                                 qint_t<W>& b) {
    static_assert(W >= 1u, "lib_qram_read_bb_dsl: W must be >= 1");
    static_assert(N >= 2u, "lib_qram_read_bb_dsl: N must be >= 2");
    // BB5 (sturm-44bt.5): is_pow2(N) gate lifted. For non-pow2 N,
    // Nprime = next_pow2(N) and phantom leaves [N, Nprime) supply
    // |0⟩^W ancilla blocks at call entry.
    constexpr std::size_t Nprime = detail_qram_bb::next_pow2(N);
    static_assert(W >= detail_qram_bb::ct_log2(Nprime),
                  "lib_qram_read_bb_dsl: address width W must cover Nprime");

    // ── RAII ancilla allocation (BB-side) ─────────────────────────
    // routers : Nprime - 1 BBRouter pairs (is_left, is_right qbools).
    // transits: (Nprime - 2) W-wide qbool blocks (internal-node bus
    //   buffers). Nprime == 2 has zero transits — leaf swap exchanges
    //   `bus` directly with the leaf.
    // bus     : one W-wide qbool block (root-side carrier).
    // phantoms: (Nprime - N) W-wide qint_t blocks initialised to |0⟩
    //   (BB5; sturm-44bt.5). Empty array when N == Nprime.
    std::array<detail_qram_bb::BBRouter, (Nprime >= 1u) ? (Nprime - 1u) : 0u>
        routers{};
    std::array<std::array<qbool, W>, (Nprime >= 2u) ? (Nprime - 2u) : 0u>
        transits{};
    std::array<qbool, W> bus{};
    // BB5 phantom-leaf storage. Owning qint_t<W> blocks (default
    // owning_=true) so qubits are released at scope exit (B6).
    constexpr std::size_t kPhantomCount = Nprime - N;
    std::array<qint_t<W>, (kPhantomCount > 0u) ? kPhantomCount : 0u> phantoms{};

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
        // BB5: allocate each phantom leaf's W qubits to |0⟩ — they are
        // pool-fresh qubits in the |0⟩ baseline, so no preparation
        // gate is emitted. owning_ stays true so destructors release.
        for (auto& ph : phantoms) {
            for (std::size_t j = 0; j < W; ++j) {
                ph.qubits[j] = QubitPool::instance().allocate();
                ph.super_mask |= (1ULL << j);
            }
            ph.value = 0;
        }
    }
#endif

    // ── BB5: build the padded leaf view of length Nprime. ─────────
    // Slots [0, N) are non-owning aliases of the user-provided a[k]
    // (qubit indices copied; owning_ = false to suppress qint dtor
    // release). Slots [N, Nprime) point at phantom leaves above.
    std::array<qint_t<W>, Nprime> padded_a{};
    for (std::size_t k = 0; k < N; ++k) {
        for (std::size_t j = 0; j < W; ++j) padded_a[k].qubits[j] = a[k].qubits[j];
        padded_a[k].value      = a[k].value;
        padded_a[k].super_mask = a[k].super_mask;
        padded_a[k].owning_    = false;  // alias — never release a[k]'s qubits
    }
    if constexpr (kPhantomCount > 0u) {
        for (std::size_t k = 0; k < kPhantomCount; ++k) {
            for (std::size_t j = 0; j < W; ++j)
                padded_a[N + k].qubits[j] = phantoms[k].qubits[j];
            padded_a[N + k].value      = phantoms[k].value;
            padded_a[N + k].super_mask = phantoms[k].super_mask;
            padded_a[N + k].owning_    = false;  // phantoms[k] owns
        }
    }

    // ── Phase 1 → Phase 2 → Phase 3 ───────────────────────────────
    detail_qram_bb::bb_setup_routers<Nprime, W>(i, routers);
    detail_qram_bb::bb_bus_traverse<Nprime, W>(routers, transits, bus,
                                                padded_a.data(), b);
    detail_qram_bb::bb_teardown_routers<Nprime, W>(i, routers);

    // ── Release ancillae back to the pool ────────────────────────
    // After Phase 3 every ancilla is in |0⟩ (B6 / B10). Release in
    // the EXACT REVERSE of allocation order so a subsequent call
    // recycles the same indices (LIFO; QubitPool's `free_.back()`).
    // This pins the recording-sink round-trip balance: forward +
    // adjoint emit SAME (kind, qubit-group) tuples. Alloc order:
    // transits[t][j], bus[j], phantoms[k][j], routers (lazy via
    // BB1 promote_router). `phantoms` qint_t destructors release the
    // (Nprime-N)*W phantom qubits automatically — but we'd like LIFO
    // ordering wrt routers, so we explicitly release here first and
    // suppress the dtor release via owning_ = false.
#ifdef STURM_BACKEND_ENABLED
    for (std::size_t k = routers.size(); k-- > 0u; ) {
        detail_qram_bb::release_qbool(routers[k].is_right);
        detail_qram_bb::release_qbool(routers[k].is_left);
    }
    if constexpr (kPhantomCount > 0u) {
        for (std::size_t k = phantoms.size(); k-- > 0u; ) {
            for (std::size_t j = W; j-- > 0u; ) {
                if (phantoms[k].qubits[j] >= 0) {
                    QubitPool::instance().release(phantoms[k].qubits[j]);
                    phantoms[k].qubits[j] = -1;
                }
            }
            phantoms[k].super_mask = 0u;
            phantoms[k].owning_    = false;  // suppress dtor double-release
        }
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
// `#include`.
#include "sturm/detail/lib/qram_read_bb_dsl_adj.hpp"
