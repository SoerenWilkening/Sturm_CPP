// qint_modular.hpp -- P4 (sturm-kgwx) public modular-arithmetic free
// functions on qint_t<W>: add_mod / mul_mod / pow_mod (PRD §3.1, plan §6.2).
//
// Each body is a thin allocate-then-call-lib_*_mod_dsl wrapper (<=15 LoC):
// build mutable non-owning views of the const inputs, allocate the result's
// W qubits, hand BitProxy arrays to the lib-level primitive, and stamp the
// classical .value field on the way out.  No new gate emission and no new
// arithmetic kernel — see PRD §4 layering rule.
//
// Beat 4.1 lands the add_mod body; beat 4.2 lands mul_mod; 4.3 fills in
// pow_mod.  The W == 0 short-circuit matches the n == 0 contract documented
// in PRD §5.
//
// Target: <=150 LoC.

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/lib/add_mod_dsl.hpp"
#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/lib/pow_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/core/qubit_pool.hpp"
#include <cstddef>
#include <cstdint>
#include <cassert>

namespace sturm {

namespace detail_qint_modular {

// Build BitProxy arrays over a/b/n inputs and a fresh result register r.
// Inputs are const-ref, so we make non-owning mutable copies (sharing the
// caller's qubit indices) to satisfy BitProxy's non-const reference ctor.
// Each result-bit is allocated to a fresh |0> qubit via ensure_quantum().
template <std::size_t W>
inline void make_proxy_quad(const qint_t<W>& a, const qint_t<W>& b,
                            const qint_t<W>& n, qint_t<W>& r,
                            qint_t<W>& a_mut, qint_t<W>& b_mut,
                            qint_t<W>& n_mut,
                            BitProxy* ab, BitProxy* bb,
                            BitProxy* nb, BitProxy* rb) {
    a_mut = qint_t<W>::make_non_owning(a.qubits, a.value, a.super_mask);
    b_mut = qint_t<W>::make_non_owning(b.qubits, b.value, b.super_mask);
    n_mut = qint_t<W>::make_non_owning(n.qubits, n.value, n.super_mask);
    for (std::size_t i = 0; i < W; ++i) {
        rb[i] = BitProxy(r, i);
        rb[i].ensure_quantum();          // alloc fresh |0> qubit for r[i]
        ab[i] = BitProxy(a_mut, i);
        bb[i] = BitProxy(b_mut, i);
        nb[i] = BitProxy(n_mut, i);
    }
}

// Classical (base^exp) mod n via repeated unsigned multiplication.  Mirrors
// the convention in lib_pow_mod_dsl (0^0 == 1, n == 0 yields 0) so the
// pow_mod wrapper can stamp .value in one ternary.
inline int64_t pow_mod_classical(int64_t base, int64_t exp, int64_t n) {
    if (n == 0) return 0;
    uint64_t acc = 1u;
    for (int64_t k = 0; k < exp; ++k)
        acc = (acc * static_cast<uint64_t>(base)) % static_cast<uint64_t>(n);
    return static_cast<int64_t>(acc);
}

}  // namespace detail_qint_modular

template <std::size_t W>
qint_t<W> add_mod(const qint_t<W>& a, const qint_t<W>& b, const qint_t<W>& n) {
    if constexpr (W == 0u) return qint_t<W>{};
    qint_t<W> r, a_mut, b_mut, n_mut;
    BitProxy ab[W], bb[W], nb[W], rb[W];
    detail_qint_modular::make_proxy_quad<W>(a, b, n, r,
                                            a_mut, b_mut, n_mut,
                                            ab, bb, nb, rb);
    lib_add_mod_dsl<BitProxy>(ab, bb, nb, W, rb);
    r.value = (n.value != 0) ? ((a.value + b.value) % n.value) : 0;
    return r;
}

template <std::size_t W>
qint_t<W> mul_mod(const qint_t<W>& a, const qint_t<W>& b, const qint_t<W>& n) {
    if constexpr (W == 0u) return qint_t<W>{};
    qint_t<W> r, a_mut, b_mut, n_mut;
    BitProxy ab[W], bb[W], nb[W], rb[W];
    detail_qint_modular::make_proxy_quad<W>(a, b, n, r,
                                            a_mut, b_mut, n_mut,
                                            ab, bb, nb, rb);
    lib_mul_mod_dsl<BitProxy>(ab, bb, nb, W, rb);
    r.value = (n.value != 0) ? ((a.value * b.value) % n.value) : 0;
    return r;
}

template <std::size_t W>
qint_t<W> pow_mod(const qint_t<W>& base, const qint_t<W>& exp,
                  const qint_t<W>& n) {
    if constexpr (W == 0u) return qint_t<W>{};
    qint_t<W> r, base_mut, exp_mut, n_mut;
    BitProxy bb[W], eb[W], nb[W], rb[W];
    // pow_mod's lib-level signature is (base, exp, n, width, r); reuse
    // make_proxy_quad's a/b/n/r slots by mapping base→a, exp→b, n→n, r→r.
    detail_qint_modular::make_proxy_quad<W>(base, exp, n, r,
                                            base_mut, exp_mut, n_mut,
                                            bb, eb, nb, rb);
    lib_pow_mod_dsl<BitProxy>(bb, eb, nb, W, rb);
    r.value = detail_qint_modular::pow_mod_classical(base.value, exp.value,
                                                     n.value);
    return r;
}

} // namespace sturm
