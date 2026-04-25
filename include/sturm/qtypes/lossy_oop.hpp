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
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/divide_oop.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/lib/c_and_dsl.hpp"
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
// Out-of-place bitwise-AND helper used by the LO-2 `a &= b` desugar.
//
// Fast-path / classical-path split (mirrors `divide_oop`):
//   * Both operands on the classical short-circuit path (a.qubits[0] < 0 AND
//     b.qubits[0] < 0) — keep the pure classical body. `example_qint_arith`
//     and `example_phase_abc_demo` rely on this; they never enter the gate
//     path so the wrapper must not allocate qubits or emit gates for them.
//   * Otherwise — at least one operand carries qubits — allocate `W` fresh
//     qubits for `tmp` and dispatch into `lib_c_AND_dsl` per bit. The DSL
//     emits a single CCX (Toffoli) per bit: `tmp_i ^= a_i & b_i`. With
//     `tmp` starting in |0…0> this leaves `tmp == a & b`.
template <std::size_t W>
inline void and_oop(const qint_t<W>& a, const qint_t<W>& b,
                    qint_t<W>& tmp) {
    tmp.value      = a.value & b.value;
    tmp.super_mask = a.super_mask & b.super_mask;

    if (a.qubits[0] < 0 && b.qubits[0] < 0) {
        // Classical short-circuit — no qubit allocation, no gate emission.
        return;
    }

    // Gate path: borrow non-owning views over a, b so we can build BitProxy
    // handles into lib_c_AND_dsl without mutating the caller's const refs.
    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    for (std::size_t i = 0; i < W; ++i) {
        tmp.qubits[i] = QubitPool::instance().allocate();
    }
    tmp.owning_ = true;

    for (std::size_t i = 0; i < W; ++i) {
        BitProxy ab(a_mut, i), bb(b_mut, i), tb(tmp, i);
        lib_c_AND_dsl<BitProxy>(ab, bb, tb);  // tb ^= (ab & bb) — one CCX
    }
}

// `and_oop_adj` is the structural inverse of `and_oop`. The forward emits a
// per-bit Toffoli (CCX is self-inverse), so re-applying the same ladder on
// the post-swap-undo state where `tmp == a & b` returns `tmp` to |0…0>.
//
// Same fast-path / classical-path split: on the classical path both
// `tmp.qubits[0] < 0`, so we just zero the bookkeeping fields. On the gate
// path we re-emit the CCX ladder (no qubit allocation — `tmp` already owns
// the qubits the forward allocated).
template <std::size_t W>
inline void and_oop_adj(const qint_t<W>& a, const qint_t<W>& b,
                        qint_t<W>& tmp) {
    if (tmp.qubits[0] < 0) {
        // Classical short-circuit — bookkeeping-only inverse.
        tmp.value = 0;
        tmp.super_mask = 0;
        return;
    }

    // Gate path: re-apply the CCX ladder; CCX is self-inverse so this zeros
    // tmp's qubits given the (a & b) post-state. Bookkeeping mirror to keep
    // tmp.value coherent with the simulator readout.
    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    for (std::size_t i = 0; i < W; ++i) {
        BitProxy ab(a_mut, i), bb(b_mut, i), tb(tmp, i);
        lib_c_AND_dsl<BitProxy>(ab, bb, tb);
    }
    tmp.value      = 0;
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

// ── STURM_REGISTER_ADJOINT pairings for and_oop<W> ───────────────────────────
// Pairs each `and_oop<W>` instantiation with its structural inverse so
// `sturm::invert<&::sturm::detail::and_oop<W>>()` resolves at compile time
// to `&::sturm::detail::and_oop_adj<W>`. The macro keys on the function-
// pointer VALUE, so each width must be registered explicitly. Covers the
// widths the LO-2 emitter / examples / tests instantiate today; extend as
// new widths land. (The classical short-circuit body is identical across
// widths, but the gate path and the inverse are width-specific.)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<1>,
                       sturm::detail::and_oop_adj<1>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<2>,
                       sturm::detail::and_oop_adj<2>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<3>,
                       sturm::detail::and_oop_adj<3>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<4>,
                       sturm::detail::and_oop_adj<4>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<8>,
                       sturm::detail::and_oop_adj<8>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<16>,
                       sturm::detail::and_oop_adj<16>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<32>,
                       sturm::detail::and_oop_adj<32>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<64>,
                       sturm::detail::and_oop_adj<64>)
