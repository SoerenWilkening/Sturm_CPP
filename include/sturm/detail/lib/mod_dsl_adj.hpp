// mod_dsl_adj.hpp — LO-1b (sturm-1rjw): adjoint + STURM_REGISTER_ADJOINT for
// lib_mod_dsl.  Carved out of mod_dsl.hpp under sturm-nmf1 to keep the forward
// header within its per-file LoC budget.
//
// __lib_mod_dsl_adj is the gate-reverse of lib_mod_dsl.  Precondition:
// remainder_bits == dividend % divisor (the forward's output).  Postcondition:
// remainder_bits |0>; dividend / divisor unchanged.  Runs the reversed
// forward gate sequence.
//
// Forward sequence: alloc q; div(a,b,q,rem); alloc tr; div(a,b,q,tr);
//                   tr ^= rem; release tr; release q.
// Adjoint: reverse that sequence, substituting __lib_div_dsl_adj for each
// div call.  Each adj-div runs on the post-forward state of its paired
// forward div, so inversion is gate-level, not invariant-based.
//
// Auto-included from mod_dsl.hpp at the bottom of that file so all callers
// of lib_mod_dsl pick up the registration without an extra #include.

#pragma once

#include "sturm/detail/lib/mod_dsl.hpp"
#include "sturm/detail/lib/div_dsl.hpp"
#include "sturm/detail/lib/div_dsl_adj.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/routines/invert.hpp"
#include <cstddef>
#include <cassert>

// Forward-declare BitProxy for the LO-1b adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

template <typename Bit>
inline void __lib_mod_dsl_adj(Bit* dividend_bits, size_t n,
                              Bit* divisor_bits,  size_t d,
                              Bit* remainder_bits) {
    if (n == 0u) return;
    assert(d == n && "__lib_mod_dsl_adj: d must equal n");
    static constexpr size_t kMaxN = 32u;
    assert(n <= kMaxN && "__lib_mod_dsl_adj: register too wide");

    // Re-allocate ancillas in forward's order (quotient, then temp_rem).
    int quotient_idx[kMaxN]; qbool quotient_own[kMaxN]; Bit quotient_bits[kMaxN];
    for (size_t i = 0u; i < n; ++i) {
        quotient_idx[i]  = QubitPool::instance().allocate();
        quotient_own[i]  = qbool::make_non_owning(quotient_idx[i]);
        quotient_bits[i] = detail_adder::make_ancilla_view<Bit>(quotient_own[i]);
    }
    int temp_rem_idx[kMaxN]; qbool temp_rem_own[kMaxN]; Bit temp_rem_bits[kMaxN];
    for (size_t i = 0u; i < n; ++i) {
        temp_rem_idx[i]  = QubitPool::instance().allocate();
        temp_rem_own[i]  = qbool::make_non_owning(temp_rem_idx[i]);
        temp_rem_bits[i] = detail_adder::make_ancilla_view<Bit>(temp_rem_own[i]);
    }

    // Reverse-of-`temp_rem ^= remainder_bits` (self-inverse): tr ← R.
    for (size_t i = 0u; i < n; ++i) temp_rem_bits[i] ^= remainder_bits[i];

    // Reverse forward-div2: input (q=0, tr=R) matches div2 post-forward →
    // output (q=Q, tr=0).
    __lib_div_dsl_adj(dividend_bits, n, divisor_bits, d,
                      quotient_bits, temp_rem_bits);

    // Reverse forward-div1: input (q=Q, rem=R) matches div1 post-forward →
    // output (q=0, rem=0).
    __lib_div_dsl_adj(dividend_bits, n, divisor_bits, d,
                      quotient_bits, remainder_bits);

    // Release in reverse of allocation (temp_rem first, quotient second).
    for (size_t i = n; i-- > 0u;) QubitPool::instance().release(temp_rem_idx[i]);
    for (size_t i = n; i-- > 0u;) QubitPool::instance().release(quotient_idx[i]);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_mod_dsl<sturm::BitProxy>,
                       sturm::__lib_mod_dsl_adj<sturm::BitProxy>)
#endif
