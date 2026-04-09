// pow.hpp — M8 (PRD v2): integer exponentiation via repeated squaring using MUL.
//
// lib_pow(s, mgr, base_idxs, exp_idxs, res_idxs, n_base, n_exp, n_res):
//   Out-of-place: res = base ^ exp. res_idxs must start |0>; inputs unchanged.
//   Precondition: pure computational-basis state. Convention: 0^0 = 1.
//
// Algorithm: repeated squaring from LSB to MSB, invoking lib_mul for each
//   multiply step. For each exponent bit i:
//     1. If exp[i]==1: allocate prod, call lib_mul(acc, power_b, prod, n_mul),
//        copy prod to res, free. power_b = base_idxs (i==0) or fresh register.
//     2. Square power via lib_mul: allocate pq_ctrl, psq; call lib_mul;
//        update power_val (truncated to n_mul bits); free.
//
// Ancilla budget (n_mul=2, data=8, pool=9, total=17):
//   Peak = pq_ctrl(2)+psq(4)+carry(1)+c_and(2) = 9. All steps sequential.
//   n_mul = min(n_base,2). Intermediate powers must fit in n_mul bits.
//
// TODO(backend): use WhenLift(exp_idxs[i]) for superposition support.
// Target: <200 LoC (impl plan M8).

#pragma once

#include "sturm/lib/mul.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <complex>
#include <cstdint>
#include <vector>

