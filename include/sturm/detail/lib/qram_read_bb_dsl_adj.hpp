// qram_read_bb_dsl_adj.hpp — sturm-44bt.3 (Beat BB3).
//
// Adjoint sibling header for `lib_qram_read_bb_dsl<W, N>` (BB3 /
// sturm-44bt.3 / `qram_read_bb_dsl.hpp`). Plan reference:
// docs/plan_qram_backend_bb.md §5 Beat BB3, goal G3.
//
// Surface:
//   template <std::size_t W, std::size_t N>
//   inline void __lib_qram_read_bb_dsl_adj(const qint_t<W>* a,
//                                          qint_t<W>& i,
//                                          qint_t<W>& b);
//
// The BB body is self-adjoint (PRD `docs/prd_qram_backend_bb.md` §4.4):
// each Phase is structurally its own inverse — Phase 1 (router setup)
// and Phase 3 (router teardown) are mutual inverses by construction;
// Phase 2 (forward bus walk + `b ^= bus` + reverse bus walk) is a
// CSWAP-tree sandwich around a self-inverse XOR payload. Re-running
// the forward body therefore restores every qubit to its entry state.
//
// We still register the pair explicitly via `STURM_REGISTER_ADJOINT`
// so `invert<&lib_qram_read_bb_dsl<W, N>>()(...)` resolves at the
// uncompute call site rather than via call-site inlining (placement-
// audit tooling, P9c spirit; matches `c_and_dsl.hpp` ↔ `_adj.hpp`).
//
// Auto-included from the bottom of `qram_read_bb_dsl.hpp` so callers
// pick up the registration without an extra `#include`.
//
// The public `__QRAM_read_adj` overloads in `qram/qram_read.hpp` are
// wired to forward to this adjoint in BB4 (sturm-44bt.4).
//
// Threading: header-only inline templates; no thread-locals introduced.
// LoC budget: ≤ 100 (plan §1, §5 / Beat BB3).

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/routines/invert.hpp"

// Defensive include: `qram_read_bb_dsl_adj.hpp` is normally auto-included
// from the bottom of `qram_read_bb_dsl.hpp`, but direct callers (none
// expected today) still need the forward helper visible.
#include "sturm/detail/lib/qram_read_bb_dsl.hpp"

#include <cstddef>

namespace sturm {

// ── __lib_qram_read_bb_dsl_adj ──────────────────────────────────────
// Adjoint of `lib_qram_read_bb_dsl<W, N>`. The body re-runs the
// forward sweep verbatim — see the file header for the self-adjoint
// argument. Single-line body keeps "compute == uncompute" obvious;
// placement-audit tooling can also match against the named adjoint
// symbol via the `STURM_REGISTER_ADJOINT` enrolment below.
template <std::size_t W, std::size_t N>
inline void __lib_qram_read_bb_dsl_adj(const qint_t<W>* a,
                                       qint_t<W>& i,
                                       qint_t<W>& b) {
    lib_qram_read_bb_dsl<W, N>(a, i, b);
}

}  // namespace sturm

// ── STURM_REGISTER_ADJOINT enrolment per P9c ─────────────────────────
// `STURM_REGISTER_ADJOINT(fn, adj)` would split `<W, N>` on the comma —
// preprocessor macros don't see template-argument lists. We use
// `STURM_REGISTER_ADJOINT_2T(W, N)` (defined inline below) to forward
// each `<W, N>` combo to the canonical macro after assembling the
// fully-qualified template names. This keeps the registration
// surface macro-driven (mirrors v1's `<W>` one-arg site) while
// supporting BB's `<W, N>` two-arg shape. (W, N) combos used by
// in-tree BB3 / BB4 / BB5 tests: (2,2), (2,4), (4,2), (4,4), (4,8).
#ifdef STURM_BACKEND_ENABLED
#define STURM_REGISTER_ADJOINT_2T(W_, N_) \
    STURM_REGISTER_ADJOINT(sturm::lib_qram_read_bb_dsl<W_##u STURM_BBC N_##u>, \
                           sturm::__lib_qram_read_bb_dsl_adj<W_##u STURM_BBC N_##u>)
#define STURM_BBC ,
STURM_REGISTER_ADJOINT_2T(2, 2)
STURM_REGISTER_ADJOINT_2T(2, 4)
STURM_REGISTER_ADJOINT_2T(4, 2)
STURM_REGISTER_ADJOINT_2T(4, 4)
STURM_REGISTER_ADJOINT_2T(4, 8)
#undef STURM_BBC
#undef STURM_REGISTER_ADJOINT_2T
#endif
