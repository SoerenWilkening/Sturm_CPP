// qint_bitwise_v3.hpp -- M19 (PRD v3): Wire qint_t<W> bitwise compound-assigns
// to DSL logic library functions. Included by qint_bitwise.hpp when
// STURM_BACKEND_ENABLED.
//
// Operators: operator^= (in-place CNOT), operator&= (out-of-place AND),
// operator|= (out-of-place OR).  M4: Uses BitProxy for per-bit lazy WHEN
// promotion with classical folding.

#pragma once

#ifndef STURM_BACKEND_ENABLED
#  error "qint_bitwise_v3.hpp must only be included when STURM_BACKEND_ENABLED is set"
#endif

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/control/when_fwd.hpp"
#include <cstddef>

namespace sturm {
namespace detail_bw {

// Build a mutable non-owning shallow copy of src preserving qubit indices.
template <std::size_t W>
qint_t<W> make_b_mut(const qint_t<W>& src) {
    qint_t<W> m;
    m.value      = src.value;
    m.super_mask = src.super_mask;
    m.qubits     = src.qubits;
    m.owning_    = false;
    return m;
}

// Release NEW qubits allocated on b_mut where orig had -1.
template <std::size_t W>
void release_temp_qubits(qint_t<W>& b_mut, const qint_t<W>& orig) {
    for (std::size_t i = 0; i < W; ++i)
        if (b_mut.qubits[i] >= 0 && orig.qubits[i] < 0)
            QubitPool::instance().release(b_mut.qubits[i]);
}

} // namespace detail_bw

// -- operator^= (in-place per-bit CNOT) ------------------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator^=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value ^= b.value;
        return *this;
    }
    auto b_mut = detail_bw::make_b_mut(b);
    for (std::size_t i = 0; i < W; ++i) {
        BitProxy this_bit(*this, i);
        BitProxy b_bit(b_mut, i);
        this_bit ^= b_bit;
    }
    detail_bw::release_temp_qubits(b_mut, b);
    value ^= b.value;
    return *this;
}

namespace detail_bw {
// Shared body for &= / |=. `Op` is a callable that takes (a_bit, b_bit) and
// returns the desired BitProxy expression. Controlled lossy paths are
// desugared at compile time by the LO transpiler pass (PRD §2); only the
// uncontrolled relabel path remains here.
template <std::size_t W, class Op>
void apply_bitwise_oop(qint_t<W>& self, const qint_t<W>& b, Op&& op) {
    const uint64_t orig_super = self.super_mask;
    auto b_mut = make_b_mut(b);
    int res_idx[W]; qbool res_qbools[W]; BitProxy res_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        res_idx[i]    = QubitPool::instance().allocate();
        res_qbools[i] = qbool::make_non_owning(res_idx[i]);
        res_bits[i]   = BitProxy(res_qbools[i]);
    }
    for (std::size_t i = 0; i < W; ++i) {
        BitProxy a_bit(self, i); BitProxy b_bit(b_mut, i);
        res_bits[i] ^= op(a_bit, b_bit);
    }
    release_temp_qubits(b_mut, b);
    self.super_mask = 0;
    for (std::size_t i = 0; i < W; ++i) {
        if (self.qubits[i] >= 0) QubitPool::instance().release(self.qubits[i]);
        self.qubits[i] = res_idx[i];
        bool a_q = (orig_super >> i) & 1, b_q = (b.super_mask >> i) & 1;
        if (a_q || b_q) self.super_mask |= (1ULL << i);
    }
}
} // namespace detail_bw

// -- operator&= (out-of-place AND via Toffoli) -----------------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator&=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value &= b.value; return *this;
    }
    detail_bw::apply_bitwise_oop<W>(*this, b,
        [](BitProxy& a_bit, BitProxy& b_bit) { return a_bit & b_bit; });
    value &= b.value;
    return *this;
}

// -- operator|= (out-of-place OR via CNOT+CNOT+Toffoli) -------------------
template <std::size_t W>
qint_t<W>& qint_t<W>::operator|=(const qint_t<W>& b) {
    if ((qubits[0] < 0 && b.qubits[0] < 0) && detail::current_control == nullptr) {
        value |= b.value; return *this;
    }
    detail_bw::apply_bitwise_oop<W>(*this, b,
        [](BitProxy& a_bit, BitProxy& b_bit) { return a_bit | b_bit; });
    value |= b.value;
    return *this;
}

} // namespace sturm
