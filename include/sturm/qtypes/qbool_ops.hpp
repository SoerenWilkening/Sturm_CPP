// qbool_ops.hpp — M13: qbool operator bodies + lazy expression materialization.
// WHEN lifting: 0 controls→direct, 1→lift×1, 2+→c_AND fold. Target: <200 LoC.
#pragma once
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
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

// ── qbool::operator^=(const AndExpr<qbool>&) ─────────────────────────────────
inline qbool& qbool::operator^=(const AndExpr<qbool>& expr) {
    assert(qubits[0] >= 0 && expr.a.qubits[0] >= 0 && expr.b.qubits[0] >= 0);
    emit_CCX_lifted(get_ctx(),
                    static_cast<uint32_t>(expr.a.qubits[0]),
                    static_cast<uint32_t>(expr.b.qubits[0]),
                    static_cast<uint32_t>(qubits[0]));
    return *this;
}

// ── qbool::operator^=(const OrExpr<qbool>&) ──────────────────────────────────
// c ^= (a | b)  =  CNOT(a,c) + CNOT(b,c) + CCX(a,b,c)
inline qbool& qbool::operator^=(const OrExpr<qbool>& expr) {
    assert(qubits[0] >= 0 && expr.a.qubits[0] >= 0 && expr.b.qubits[0] >= 0);
    BackendContext& ctx = get_ctx();
    const uint32_t a   = static_cast<uint32_t>(expr.a.qubits[0]);
    const uint32_t b   = static_cast<uint32_t>(expr.b.qubits[0]);
    const uint32_t tgt = static_cast<uint32_t>(qubits[0]);
    emit_CX_lifted(ctx, a, tgt);
    emit_CX_lifted(ctx, b, tgt);
    emit_CCX_lifted(ctx, a, b, tgt);
    return *this;
}

// ── qbool::flip() ────────────────────────────────────────────────────────────
inline qbool& qbool::flip() {
    assert(qubits[0] >= 0);
    emit_X_lifted(get_ctx(), static_cast<uint32_t>(qubits[0]));
    return *this;
}

// ── qbool::operator~() ───────────────────────────────────────────────────────
// Allocates ancilla, emits X. Uncompute: ADD_CONST(1) → sub_const(1) → X.
inline qbool qbool::operator~() const {
    assert(qubits[0] >= 0);
    int anc_idx = QubitPool::instance().allocate();
    emit_X_lifted(get_ctx(), static_cast<uint32_t>(anc_idx));
    qbool result;
    result.qubits[0]  = anc_idx;
    result.owning_    = true;
    result.super_mask = 1ULL;
#ifdef STURM_BACKEND_ENABLED
    result.uncompute_ = uncompute_op::make_add_const(1);
#endif
    return result;
}

// ── AndExpr<qbool>::operator qbool() ─────────────────────────────────────────
// Allocate ancilla, CCX(a,b,anc). Uncompute: BITWISE_SELF(AND, qa, qb).
// Handles classical operands (no qubit allocated): classical short-circuit
// avoids gate emission.  Both-quantum path is the original full circuit.
template<>
inline AndExpr<qbool>::operator qbool() const {
    const bool a_q = (a.qubits[0] >= 0);
    const bool b_q = (b.qubits[0] >= 0);

    // Both purely classical (no qubits): compute eagerly.
    if (!a_q && !b_q)
        return qbool(static_cast<bool>((a.value & b.value) & 1));

    // Both have qubits: allocate ancilla and emit AND circuit.
    if (a_q && b_q) {
        BackendContext& ctx = get_ctx();
        int anc_idx = QubitPool::instance().allocate();
        const uint32_t anc = static_cast<uint32_t>(anc_idx);
        const uint32_t qa  = static_cast<uint32_t>(a.qubits[0]);
        const uint32_t qb  = static_cast<uint32_t>(b.qubits[0]);
        primitive_AND(ctx, qa, qb, anc);
        qbool result;
        result.qubits[0]  = anc_idx;
        result.owning_    = true;
        result.super_mask = 1ULL;
#ifdef STURM_BACKEND_ENABLED
        result.uncompute_ = uncompute_op::make_bitwise_qbool(qa, qb, 0u); // 0=AND
#endif
        return result;
    }

    // Mixed: one classical (no qubit), one quantum.
    // false & x = false; true & x = x.
    if (!a_q)
        return (a.value & 1)
            ? qbool::make_non_owning(b.qubits[0], b.value, b.super_mask)
            : qbool(false);
    // !b_q
    return (b.value & 1)
        ? qbool::make_non_owning(a.qubits[0], a.value, a.super_mask)
        : qbool(false);
}

// ── OrExpr<qbool>::operator qbool() ──────────────────────────────────────────
// Allocate ancilla, CX+CX+CCX. Uncompute: BITWISE_SELF(OR, qa, qb).
// Handles classical operands (no qubit allocated): classical short-circuit
// avoids gate emission.  Both-quantum path is the original full circuit.
template<>
inline OrExpr<qbool>::operator qbool() const {
    const bool a_q = (a.qubits[0] >= 0);
    const bool b_q = (b.qubits[0] >= 0);

    // Both purely classical (no qubits): compute eagerly.
    if (!a_q && !b_q)
        return qbool(static_cast<bool>((a.value | b.value) & 1));

    // Both have qubits: allocate ancilla and emit OR circuit.
    if (a_q && b_q) {
        BackendContext& ctx = get_ctx();
        int anc_idx = QubitPool::instance().allocate();
        const uint32_t anc = static_cast<uint32_t>(anc_idx);
        const uint32_t qa  = static_cast<uint32_t>(a.qubits[0]);
        const uint32_t qb  = static_cast<uint32_t>(b.qubits[0]);
        primitive_XOR(ctx, qa, anc);
        primitive_XOR(ctx, qb, anc);
        primitive_AND(ctx, qa, qb, anc);
        qbool result;
        result.qubits[0]  = anc_idx;
        result.owning_    = true;
        result.super_mask = 1ULL;
#ifdef STURM_BACKEND_ENABLED
        result.uncompute_ = uncompute_op::make_bitwise_qbool(qa, qb, 1u); // 1=OR
#endif
        return result;
    }

    // Mixed: one classical (no qubit), one quantum.
    // true | x = true; false | x = x.
    if (!a_q)
        return (a.value & 1)
            ? qbool(true)
            : qbool::make_non_owning(b.qubits[0], b.value, b.super_mask);
    // !b_q
    return (b.value & 1)
        ? qbool(true)
        : qbool::make_non_owning(a.qubits[0], a.value, a.super_mask);
}

} // namespace sturm
