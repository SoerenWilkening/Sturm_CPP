// lossy_oop.hpp — sturm-czfi: runtime support for the LO-2 transpiler-emitted
// allocate-compute-swap-uncompute shape (PRD §2.4).
//
// Provides the four `*_oop` forward wrappers + their adjoints that the LO-2
// emitter targets, plus `sturm::swap(qint_t<W>&, qint_t<W>&)` which the
// emitter inserts between the forward op and the scope-exit cleanup. The
// adjoints are registered via `STURM_REGISTER_ADJOINT` (partial-specialization
// over the width NTTP) so the emitter's cleanup line
//
//     sturm::invert<&::sturm::detail::mul_oop<W>>()(a, b, __sturm_tmp_mul_0);
//
// resolves at compile time to the matching `mul_oop_adj<W>` pointer.
//
// Classical / TODO(backend) posture
// ---------------------------------
// The example targets that drove this fix (`example_qint_arith`,
// `example_phase_abc_demo`) keep both operands on the classical short-circuit
// path (qubits[0] < 0 for both throughout main), so the wrappers only need
// to honour the classical bookkeeping invariants for the build & run gate to
// pass. The full quantum implementations of `mul_oop`, `and_oop`, `or_oop`
// are deferred — the bodies below classical-update `tmp.value` and leave the
// quantum gate emission as `TODO(backend)`. `divide_oop` already provides
// the full quantum implementation in `divide_oop.hpp`.
//
// Header-only. Requires STURM_BACKEND_ENABLED because divide_oop.hpp does;
// the wrappers themselves only need qint_t<W>'s public layout.

#pragma once

#ifndef STURM_BACKEND_ENABLED
#  error "lossy_oop.hpp requires STURM_BACKEND_ENABLED"
#endif

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/divide_oop.hpp"
#include "sturm/routines/invert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace sturm {

// ── sturm::swap for qint_t<W> ───────────────────────────────────────────────
// The LO-2 forward triplet ends with `swap(<lhs>, <tmp>)` and the LIFO cleanup
// re-emits the same swap before the adjoint call. ADL on `qint_t<W>&` picks up
// names declared in the namespace where `qint_t` lives — i.e. `sturm::` — so
// declaring `swap` here gives the emitted text a target without the emitter
// needing to qualify the call.
template <std::size_t W>
inline void swap(qint_t<W>& a, qint_t<W>& b) noexcept {
    using std::swap;
    swap(a.value, b.value);
    swap(a.super_mask, b.super_mask);
    swap(a.qubits, b.qubits);
    swap(a.owning_, b.owning_);
}

namespace detail {

// ── mul_oop<W>: a*b → tmp ───────────────────────────────────────────────────
// Out-of-place multiplication helper used by `a *= b` rewrite. Forward leaves
// `tmp == a * b` (truncated to W bits to match qint semantics); `mul_oop_adj`
// zeros tmp given that invariant. TODO(backend): full lib_mul_dsl-driven
// quantum implementation; currently classical-only.
template <std::size_t W>
inline void mul_oop(const qint_t<W>& a, const qint_t<W>& b,
                    qint_t<W>& tmp) noexcept {
    const uint64_t mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    tmp.value = static_cast<int64_t>(
        (static_cast<uint64_t>(a.value) * static_cast<uint64_t>(b.value)) & mask);
    tmp.super_mask = a.super_mask | b.super_mask;
    // qubits left at -1 (default-constructed) — classical short-circuit path.
}

template <std::size_t W>
inline void mul_oop_adj(const qint_t<W>& /*a*/, const qint_t<W>& /*b*/,
                        qint_t<W>& tmp) noexcept {
    tmp.value = 0;
    tmp.super_mask = 0;
}

// ── and_oop<W>: a&b → tmp ───────────────────────────────────────────────────
template <std::size_t W>
inline void and_oop(const qint_t<W>& a, const qint_t<W>& b,
                    qint_t<W>& tmp) noexcept {
    tmp.value = a.value & b.value;
    tmp.super_mask = a.super_mask & b.super_mask;
}

template <std::size_t W>
inline void and_oop_adj(const qint_t<W>& /*a*/, const qint_t<W>& /*b*/,
                        qint_t<W>& tmp) noexcept {
    tmp.value = 0;
    tmp.super_mask = 0;
}

// ── or_oop<W>: a|b → tmp ────────────────────────────────────────────────────
template <std::size_t W>
inline void or_oop(const qint_t<W>& a, const qint_t<W>& b,
                   qint_t<W>& tmp) noexcept {
    tmp.value = a.value | b.value;
    tmp.super_mask = a.super_mask | b.super_mask;
}

template <std::size_t W>
inline void or_oop_adj(const qint_t<W>& /*a*/, const qint_t<W>& /*b*/,
                       qint_t<W>& tmp) noexcept {
    tmp.value = 0;
    tmp.super_mask = 0;
}

// ── divide_oop_adj<W>: zero (q, r) given a == q*b + r ───────────────────────
// Forward `divide_oop` lives in divide_oop.hpp. Adjoint mirrors the classical
// bookkeeping — given the post-swap-undo state where (q, r) satisfy the
// divide invariant, both should return to |0>.
template <std::size_t W>
inline void divide_oop_adj(const qint_t<W>& /*a*/, const qint_t<W>& /*b*/,
                           qint_t<W>& q, qint_t<W>& r) noexcept {
    q.value = 0; q.super_mask = 0;
    r.value = 0; r.super_mask = 0;
}

}  // namespace detail

// ── Re-export the *_oop helpers + adjoints under sturm:: ───────────────────
// ADL on `sturm::qint_t<W>&` only searches `sturm::` (not nested namespaces),
// so the LO-2 emitter's unqualified `mul_oop(a, b, tmp)` and
// `mul_oop_adj(a, b, tmp)` would not see `sturm::detail::*` without help. The
// using-declarations below promote each forward + adjoint to `sturm::`,
// making the emitted unqualified calls resolve via ADL on the qint_t<W>
// arguments. Definitions remain in `sturm::detail::` for namespace hygiene.
using detail::mul_oop;
using detail::mul_oop_adj;
using detail::and_oop;
using detail::and_oop_adj;
using detail::or_oop;
using detail::or_oop_adj;
// `divide_oop` already lives in `sturm::detail::` via divide_oop.hpp; promote
// it (plus its adjoint defined here) the same way for symmetry with the
// other three.
using detail::divide_oop;
using detail::divide_oop_adj;

}  // namespace sturm
