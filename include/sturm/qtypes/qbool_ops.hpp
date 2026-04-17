// qbool_ops.hpp — Phase K PK-2: qbool operators returning owning qbool directly.
// After PK-2 the lazy `AndExpr<qbool>` / `OrExpr<qbool>` wrappers are gone; the
// zero-ancilla optimization for `qbool __t = a & b; x ^= __t;` lives in the
// transpiler IR pass (Phase J PJ-1, see docs/roadmap_transpiler_post_mvp.md
// and include/sturm/uncompute/uncompute_api.hpp sturm::ccnot_inplace).
//
// operator& / operator| / operator^ / operator~ each acquire a fresh qubit
// from `QubitPool::instance()`, emit the forward gate(s) via the active
// BackendContext, and return an owning qbool by value.  Non-transpiled callers
// therefore leak one qubit per operation (no RAII uncompute under the default
// STURM_AUTO_UNCOMPUTE=OFF); the transpile path is the contract — the
// transpiler injects explicit `uncompute_and` / `uncompute_or` calls at scope
// exit (see include/sturm/uncompute/uncompute_api.hpp).
//
// WHEN lifting: 0 controls→direct, 1→lift×1, 2+→c_AND fold (emit_*_lifted).
// Target: <300 LoC.
#pragma once
#include "sturm/qtypes/qbool.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include <cassert>
#include <cstdint>

namespace sturm {

// ── get_ctx ───────────────────────────────────────────────────────────────────
inline BackendContext& get_ctx() {
    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "qbool ops: no BackendContext installed");
    return *raw;
}

// ── emit_X_lifted ─────────────────────────────────────────────────────────────
// 0 controls→X, 1→CX, 2→CCX, 3+→c_AND fold with borrowed ancilla.
inline void emit_X_lifted(BackendContext& ctx, uint32_t target) {
    const auto     ctrls = ctx.control_stack.controls();
    const uint32_t depth = static_cast<uint32_t>(ctrls.size());
    if (depth == 0u) {
        primitive_X(ctx, target);
    } else if (depth == 1u) {
        primitive_XOR(ctx, ctrls[0], target);
    } else if (depth == 2u) {
        primitive_AND(ctx, ctrls[0], ctrls[1], target);
    } else {
        // 3+ controls: Nielsen-Chuang sandwich with borrowed ancilla.
        int anc_idx = QubitPool::instance().allocate();
        const auto anc = static_cast<uint32_t>(anc_idx);
        primitive_AND(ctx, ctrls[0], ctrls[1], anc);   // compute
        if (depth == 3u) {
            primitive_AND(ctx, anc, ctrls[2], target);
        } else {
            // depth 4+: one more ancilla level.
            int anc2_idx = QubitPool::instance().allocate();
            const auto anc2 = static_cast<uint32_t>(anc2_idx);
            primitive_AND(ctx, anc, ctrls[2], anc2);
            primitive_AND(ctx, anc2, ctrls[3], target); // TODO(backend): depth>4
            primitive_AND(ctx, anc, ctrls[2], anc2);    // uncompute anc2
            QubitPool::instance().release(anc2_idx);
        }
        primitive_AND(ctx, ctrls[0], ctrls[1], anc);   // uncompute
        QubitPool::instance().release(anc_idx);
    }
}

// ── emit_CX_lifted ────────────────────────────────────────────────────────────
// 0→CX, 1→CCX, 2+→c_AND fold.
inline void emit_CX_lifted(BackendContext& ctx, uint32_t ctrl, uint32_t target) {
    const auto     ctrls = ctx.control_stack.controls();
    const uint32_t depth = static_cast<uint32_t>(ctrls.size());
    if (depth == 0u) {
        primitive_XOR(ctx, ctrl, target);
    } else if (depth == 1u) {
        primitive_AND(ctx, ctrls[0], ctrl, target);
    } else {
        int anc_idx = QubitPool::instance().allocate();
        const auto anc = static_cast<uint32_t>(anc_idx);
        primitive_AND(ctx, ctrls[0], ctrl, anc);        // compute
        if (depth == 2u) {
            primitive_AND(ctx, anc, ctrls[1], target);
        } else {
            int anc2_idx = QubitPool::instance().allocate();
            const auto anc2 = static_cast<uint32_t>(anc2_idx);
            primitive_AND(ctx, anc, ctrls[1], anc2);
            primitive_AND(ctx, anc2, ctrls[2], target); // TODO(backend): depth>3
            primitive_AND(ctx, anc, ctrls[1], anc2);    // uncompute anc2
            QubitPool::instance().release(anc2_idx);
        }
        primitive_AND(ctx, ctrls[0], ctrl, anc);        // uncompute
        QubitPool::instance().release(anc_idx);
    }
}

