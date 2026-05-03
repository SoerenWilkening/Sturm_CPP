// qram_read_dsl_adj.hpp -- sturm-2w6h.5 (Beat B3).
//
// Adjoint sibling header for `lib_qram_read_qrom_dsl` (B2 / sturm-2w6h.4).
// Plan reference: docs/plan_qram_backend.md §5 Beat B3, goal G3.
//
// Surface:
//   template <std::size_t W>
//   inline void __lib_qram_read_qrom_dsl_adj(const qint_t<W>* a,
//                                            std::size_t n,
//                                            qint_t<W>& i,
//                                            qint_t<W>& b);
//
// The QROM body in `lib_qram_read_qrom_dsl` is *self-adjoint* (PRD
// `docs/prd_qram_backend.md` §4 paragraph 4): the predicate
// compute/uncompute pair around a `WHEN(eq_k)`-lifted XOR-fanout
// payload cancels when the same body is run a second time. The
// XOR-fanout itself is a sequence of CX gates (one per set bit of
// `a[k]`) on disjoint targets — each CX is its own adjoint, and they
// commute pairwise on disjoint targets, so the whole payload is
// self-inverse. The predicate compute/uncompute body (B2a /
// `qram_read_predicate.hpp`) is a Toffoli/X-flip sandwich that is
// also self-inverse by construction.
//
// Therefore the adjoint body re-runs the forward sweep verbatim —
// it just calls `lib_qram_read_qrom_dsl(a, n, i, b)`. We still
// register the pair explicitly via `STURM_REGISTER_ADJOINT` so
// `invert<&lib_qram_read_qrom_dsl<W>>()(...)` resolves at the
// uncompute call site rather than relying on call-site inlining
// (placement-audit tooling, P9c spirit; matches the
// `c_and_dsl.hpp` / `c_and_dsl_adj.hpp` pattern from sturm-eum8).
//
// Auto-included from the bottom of `qram_read_dsl.hpp` so callers of
// the forward helper pick up the registration without an extra
// `#include` (matches `c_and_dsl.hpp` ↔ `c_and_dsl_adj.hpp`).
//
// The `__QRAM_read_adj` *public* overloads in
// `include/sturm/qram/qram_read.hpp` (touched in B1 / sturm-2w6h.2)
// forward to `__lib_qram_read_qrom_dsl_adj` for the QROM path —
// see the qreg-side guard in the public adjoint helper for the
// dispatch logic.
//
// Threading: header-only inline templates; no thread-locals introduced.
// LoC budget: <= 150 (plan §1, §5 / B3).

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"

// The forward header pulls in `qram_read_predicate.hpp`, `when.hpp`,
// and (under STURM_BACKEND_ENABLED) `bit_proxy.hpp` — we re-include
// `qram_read_dsl.hpp` for the forward declaration so the adjoint
// body can call into it. Note: this header is auto-included from
// the bottom of `qram_read_dsl.hpp`, so the forward helper is
// already fully declared when `__lib_qram_read_qrom_dsl_adj` is
// defined; the include below is defensive — direct `#include
// "qram_read_dsl_adj.hpp"` callers (none expected) still get the
// forward declaration.
#include "sturm/detail/lib/qram_read_dsl.hpp"

#include <cstddef>

namespace sturm {

// ── __lib_qram_read_qrom_dsl_adj ────────────────────────────────────
// Adjoint of `lib_qram_read_qrom_dsl` for the uncompute pipeline.
// The body re-runs the forward sweep verbatim — see the file header
// for the self-adjoint argument. The single-line body keeps the
// "compute == uncompute" identity obvious from one read; placement-
// audit tooling can also match against the named adjoint symbol via
// the `STURM_REGISTER_ADJOINT` enrolment below.
template <std::size_t W>
inline void __lib_qram_read_qrom_dsl_adj(const qint_t<W>* a,
                                         std::size_t n,
                                         qint_t<W>& i,
                                         qint_t<W>& b) {
    lib_qram_read_qrom_dsl<W>(a, n, i, b);
}

}  // namespace sturm

// ── STURM_REGISTER_ADJOINT enrolment ─────────────────────────────────
// Per-width registration matches the existing pattern in
// `include/sturm/detail/qtypes/lossy_oop.hpp` and
// `include/sturm/qram/qram_read.hpp` — the macro keys on the
// function-pointer VALUE, so each template instantiation needs its
// own `STURM_REGISTER_ADJOINT` line. We enrol the widths used by
// in-tree tests + the public `QRAM_read<8>` registration site:
//   - W=2  (test_qram_read_dsl_adjoint.cpp T1 (N=2, W=2) leg)
//   - W=4  (test_qram_read_dsl_adjoint.cpp T1/T2/T3 (N=4, W=4) legs;
//           also covered by the recording / simulate B2 tests)
//   - W=8  (mirrors the existing public `QRAM_read<8>` registration in
//           `include/sturm/qram/qram_read.hpp`)
// New widths added downstream should append a corresponding
// `STURM_REGISTER_ADJOINT` line here and (where the public arm needs
// it) a paired one on the `QRAM_read<W>` public surface.
#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_qram_read_qrom_dsl<2u>,
                       sturm::__lib_qram_read_qrom_dsl_adj<2u>)
STURM_REGISTER_ADJOINT(sturm::lib_qram_read_qrom_dsl<4u>,
                       sturm::__lib_qram_read_qrom_dsl_adj<4u>)
STURM_REGISTER_ADJOINT(sturm::lib_qram_read_qrom_dsl<8u>,
                       sturm::__lib_qram_read_qrom_dsl_adj<8u>)
#endif
