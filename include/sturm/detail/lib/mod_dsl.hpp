// mod_dsl.hpp — M17: lib_mod_dsl (LO-1b adjoint lives in mod_dsl_adj.hpp).
//
// lib_mod_dsl(a, n, b, d, rem): out-of-place n-bit modulo; rem must start |0>.
// Forward sequence: alloc q; div(a,b,q,rem); alloc tr; div(a,b,q,tr);
//                   tr ^= rem; release tr; release q.
//
// Target: <80 LoC.  The __lib_mod_dsl_adj template + STURM_REGISTER_ADJOINT
// block lives in the sibling mod_dsl_adj.hpp header (auto-included from the
// bottom of this file; sturm-nmf1).

#pragma once
#include "sturm/detail/lib/div_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include <cstddef>
#include <cassert>

namespace sturm {

// lib_mod_dsl: out-of-place n-bit modulo. remainder = dividend % divisor.
// remainder_bits must start |0>. Inputs unchanged.
template <typename Bit>
inline void lib_mod_dsl(Bit* dividend_bits, size_t n,
                        Bit* divisor_bits,  size_t d,
                        Bit* remainder_bits) {
    if (n == 0u) return;
    assert(d == n && "lib_mod_dsl: d must equal n");
    static constexpr size_t kMaxN = 32u;
    assert(n <= kMaxN && "lib_mod_dsl: register too wide");

    // Allocate quotient ancilla. Owning qbool manages pool; Bit views for ops.
    int quotient_idx[kMaxN]; qbool quotient_own[kMaxN]; Bit quotient_bits[kMaxN];
    for (size_t i = 0u; i < n; ++i) {
        quotient_idx[i]  = QubitPool::instance().allocate();
        quotient_own[i]  = qbool::make_non_owning(quotient_idx[i]);
        quotient_bits[i] = detail_adder::make_ancilla_view<Bit>(quotient_own[i]);
    }
    lib_div_dsl(dividend_bits, n, divisor_bits, d, quotient_bits, remainder_bits);

    // Uncompute quotient: re-run division into temp_rem, XOR to zero.
    int temp_rem_idx[kMaxN]; qbool temp_rem_own[kMaxN]; Bit temp_rem_bits[kMaxN];
    for (size_t i = 0u; i < n; ++i) {
        temp_rem_idx[i]  = QubitPool::instance().allocate();
        temp_rem_own[i]  = qbool::make_non_owning(temp_rem_idx[i]);
        temp_rem_bits[i] = detail_adder::make_ancilla_view<Bit>(temp_rem_own[i]);
    }
    lib_div_dsl(dividend_bits, n, divisor_bits, d, quotient_bits, temp_rem_bits);
    for (size_t i = 0u; i < n; ++i) temp_rem_bits[i] ^= remainder_bits[i];
    for (size_t i = n; i-- > 0u;) QubitPool::instance().release(temp_rem_idx[i]);
    for (size_t i = n; i-- > 0u;) QubitPool::instance().release(quotient_idx[i]);
}

} // namespace sturm

// Sibling header carries __lib_mod_dsl_adj + STURM_REGISTER_ADJOINT
// (sturm-nmf1).  Auto-included so callers of lib_mod_dsl pick up the
// adjoint-pair registration without an extra #include.
#include "sturm/detail/lib/mod_dsl_adj.hpp"
