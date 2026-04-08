// exec_count.hpp — M7: COUNT_ONLY executor.
//
// The COUNT_ONLY executor is intentionally a no-op.  Gate counting is
// handled unconditionally by the Layer B dispatcher (M13) before it
// selects an executor, so this function has nothing further to do.
//
// Declaring it as a named function (rather than inlining the empty body
// directly in the dispatcher) keeps the mode-switch symmetric: every
// execution mode has a named exec_* entry point.
//
// PRD §5: "COUNT_ONLY — increments gate counter; no other side-effects."
//
// Parameters (mirrored from exec_simulate and exec_append for symmetry):
//   ctx   — the active BackendContext; not modified here.
//   kind  — gate kind; ignored.
//   qubits — physical qubit index array; ignored.
//   n      — arity; ignored.
//   param  — rotation angle; ignored.
//
// This function never allocates, never throws, and never touches any
// state.  It is marked [[maybe_unused]] to suppress warnings when the
// compiler sees the empty body and deduces all parameters are unused.

#pragma once

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>

namespace sturm {

[[maybe_unused]]
inline void exec_count(BackendContext& /*ctx*/,
                       sturm_gate_kind_t /*kind*/,
                       const uint32_t*   /*qubits*/,
                       uint8_t           /*n*/,
                       double            /*param*/) noexcept {
    // Intentional no-op.
    // Gate counter increment is the dispatcher's responsibility (M13).
}

} // namespace sturm
