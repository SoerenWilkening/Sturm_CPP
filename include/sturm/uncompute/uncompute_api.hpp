// uncompute_api.hpp — Public free-function inverse API (transpiler-MVP M3).
//
// Landing zone for explicit `uncompute_*` free functions that the
// Clang LibTooling transpiler emits in place of RAII destructor-driven
// uncomputation.  See docs/prd_transpiler_uncompute.md.
//
// MVP scope: exactly one inverse — `uncompute_or(r, a, b)`.
// Phases A-D (docs/roadmap_transpiler_post_mvp.md) will extend this header
// with inverses for AND, XOR, NOT, compare, arithmetic, etc.
//
// Design contract:
//   - Pure free functions.  No hidden state, no RAII.
//   - Operate through the active `BackendContext` via `execute_gate` /
//     `current_sink()`.  Do NOT allocate or release qubits.
//   - Preconditions: all operands live; `r` owning; `a` and `b` unchanged
//     since the forward operation that populated `r`.
//   - Postconditions: the ancilla qubit held by `r` is returned to |0>,
//     the gate stream records the inverse sequence, and the input operands
//     are left byte-identical (Bennett discipline).
//
// This file is header-only (declarations).  The body lives in
// src/sturm/uncompute/uncompute_api.cpp.
//
// LOC budget: ≤ 50 (implementation plan M3).
#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cstddef>

namespace sturm {

// ── uncompute_or ──────────────────────────────────────────────────────────────
//
// Uncompute an inclusive-OR ancilla produced by `r = a | b`.
// Emits the inverse of the forward OR decomposition against the active
// sink.  For the quantum-quantum path this is CCX(a, b, r) followed by
// CX(b, r) and CX(a, r), which is the exact reverse of the forward
// CX + CX + CCX sequence used by qbool_ops.hpp and bit_proxy.hpp.
//
// The mixed and classical cases follow the four-quadrant rules already
// used by `materialize_or` (see include/sturm/qtypes/bit_proxy.hpp) and
// each adjoint is the exact reversed gate list of the forward emission
// with every gate replaced by its inverse (X, CX and CCX are each
// self-inverse):
//   (a_q, b_q)   fwd CX(a,r)+CX(b,r)+CCX(a,b,r)
//                adj CCX(a,b,r)+CX(b,r)+CX(a,r)
//   (a_q, b=0)   fwd CX(a,r)                       adj CX(a,r)
//   (a_q, b=1)   fwd CX(a,r)+X(r)+CX(a,r)         adj CX(a,r)+X(r)+CX(a,r)
//   (a=0, b_q)   fwd CX(b,r)                       adj CX(b,r)
//   (a=1, b_q)   fwd X(r)+CX(b,r)+CX(b,r)         adj CX(b,r)+CX(b,r)+X(r)
//   (a=0, b=0)   fwd (none)                        adj (none)
//   (a=0, b=1)   fwd X(r)                          adj X(r)
//   (a=1, b=0)   fwd X(r)                          adj X(r)
//   (a=1, b=1)   fwd X(r)+X(r)+X(r)               adj X(r)+X(r)+X(r)
//
// Preconditions: r, a, b all live; r owning; a and b byte-identical to
// their state at the time of the forward OR.  Does not release any qubits.
void uncompute_or(qbool& r, const qbool& a, const qbool& b);

// ── Phase C — qint-qint arithmetic inverses ──────────────────────────────────
//
// Emitted verbatim by sturm-transpile as the inverse of each qint-qint
// compound-assign. Every variant delegates to the forward compound-assign
// on qint_t<W> (defined in qint_arith_v3.hpp); under STURM_BACKEND_ENABLED
// those compound-assigns emit their library circuits against the active
// BackendContext. The transpiler places the call inside the same scope as
// the forward statement so `a` and `b` are still live when the inverse
// runs.
//
// Caveats (user responsibility — the transpiler does not emit a runtime
// guard, matching the Phase B coprime/overflow policy):
//   uncompute_mul_qint: b must be coprime with 2^W (otherwise `a *= b` is
//                       not invertible over the W-bit modular ring).
//   uncompute_div_qint: the product `a * b` must not overflow W bits
//                       (otherwise `a /= b` was lossy and cannot be
//                       undone by multiplication).
//   uncompute_mod_qint: no clean dual exists — the forward op throws away
//                       the quotient. Ships as a stub body pending a real
//                       modular-inverse adjoint (see TODO in the body).
template <std::size_t W>
inline void uncompute_add_qint(qint_t<W>& a, const qint_t<W>& b) { a -= b; }
template <std::size_t W>
inline void uncompute_sub_qint(qint_t<W>& a, const qint_t<W>& b) { a += b; }
template <std::size_t W>
inline void uncompute_mul_qint(qint_t<W>& a, const qint_t<W>& b) { a /= b; }
template <std::size_t W>
inline void uncompute_div_qint(qint_t<W>& a, const qint_t<W>& b) { a *= b; }
template <std::size_t W>
inline void uncompute_mod_qint(qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {
    // TODO(phase-later): real modular-inverse adjoint.
    // No simple dual — the forward `a %= b` throws away the quotient.
}

} // namespace sturm
