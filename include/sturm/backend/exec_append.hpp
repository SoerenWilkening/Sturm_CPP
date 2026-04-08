// exec_append.hpp — M8: APPEND executor.
//
// Provides:
//   sturm::exec_append(BackendContext&, gate_kind, qubits, n, param)
//
// In APPEND mode the Layer B dispatcher calls this function instead of
// exec_count or exec_simulate.  It constructs a GateRecord from the
// supplied operands and pushes it onto ctx.ir (the GateIR buffer owned
// by the BackendContext).
//
// Responsibilities of THIS function:
//   - Build the GateRecord, zeroing any qubit slots beyond the gate's arity n.
//   - Append the record to ctx.ir.
//
// NOT this function's responsibility:
//   - Incrementing ctx.gate_count  (that is the dispatcher's job, M13).
//   - Validating that n matches the PRD arity for kind (enforced by Layer A).
//
// LOC budget: <80 (this file is well under).

#pragma once

#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>
#include <cstring>   // std::memset

namespace sturm {

inline void exec_append(BackendContext&   ctx,
                        sturm_gate_kind_t kind,
                        const uint32_t*   qubits,
                        uint8_t           n,
                        double            param) noexcept {
    GateRecord rec{};
    rec.kind  = kind;
    rec.n     = n;
    rec.param = param;

    // Copy up to n qubit indices; slots [n, 3) remain zero-initialised
    // (GateRecord{} zero-inits the array via value-init of the struct).
    for (uint8_t i = 0; i < n && i < 3u; ++i) {
        rec.qubits[i] = qubits[i];
    }

    ctx.ir.append(rec);
}

} // namespace sturm
