// qint_arith_v3.hpp -- M19/M9: qint_t<W> arithmetic compound-assigns via DSL.
// Uses BitProxy for per-bit lazy WHEN promotion with classical folding.
// Operators: +=, -=, *=, /=, %=
#pragma once
#ifndef STURM_BACKEND_ENABLED
#  error "qint_arith_v3.hpp must only be included when STURM_BACKEND_ENABLED is set"
#endif
#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/lib/adder_dsl.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"
#include "sturm/lib/mod_dsl.hpp"
#include "sturm/control/when_fwd.hpp"
#include "sturm/control/garbage_registry.hpp"
#include <cstddef>

namespace sturm {
namespace detail_arith {

template <std::size_t W>
qint_t<W> make_b_mut(const qint_t<W>& s) {
    qint_t<W> m; m.value = s.value; m.super_mask = s.super_mask;
    m.qubits = s.qubits; m.owning_ = false; return m;
}
template <std::size_t W>
void release_temp_qubits(qint_t<W>& bm, const qint_t<W>& orig) {
    for (std::size_t i = 0; i < W; ++i)
        if (bm.qubits[i] >= 0 && orig.qubits[i] < 0)
            QubitPool::instance().release(bm.qubits[i]);
}
template <std::size_t W>
void rebuild_super_mask(qint_t<W>& q) {
    q.super_mask = 0;
    for (std::size_t i = 0; i < W; ++i)
        if (q.qubits[i] >= 0) q.super_mask |= (1ULL << i);
}
template <std::size_t W>
void build_proxy_pair(qint_t<W>& self, qint_t<W>& bm,
                      BitProxy* sp, BitProxy* bp) {
    for (std::size_t i = 0; i < W; ++i) {
        sp[i] = BitProxy(self, i);
        bp[i] = BitProxy(bm, i);
    }
}
inline void release_anc(qbool& q) {
    if (q.qubits[0] >= 0) {
        QubitPool::instance().release(q.qubits[0]);
        q.qubits[0] = -1;
    }
}

} // namespace detail_arith

// -- operator+= (in-place Cuccaro add) ----------------------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator+=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value = value + b.value; return *this;
    }
    auto b_mut = detail_arith::make_b_mut(b);
    BitProxy tb[W], bb[W];
    detail_arith::build_proxy_pair(*this, b_mut, tb, bb);
    qbool carry_q; BitProxy carry(carry_q);
    lib_add_dsl<BitProxy>(bb, tb, carry, W);
    detail_arith::release_anc(carry_q);
    detail_arith::release_temp_qubits(b_mut, b);
    detail_arith::rebuild_super_mask(*this);
    value = value + b.value;
    return *this;
}

// -- operator-= (in-place Cuccaro sub) ----------------------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator-=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value = value - b.value; return *this;
    }
    auto b_mut = detail_arith::make_b_mut(b);
    BitProxy tb[W], bb[W];
    detail_arith::build_proxy_pair(*this, b_mut, tb, bb);
    qbool borrow_q; BitProxy borrow(borrow_q);
    lib_sub_dsl<BitProxy>(bb, tb, borrow, W);
    detail_arith::release_anc(borrow_q);
    detail_arith::release_temp_qubits(b_mut, b);
    detail_arith::rebuild_super_mask(*this);
    value = value - b.value;
    return *this;
}

// -- operator*= (out-of-place mul, keep lower W bits) -------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator*=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value = value * b.value; return *this;
    }
    auto b_mut = detail_arith::make_b_mut(b);
    BitProxy ab[W], bb[W];
    detail_arith::build_proxy_pair(*this, b_mut, ab, bb);
    static constexpr std::size_t RW = 2u * W;
    int ri[RW]; qbool rq[RW]; BitProxy rb[RW];
    for (std::size_t i = 0; i < RW; ++i) {
        ri[i] = QubitPool::instance().allocate();
        rq[i] = qbool::make_non_owning(ri[i]);
        rb[i] = BitProxy(rq[i]);
    }
    lib_mul_dsl<BitProxy>(ab, W, bb, W, rb, RW);
    detail_arith::release_temp_qubits(b_mut, b);
    for (std::size_t i = 0; i < W; ++i)
        if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
    for (std::size_t i = 0; i < W; ++i) qubits[i] = ri[i];
    for (std::size_t i = W; i < RW; ++i) QubitPool::instance().release(ri[i]);
    detail_arith::rebuild_super_mask(*this);
    value = value * b.value;
    return *this;
}

