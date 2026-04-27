// uncompute_api.cpp — Implementation of `sturm::uncompute_or` (M3 transpiler-MVP).
//
// The gate sequence is the self-adjoint of the forward OR circuit that
// `operator|` emits in include/sturm/qtypes/qbool_ops.hpp (quantum-quantum
// path: 2x CX + CCX onto the result qubit).  Mixed and classical cases
// reuse the four-quadrant rules used by `materialize_or` in
// include/sturm/detail/qtypes/bit_proxy.hpp and the qbool-level `operator|` in
// include/sturm/qtypes/qbool_ops.hpp.
//
// This file does NOT allocate or release qubits; the caller retains
// ownership.  All gate emission goes through `execute_gate` so the active
// sink (APPEND / SIMULATE / COUNT) records the inverse sequence.
//
// Adjoint construction rule: for every forward decomposition we emit the
// gates in REVERSE order, each replaced by its inverse.  Since X, CX and CCX
// are each self-inverse this amounts to replaying the forward list in
// reverse.  We never rely on net-effect shortcuts so that the adjoint is
// valid even if intermediate amplitude branches briefly observe `r`.

#include "sturm/uncompute/uncompute_api.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdint>

namespace sturm {

// ── quantumness helper ────────────────────────────────────────────────────────
// A qbool is treated as "quantum" when it holds a valid qubit index.  Callers
// that built the qbool via classical constructors carry qubits[0] == -1; the
// superposed / materialized paths always allocate an index first.
static inline bool is_quantum(const qbool& q) noexcept {
    return q.qubits[0] >= 0;
}

// ── uncompute_or ──────────────────────────────────────────────────────────────
// See header for the four-quadrant contract.  Uses the thread-local
// BackendContext resolved via sturm_get_thread_context().
//
// Forward decompositions (from bit_proxy.hpp `materialize_or`):
//   (a_q, b_q)   : CX(a,r) + CX(b,r) + CCX(a,b,r)
//   (a_q, b=0)   : CX(a,r)
//   (a_q, b=1)   : CX(a,r) + X(r) + CX(a,r)
//   (a=0, b_q)   : CX(b,r)
//   (a=1, b_q)   : X(r)   + CX(b,r) + CX(b,r)
//   (classical)  : 0..3 X(r) gates depending on literals
//
// The adjoint is the reversed list with each gate inverted; X, CX and CCX
// are each self-inverse.
void uncompute_or(qbool& r, const qbool& a, const qbool& b) {
    const bool a_q = is_quantum(a);
    const bool b_q = is_quantum(b);
    const bool r_q = is_quantum(r);

    // All-classical case: uncompute_or is only invoked when the forward
    // path produced an ancilla (r quantum).  Pure-classical OR (both a and
    // b classical with no ancilla) never allocates `r`, so we have nothing
    // to undo in that trivial subcase.  The reviewer's contract however
    // still requires that the ancilla-case classical variants emit the
    // exact reversed adjoint — handled below once we know r has a qubit.
    if (!a_q && !b_q && !r_q) {
        return;
    }

    // Inverse requires an ancilla qubit on r.  If r has no qubit, the
    // forward operation never allocated one (pure-classical), which is
    // handled above.  A missing r qubit with quantum inputs would indicate
    // the caller paired uncompute_or with a non-materialized result — not
    // supported by this API.
    assert(r_q && "uncompute_or: r must hold an ancilla qubit");
    if (!r_q) {
        return;
    }

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "uncompute_or: no BackendContext installed");
    BackendContext& ctx = *raw;

    const uint32_t qr = static_cast<uint32_t>(r.qubits[0]);

    // Emitter helpers (local) — keep call sites readable.
    auto emit_x = [&](uint32_t q) {
        uint32_t args[1] = {q};
        execute_gate(ctx, STURM_GATE_X, args, 1u, 0.0);
    };
    auto emit_cx = [&](uint32_t ctrl, uint32_t tgt) {
        uint32_t args[2] = {ctrl, tgt};
        execute_gate(ctx, STURM_GATE_CX, args, 2u, 0.0);
    };
    auto emit_ccx = [&](uint32_t c0, uint32_t c1, uint32_t tgt) {
        uint32_t args[3] = {c0, c1, tgt};
        execute_gate(ctx, STURM_GATE_CCX, args, 3u, 0.0);
    };

