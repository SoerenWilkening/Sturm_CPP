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
// Classical / gate-path posture
// -----------------------------
// All four wrappers (`divide_oop`, `and_oop`, `mul_oop`, `or_oop`) now provide
// a real reversible implementation that emits gates when at least one operand
// has allocated qubits, and a classical short-circuit body for the case where
// both operands are still on the `qubits[0] < 0` fast path
// (`example_qint_arith`, `example_phase_abc_demo` rely on this). The
// gate-path bodies dispatch into the matching `lib_*_dsl` from
// `include/sturm/lib/`; the adjoints are registered via
// `STURM_REGISTER_ADJOINT` per width.
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
#include "sturm/lib/logic_dsl.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/routines/invert.hpp"

#include <array>
#include <cassert>
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

// ── mul_oop upper-half stash (sturm-ph6f.2) ──────────────────────────────────
// `lib_mul_dsl(a, W, b, W, result, 2W)` writes a `2W`-bit product across two
// halves: the low `W` bits land in `tmp.qubits` (the slot the LO-2 emitter
// gives us); the high `W` bits land in ancillas the wrapper allocates. The
// matching `mul_oop_adj` needs those high-`W` ancillas to reverse the
// network — but the adjoint signature only sees the low-`W` `tmp`. We park
// the high-`W` indices in a thread-local LIFO stack keyed by `&tmp` for the
// forward call, and pop them in the adjoint. LIFO matches the LO-2
// emitter's scope-exit cleanup ordering (PRD §2.4).
struct mul_oop_upper_stash {
    static constexpr int kMaxEntries = 32;
    static constexpr int kMaxW       = 64;
    struct Entry {
        const void* key = nullptr;       // &tmp
        int n            = 0;            // W
        int qubits[kMaxW] = {};
    };
    Entry entries[kMaxEntries];
    int top = 0;
};
inline mul_oop_upper_stash& mul_oop_stash() {
    static thread_local mul_oop_upper_stash s;
    return s;
}
inline void mul_oop_push(const void* key, const int* qubits, int n) {
    auto& s = mul_oop_stash();
    assert(s.top < mul_oop_upper_stash::kMaxEntries
           && "mul_oop: nested-call stash overflow");
    assert(n <= mul_oop_upper_stash::kMaxW);
    auto& e = s.entries[s.top++];
    e.key = key; e.n = n;
    for (int i = 0; i < n; ++i) e.qubits[i] = qubits[i];
}
inline void mul_oop_pop(const void* key, int* qubits, int n) {
    auto& s = mul_oop_stash();
    assert(s.top > 0 && "mul_oop_adj: no matching mul_oop forward on stash");
    auto& e = s.entries[s.top - 1];
    assert(e.key == key && "mul_oop/mul_oop_adj nesting violated (LIFO)");
    assert(e.n == n   && "mul_oop_adj: width mismatch with stashed forward");
    for (int i = 0; i < n; ++i) qubits[i] = e.qubits[i];
    --s.top;
}

// Build the (2W)-slot BitProxy result register for lib_mul_dsl: low-W bits
// view `tmp` directly, high-W bits view freshly-built non-owning qbools that
// wrap the supplied `upper_anc` indices. `upper_q[]` storage is supplied by
// the caller so the qbool views outlive the BitProxy `mask_ptr`/`value_ptr`
// pointers they hand to the DSL kernel.
template <std::size_t W>
inline void mul_oop_make_result_proxies(qint_t<W>& tmp, const int* upper_anc,
                                        qbool* upper_q, BitProxy* rb) {
    for (std::size_t i = 0; i < W; ++i) rb[i] = BitProxy(tmp, i);
    for (std::size_t i = 0; i < W; ++i) {
        upper_q[i] = qbool::make_non_owning(upper_anc[i]);
        rb[W + i]  = BitProxy(upper_q[i]);
    }
}

