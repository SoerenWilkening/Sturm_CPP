// measure.hpp — M5: Measurement op per mode.
//
// Declares the C++ measure_qubit helper and exposes the C ABI sturm_measure
// function.  Per PRD §5 and implementation plan M5:
//
//   COUNT_ONLY / APPEND:
//     Returns the stored classical value from ctx.classical_values[qubit].
//     Does not touch the statevector.
//
//   SIMULATE:
//     Computes the probability of outcome 0 for the given qubit by marginalising
//     the Orkan statevector over all other qubits.  Draws a uniform random
//     number and collapses the state.  Updates ctx.classical_values[qubit] with
//     the sampled outcome.
//
// The C ABI entry point (sturm_measure) is declared in core.h and implemented
// in measure.cpp; it fetches the thread-local context and calls measure_qubit.
//
// LOC budget: <120 (implementation plan).

#pragma once

#include "sturm/core/core.h"    // sturm_mode_t, sturm_measure declaration
#include "sturm/core/context.hpp"

#include <cstdint>

namespace sturm {

// ── C++ helper ────────────────────────────────────────────────────────────────
//
// Measure physical qubit `qubit` according to the mode set in `ctx`.
//
// Parameters:
//   qubit — physical qubit index.  Any non-negative index is accepted; the
//           context's classical_values vector grows on demand to record the
//           outcome (sturm-t2sk).
//   ctx   — the active backend context.
//
// Returns 0 or 1 (the measurement outcome).
//
// Thread-safe only if distinct threads use distinct contexts.

int measure_qubit(uint32_t qubit, BackendContext& ctx);

} // namespace sturm