    // ── Both quantum ───────────────────────────────────────────────────
    // Forward: CX(a,r) + CX(b,r) + CCX(a,b,r)
    // Adjoint: CCX(a,b,r) + CX(b,r) + CX(a,r)
    if (a_q && b_q) {
        const uint32_t qa = static_cast<uint32_t>(a.qubits[0]);
        const uint32_t qb = static_cast<uint32_t>(b.qubits[0]);
        emit_ccx(qa, qb, qr);
        emit_cx(qb, qr);
        emit_cx(qa, qr);
        return;
    }

    // ── Mixed: a quantum, b classical ──────────────────────────────────
    if (a_q) {
        const uint32_t qa = static_cast<uint32_t>(a.qubits[0]);
        if (b.value & 1) {
            // Forward (a_q, b=1): CX(a,r) + X(r) + CX(a,r)
            // Adjoint (exact reverse, each gate inverted/self-inverse):
            //          CX(a,r) + X(r) + CX(a,r)    (3 gates)
            emit_cx(qa, qr);
            emit_x(qr);
            emit_cx(qa, qr);
        } else {
            // Forward (a_q, b=0): CX(a,r)
            // Adjoint: CX(a,r)
            emit_cx(qa, qr);
        }
        return;
    }

    // ── Mixed: a classical, b quantum ──────────────────────────────────
    if (b_q) {
        const uint32_t qb = static_cast<uint32_t>(b.qubits[0]);
        if (a.value & 1) {
            // Forward (a=1, b_q): X(r) + CX(b,r) + CX(b,r)
            // Adjoint (exact reverse, each gate inverted/self-inverse):
            //          CX(b,r) + CX(b,r) + X(r)   (3 gates)
            emit_cx(qb, qr);
            emit_cx(qb, qr);
            emit_x(qr);
        } else {
            // Forward (a=0, b_q): CX(b,r)
            // Adjoint: CX(b,r)
            emit_cx(qb, qr);
        }
        return;
    }

    // ── Both classical, r quantum (materialize_or classical branch) ────
    // Forward gate count equals the parity of literal X emissions on r:
    //   (0, 0): nothing
    //   (0, 1): X(r)                            -> adjoint X(r)
    //   (1, 0): X(r)                            -> adjoint X(r)
    //   (1, 1): X(r) + X(r) + X(r)              -> adjoint X(r)+X(r)+X(r)
    // Emit the exact reversed list (X is self-inverse).
    const bool av = (a.value & 1) != 0;
    const bool bv = (b.value & 1) != 0;
    if (av && bv) {
        // Three X gates; reversed list is three X gates (identical).
        emit_x(qr);
        emit_x(qr);
        emit_x(qr);
    } else if (av || bv) {
        emit_x(qr);
    }
    // (0, 0): nothing to do.
}