// ── mul_oop<W>: a*b → tmp (sturm-ph6f.2) ────────────────────────────────────
// Out-of-place multiplication helper used by the LO-2 `a *= b` desugar.
// Forward leaves `tmp == (a * b) & mask` in the low `W` bits and parks the
// high `W` bits of the product in stashed ancillas; `mul_oop_adj` uncomputes
// both halves via `__lib_mul_dsl_adj`.
//
// Fast-path / classical-path split (mirrors `divide_oop` and `and_oop`):
//   * Both operands on the classical short-circuit path
//     (`a.qubits[0] < 0 && b.qubits[0] < 0`) — keep the pure classical body.
//     `example_qint_arith` / `example_phase_abc_demo` rely on this.
//   * Otherwise — allocate `W` qubits for `tmp` plus `W` ancillas for the
//     high half, run `lib_mul_dsl`, and stash the high-`W` indices keyed by
//     `&tmp`.
template <std::size_t W>
inline void mul_oop(const qint_t<W>& a, const qint_t<W>& b,
                    qint_t<W>& tmp) {
    const uint64_t mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    tmp.value = static_cast<int64_t>(
        (static_cast<uint64_t>(a.value) * static_cast<uint64_t>(b.value)) & mask);
    tmp.super_mask = a.super_mask | b.super_mask;

    if (a.qubits[0] < 0 && b.qubits[0] < 0) {
        return;  // Classical short-circuit — qubits stay at -1.
    }

    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    for (std::size_t i = 0; i < W; ++i)
        tmp.qubits[i] = QubitPool::instance().allocate();
    tmp.owning_    = true;
    tmp.super_mask = mask;

    int upper_anc[W];
    for (std::size_t i = 0; i < W; ++i)
        upper_anc[i] = QubitPool::instance().allocate();

    BitProxy ab[W], bb[W];
    for (std::size_t i = 0; i < W; ++i) {
        ab[i] = BitProxy(a_mut, i);
        bb[i] = BitProxy(b_mut, i);
    }
    qbool    upper_q[W];
    BitProxy rb[2 * W];
    mul_oop_make_result_proxies<W>(tmp, upper_anc, upper_q, rb);

    lib_mul_dsl<BitProxy>(ab, W, bb, W, rb, 2u * W);

    mul_oop_push(static_cast<const void*>(&tmp), upper_anc,
                 static_cast<int>(W));
}

// `mul_oop_adj<W>` rebuilds the BitProxy 2W-slot from `tmp.qubits` (low half)
// and the LIFO-popped upper-W ancillas, runs `__lib_mul_dsl_adj` to drive
// the network in reverse (zeroing both halves given the post-swap-undo
// state where the concatenated register equals `a * b`), then releases the
// upper-W ancillas. The low-W belongs to `tmp` — its destructor releases.
template <std::size_t W>
inline void mul_oop_adj(const qint_t<W>& a, const qint_t<W>& b,
                        qint_t<W>& tmp) {
    if (tmp.qubits[0] < 0) {
        tmp.value = 0; tmp.super_mask = 0;
        return;  // Classical short-circuit — bookkeeping-only inverse.
    }

    int upper_anc[W];
    mul_oop_pop(static_cast<const void*>(&tmp), upper_anc,
                static_cast<int>(W));

    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    BitProxy ab[W], bb[W];
    for (std::size_t i = 0; i < W; ++i) {
        ab[i] = BitProxy(a_mut, i);
        bb[i] = BitProxy(b_mut, i);
    }
    qbool    upper_q[W];
    BitProxy rb[2 * W];
    mul_oop_make_result_proxies<W>(tmp, upper_anc, upper_q, rb);

    __lib_mul_dsl_adj<BitProxy>(ab, W, bb, W, rb, 2u * W);

    for (std::size_t i = 0; i < W; ++i)
        QubitPool::instance().release(upper_anc[i]);
    tmp.value = 0;
    tmp.super_mask = 0;
}

// ── and_oop<W>: a&b → tmp ───────────────────────────────────────────────────
// Out-of-place bitwise-AND helper for the LO-2 `a &= b` desugar. Fast-path /
// classical-path split (mirrors `divide_oop`): both operands on the classical
// short-circuit path (qubits[0] < 0) keep the pure classical body; otherwise
// allocate `W` qubits for `tmp` and dispatch into `lib_c_AND_dsl` per bit
// (single CCX: `tmp_i ^= a_i & b_i`).
template <std::size_t W>
inline void and_oop(const qint_t<W>& a, const qint_t<W>& b,
                    qint_t<W>& tmp) {
    tmp.value      = a.value & b.value;
    tmp.super_mask = a.super_mask & b.super_mask;

    if (a.qubits[0] < 0 && b.qubits[0] < 0) return;  // Classical short-circuit.

    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    for (std::size_t i = 0; i < W; ++i)
        tmp.qubits[i] = QubitPool::instance().allocate();
    tmp.owning_ = true;

    for (std::size_t i = 0; i < W; ++i) {
        BitProxy ab(a_mut, i), bb(b_mut, i), tb(tmp, i);
        lib_c_AND_dsl<BitProxy>(ab, bb, tb);  // tb ^= (ab & bb) — one CCX
    }
}

// `and_oop_adj` re-emits the per-bit CCX ladder (CCX is self-inverse), zeroing
// tmp given the post-swap-undo state where `tmp == a & b`.
template <std::size_t W>
inline void and_oop_adj(const qint_t<W>& a, const qint_t<W>& b,
                        qint_t<W>& tmp) {
    if (tmp.qubits[0] < 0) {
        tmp.value = 0; tmp.super_mask = 0;
        return;  // Classical short-circuit — bookkeeping-only inverse.
    }

    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    for (std::size_t i = 0; i < W; ++i) {
        BitProxy ab(a_mut, i), bb(b_mut, i), tb(tmp, i);
        lib_c_AND_dsl<BitProxy>(ab, bb, tb);
    }
    tmp.value = 0; tmp.super_mask = 0;
}

