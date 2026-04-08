// uncompute_run.hpp — M20: RAII uncompute runner.
//
// Entry point: sturm::run_uncompute(op, reg, ctx)
//
// Called from qint_t<W>::~qint_t and qbool::~qbool (M21+).
// Execution order (PRD §9):
//   1. op.apply(ctx, reg)  — emit the semantic inverse (skipped if tag == NONE).
//   2. For each bit i set in reg.promotion_mask, emit X via execute_gate.
//   3. Release qubits that are in superposition (reg.super_mask bits) back to
//      the pool.
//
// The release step is a stub: it calls release_qubits(reg, ctx.pool) which
// iterates reg.super_mask and calls pool.release() per set bit.
//
// TODO(backend): when the full QubitPool / BackendContext wiring lands (M18),
// replace the per-context pool stub call with the authoritative allocator.
//
// LOC budget: < 150 (this header + uncompute_run.cpp combined).

#pragma once

#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cstdint>

namespace sturm {

// ── release_qubits ────────────────────────────────────────────────────────────
//
// Releases every qubit index stored in reg.qubits[i] for which bit i is set in
// reg.super_mask back to pool.  Called after all inverse gates have been emitted
// so the pool always receives |0⟩-state qubits.
//
// TODO(backend): tie to M18's authoritative per-context pool once it enforces
// the 17-qubit cap.  The pool.release() here is the same call M18 will use, so
// no interface change is expected.

inline void release_qubits(const qint_base& reg, QubitPool& pool) noexcept {
    for (uint8_t i = 0; i < reg.width && i < QINT_BASE_MAX_WIDTH; ++i) {
        if ((reg.super_mask >> i) & 1u) {
            // TODO(backend): assert qubit state == |0⟩ in SIMULATE mode (M18).
            pool.release(static_cast<int>(reg.qubits[i]));
        }
    }
}

// ── run_uncompute ─────────────────────────────────────────────────────────────
//
// The three-step RAII protocol invoked from qint/qbool destructors:
//
//   Step 1 — Apply inverse op (skip if NONE).
//   Step 2 — Emit X for each promotion_mask bit (resets |1⟩→|0⟩ before release).
//   Step 3 — Release qubits (stub; TODO see above).

inline void run_uncompute(const uncompute_op& op,
                          qint_base&          reg,
                          BackendContext&     ctx) noexcept {
    // Step 1: semantic inverse (no-op when tag == NONE).
    op.apply(ctx, reg);

    // Step 2: emit X gate for every bit that was |1⟩ at promotion time.
    for (uint8_t i = 0; i < reg.width && i < QINT_BASE_MAX_WIDTH; ++i) {
        if ((reg.promotion_mask >> i) & 1u) {
            uint32_t q[1] = {reg.qubits[i]};
            execute_gate(ctx, STURM_GATE_X, q, 1u, 0.0);
        }
    }

    // Step 3: release qubits back to the pool.
    // TODO(backend): use the authoritative per-context pool (M18).
    release_qubits(reg, ctx.pool);
}

} // namespace sturm
