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
// This file is header-only (declarations + inline template bodies). The
// non-template `uncompute_or` body lives in src/sturm/uncompute/uncompute_api.cpp;
// the template inverses (arithmetic in Phase C, comparisons in Phase D) are
// `inline` here so callers do not need an extra translation unit.
//
// LOC budget: ≤ 300 (CLAUDE.md header budget).
#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qint_compare_v3.hpp"   // detail::bit_array_view + promote
#  include "sturm/lib/compare_dsl.hpp"          // lib_{eq,ne,lt,le,gt,ge}_dsl
#  include "sturm/core/context.hpp"             // sturm_get_thread_context
#endif

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

// ── uncompute_and ─────────────────────────────────────────────────────────────
//
// Uncompute a bitwise-AND ancilla produced by `r = a & b`.  Emits the
// inverse of the forward AND decomposition (see
// include/sturm/qtypes/bit_proxy.hpp `materialize_and`) against the
// active sink.  The forward decomposition is a *single* gate per
// quadrant, so the adjoint is also a single gate — X, CX and CCX are
// each self-inverse:
//   (a_q, b_q)   fwd CCX(a,b,r)                     adj CCX(a,b,r)
//   (a_q, b=0)   fwd (none)                          adj (none)
//   (a_q, b=1)   fwd CX(a,r)                         adj CX(a,r)
//   (a=0, b_q)   fwd (none)                          adj (none)
//   (a=1, b_q)   fwd CX(b,r)                         adj CX(b,r)
//   (a=0, b=0)   fwd (none)                          adj (none)
//   (a=0, b=1)   fwd (none)                          adj (none)
//   (a=1, b=0)   fwd (none)                          adj (none)
//   (a=1, b=1)   fwd X(r)                            adj X(r)
//
// Preconditions: r, a, b all live; r owning; a and b byte-identical to
// their state at the time of the forward AND.  Does not release any
// qubits.  Phase E transpiler-emitted call site: the outer result qbool
// of a compound expression such as `qbool r = (b | c) & d;` — see
// docs/implementation_plan_transpiler_phase_e.md.
void uncompute_and(qbool& r, const qbool& a, const qbool& b);

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

// ── Phase D — qint-qint comparison inverses (sturm-999i) ─────────────────────
//
// Emitted verbatim by sturm-transpile as the inverse of each qint-qint
// comparison `qbool r = (a OP b);`. The forward comparison is produced by
// qint_t<W>::operator OP (see include/sturm/qtypes/qint_compare_v3.hpp) which
// calls the corresponding `lib_{eq,ne,lt,le,gt,ge}_dsl` library routine on a
// freshly-allocated result qubit held by `r`.
//
// The library DSL routines are self-uncomputing under the Bennett discipline:
// each one flips `r` by XOR-ing the comparison result and leaves every
// intermediate ancilla back at |0>. Calling the same routine a second time
// therefore flips `r` once more, returning the ancilla it owns to |0>. This
// header's six inverses are thin wrappers that rebuild the same bit-view
// arrays the forward call used (via `detail::bit_array_view<W>`) and invoke
// the same DSL routine a second time.
//
// Preconditions (user responsibility — the transpiler does not emit runtime
// guards): `r` is owning and still holds the qubit allocated by the forward
// operator OP; `a` and `b` are byte-identical to their state at the forward
// call (no intervening writes to their qubits). No qubit is released — the
// caller's scope continues to own `r`, `a`, and `b` after the inverse runs.
//
// Under STURM_BACKEND_ENABLED the bodies delegate to the library DSL; in the
// non-backend build profile the forward comparison operators are lowered by
// `qint_compare.hpp` through `dispatch_compare` (no ancilla allocation, no
// self-inverse DSL circuit), so the adjoint is a no-op and the functions
// simply return. This matches the existing split between `qint_compare.hpp`
// and `qint_compare_v3.hpp`.

#ifdef STURM_BACKEND_ENABLED

template <std::size_t W>
inline void uncompute_eq_qint(qbool& r, const qint_t<W>& a, const qint_t<W>& b) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = detail::bit_array_view<W>(a, ctx);
    auto b_bits = detail::bit_array_view<W>(b, ctx);
    lib_eq_dsl(a_bits.data(), b_bits.data(), W, r);
}

template <std::size_t W>
inline void uncompute_ne_qint(qbool& r, const qint_t<W>& a, const qint_t<W>& b) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = detail::bit_array_view<W>(a, ctx);
    auto b_bits = detail::bit_array_view<W>(b, ctx);
    lib_ne_dsl(a_bits.data(), b_bits.data(), W, r);
}

template <std::size_t W>
inline void uncompute_lt_qint(qbool& r, const qint_t<W>& a, const qint_t<W>& b) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = detail::bit_array_view<W>(a, ctx);
    auto b_bits = detail::bit_array_view<W>(b, ctx);
    lib_lt_dsl(a_bits.data(), b_bits.data(), W, r);
}

template <std::size_t W>
inline void uncompute_le_qint(qbool& r, const qint_t<W>& a, const qint_t<W>& b) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = detail::bit_array_view<W>(a, ctx);
    auto b_bits = detail::bit_array_view<W>(b, ctx);
    lib_le_dsl(a_bits.data(), b_bits.data(), W, r);
}

template <std::size_t W>
inline void uncompute_gt_qint(qbool& r, const qint_t<W>& a, const qint_t<W>& b) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = detail::bit_array_view<W>(a, ctx);
    auto b_bits = detail::bit_array_view<W>(b, ctx);
    lib_gt_dsl(a_bits.data(), b_bits.data(), W, r);
}

template <std::size_t W>
inline void uncompute_ge_qint(qbool& r, const qint_t<W>& a, const qint_t<W>& b) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    auto a_bits = detail::bit_array_view<W>(a, ctx);
    auto b_bits = detail::bit_array_view<W>(b, ctx);
    lib_ge_dsl(a_bits.data(), b_bits.data(), W, r);
}

#else  // !STURM_BACKEND_ENABLED

// Non-backend build: the forward comparison does not emit a self-inverse DSL
// circuit (see include/sturm/qtypes/qint_compare.hpp — dispatch_compare path),
// so the adjoint has nothing to undo. Ship a no-op so the transpiler-emitted
// call sites compile against both build profiles.
template <std::size_t W>
inline void uncompute_eq_qint(qbool& /*r*/, const qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {}
template <std::size_t W>
inline void uncompute_ne_qint(qbool& /*r*/, const qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {}
template <std::size_t W>
inline void uncompute_lt_qint(qbool& /*r*/, const qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {}
template <std::size_t W>
inline void uncompute_le_qint(qbool& /*r*/, const qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {}
template <std::size_t W>
inline void uncompute_gt_qint(qbool& /*r*/, const qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {}
template <std::size_t W>
inline void uncompute_ge_qint(qbool& /*r*/, const qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {}

#endif  // STURM_BACKEND_ENABLED

} // namespace sturm
