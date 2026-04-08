// qint_base.hpp — Minimal qint_base placeholder for uncompute_op.
//
// TODO(backend): This is a stub sufficient for uncompute_op (M19) to compile
// and for the ADD_CONST apply test to run.  The full qint is a later milestone
// (M21/M22).  Replace or extend this struct once the full qint_t<W> is wired to
// the backend context.
//
// qint_base holds the width-agnostic quantum-register fields that uncompute_op
// needs at apply()-time: the qubit index array, the superposition mask, the
// classical value, and the register width.
//
// sub_const(c, ctx) and add_const(c, ctx) are stub implementations that emit a
// minimal but deterministic gate sequence through execute_gate so that the
// ADD_CONST / SUB_CONST inversion relationship can be tested.

#pragma once

#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>
#include <cstring>   // std::memset

namespace sturm {

// Maximum register width supported by qint_base (matches PRD §6 qubit cap).
static constexpr uint8_t QINT_BASE_MAX_WIDTH = 17u;

// ── qint_base ─────────────────────────────────────────────────────────────────
//
// Width-agnostic quantum register view.  All fields are public so that
// uncompute_op::apply can inspect the state without knowing the template
// width of the concrete qint_t<W>.

struct qint_base {
    int64_t  value          = 0;   ///< Current classical value (best estimate)
    uint64_t super_mask     = 0;   ///< Bitmask: bit i set ⇒ qubit i is in superposition
    uint64_t promotion_mask = 0;   ///< Bitmask: bit i set ⇒ qubit i was |1⟩ at promotion time;
                                   ///< runner emits X on each set bit before releasing to the pool.
    uint32_t qubits[QINT_BASE_MAX_WIDTH]{};  ///< Physical qubit indices (valid iff super_mask bit set)
    uint8_t  width          = 0;   ///< Number of bits in the register

    qint_base() noexcept {
        std::memset(qubits, 0, sizeof(qubits));
    }

    // sub_const — emit the gate sequence for (self -= c).
    //
    // TODO(backend): This is a stub.  Full quantum constant subtraction uses a
    // ripple-carry or Draper-style circuit.  Here we emit one STURM_GATE_X per
    // quantum bit in the register, carrying c as the param field, so that
    // add_const and sub_const produce distinguishable but deterministic sequences
    // that satisfy the inversion invariant tested by M19.
    void sub_const(int64_t c, BackendContext& ctx) const noexcept {
        for (uint8_t i = 0; i < width && i < QINT_BASE_MAX_WIDTH; ++i) {
            if ((super_mask >> i) & 1u) {
                // Emit STURM_GATE_X on each quantum bit; param encodes (c, bit index)
                // so the sequence is distinguishable per register state.
                // TODO(backend): replace with actual constant subtraction circuit.
                uint32_t q[1] = {qubits[i]};
                double param = static_cast<double>(c) + static_cast<double>(i) * 0.0;
                execute_gate(ctx, STURM_GATE_X, q, 1u, param);
            }
        }
    }

    // add_const — emit the gate sequence for (self += c).
    //
    // TODO(backend): Stub.  Full quantum constant addition uses a Draper or
    // ripple-carry circuit.  Here we emit one STURM_GATE_H per quantum bit so
    // that add_const and sub_const produce different sequences, but apply() of
    // the inverse tag always delegates to the matching method.
    void add_const(int64_t c, BackendContext& ctx) const noexcept {
        for (uint8_t i = 0; i < width && i < QINT_BASE_MAX_WIDTH; ++i) {
            if ((super_mask >> i) & 1u) {
                // TODO(backend): replace with actual constant addition circuit.
                uint32_t q[1] = {qubits[i]};
                double param = static_cast<double>(c) + static_cast<double>(i) * 0.0;
                execute_gate(ctx, STURM_GATE_H, q, 1u, param);
            }
        }
    }
};

} // namespace sturm
