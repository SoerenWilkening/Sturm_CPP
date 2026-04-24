// div_dsl.hpp — M16 (PRD v3): non-restoring binary division in DSL style.
//
// lib_div_dsl(dividend_bits, n, divisor_bits, d, quotient_bits, remainder_bits):
//   Out-of-place n-bit non-restoring division: quotient = a/b, remainder = a%b.
//   quotient_bits and remainder_bits must start |0>. a and b are unchanged.
//
// __lib_div_dsl_adj (LO-1b, sturm-1rjw): gate-reverse of lib_div_dsl.  Given
//   state satisfying the divide invariant (dividend == quotient * divisor +
//   remainder), zeros quotient and remainder.  Registered via
//   STURM_REGISTER_ADJOINT so `invert(lib_div_dsl)(…)` resolves at the LO
//   rewrite's scope-exit cleanup.
//
// Algorithm: non-restoring shift-subtract/add with sign tracking.
//   Forward: for bit i = n-1 downto 0: compute overflow, conditionally
//   negate scratch, run Cuccaro MAJ/UMA add with carry_anc, set quotient
//   bit + sign, uncompute.  Final correction: WHEN NOT(q[0]): P += b.
//   Adjoint: same schedule in reverse — bit loop runs 0 .. n-1, final
//   correction first, MAJ↔MAJ^-1 / UMA↔UMA^-1 swap, lib_add_dsl↔adj.
//
// Target: <300 LoC.

#pragma once

#include "sturm/lib/adder_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/routines/invert.hpp"

#include <cstddef>
#include <cassert>
#include <type_traits>
#include <vector>

// Forward-declare BitProxy for the LO-1b adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

// ── compute_overflow_or_dsl ───────────────────────────────────────────────────
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

// Shared body for forward + adjoint division.  Direction `Forward` picks
// loop order and maj/uma vs maj_adj/uma_adj + add vs add_adj.  The per-bit
// body is palindromic in its pre/post halves (every gate but the MAJ/UMA
// chain is self-inverse), so Direction only changes: (i) bit-loop order,
// (ii) MAJ/UMA variants, (iii) final correction order + add variant, (iv)
// initial dividend copy position.
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

    // Final-correction body, self-inverse halves except the add/add_adj.
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

    // ── Forward: dividend -> remainder (copy happens at start). ──────────────
    if constexpr (Forward) {
        for (size_t i = 0u; i < n; ++i) remainder_bits[i] ^= dividend_bits[i];
    } else {
        // Adjoint: run final correction reversed FIRST.
        run_final_adj();
    }

    // ── Main bit loop (direction-dependent order). ──────────────────────────
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

// ── lib_div_dsl ───────────────────────────────────────────────────────────────
// Non-restoring n-bit division: quotient_bits = a/b, remainder_bits = a%b.
// d must equal n (asserted). quotient_bits and remainder_bits must start |0>.
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

// ── __lib_div_dsl_adj (LO-1b, sturm-1rjw) ────────────────────────────────────
// Gate-reverse of lib_div_dsl.  Precondition: dividend == quotient * divisor
// + remainder.  Postcondition: quotient, remainder both |0>; dividend and
// divisor unchanged.  See plan_lossy_compound_reversibility §3 LO-1b.
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