// ── emit_CCX_lifted ───────────────────────────────────────────────────────────
// 0→CCX, 1+→fold c0&c1 into ancilla then emit_X_lifted.
inline void emit_CCX_lifted(BackendContext& ctx,
                             uint32_t c0, uint32_t c1, uint32_t target) {
    if (ctx.control_stack.depth() == 0u) {
        primitive_AND(ctx, c0, c1, target);
    } else {
        int anc_idx = QubitPool::instance().allocate();
        const auto anc = static_cast<uint32_t>(anc_idx);
        primitive_AND(ctx, c0, c1, anc);                // compute c0&c1
        ctx.control_stack.push_control(anc);
        emit_X_lifted(ctx, target);                     // lift under all controls+anc
        ctx.control_stack.pop_control();
        primitive_AND(ctx, c0, c1, anc);                // uncompute
        QubitPool::instance().release(anc_idx);
    }
}

// ── qbool::operator^=(const qbool&) ──────────────────────────────────────────
inline qbool& qbool::operator^=(const qbool& other) {
    assert(qubits[0] >= 0 && other.qubits[0] >= 0);
    emit_CX_lifted(get_ctx(),
                   static_cast<uint32_t>(other.qubits[0]),
                   static_cast<uint32_t>(qubits[0]));
    return *this;
}

// ── qbool::flip() ────────────────────────────────────────────────────────────
inline qbool& qbool::flip() {
    assert(qubits[0] >= 0);
    emit_X_lifted(get_ctx(), static_cast<uint32_t>(qubits[0]));
    return *this;
}

// ── qbool::operator~() ───────────────────────────────────────────────────────
// Phase K PK-2: allocate a fresh qubit via QubitPool::instance().acquire(),
// emit an X gate into it, and return an owning qbool.  Non-transpiled callers
// leak the qubit (STURM_AUTO_UNCOMPUTE=OFF default); transpile injects the
// uncompute.
inline qbool qbool::operator~() const {
    assert(qubits[0] >= 0);
    int anc_idx = QubitPool::instance().acquire();
    emit_X_lifted(get_ctx(), static_cast<uint32_t>(anc_idx));
    qbool result;
    result.qubits[0]  = anc_idx;
    result.owning_    = true;
    result.super_mask = 1ULL;
    return result;
}

// ── operator& (free) ──────────────────────────────────────────────────────────
// Phase K PK-2: returns owning qbool directly (no lazy AndExpr wrapper).
// Quantum-quantum: allocate ancilla, emit CCX(a, b, anc).
// Mixed / classical cases: classical-fold without allocating a qubit when
// the result is trivially classical (false & x = false; true & x = x).
inline qbool operator&(const qbool& a, const qbool& b) {
    const bool a_q = (a.qubits[0] >= 0);
    const bool b_q = (b.qubits[0] >= 0);

    // Both purely classical (no qubits): compute eagerly.
    if (!a_q && !b_q) {
        return qbool(static_cast<bool>((a.value & b.value) & 1));
    }

    // Both have qubits: allocate ancilla and emit AND circuit.
    if (a_q && b_q) {
        int anc_idx = QubitPool::instance().acquire();
        const auto anc = static_cast<uint32_t>(anc_idx);
        const auto qa  = static_cast<uint32_t>(a.qubits[0]);
        const auto qb  = static_cast<uint32_t>(b.qubits[0]);
        primitive_AND(get_ctx(), qa, qb, anc);
        qbool result;
        result.qubits[0]  = anc_idx;
        result.owning_    = true;
        result.super_mask = 1ULL;
        return result;
    }

    // Mixed: one classical (no qubit), one quantum.
    // false & x = false; true & x = x.
    if (!a_q) {
        return (a.value & 1)
            ? qbool::make_non_owning(b.qubits[0], b.value, b.super_mask)
            : qbool(false);
    }
    // !b_q
    return (b.value & 1)
        ? qbool::make_non_owning(a.qubits[0], a.value, a.super_mask)
        : qbool(false);
}

