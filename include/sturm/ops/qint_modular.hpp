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

/**
 * @brief Modular addition on `qint_t<W>`: returns `(a + b) mod n`.
 *
 * Allocates a fresh owning `qint_t<W>` for the result and lowers to
 * `lib_add_mod_dsl` (PRD §3.1, §3.2). The wrapper itself emits no
 * gates; every Toffoli/CNOT/X comes from the underlying primitive
 * (PRD §4 layering rule).
 *
 * @param a Left operand register, read-only.
 * @param b Right operand register, read-only.
 * @param n Modulus register, read-only. `n == 0` short-circuits to a
 *          fresh zero register (matches the convention documented in
 *          PRD §5 / §8 #3 and `lib_add_mod_dsl`).
 *
 * @return A fresh owning `qint_t<W>` whose underlying register holds
 *         `(a + b) mod n` and whose classical `.value` field tracks
 *         that result.
 *
 * @pre `a, b ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the function
 *      short-circuits to zero — see PRD §5 / §8 #3).
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition. Calling `add_mod`
 * with operands outside `[0, n)` is **undefined behavior** — the
 * returned value is representable but is not the mathematical answer.
 * This matches the trust model of classical `pow(a, b, c)` /
 * GMP / OpenSSL (PRD §5).
 *
 * @sa lib_add_mod_dsl, mul_mod, pow_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
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

/**
 * @brief Modular multiplication on `qint_t<W>`: returns `(a * b) mod n`.
 *
 * Allocates a fresh owning `qint_t<W>` for the result and lowers to
 * `lib_mul_mod_dsl` (PRD §3.1, §3.2). The wrapper itself emits no
 * gates; every Toffoli/CNOT/X comes from the underlying primitive
 * (PRD §4 layering rule).
 *
 * @param a Left operand register, read-only.
 * @param b Right operand register, read-only.
 * @param n Modulus register, read-only. `n == 0` short-circuits to a
 *          fresh zero register (matches the convention documented in
 *          PRD §5 / §8 #3 and `lib_mul_mod_dsl`).
 *
 * @return A fresh owning `qint_t<W>` whose underlying register holds
 *         `(a * b) mod n` and whose classical `.value` field tracks
 *         that result.
 *
 * @pre `a, b ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the function
 *      short-circuits to zero — see PRD §5 / §8 #3).
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition. Calling `mul_mod`
 * with operands outside `[0, n)` is **undefined behavior** — the
 * returned value is representable but is not the mathematical answer.
 * This matches the trust model of classical `pow(a, b, c)` /
 * GMP / OpenSSL (PRD §5).
 *
 * @sa lib_mul_mod_dsl, add_mod, pow_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
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

/**
 * @brief Modular exponentiation on `qint_t<W>`: returns
 *        `(base ^ exp) mod n`.
 *
 * Allocates a fresh owning `qint_t<W>` for the result and lowers to
 * `lib_pow_mod_dsl` (PRD §3.1, §3.2). The wrapper itself emits no
 * gates; every Toffoli/CNOT/X comes from the underlying primitive
 * (PRD §4 layering rule).
 *
 * Convention: `0^0 == 1` (matches `lib_pow_dsl` and Python's
 * three-argument `pow`).
 *
 * @param base Base register, read-only.
 * @param exp  Exponent register, read-only. May hold any non-negative
 *             integer up to the W-bit register width.
 * @param n    Modulus register, read-only. `n == 0` short-circuits to
 *             a fresh zero register (matches the convention documented
 *             in PRD §5 / §8 #3 and `lib_pow_mod_dsl`).
 *
 * @return A fresh owning `qint_t<W>` whose underlying register holds
 *         `(base ^ exp) mod n` and whose classical `.value` field
 *         tracks that result.
 *
 * @pre `base ∈ [0, n)` and `n ≥ 1` (when `n == 0`, the function
 *      short-circuits to zero — see PRD §5 / §8 #3). The exponent
 *      `exp` is unconstrained beyond fitting in W bits.
 *
 * @par Behavior on precondition violation
 * The library does **not** check the precondition. Calling `pow_mod`
 * with `base` outside `[0, n)` is **undefined behavior** — the
 * returned value is representable but is not the mathematical answer.
 * This matches the trust model of classical `pow(a, b, c)` /
 * GMP / OpenSSL (PRD §5).
 *
 * @sa lib_pow_mod_dsl, add_mod, mul_mod
 * @see PRD §5 (Trust model and precondition contract).
 */
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
