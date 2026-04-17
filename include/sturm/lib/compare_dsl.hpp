// compare_dsl.hpp — M17 (PRD v3): EQ, LT, LE, GT, GE, NE in DSL style.
//
// lib_eq_dsl(a, b, n, r): r ^= (a == b)  — XNOR per bit + n-AND
// lib_lt_dsl(a, b, n, r): r ^= (a <  b)  — subtraction sign bit (borrow)
// lib_le_dsl(a, b, n, r): r ^= (a <= b)  — LT OR EQ
// lib_gt_dsl(a, b, n, r): r ^= (a >  b)  — LT(b,a)
// lib_ge_dsl(a, b, n, r): r ^= (a >= b)  — NOT LT(a,b)
// lib_ne_dsl(a, b, n, r): r ^= (a != b)  — NOT EQ(a,b)
//
// All in sturm:: namespace. No _when variants. WHEN lifting is automatic.
// Target: <200 LoC.

#pragma once

#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/lib/logic_dsl.hpp"
#include "sturm/lib/c_and_dsl.hpp"
#include "sturm/lib/adder_dsl.hpp"

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace sturm {

// ── Ancilla helper ───────────────────────────────────────────────────────────
namespace detail_compare {

template <typename Bit>
Bit make_ancilla_view(qbool& owner) {
    if constexpr (std::is_same_v<Bit, qbool>) {
        return qbool::make_non_owning(owner.qubits[0]);
    } else {
        return Bit(owner);
    }
}

} // namespace detail_compare

// ── lib_eq_dsl ────────────────────────────────────────────────────────────────
// result ^= (a == b). n==0: vacuously equal.
template <typename Bit>
inline void lib_eq_dsl(Bit* a_bits, Bit* b_bits, size_t n, Bit& result) {
    if (n == 0u) { result.flip(); return; }

    static constexpr size_t kMaxN = 16u;
    assert(n <= kMaxN);

    int   xnor_idx[kMaxN];
    qbool xnor_own[kMaxN];
    Bit   xnor_bits[kMaxN];
    for (size_t i = 0u; i < n; ++i) {
        xnor_idx[i]  = QubitPool::instance().allocate();
        xnor_own[i]  = qbool::make_non_owning(xnor_idx[i]);
        xnor_bits[i] = detail_compare::make_ancilla_view<Bit>(xnor_own[i]);
    }
    for (size_t i = 0u; i < n; ++i)
        lib_xnor_dsl(a_bits[i], b_bits[i], xnor_bits[i]);

    lib_c_n_AND_dsl(xnor_bits, n, result);

    for (size_t i = 0u; i < n; ++i)
        lib_xnor_dsl(a_bits[i], b_bits[i], xnor_bits[i]);
    for (size_t i = n; i-- > 0u;)
        QubitPool::instance().release(xnor_idx[i]);
}

// ── lib_lt_dsl ────────────────────────────────────────────────────────────────
// result ^= (a < b) unsigned.
// Subtraction sign-bit approach (PRD v3 §8):
//   Compute a_copy = a (in ancilla), then a_copy -= b.
//   The borrow output is 1 iff a < b.
//   Copy borrow to result, then uncompute.
template <typename Bit>
inline void lib_lt_dsl(Bit* a_bits, Bit* b_bits, size_t n, Bit& result) {
    if (n == 0u) return;

    static constexpr size_t kMaxN = 16u;
    assert(n <= kMaxN);

    // Allocate a_copy[n] ancilla (starts |0>).
    int   a_copy_idx[kMaxN];
    qbool a_copy_own[kMaxN];
    Bit   a_copy[kMaxN];
    for (size_t i = 0u; i < n; ++i) {
        a_copy_idx[i] = QubitPool::instance().allocate();
        a_copy_own[i] = qbool::make_non_owning(a_copy_idx[i]);
        a_copy[i]     = detail_compare::make_ancilla_view<Bit>(a_copy_own[i]);
    }
    // Copy a into a_copy.
    for (size_t i = 0u; i < n; ++i)
        a_copy[i] ^= a_bits[i];

    // Allocate borrow and carry_scratch ancilla.
    int   borrow_idx        = QubitPool::instance().allocate();
    int   carry_scratch_idx = QubitPool::instance().allocate();
    qbool borrow_own        = qbool::make_non_owning(borrow_idx);
    qbool carry_scratch_own = qbool::make_non_owning(carry_scratch_idx);
    Bit   borrow            = detail_compare::make_ancilla_view<Bit>(borrow_own);
    Bit   carry_scratch     = detail_compare::make_ancilla_view<Bit>(carry_scratch_own);

    // Compute a_copy -= b; borrow = (a < b).
    lib_sub_dsl(b_bits, a_copy, borrow, n);

    // Copy borrow to result.
    result ^= borrow;

    // Uncompute borrow (XOR result back).
    borrow ^= result;

    // Restore a_copy by computing a_copy += b; carry_scratch = overflow.
    lib_add_dsl(b_bits, a_copy, carry_scratch, n);

    // carry_scratch == (result == 1) because add undoes sub exactly;
    // zero carry_scratch by XORing result.
    carry_scratch ^= result;

    // Restore a_copy to |0> by un-copying a.
    for (size_t i = 0u; i < n; ++i)
        a_copy[i] ^= a_bits[i];

    // Release ancilla.
    QubitPool::instance().release(carry_scratch_idx);
    QubitPool::instance().release(borrow_idx);
    for (size_t i = n; i-- > 0u;)
        QubitPool::instance().release(a_copy_idx[i]);
}

// ── lib_le_dsl ────────────────────────────────────────────────────────────────
// result ^= (a <= b) = LT(a,b) OR EQ(a,b).
template <typename Bit>
inline void lib_le_dsl(Bit* a_bits, Bit* b_bits, size_t n, Bit& result) {
    int lt_idx = QubitPool::instance().allocate();
    int eq_idx = QubitPool::instance().allocate();
    qbool lt_own = qbool::make_non_owning(lt_idx);
    qbool eq_own = qbool::make_non_owning(eq_idx);
    Bit lt_r = detail_compare::make_ancilla_view<Bit>(lt_own);
    Bit eq_r = detail_compare::make_ancilla_view<Bit>(eq_own);
    lib_lt_dsl(a_bits, b_bits, n, lt_r);
    lib_eq_dsl(a_bits, b_bits, n, eq_r);
    lib_or_dsl(lt_r, eq_r, result);
    lib_lt_dsl(a_bits, b_bits, n, lt_r);
    lib_eq_dsl(a_bits, b_bits, n, eq_r);
    QubitPool::instance().release(eq_idx);
    QubitPool::instance().release(lt_idx);
}

// ── lib_gt_dsl ────────────────────────────────────────────────────────────────
// result ^= (a > b) = LT(b, a).
template <typename Bit>
inline void lib_gt_dsl(Bit* a_bits, Bit* b_bits, size_t n, Bit& result) {
    lib_lt_dsl(b_bits, a_bits, n, result);
}

// ── lib_ge_dsl ────────────────────────────────────────────────────────────────
// result ^= (a >= b) = NOT LT(a,b).
template <typename Bit>
inline void lib_ge_dsl(Bit* a_bits, Bit* b_bits, size_t n, Bit& result) {
    int lt_idx = QubitPool::instance().allocate();
    qbool lt_own = qbool::make_non_owning(lt_idx);
    Bit lt_r = detail_compare::make_ancilla_view<Bit>(lt_own);
    lib_lt_dsl(a_bits, b_bits, n, lt_r);
    lt_r.flip(); result ^= lt_r; lt_r.flip();
    lib_lt_dsl(a_bits, b_bits, n, lt_r);
    QubitPool::instance().release(lt_idx);
}

// ── lib_ne_dsl ────────────────────────────────────────────────────────────────
// result ^= (a != b) = NOT EQ(a,b).
template <typename Bit>
inline void lib_ne_dsl(Bit* a_bits, Bit* b_bits, size_t n, Bit& result) {
    int eq_idx = QubitPool::instance().allocate();
    qbool eq_own = qbool::make_non_owning(eq_idx);
    Bit eq_r = detail_compare::make_ancilla_view<Bit>(eq_own);
    lib_eq_dsl(a_bits, b_bits, n, eq_r);
    eq_r.flip(); result ^= eq_r; eq_r.flip();
    lib_eq_dsl(a_bits, b_bits, n, eq_r);
    QubitPool::instance().release(eq_idx);
}

} // namespace sturm
