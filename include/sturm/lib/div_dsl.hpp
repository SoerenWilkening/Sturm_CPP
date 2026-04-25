// div_dsl.hpp — M16 (PRD v3): non-restoring binary division in DSL style.
//
// lib_div_dsl(a, n, b, d, q, r): out-of-place n-bit non-restoring division;
//   q and r must start |0>; a and b unchanged.  Non-restoring shift-subtract/
//   add with sign tracking.  Forward / adjoint schedules share a single
//   `div_kernel<Bit, Forward>` template (the kernel toggles bit-loop order,
//   MAJ/UMA variants, and final-correction order on `Forward`).
//
// __lib_div_dsl_adj + STURM_REGISTER_ADJOINT live in the sibling
// div_dsl_adj.hpp header (auto-included at the bottom; sturm-nmf1).

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <cstddef>
#include <cassert>
#include <type_traits>
#include <vector>

namespace sturm {

// overflow ^= OR(b_bits[lo..n-1]).  Self-inverse (call twice to restore).
// lo >= n: no-op.
template <typename Bit>
static inline void compute_overflow_or_dsl(
        Bit& overflow, Bit* b_bits, size_t lo, size_t n) {
    if (lo >= n) return;
    overflow ^= b_bits[lo];
    for (size_t j = lo + 1u; j < n; ++j) {
        if constexpr (std::is_same_v<Bit, qbool>) {
            qbool tmp = (overflow & b_bits[j]);
            overflow ^= b_bits[j];
            overflow ^= tmp;
        } else {
            qbool tmp = materialize_and(overflow, b_bits[j]);
            overflow ^= b_bits[j];
            Bit tmp_proxy(tmp);
            overflow ^= tmp_proxy;
        }
    }
}

namespace detail_div {

// Gate-reverses of maj_dsl / uma_dsl / lib_add_dsl.  Bodies live in
// div_dsl_adj.hpp (auto-included at the bottom of this header; sturm-nmf1).
template <typename Bit> inline void maj_adj(Bit& a, Bit& b, Bit& c);
template <typename Bit> inline void uma_adj(Bit& a, Bit& b, Bit& c);
template <typename Bit>
inline void lib_add_adj(Bit* a_bits, Bit* b_bits, Bit& carry_out, size_t n);

// Shared forward+adjoint kernel.  `Forward` toggles bit-loop order,
// MAJ/UMA variants, final-correction order, and initial dividend copy.
template <typename Bit, bool Forward>
inline void div_kernel(Bit* dividend_bits, size_t n, Bit* divisor_bits,
                       Bit* quotient_bits, Bit* remainder_bits) {
    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_div_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;

    std::vector<int>   scratch_idx(n);
    std::vector<qbool> scratch_own(n);
    std::vector<Bit>   scratch(n);
    for (size_t k = 0u; k < n; ++k) {
        scratch_idx[k] = QubitPool::instance().allocate();
        scratch_own[k] = qbool::make_non_owning(scratch_idx[k]);
        scratch[k]     = detail_adder::make_ancilla_view<Bit>(scratch_own[k]);
    }
    size_t num_sgn = (n >= 2u) ? n - 1u : 0u;
    std::vector<int>   sgn_idx(num_sgn);
    std::vector<qbool> sgn_own(num_sgn);
    std::vector<Bit>   sgn(num_sgn);
    for (size_t k = 0u; k < num_sgn; ++k) {
        sgn_idx[k] = QubitPool::instance().allocate();
        sgn_own[k] = qbool::make_non_owning(sgn_idx[k]);
        sgn[k]     = detail_adder::make_ancilla_view<Bit>(sgn_own[k]);
    }
    int   overflow_idx  = QubitPool::instance().allocate();
    qbool overflow_own  = qbool::make_non_owning(overflow_idx);
    Bit   overflow      = detail_adder::make_ancilla_view<Bit>(overflow_own);
    int   carry_anc_idx = QubitPool::instance().allocate();
    qbool carry_anc_own = qbool::make_non_owning(carry_anc_idx);
    Bit   carry_anc     = detail_adder::make_ancilla_view<Bit>(carry_anc_own);

    // Pushes overflow as a control; lambda captures to share across halves.
    auto push_ov = [&]() {
        if constexpr (std::is_same_v<Bit, qbool>) {
            ctx.control_stack.push_control(
                static_cast<uint32_t>(overflow.qubits[0]));
        } else {
            overflow.ensure_quantum();
            ctx.control_stack.push_control(
                static_cast<uint32_t>(overflow.qubit_index()));
        }
    };
    auto push_sgn_i = [&](size_t i) {
        if constexpr (std::is_same_v<Bit, qbool>) {
            ctx.control_stack.push_control(
                static_cast<uint32_t>(sgn[i].qubits[0]));
        } else {
            sgn[i].ensure_quantum();
            ctx.control_stack.push_control(
                static_cast<uint32_t>(sgn[i].qubit_index()));
        }
    };
    auto push_q0 = [&]() {
        if constexpr (std::is_same_v<Bit, qbool>) {
            ctx.control_stack.push_control(
                static_cast<uint32_t>(quotient_bits[0].qubits[0]));
        } else {
            quotient_bits[0].ensure_quantum();
            ctx.control_stack.push_control(
                static_cast<uint32_t>(quotient_bits[0].qubit_index()));
        }
    };

    // Final-correction body (self-inverse halves except the add/add_adj).
    auto run_final = [&]() {
        quotient_bits[0].flip();
        push_q0();
        if constexpr (Forward) {
            lib_add_dsl(divisor_bits, remainder_bits, carry_anc, n);
        } else {
            detail_div::lib_add_adj(divisor_bits, remainder_bits, carry_anc, n);
        }
        ctx.control_stack.pop_control();
        quotient_bits[0].flip();
        carry_anc ^= quotient_bits[0];
        carry_anc.flip();
    };
    // Reverse of run_final: reflect the gate order.
    auto run_final_adj = [&]() {
        carry_anc.flip();
        carry_anc ^= quotient_bits[0];
        quotient_bits[0].flip();
        push_q0();
        detail_div::lib_add_adj(divisor_bits, remainder_bits, carry_anc, n);
        ctx.control_stack.pop_control();
        quotient_bits[0].flip();
    };

    // Conditional-negate block (palindromic).
    auto negate_cond = [&](size_t i, bool first) {
        if (first) {
            for (size_t j = 0u; j < n; ++j) scratch[j].flip();
            carry_anc.flip();
        } else {
            sgn[i].flip();
            push_sgn_i(i);
            for (size_t j = 0u; j < n; ++j) scratch[j].flip();
            carry_anc.flip();
            ctx.control_stack.pop_control();
            sgn[i].flip();
        }
    };

    // G11 block: uncompute sgn[i] using overflow_{i+1}.
    auto uncompute_sgn_i = [&](size_t i, size_t lo) {
        size_t lo_prev = lo - 1u;
        compute_overflow_or_dsl(overflow, divisor_bits, lo_prev, n);
        sgn[i] ^= quotient_bits[i + 1u];
        overflow.flip();
        push_ov();
        sgn[i].flip();
        ctx.control_stack.pop_control();
        overflow.flip();
        compute_overflow_or_dsl(overflow, divisor_bits, lo_prev, n);
    };
    // Reverse of uncompute_sgn_i.
    auto uncompute_sgn_i_adj = [&](size_t i, size_t lo) {
        size_t lo_prev = lo - 1u;
        compute_overflow_or_dsl(overflow, divisor_bits, lo_prev, n);
        overflow.flip();
        push_ov();
        sgn[i].flip();
        ctx.control_stack.pop_control();
        overflow.flip();
        sgn[i] ^= quotient_bits[i + 1u];
        compute_overflow_or_dsl(overflow, divisor_bits, lo_prev, n);
    };

    // Forward: copy dividend → remainder.  Adjoint: undo final-correction first.
    if constexpr (Forward) {
        for (size_t i = 0u; i < n; ++i) remainder_bits[i] ^= dividend_bits[i];
    } else {
        run_final_adj();
    }

    // Main bit loop (direction-dependent order).
    for (size_t step = 0u; step < n; ++step) {
        size_t i    = Forward ? (n - 1u - step) : step;
        bool   first = (i == n - 1u);
        size_t lo    = n - i;

        if constexpr (Forward) {
            compute_overflow_or_dsl(overflow, divisor_bits, lo, n);
            overflow.flip();
            push_ov();
            for (size_t j = i; j < n; ++j) scratch[j] ^= divisor_bits[j - i];
            negate_cond(i, first);
            // Cuccaro MAJ/UMA add: r += scratch + carry_anc; q[i] ^= carry_out.
            maj_dsl(carry_anc, remainder_bits[0], scratch[0]);
            for (size_t k = 1u; k < n; ++k)
                maj_dsl(scratch[k - 1u], remainder_bits[k], scratch[k]);
            quotient_bits[i] ^= scratch[n - 1u];
            for (size_t k = n - 1u; k >= 1u; --k)
                uma_dsl(scratch[k - 1u], remainder_bits[k], scratch[k]);
            uma_dsl(carry_anc, remainder_bits[0], scratch[0]);
            negate_cond(i, first);  // self-inverse, undoes previous
            for (size_t j = i; j < n; ++j) scratch[j] ^= divisor_bits[j - i];
            if (i > 0u) {
                quotient_bits[i].flip();
                sgn[i - 1u] ^= quotient_bits[i];
                quotient_bits[i].flip();
            }
            ctx.control_stack.pop_control();
            overflow.flip();
            if (!first && i > 0u) {
                push_ov();
                sgn[i - 1u] ^= sgn[i];
                ctx.control_stack.pop_control();
            }
            if (!first) uncompute_sgn_i(i, lo);
            compute_overflow_or_dsl(overflow, divisor_bits, lo, n);
        } else {
            // Adjoint: reverse of forward per-bit body.
            compute_overflow_or_dsl(overflow, divisor_bits, lo, n);
            if (!first) uncompute_sgn_i_adj(i, lo);
            if (!first && i > 0u) {
                push_ov();
                sgn[i - 1u] ^= sgn[i];
                ctx.control_stack.pop_control();
            }
            overflow.flip();
            push_ov();
            if (i > 0u) {
                quotient_bits[i].flip();
                sgn[i - 1u] ^= quotient_bits[i];
                quotient_bits[i].flip();
            }
            for (size_t j = i; j < n; ++j) scratch[j] ^= divisor_bits[j - i];
            negate_cond(i, first);
            // Reverse MAJ/UMA chain: UMA^-1 first-run, then MAJ^-1 last-run.
            detail_div::uma_adj(carry_anc, remainder_bits[0], scratch[0]);
            for (size_t k = 1u; k < n; ++k)
                detail_div::uma_adj(scratch[k - 1u], remainder_bits[k],
                                    scratch[k]);
            quotient_bits[i] ^= scratch[n - 1u];
            for (size_t k = n - 1u; k >= 1u; --k)
                detail_div::maj_adj(scratch[k - 1u], remainder_bits[k],
                                    scratch[k]);
            detail_div::maj_adj(carry_anc, remainder_bits[0], scratch[0]);
            negate_cond(i, first);
            for (size_t j = i; j < n; ++j) scratch[j] ^= divisor_bits[j - i];
            ctx.control_stack.pop_control();
            overflow.flip();
            compute_overflow_or_dsl(overflow, divisor_bits, lo, n);
        }
    }

    if constexpr (Forward) {
        run_final();
    } else {
        for (size_t i = 0u; i < n; ++i) remainder_bits[i] ^= dividend_bits[i];
    }

    QubitPool::instance().release(carry_anc_idx);
    QubitPool::instance().release(overflow_idx);
    for (size_t k = 0u; k < num_sgn; ++k)
        QubitPool::instance().release(sgn_idx[num_sgn - 1u - k]);
    for (size_t k = 0u; k < n; ++k)
        QubitPool::instance().release(scratch_idx[n - 1u - k]);
}

}  // namespace detail_div

// lib_div_dsl: non-restoring n-bit division.  q = a/b, r = a%b; d == n;
// q and r must start |0>.
template <typename Bit>
inline void lib_div_dsl(Bit* dividend_bits, size_t n,
                        Bit* divisor_bits,  size_t d,
                        Bit* quotient_bits,
                        Bit* remainder_bits) {
    if (n == 0u) return;
    assert(d == n && "lib_div_dsl: d must equal n");
    detail_div::div_kernel<Bit, true>(dividend_bits, n, divisor_bits,
                                       quotient_bits, remainder_bits);
}

} // namespace sturm

// Adjoint sibling: __lib_div_dsl_adj + STURM_REGISTER_ADJOINT (sturm-nmf1).
#include "sturm/lib/div_dsl_adj.hpp"