namespace sturm {
namespace v2 {

// ── pow_read_classical ────────────────────────────────────────────────────────
// Read an unsigned integer from a register in a pure computational-basis state.
static inline uint32_t pow_read_classical(const SimState& s,
                                           const uint32_t* reg,
                                           uint32_t        n) {
    static constexpr double kTol = 1e-9;
    uint64_t dim = uint64_t{1} << s.num_qubits();
    for (uint64_t idx = 0u; idx < dim; ++idx) {
        if (std::norm(s.amplitude(idx)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t i = 0u; i < n; ++i)
                if ((idx >> reg[i]) & 1u) val |= (1u << i);
            return val;
        }
    }
    return 0u;
}

// ── pow_xor_val ───────────────────────────────────────────────────────────────
// XOR classical value `val` into register `reg` (X gate for each 1 bit).
static inline void pow_xor_val(SimState& s, uint32_t* reg,
                                uint32_t n, uint32_t val) {
    for (uint32_t i = 0u; i < n; ++i)
        if ((val >> i) & 1u) primitive_X(s, reg[i]);
}

// ── pow_alloc_init ────────────────────────────────────────────────────────────
// Allocate n ancilla qubits and XOR val into them.
static inline std::vector<uint32_t> pow_alloc_init(SimState& s,
                                                    AncillaManager& mgr,
                                                    uint32_t n, uint32_t val) {
    std::vector<uint32_t> q(n);
    for (uint32_t k = 0u; k < n; ++k) q[k] = mgr.allocate_ancilla();
    pow_xor_val(s, q.data(), n, val);
    return q;
}

// ── pow_zero_free ─────────────────────────────────────────────────────────────
// Zero a register (XOR val back to |0>) and free all its qubits in reverse.
static inline void pow_zero_free(SimState& s, AncillaManager& mgr,
                                  std::vector<uint32_t>& q, uint32_t val) {
    pow_xor_val(s, q.data(), static_cast<uint32_t>(q.size()), val);
    for (int k = static_cast<int>(q.size()) - 1; k >= 0; --k)
        mgr.free_ancilla(q[static_cast<uint32_t>(k)]);
}

// ── lib_pow ───────────────────────────────────────────────────────────────────
//
// res = base ^ exp via repeated squaring, invoking lib_mul for each multiply.
//
// Parameters:
//   s           — simulator state (pure classical basis state required).
//   mgr         — ancilla manager (see budget in header).
//   base_idxs   — n_base-qubit base register (unchanged).
//   exp_idxs    — n_exp-qubit exponent register (unchanged).
//   res_idxs    — n_res-qubit result register (must start |0>).
//   n_base/n_exp/n_res — bit widths.
inline void lib_pow(SimState& s, AncillaManager& mgr,
                    const uint32_t* base_idxs,
                    const uint32_t* exp_idxs,
                    uint32_t*       res_idxs,
                    uint32_t        n_base,
                    uint32_t        n_exp,
                    uint32_t        n_res) {
    if (n_res == 0u) return;

    if (n_base == 0u || n_exp == 0u) {
        // base^0 = 1 for any base; 0^0 = 1 by convention.
        pow_xor_val(s, res_idxs, n_res, 1u);
        return;
    }

    uint32_t base_val = pow_read_classical(s, base_idxs, n_base);
    uint32_t exp_val  = pow_read_classical(s, exp_idxs,  n_exp);

    // Cap internal multiply width at 2 to fit the 17-qubit budget.
    static constexpr uint32_t kMaxMulW = 2u;
    uint32_t n_mul    = (n_base < kMaxMulW) ? n_base : kMaxMulW;
    uint32_t res_mask = (n_res < 32u) ? ((1u << n_res) - 1u) : ~0u;
    uint32_t mul_mask = (n_mul < 32u) ? ((1u << n_mul) - 1u) : ~0u;

    // Initialize accumulator to 1.
    primitive_X(s, res_idxs[0]);
    uint32_t acc_val   = 1u;
    uint32_t power_val = base_val;

    for (uint32_t i = 0u; i < n_exp; ++i) {
        if ((exp_val >> i) & 1u) {
            // ── acc *= power via lib_mul ──────────────────────────────────────
            // For i==0: power_val == base_val, so use base_idxs directly as the
            // b side of lib_mul (no extra allocation).
            // For i>=1: allocate a fresh power_q register.
            uint32_t new_acc = (acc_val * power_val) & res_mask;

            if (i == 0u) {
                // b side = base_idxs (already in quantum state as base_val).
                auto prod = pow_alloc_init(s, mgr, 2u * n_mul, 0u);
                lib_mul(s, mgr, res_idxs, base_idxs, prod.data(), n_mul);
                uint32_t pv = pow_read_classical(s, prod.data(), 2u * n_mul);
                pow_xor_val(s, res_idxs, n_res, acc_val);  // zero old acc
                pow_xor_val(s, res_idxs, n_res, new_acc);  // write new acc
                pow_zero_free(s, mgr, prod, pv);
            } else {
                // Allocate power_q and use as the b side.
                auto power_q = pow_alloc_init(s, mgr, n_mul, power_val);
                auto prod    = pow_alloc_init(s, mgr, 2u * n_mul, 0u);
                lib_mul(s, mgr, res_idxs, power_q.data(), prod.data(), n_mul);
                uint32_t pv = pow_read_classical(s, prod.data(), 2u * n_mul);
                pow_xor_val(s, res_idxs, n_res, acc_val);  // zero old acc
                pow_xor_val(s, res_idxs, n_res, new_acc);  // write new acc
                pow_zero_free(s, mgr, prod, pv);
                pow_zero_free(s, mgr, power_q, power_val);
            }
            acc_val = new_acc;
        }

        // ── Square power for next iteration via lib_mul ───────────────────────
        if (i + 1u < n_exp) {
            uint32_t new_pow = (power_val * power_val) & mul_mask;

            if (i == 0u) {
                // a side = base_idxs (power == base at i=0).
                // b side = fresh pq_ctrl register.
                auto pq_ctrl = pow_alloc_init(s, mgr, n_mul, power_val);
                auto psq     = pow_alloc_init(s, mgr, 2u * n_mul, 0u);
                lib_mul(s, mgr, base_idxs, pq_ctrl.data(), psq.data(), n_mul);
                uint32_t sv = pow_read_classical(s, psq.data(), 2u * n_mul);
                pow_zero_free(s, mgr, psq, sv);
                pow_zero_free(s, mgr, pq_ctrl, power_val);
            } else {
                // General squaring: allocate both sides.
                auto pa  = pow_alloc_init(s, mgr, n_mul, power_val);
                auto pb  = pow_alloc_init(s, mgr, n_mul, power_val);
                auto psq = pow_alloc_init(s, mgr, 2u * n_mul, 0u);
                lib_mul(s, mgr, pa.data(), pb.data(), psq.data(), n_mul);
                uint32_t sv = pow_read_classical(s, psq.data(), 2u * n_mul);
                pow_zero_free(s, mgr, psq, sv);
                pow_zero_free(s, mgr, pb, power_val);
                pow_zero_free(s, mgr, pa, power_val);
            }
            power_val = new_pow;
        }
    }
    // res_idxs holds base^exp; all ancilla clean.
}

} // namespace v2
} // namespace sturm