// ── uncompute_and ─────────────────────────────────────────────────────────────
// See header for the full quadrant contract.  Uses the thread-local
// BackendContext resolved via sturm_get_thread_context().
//
// Forward decompositions (from bit_proxy.hpp `materialize_and`):
//   (a_q, b_q)   : CCX(a, b, r)
//   (a_q, b=0)   : (none)
//   (a_q, b=1)   : CX(a, r)
//   (a=0, b_q)   : (none)
//   (a=1, b_q)   : CX(b, r)
//   (classical)  : X(r) when a=b=1, otherwise (none)
//
// The adjoint is the reversed list with each gate inverted; every gate
// used by the forward path (X, CX, CCX) is self-inverse, so the
// adjoint is the same single gate as the forward emission — except no
// gate fires when the forward path was empty.
void uncompute_and(qbool& r, const qbool& a, const qbool& b) {
    const bool a_q = is_quantum(a);
    const bool b_q = is_quantum(b);
    const bool r_q = is_quantum(r);

    // Pure-classical (no ancilla) case: the forward AND never allocated
    // `r`, so there is nothing to undo.  Mirrors the uncompute_or short
    // circuit.
    if (!a_q && !b_q && !r_q) {
        return;
    }

    // Inverse requires an ancilla qubit on r.  Missing r qubit with
    // quantum inputs would mean the caller paired uncompute_and with a
    // non-materialized result — not supported by this API.
    assert(r_q && "uncompute_and: r must hold an ancilla qubit");
    if (!r_q) {
        return;
    }

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "uncompute_and: no BackendContext installed");
    BackendContext& ctx = *raw;

    const uint32_t qr = static_cast<uint32_t>(r.qubits[0]);

    auto emit_x = [&](uint32_t q) {
        uint32_t args[1] = {q};
        execute_gate(ctx, STURM_GATE_X, args, 1u, 0.0);
    };
    auto emit_cx = [&](uint32_t ctrl, uint32_t tgt) {
        uint32_t args[2] = {ctrl, tgt};
        execute_gate(ctx, STURM_GATE_CX, args, 2u, 0.0);
    };
    auto emit_ccx = [&](uint32_t c0, uint32_t c1, uint32_t tgt) {
        uint32_t args[3] = {c0, c1, tgt};
        execute_gate(ctx, STURM_GATE_CCX, args, 3u, 0.0);
    };

    // ── Both quantum ───────────────────────────────────────────────────
    // Forward: CCX(a, b, r).  Adjoint: CCX(a, b, r) — self-inverse.
    if (a_q && b_q) {
        const uint32_t qa = static_cast<uint32_t>(a.qubits[0]);
        const uint32_t qb = static_cast<uint32_t>(b.qubits[0]);
        emit_ccx(qa, qb, qr);
        return;
    }

    // ── Mixed: a quantum, b classical ──────────────────────────────────
    if (a_q) {
        if (b.value & 1) {
            // Forward (a_q, b=1): CX(a, r).  Adjoint: CX(a, r).
            const uint32_t qa = static_cast<uint32_t>(a.qubits[0]);
            emit_cx(qa, qr);
        }
        // (a_q, b=0): forward emits nothing; adjoint emits nothing.
        return;
    }

    // ── Mixed: a classical, b quantum ──────────────────────────────────
    if (b_q) {
        if (a.value & 1) {
            // Forward (a=1, b_q): CX(b, r).  Adjoint: CX(b, r).
            const uint32_t qb = static_cast<uint32_t>(b.qubits[0]);
            emit_cx(qb, qr);
        }
        // (a=0, b_q): forward emits nothing; adjoint emits nothing.
        return;
    }

    // ── Both classical, r quantum (materialize_and classical branch) ───
    // Forward only emits when a=b=1: single X(r); otherwise nothing.
    // Adjoint reverses that single-gate list (X self-inverse).
    const bool av = (a.value & 1) != 0;
    const bool bv = (b.value & 1) != 0;
    if (av && bv) {
        emit_x(qr);
    }
    // (0,0), (0,1), (1,0): nothing to do.
}

// ── ccnot_inplace ────────────────────────────────────────────────────────────
// Phase J PJ-1b zero-ancilla fusion helper (sturm-8cxd).  See the header
// for the full contract.  Delegates to primitive_AND (CCX-to-target) so
// the emitted gate stream is exactly one STURM_GATE_CCX record with
// qubit arguments (a, b, x) — matching the fusion pattern
// `qbool __t = a & b; x ^= __t;` collapsed into a single CCX with no
// intermediate ancilla.
//
// Self-adjoint: CCX is its own inverse, so invoking this helper a
// second time on the live state cancels the first call exactly.  The
// PJ-1c render case in uncompute_pass.cpp emits the same `ccnot_inplace`
// symbol at the uncompute point — one matcher, one symbol, forward +
// inverse identical textually.
void ccnot_inplace(qbool& x, const qbool& a, const qbool& b) {
    assert(a.qubits[0] >= 0 && "ccnot_inplace: a must hold a qubit");
    assert(b.qubits[0] >= 0 && "ccnot_inplace: b must hold a qubit");
    assert(x.qubits[0] >= 0 && "ccnot_inplace: x must hold a qubit");

    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "ccnot_inplace: no BackendContext installed");

    primitive_AND(*raw,
                   static_cast<uint32_t>(a.qubits[0]),
                   static_cast<uint32_t>(b.qubits[0]),
                   static_cast<uint32_t>(x.qubits[0]));
}

} // namespace sturm
