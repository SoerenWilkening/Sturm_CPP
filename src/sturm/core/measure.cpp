// measure.cpp — M5: Measurement op per mode.
//
// Implements sturm::measure_qubit (C++ helper) and the C ABI sturm_measure.
//
// COUNT_ONLY / APPEND:
//   Return ctx.classical_values[qubit] directly.  No statevector access.
//   This is the deterministic placeholder behavior for non-SIMULATE modes.
//
// SIMULATE:
//   1. Compute P(outcome=0) by summing |amplitude[i]|^2 over all basis states i
//      where bit `qubit` is 0.
//   2. Draw a uniform random number r in [0,1).
//   3. outcome = (r >= P0) ? 1 : 0.
//   4. Collapse the statevector:
//        - Zero out all amplitudes inconsistent with the outcome.
//        - Renormalise so the remaining amplitudes sum to probability 1.
//   5. Update ctx.classical_values[qubit] = outcome.
//   6. Return outcome.
//
// LOC budget: <120 (implementation plan).

#include "sturm/core/measure.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>   // rand, RAND_MAX

// Forward: standard C RNG used for sampling.  A cryptographically-strong RNG
// is not required here; the tests only assert statistical properties.

namespace sturm {

// ── Internal: sample from Orkan statevector ───────────────────────────────────

static int simulate_measure(uint32_t qubit, BackendContext& ctx) {
    assert(ctx.orkan_state_ptr && "simulate_measure: no Orkan bridge attached");

    auto* bridge = static_cast<OrkanBridge*>(ctx.orkan_state_ptr);
    orkan::state_t& sv = bridge->state();

    const uint32_t n = sv.n_qubits;
    assert(qubit < n && "simulate_measure: qubit index out of range");

    const uint64_t dim       = uint64_t{1} << n;
    const uint64_t qubit_bit = uint64_t{1} << qubit;

    // ── Step 1: Compute P(qubit = 0) ─────────────────────────────────────────
    double p0 = 0.0;
    for (uint64_t i = 0; i < dim; ++i) {
        if ((i & qubit_bit) == 0u) {
            const auto& a = sv.amplitudes[i];
            p0 += a.real() * a.real() + a.imag() * a.imag();
        }
    }

    // ── Step 2: Sample ────────────────────────────────────────────────────────
    // Use rand() seeded by the calling code (or default C seed).
    // Divide by (RAND_MAX + 1.0) to get r in [0, 1).
    double r = static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0);
    int outcome = (r >= p0) ? 1 : 0;

    // ── Step 3: Collapse statevector ──────────────────────────────────────────
    double norm_sq = 0.0;
    for (uint64_t i = 0; i < dim; ++i) {
        bool bit_matches = ((i & qubit_bit) != 0u) == (outcome == 1);
        if (!bit_matches) {
            sv.amplitudes[i] = {0.0, 0.0};
        } else {
            const auto& a = sv.amplitudes[i];
            norm_sq += a.real() * a.real() + a.imag() * a.imag();
        }
    }

    // ── Step 4: Renormalise ───────────────────────────────────────────────────
    if (norm_sq > 0.0) {
        double inv_norm = 1.0 / std::sqrt(norm_sq);
        for (uint64_t i = 0; i < dim; ++i) {
            sv.amplitudes[i] *= inv_norm;
        }
    }

    // ── Step 5: Update classical value ────────────────────────────────────────
    if (qubit < BackendContext::kMaxClassicalQubits) {
        ctx.classical_values[qubit] = outcome;
    }

    // ── Step 6: Clear super_mask and promotion_mask for measured qubit ────────
    // After collapse the qubit is in a definite computational basis state, so
    // it is no longer superposed and any pending promotion is resolved.
    if (qubit < 32u) {
        ctx.super_mask     &= ~(uint32_t{1} << qubit);
        ctx.promotion_mask &= ~(uint32_t{1} << qubit);
    }

    return outcome;
}

// ── measure_qubit — C++ public entry point ────────────────────────────────────

int measure_qubit(uint32_t qubit, BackendContext& ctx) {
    switch (ctx.mode) {

    case STURM_MODE_COUNT_ONLY:
    case STURM_MODE_APPEND:
        // Deterministic placeholder: return the stored classical value.
        if (qubit < BackendContext::kMaxClassicalQubits) {
            return ctx.classical_values[qubit];
        }
        return 0;

    case STURM_MODE_SIMULATE:
        return simulate_measure(qubit, ctx);

    default:
        assert(false && "measure_qubit: unknown execution mode");
        return 0;
    }
}

} // namespace sturm

// ── C ABI entry point ─────────────────────────────────────────────────────────
//
// Replaces the stub in context.cpp.  Fetches the thread-local context and
// delegates to the C++ measure_qubit helper.

extern "C"
int sturm_measure(uint32_t qubit) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    if (!ctx) return 0;
    return sturm::measure_qubit(qubit, *ctx);
}