// ── operator| (free) ──────────────────────────────────────────────────────────
// Phase K PK-2: returns owning qbool directly (no lazy OrExpr wrapper).
// Quantum-quantum: allocate ancilla, emit CX(a,r) + CX(b,r) + CCX(a,b,r).
// Mixed / classical cases: classical-fold without allocating a qubit when
// the result is trivially classical (true | x = true; false | x = x).
inline qbool operator|(const qbool& a, const qbool& b) {
    const bool a_q = (a.qubits[0] >= 0);
    const bool b_q = (b.qubits[0] >= 0);

    // Both purely classical (no qubits): compute eagerly.
    if (!a_q && !b_q) {
        return qbool(static_cast<bool>((a.value | b.value) & 1));
    }

    // Both have qubits: allocate ancilla and emit OR circuit.
    if (a_q && b_q) {
        int anc_idx = QubitPool::instance().acquire();
        const auto anc = static_cast<uint32_t>(anc_idx);
        const auto qa  = static_cast<uint32_t>(a.qubits[0]);
        const auto qb  = static_cast<uint32_t>(b.qubits[0]);
        BackendContext& ctx = get_ctx();
        primitive_XOR(ctx, qa, anc);
        primitive_XOR(ctx, qb, anc);
        primitive_AND(ctx, qa, qb, anc);
        qbool result;
        result.qubits[0]  = anc_idx;
        result.owning_    = true;
        result.super_mask = 1ULL;
        return result;
    }

    // Mixed: one classical (no qubit), one quantum.
    // true | x = true; false | x = x.
    if (!a_q) {
        return (a.value & 1)
            ? qbool(true)
            : qbool::make_non_owning(b.qubits[0], b.value, b.super_mask);
    }
    // !b_q
    return (b.value & 1)
        ? qbool(true)
        : qbool::make_non_owning(a.qubits[0], a.value, a.super_mask);
}

// ── operator^ (free) ──────────────────────────────────────────────────────────
// Phase K PK-2: returns owning qbool directly (no lazy wrapper).
// Quantum-quantum: allocate ancilla, emit CX(a,r) + CX(b,r).
// Mixed / classical: classical-fold or share the quantum operand.
inline qbool operator^(const qbool& a, const qbool& b) {
    const bool a_q = (a.qubits[0] >= 0);
    const bool b_q = (b.qubits[0] >= 0);

    // Both purely classical (no qubits): compute eagerly.
    if (!a_q && !b_q) {
        return qbool(static_cast<bool>((a.value ^ b.value) & 1));
    }

    // Both have qubits: allocate ancilla and emit XOR circuit.
    if (a_q && b_q) {
        int anc_idx = QubitPool::instance().acquire();
        const auto anc = static_cast<uint32_t>(anc_idx);
        const auto qa  = static_cast<uint32_t>(a.qubits[0]);
        const auto qb  = static_cast<uint32_t>(b.qubits[0]);
        BackendContext& ctx = get_ctx();
        primitive_XOR(ctx, qa, anc);
        primitive_XOR(ctx, qb, anc);
        qbool result;
        result.qubits[0]  = anc_idx;
        result.owning_    = true;
        result.super_mask = 1ULL;
        return result;
    }

    // Mixed: one classical (no qubit), one quantum.  a^b with classical=1 is
    // ~quantum; classical=0 is the quantum operand itself.  For classical=1
    // allocate an ancilla and emit CX + X to avoid mutating the input.
    const qbool& q_ref = a_q ? a : b;
    const qbool& c_ref = a_q ? b : a;
    if (!(c_ref.value & 1)) {
        // classical=0: result mirrors the quantum operand (non-owning view).
        return qbool::make_non_owning(q_ref.qubits[0], q_ref.value, q_ref.super_mask);
    }
    // classical=1: allocate ancilla, copy the quantum operand in, flip.
    int anc_idx = QubitPool::instance().acquire();
    const auto anc = static_cast<uint32_t>(anc_idx);
    const auto qq  = static_cast<uint32_t>(q_ref.qubits[0]);
    BackendContext& ctx = get_ctx();
    primitive_XOR(ctx, qq, anc);
    primitive_X(ctx, anc);
    qbool result;
    result.qubits[0]  = anc_idx;
    result.owning_    = true;
    result.super_mask = 1ULL;
    return result;
}

} // namespace sturm