// -- operator/= (out-of-place div, keep quotient) -----------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator/=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value = (b.value != 0) ? (value / b.value) : 0; return *this;
    }
    auto b_mut = detail_arith::make_b_mut(b);
    BitProxy ab[W], bm[W];
    detail_arith::build_proxy_pair(*this, b_mut, ab, bm);
    int qi[W], ri[W]; qbool qq[W], rq[W]; BitProxy qb[W], rb[W];
    for (std::size_t i = 0; i < W; ++i) {
        qi[i] = QubitPool::instance().allocate();
        ri[i] = QubitPool::instance().allocate();
        qq[i] = qbool::make_non_owning(qi[i]);
        rq[i] = qbool::make_non_owning(ri[i]);
        qb[i] = BitProxy(qq[i]); rb[i] = BitProxy(rq[i]);
    }
    lib_div_dsl<BitProxy>(ab, W, bm, W, qb, rb);
    detail_arith::release_temp_qubits(b_mut, b);
    // Discarded remainder register ri[] keeps unconditional release
    // (pre-existing lossy leak in the ctrl=|0> branch, out of scope for
    // this issue — tracked separately).
    for (std::size_t i = 0; i < W; ++i) QubitPool::instance().release(ri[i]);
    if (detail::current_control == nullptr) {
        // Uncontrolled fast path: release old A, pointer-relabel to quotient.
        for (std::size_t i = 0; i < W; ++i)
            if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
        for (std::size_t i = 0; i < W; ++i) qubits[i] = qi[i];
        detail_arith::rebuild_super_mask(*this);
    } else {
        // Controlled path: per-bit Fredkin (CSWAP) between this->qubits[i]
        // and qi[i] via the a^=b; b^=a; a^=b idiom. BitProxy lifts these
        // CXs to controlled form under the current WHEN scope. After the
        // swap, this->qubits[i] physically holds the quotient (iff ctrl=1)
        // and qi[i] holds garbage (ctrl·old_A). Leak qi[] — never release.
        for (std::size_t i = 0; i < W; ++i) {
            // BitProxy over *this at bit i handles classical→quantum
            // promotion internally via ensure_quantum (matches the existing
            // compute-path semantics for classical input bits under WHEN).
            BitProxy a_bit(*this, i);
            qbool r_q = qbool::make_non_owning(qi[i]);
            BitProxy r_bit(r_q);
            a_bit ^= r_bit;
            r_bit ^= a_bit;
            a_bit ^= r_bit;
        }
        // All W bits now live on physical qubits — force super_mask to all-ones.
        super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
        // Register the leaked quotient register with the garbage registry
        // so a future transpiler pass can emit proper uncomputation.
        detail::garbage_registry::register_garbage(
            detail::garbage_registry::source_op_tag::DIV_ASSIGN,
            detail::current_control_qubit,
            static_cast<int>(W),
            qi);
    }
    value = (b.value != 0) ? (value / b.value) : 0;
    return *this;
}

// -- operator%= (out-of-place mod, keep remainder) ----------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator%=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value = (b.value != 0) ? (value % b.value) : 0; return *this;
    }
    auto b_mut = detail_arith::make_b_mut(b);
    BitProxy ab[W], bm[W];
    detail_arith::build_proxy_pair(*this, b_mut, ab, bm);
    int ri[W]; qbool rq[W]; BitProxy rb[W];
    for (std::size_t i = 0; i < W; ++i) {
        ri[i] = QubitPool::instance().allocate();
        rq[i] = qbool::make_non_owning(ri[i]);
        rb[i] = BitProxy(rq[i]);
    }
    lib_mod_dsl<BitProxy>(ab, W, bm, W, rb);
    detail_arith::release_temp_qubits(b_mut, b);
    if (detail::current_control == nullptr) {
        // Uncontrolled fast path: release old A, pointer-relabel to remainder.
        for (std::size_t i = 0; i < W; ++i)
            if (qubits[i] >= 0) QubitPool::instance().release(qubits[i]);
        for (std::size_t i = 0; i < W; ++i) qubits[i] = ri[i];
        detail_arith::rebuild_super_mask(*this);
    } else {
        // Controlled path: per-bit Fredkin (CSWAP) between this->qubits[i]
        // and ri[i]. Leak ri[] — never release.
        for (std::size_t i = 0; i < W; ++i) {
            BitProxy a_bit(*this, i);
            qbool r_q = qbool::make_non_owning(ri[i]);
            BitProxy r_bit(r_q);
            a_bit ^= r_bit;
            r_bit ^= a_bit;
            a_bit ^= r_bit;
        }
        super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
        detail::garbage_registry::register_garbage(
            detail::garbage_registry::source_op_tag::MOD_ASSIGN,
            detail::current_control_qubit,
            static_cast<int>(W),
            ri);
    }
    value = (b.value != 0) ? (value % b.value) : 0;
    return *this;
}

} // namespace sturm
