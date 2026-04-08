// ir.hpp — M6: IR buffer + GateRecord.
//
// Provides:
//   struct GateRecord   — one gate invocation (kind, qubit indices, arity, param)
//   class  GateIR       — growable ordered buffer of GateRecords
//
// The IR buffer is used exclusively in APPEND mode.  The BackendContext holds
// a GateIR instance (M3 TODO replaced here).  Layer B's exec_append pushes
// records; the frontend retrieves them for serialisation or later execution.
//
// Design notes:
//   - GateRecord uses a fixed-size array<uint32_t,3> for qubit indices.
//     Gates with arity < 3 leave trailing slots as 0.
//   - GateIR::at() performs bounds checking and throws std::out_of_range on
//     invalid indices (consistent with std::vector::at semantics).
//   - GateIR::reserve() exposes the underlying vector's capacity management so
//     callers can prevent reallocation (and thus pointer invalidation) when the
//     number of gates is known in advance.
//
// LOC budget: <150 (this file is well under).

#pragma once

#include "sturm/core/gate_kind.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace sturm {

// ── GateRecord ────────────────────────────────────────────────────────────────

struct GateRecord {
    sturm_gate_kind_t        kind;    ///< Which gate
    std::array<uint32_t, 3>  qubits;  ///< Physical qubit indices; slots >= n are 0
    uint8_t                  n;       ///< Arity (number of qubit operands, 1–3)
    double                   param;   ///< Rotation angle (radians); 0.0 for non-param gates
};

// ── GateIR ────────────────────────────────────────────────────────────────────

class GateIR {
public:
    // Append a copy of rec to the end of the buffer.
    void append(const GateRecord& rec) {
        records_.push_back(rec);
    }

    // Return the number of records currently in the buffer.
    [[nodiscard]] size_t size() const noexcept {
        return records_.size();
    }

    // Return a const reference to the record at index i.
    // Throws std::out_of_range if i >= size().
    [[nodiscard]] const GateRecord& at(size_t i) const {
        return records_.at(i);  // std::vector::at already throws out_of_range
    }

    // Remove all records; does not release allocated memory.
    void clear() noexcept {
        records_.clear();
    }

    // Pre-allocate storage for at least n records.
    // After this call, appending up to n records will not reallocate, keeping
    // pointers and references to existing elements stable.
    void reserve(size_t n) {
        records_.reserve(n);
    }

private:
    std::vector<GateRecord> records_;
};

} // namespace sturm