// ── or_oop<W>: a|b → tmp (sturm-ph6f.1) ─────────────────────────────────────
// Out-of-place bitwise-OR helper for the LO-2 `a |= b` desugar. Mirrors the
// `and_oop` fast-path / classical-path split: both operands on the classical
// short-circuit path (qubits[0] < 0) keep the bookkeeping-only body; otherwise
// allocate `W` qubits for `tmp` and dispatch into `lib_or_dsl` per bit, which
// emits the reversible OR pattern `tmp_i ^= a_i | b_i` (CX+CX+CCX — equivalent
// to the De Morgan ladder NOT(NOT a AND NOT b) up to operand restoration).
template <std::size_t W>
inline void or_oop(const qint_t<W>& a, const qint_t<W>& b,
                   qint_t<W>& tmp) {
    tmp.value      = a.value | b.value;
    tmp.super_mask = a.super_mask | b.super_mask;

    if (a.qubits[0] < 0 && b.qubits[0] < 0) {
        return;  // Classical short-circuit — qubits stay at -1.
    }

    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    for (std::size_t i = 0; i < W; ++i)
        tmp.qubits[i] = QubitPool::instance().allocate();
    tmp.owning_ = true;

    for (std::size_t i = 0; i < W; ++i) {
        BitProxy ab(a_mut, i), bb(b_mut, i), tb(tmp, i);
        lib_or_dsl<BitProxy>(ab, bb, tb);  // tb ^= (ab | bb)
    }
}

// `or_oop_adj` invokes `__lib_or_dsl_adj` per bit (reverse-order CX+CX+CCX),
// which zeros tmp given the post-swap-undo state where `tmp == a | b`.
template <std::size_t W>
inline void or_oop_adj(const qint_t<W>& a, const qint_t<W>& b,
                       qint_t<W>& tmp) {
    if (tmp.qubits[0] < 0) {
        tmp.value = 0; tmp.super_mask = 0;
        return;  // Classical short-circuit — bookkeeping-only inverse.
    }

    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    constexpr auto adj = invert<&lib_or_dsl<BitProxy>>();
    static_assert(adj != nullptr, "lib_or_dsl<BitProxy> adjoint must be registered");
    for (std::size_t i = 0; i < W; ++i) {
        BitProxy ab(a_mut, i), bb(b_mut, i), tb(tmp, i);
        adj(ab, bb, tb);
    }
    tmp.value = 0; tmp.super_mask = 0;
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

// ── STURM_REGISTER_ADJOINT pairings (per width, per *_oop wrapper) ──────────
// The macro keys on the function-pointer VALUE, so each {wrapper, width}
// instantiation is registered explicitly. Covers the widths the LO-2 emitter
// / examples / tests instantiate today; extend as new widths land. The
// classical short-circuit body is shared across widths but the gate path
// and its inverse are width-specific.
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<1>,  sturm::detail::and_oop_adj<1>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<2>,  sturm::detail::and_oop_adj<2>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<3>,  sturm::detail::and_oop_adj<3>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<4>,  sturm::detail::and_oop_adj<4>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<8>,  sturm::detail::and_oop_adj<8>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<16>, sturm::detail::and_oop_adj<16>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<32>, sturm::detail::and_oop_adj<32>)
STURM_REGISTER_ADJOINT(sturm::detail::and_oop<64>, sturm::detail::and_oop_adj<64>)

STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<1>,  sturm::detail::mul_oop_adj<1>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<2>,  sturm::detail::mul_oop_adj<2>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<3>,  sturm::detail::mul_oop_adj<3>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<4>,  sturm::detail::mul_oop_adj<4>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<8>,  sturm::detail::mul_oop_adj<8>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<16>, sturm::detail::mul_oop_adj<16>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<32>, sturm::detail::mul_oop_adj<32>)
STURM_REGISTER_ADJOINT(sturm::detail::mul_oop<64>, sturm::detail::mul_oop_adj<64>)

STURM_REGISTER_ADJOINT(sturm::detail::or_oop<1>,   sturm::detail::or_oop_adj<1>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<2>,   sturm::detail::or_oop_adj<2>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<3>,   sturm::detail::or_oop_adj<3>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<4>,   sturm::detail::or_oop_adj<4>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<8>,   sturm::detail::or_oop_adj<8>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<16>,  sturm::detail::or_oop_adj<16>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<32>,  sturm::detail::or_oop_adj<32>)
STURM_REGISTER_ADJOINT(sturm::detail::or_oop<64>,  sturm::detail::or_oop_adj<64>)
