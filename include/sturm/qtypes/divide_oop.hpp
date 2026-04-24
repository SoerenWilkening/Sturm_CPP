// divide_oop.hpp — LO-1e (sturm-a3qp): OOP division free function.
//
// Used by the LO compile-time rewrite pass to desugar `a /= b` / `a %= b`
// into the compute-use-uncompute shape (PRD §2.1):
//
//    qint tmp_q, tmp_r;
//    sturm::detail::divide_oop(a, b, tmp_q, tmp_r);
//    swap(a, tmp_q);
//
// Out-of-place: a, b unchanged; q, r freshly allocated W-qubit registers.
// Invariant: a == q * b + r.  Built directly on lib_div_dsl (operator/
// returns only the quotient; does not expose the (a,b,q,r) shape).
// Target: ≤ 80 LoC.

#pragma once

#ifndef STURM_BACKEND_ENABLED
#  error "divide_oop.hpp requires STURM_BACKEND_ENABLED"
#endif

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/lib/div_dsl.hpp"

#include <cstddef>
#include <cstdint>

namespace sturm {
namespace detail {

template <std::size_t W>
inline void divide_oop(const qint_t<W>& a, const qint_t<W>& b,
                       qint_t<W>& q, qint_t<W>& r) {
    // Non-owning mutable views over a, b so we can feed BitProxy handles
    // into lib_div_dsl without mutating the caller's const references.
    qint_t<W> a_mut; a_mut.value = a.value; a_mut.super_mask = a.super_mask;
    a_mut.qubits = a.qubits; a_mut.owning_ = false;
    qint_t<W> b_mut; b_mut.value = b.value; b_mut.super_mask = b.super_mask;
    b_mut.qubits = b.qubits; b_mut.owning_ = false;

    // Fresh W-qubit registers for q, r. Caller owns (RAII release per B10).
    for (std::size_t i = 0; i < W; ++i) {
        q.qubits[i] = QubitPool::instance().allocate();
        r.qubits[i] = QubitPool::instance().allocate();
    }
    q.super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    r.super_mask = q.super_mask;
    q.owning_ = true; r.owning_ = true;

    BitProxy ab[W], bb[W], qb[W], rb[W];
    for (std::size_t i = 0; i < W; ++i) {
        ab[i] = BitProxy(a_mut, i); bb[i] = BitProxy(b_mut, i);
        qb[i] = BitProxy(q,     i); rb[i] = BitProxy(r,     i);
    }
    lib_div_dsl<BitProxy>(ab, W, bb, W, qb, rb);

    // Classical bookkeeping — dispatch usually folds this, but divide_oop is
    // invoked directly by the LO rewrite, so we keep the q.value / r.value
    // fields coherent with the simulator result.
    const int64_t bv = b.value;
    q.value = (bv != 0) ? (a.value / bv) : 0;
    r.value = (bv != 0) ? (a.value % bv) : 0;
}

} // namespace detail
} // namespace sturm
