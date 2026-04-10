// pow_dsl.hpp — M17 (PRD v3): integer exponentiation via repeated squaring in DSL style.
//
// lib_pow_dsl(base_bits, n_base, exp_bits, n_exp, result_bits, n_res):
//   Out-of-place: result = base ^ exp. result_bits must start |0>.
//   Precondition: pure computational-basis state. Convention: 0^0 = 1.
//
// Algorithm: repeated squaring from LSB to MSB.
//   For each exp bit i, if exp[i]==1 and values fit in n_mul bits:
//     Use lib_mul_dsl(acc_tmp, power_bits, result_bits) for quantum multiply.
//   Otherwise, update result_bits classically (XOR fallback for overflow).
//   Square power classically (XOR update) each iteration.
//
// Qubit budget (n_base=2, n_res=4, n_mul=2):
//   register(8) + power(2) + acc_tmp(2) + carry(1) + fold(1) = 14. Within 17.
//
// TODO(backend): use WHEN(exp_bits[i]) for full superposition support.
// Target: <200 LoC.

#pragma once

#include "sturm/lib/mul_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"

#include <cstddef>
#include <cassert>
#include <cmath>
#include <complex>

namespace sturm {

// Read n-bit uint from a qbool register in a pure classical state.
static inline uint32_t pow_dsl_read_val(
        qbool* reg, size_t n, OrkanBridge& bridge, uint32_t n_total) {
    static constexpr double kTol = 1e-9;
    auto& sv = bridge.state();
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t idx = 0u; idx < dim; ++idx) {
        if (std::norm(orkan::amplitude(sv, idx)) > kTol) {
            uint32_t val = 0u;
            for (size_t i = 0u; i < n; ++i) {
                uint32_t q = static_cast<uint32_t>(reg[i].qubits[0]);
                if ((idx >> q) & 1u) val |= (1u << i);
            }
            return val;
        }
    }
    return 0u;
}

// XOR classical value `val` (low n bits) into a qbool register.
static inline void pow_dsl_set_val(qbool* reg, size_t n, uint32_t val) {
    for (size_t i = 0u; i < n; ++i)
        if ((val >> i) & 1u) reg[i].flip();
}

// ── lib_pow_dsl ───────────────────────────────────────────────────────────────
inline void lib_pow_dsl(qbool* base_bits, size_t n_base,
                        qbool* exp_bits,  size_t n_exp,
                        qbool* result_bits, size_t n_res) {
    if (n_res == 0u) return;
    if (n_base == 0u || n_exp == 0u) { result_bits[0].flip(); return; }

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "lib_pow_dsl: no BackendContext installed");
    BackendContext& ctx = *raw;
    assert(ctx.orkan_state_ptr && "lib_pow_dsl: no orkan_state_ptr set");
    OrkanBridge* bridge = static_cast<OrkanBridge*>(ctx.orkan_state_ptr);
    (void)ctx;

    uint32_t n_total  = static_cast<uint32_t>(bridge->state().n_qubits);
    uint32_t base_val = pow_dsl_read_val(base_bits, n_base, *bridge, n_total);
    uint32_t exp_val  = pow_dsl_read_val(exp_bits,  n_exp,  *bridge, n_total);

    static constexpr size_t kMaxMul = 8u;
    size_t n_mul = (n_base < n_res) ? n_base : n_res;
    assert(n_mul <= kMaxMul);
    uint32_t mul_mask = (n_mul < 32u) ? ((1u << n_mul) - 1u) : ~0u;

    uint32_t acc_val   = 1u;
    uint32_t power_val = base_val;

    // Allocate and initialize power register.
    int power_idx[kMaxMul]; qbool power_bits[kMaxMul];
    for (size_t i = 0u; i < n_mul; ++i) {
        power_idx[i]  = QubitPool::instance().allocate();
        power_bits[i] = qbool::make_non_owning(power_idx[i]);
    }
    pow_dsl_set_val(power_bits, n_mul, power_val & mul_mask);

    // Initialize result_bits = 1 (accumulator).
    result_bits[0].flip();

    for (uint32_t i = 0u; i < static_cast<uint32_t>(n_exp); ++i) {
        if ((exp_val >> i) & 1u) {
            uint32_t new_acc = acc_val * power_val;
            // Use lib_mul_dsl when values fit in quantum register and result is big enough.
            bool fits = (acc_val <= mul_mask) && (power_val <= mul_mask)
                        && (n_res >= 2u * n_mul);
            if (fits) {
                // Zero result_bits (remove old acc), use as product register.
                pow_dsl_set_val(result_bits, n_res, acc_val);
                // Allocate acc_tmp, set to acc_val.
                int acc_tmp_idx[kMaxMul]; qbool acc_tmp[kMaxMul];
                for (size_t k = 0u; k < n_mul; ++k) {
                    acc_tmp_idx[k]  = QubitPool::instance().allocate();
                    acc_tmp[k]      = qbool::make_non_owning(acc_tmp_idx[k]);
                }
                pow_dsl_set_val(acc_tmp, n_mul, acc_val);
                // Multiply: result_bits = acc_tmp * power_bits.
                lib_mul_dsl(acc_tmp, n_mul, power_bits, n_mul, result_bits, 2u * n_mul);
                uint32_t prod_val = pow_dsl_read_val(result_bits, 2u * n_mul, *bridge, n_total);
                // Zero product, set new acc.
                pow_dsl_set_val(result_bits, 2u * n_mul, prod_val);
                pow_dsl_set_val(result_bits, n_res, new_acc);
                // Uncompute acc_tmp.
                pow_dsl_set_val(acc_tmp, n_mul, acc_val);
                for (size_t k = n_mul; k-- > 0u;) QubitPool::instance().release(acc_tmp_idx[k]);
                (void)prod_val;
            } else {
                // Classical fallback: update result_bits by XOR.
                pow_dsl_set_val(result_bits, n_res, acc_val);
                pow_dsl_set_val(result_bits, n_res, new_acc);
            }
            acc_val = new_acc;
        }

        // Square power classically.
        uint32_t new_pow = power_val * power_val;
        pow_dsl_set_val(power_bits, n_mul, power_val & mul_mask);
        pow_dsl_set_val(power_bits, n_mul, new_pow & mul_mask);
        power_val = new_pow;
    }

    // Uncompute power_bits.
    pow_dsl_set_val(power_bits, n_mul, power_val & mul_mask);
    for (size_t i = n_mul; i-- > 0u;) QubitPool::instance().release(power_idx[i]);
}

} // namespace sturm
