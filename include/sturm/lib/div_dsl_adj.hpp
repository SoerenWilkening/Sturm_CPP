// div_dsl_adj.hpp — LO-1b (sturm-1rjw): adjoint + STURM_REGISTER_ADJOINT for
// lib_div_dsl.  Carved out of div_dsl.hpp under sturm-nmf1 to keep the forward
// header within its per-file LoC budget.
//
// __lib_div_dsl_adj is the gate-reverse of lib_div_dsl.  Precondition:
// dividend == quotient * divisor + remainder.  Postcondition: quotient,
// remainder both |0>; dividend and divisor unchanged.  Registered via
// STURM_REGISTER_ADJOINT so `invert<&lib_div_dsl>()(…)` resolves at the LO
// rewrite's scope-exit cleanup.
//
// Auto-included from div_dsl.hpp at the bottom of that file so all callers
// of lib_div_dsl pick up the registration without an extra #include.

#pragma once

#include "sturm/lib/div_dsl.hpp"
#include "sturm/routines/invert.hpp"

// Forward-declare BitProxy for the LO-1b adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {
namespace detail_div {

// Bodies for the forward declarations in div_dsl.hpp.  Inline + header-local
// so callers that include div_dsl.hpp (which transitively includes this
// header) can instantiate `div_kernel<Bit, false>` and `mul_kernel<Bit, false>`
// without seeing redeclarations.

// Gate-reverse of maj_dsl (undoes b^=c; a^=c; c^=(a&b)).
template <typename Bit>
inline void maj_adj(Bit& a, Bit& b, Bit& c) {
    c ^= (a & b); a ^= c; b ^= c;
}
// Gate-reverse of uma_dsl (undoes c^=(a&b); a^=c; b^=a).
template <typename Bit>
inline void uma_adj(Bit& a, Bit& b, Bit& c) {
    b ^= a; a ^= c; c ^= (a & b);
}
// Gate-reverse of lib_add_dsl: given (a, a+b, carry), returns (a, b, 0).
template <typename Bit>
inline void lib_add_adj(Bit* a_bits, Bit* b_bits, Bit& carry_out, size_t n) {
    if (n == 0u) return;
    qbool carry_anc_qbool;
    carry_anc_qbool.qubits[0]  = QubitPool::instance().allocate();
    carry_anc_qbool.super_mask = 1;
    Bit carry_anc = detail_adder::make_ancilla_view<Bit>(carry_anc_qbool);
    uma_adj(carry_anc, b_bits[0], a_bits[0]);
    for (size_t i = 1u; i < n; ++i)
        uma_adj(a_bits[i - 1u], b_bits[i], a_bits[i]);
    carry_out ^= a_bits[n - 1u];
    for (size_t i = n - 1u; i >= 1u; --i)
        maj_adj(a_bits[i - 1u], b_bits[i], a_bits[i]);
    maj_adj(carry_anc, b_bits[0], a_bits[0]);
}

}  // namespace detail_div

template <typename Bit>
inline void __lib_div_dsl_adj(Bit* dividend_bits, size_t n,
                              Bit* divisor_bits,  size_t d,
                              Bit* quotient_bits,
                              Bit* remainder_bits) {
    if (n == 0u) return;
    assert(d == n && "__lib_div_dsl_adj: d must equal n");
    detail_div::div_kernel<Bit, false>(dividend_bits, n, divisor_bits,
                                        quotient_bits, remainder_bits);
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_div_dsl<sturm::BitProxy>,
                       sturm::__lib_div_dsl_adj<sturm::BitProxy>)
#endif
