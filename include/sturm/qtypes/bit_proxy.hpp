// bit_proxy.hpp -- M1: BitProxy struct + operator bodies.
//
// Lightweight reference view into a single bit of a qint_t<W> register.
// Holds pointers to parent's qubit, super_mask, value, and bit_pos.
// Constructible from qint_t<W> or standalone qbool.
// Gate operators perform classical folding (skip/fold when operand classical).
// Guard: entire file is backend-only (STURM_BACKEND_ENABLED).

#pragma once

#ifdef STURM_BACKEND_ENABLED

#include "sturm/control/when_fwd.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace sturm {

// ── BitProxy ─────────────────────────────────────────────────────────────────

struct BitProxy {
    int*      qubit_ptr;   // &parent.qubits[i]  or  &qbool.qubits[0]
    uint64_t* mask_ptr;    // &parent.super_mask  or  &qbool.super_mask
    int64_t*  value_ptr;   // &parent.value       or  &qbool.value
    size_t    bit_pos;     // which bit in the parent (0 for qbool)

    // ── Constructors ─────────────────────────────────────────────────────

    // From qint_t<W> parent + bit index.
    template <std::size_t W>
    BitProxy(qint_t<W>& parent, size_t i)
        : qubit_ptr(&parent.qubits[i]),
          mask_ptr(&parent.super_mask),
          value_ptr(&parent.value),
          bit_pos(i) {}

    // From standalone qbool (wraps qbool's own fields, bit_pos = 0).
    BitProxy(qbool& q)  // NOLINT(google-explicit-constructor)
        : qubit_ptr(&q.qubits[0]),
          mask_ptr(&q.super_mask),
          value_ptr(&q.value),
          bit_pos(0) {}

    // Default (null -- for array construction, must be assigned before use).
    BitProxy()
        : qubit_ptr(nullptr), mask_ptr(nullptr),
          value_ptr(nullptr), bit_pos(0) {}

    // ── Accessors ────────────────────────────────────────────────────────

    int  qubit_index() const { return *qubit_ptr; }
    bool is_quantum()  const { return *qubit_ptr >= 0; }
    bool bit_value()   const { return (*value_ptr >> bit_pos) & 1; }

    void set_bit_value(bool v) {
        if (v) *value_ptr |=  (int64_t(1) << bit_pos);
        else   *value_ptr &= ~(int64_t(1) << bit_pos);
    }

    // ── Promotion ────────────────────────────────────────────────────────
    // Allocate qubit if unallocated.  Initialize to classical value via X
    // gate.  Update parent's super_mask.  Requires active BackendContext.
    void ensure_quantum() {
        if (*qubit_ptr >= 0) return;  // already allocated
        *qubit_ptr = QubitPool::instance().allocate();
        *mask_ptr |= (1ULL << bit_pos);
        if (bit_value()) {
            emit_X_lifted(get_ctx(), static_cast<uint32_t>(*qubit_ptr));
        }
    }

    // ── Gate operators ───────────────────────────────────────────────────

    // CNOT with classical folding.
    BitProxy& operator^=(const BitProxy& other) {
        if (other.is_quantum()) {
            // Quantum source: promote target, emit CX.
            this->ensure_quantum();
            emit_CX_lifted(get_ctx(),
                           static_cast<uint32_t>(other.qubit_index()),
                           static_cast<uint32_t>(this->qubit_index()));
        } else {
            // Classical source.
            if (other.bit_value()) {
                // XOR with classical 1.
                if (detail::current_control != nullptr || this->is_quantum()) {
                    this->ensure_quantum();
                    emit_X_lifted(get_ctx(),
                                  static_cast<uint32_t>(this->qubit_index()));
                }
                // else: pure classical, no quantum effect.
            }
            // XOR with classical 0: identity, skip.
        }
        return *this;
    }

    // Toffoli with classical folding.
    BitProxy& operator^=(const AndExpr<BitProxy>& expr) {
        const BitProxy& a = expr.a;
        const BitProxy& b = expr.b;

        if (a.is_quantum() && b.is_quantum()) {
            // Both quantum: full Toffoli.
            this->ensure_quantum();
            emit_CCX_lifted(get_ctx(),
                            static_cast<uint32_t>(a.qubit_index()),
                            static_cast<uint32_t>(b.qubit_index()),
                            static_cast<uint32_t>(this->qubit_index()));
        } else if (a.is_quantum() && !b.is_quantum()) {
            // a quantum, b classical.
            if (b.bit_value()) {
                this->ensure_quantum();
                emit_CX_lifted(get_ctx(),
                               static_cast<uint32_t>(a.qubit_index()),
                               static_cast<uint32_t>(this->qubit_index()));
            }
            // b == 0: AND result always 0, skip.
        } else if (!a.is_quantum() && b.is_quantum()) {
            // a classical, b quantum.
            if (a.bit_value()) {
                this->ensure_quantum();
                emit_CX_lifted(get_ctx(),
                               static_cast<uint32_t>(b.qubit_index()),
                               static_cast<uint32_t>(this->qubit_index()));
            }
            // a == 0: AND result always 0, skip.
        } else {
            // Both classical.
            bool classical_result = a.bit_value() & b.bit_value();
            if (classical_result) {
                if (detail::current_control != nullptr || this->is_quantum()) {
                    this->ensure_quantum();
                    emit_X_lifted(get_ctx(),
                                  static_cast<uint32_t>(this->qubit_index()));
                }
            }
            // else: classical AND result is 0, skip.
        }
        return *this;
    }

    // OR decomposition: c ^= (a | b) = c ^= a; c ^= b; c ^= (a & b).
    BitProxy& operator^=(const OrExpr<BitProxy>& expr) {
        *this ^= expr.a;
        *this ^= expr.b;
        AndExpr<BitProxy> and_expr(expr.a, expr.b);
        *this ^= and_expr;
        return *this;
    }

    // X gate (flip).
    BitProxy& flip() {
        if (this->is_quantum() || detail::current_control != nullptr) {
            this->ensure_quantum();
            emit_X_lifted(get_ctx(),
                          static_cast<uint32_t>(this->qubit_index()));
        }
        // else: no quantum effect, caller handles classical value.
        return *this;
    }
};

// ── Lazy expressions ─────────────────────────────────────────────────────────

inline AndExpr<BitProxy> operator&(const BitProxy& a,
                                   const BitProxy& b) noexcept {
    return AndExpr<BitProxy>{a, b};
}

inline OrExpr<BitProxy> operator|(const BitProxy& a,
                                  const BitProxy& b) noexcept {
    return OrExpr<BitProxy>{a, b};
}

// ── Materialization: AndExpr/OrExpr<BitProxy> -> qbool ───────────────────────
// Free functions (not template specializations) because AndExpr<T>::operator T()
// converts to T=BitProxy, not qbool.  Usage: qbool tmp = materialize_and(a, b);

inline qbool materialize_and(const BitProxy& a, const BitProxy& b) {
    BackendContext& ctx = get_ctx();
    int anc_idx = QubitPool::instance().allocate();
    const auto anc = static_cast<uint32_t>(anc_idx);

    if (a.is_quantum() && b.is_quantum()) {
        primitive_AND(ctx,
                      static_cast<uint32_t>(a.qubit_index()),
                      static_cast<uint32_t>(b.qubit_index()),
                      anc);
    } else if (a.is_quantum() && !b.is_quantum()) {
        if (b.bit_value()) {
            primitive_XOR(ctx,
                          static_cast<uint32_t>(a.qubit_index()),
                          anc);
        }
        // b == 0: ancilla stays |0>.
    } else if (!a.is_quantum() && b.is_quantum()) {
        if (a.bit_value()) {
            primitive_XOR(ctx,
                          static_cast<uint32_t>(b.qubit_index()),
                          anc);
        }
        // a == 0: ancilla stays |0>.
    } else {
        // Both classical.
        if (a.bit_value() && b.bit_value()) {
            emit_X_lifted(ctx, anc);
        }
    }

    qbool result;
    result.qubits[0]  = anc_idx;
    result.owning_    = true;
    result.super_mask = 1ULL;
    result.uncompute_ = uncompute_op::make_bitwise_qbool(
        a.is_quantum() ? static_cast<uint32_t>(a.qubit_index()) : 0u,
        b.is_quantum() ? static_cast<uint32_t>(b.qubit_index()) : 0u,
        0u);  // 0 = AND
    return result;
}

// Overload accepting an AndExpr<BitProxy> directly for convenience.
inline qbool materialize_and(const AndExpr<BitProxy>& expr) {
    return materialize_and(expr.a, expr.b);
}

inline qbool materialize_or(const BitProxy& a, const BitProxy& b) {
    BackendContext& ctx = get_ctx();
    int anc_idx = QubitPool::instance().allocate();
    const auto anc = static_cast<uint32_t>(anc_idx);

    // CX(a, anc) -- if a is quantum.
    if (a.is_quantum()) {
        primitive_XOR(ctx, static_cast<uint32_t>(a.qubit_index()), anc);
    } else if (a.bit_value()) {
        emit_X_lifted(ctx, anc);
    }

    // CX(b, anc) -- if b is quantum.
    if (b.is_quantum()) {
        primitive_XOR(ctx, static_cast<uint32_t>(b.qubit_index()), anc);
    } else if (b.bit_value()) {
        emit_X_lifted(ctx, anc);
    }

    // CCX(a, b, anc) -- AND term.
    if (a.is_quantum() && b.is_quantum()) {
        primitive_AND(ctx,
                      static_cast<uint32_t>(a.qubit_index()),
                      static_cast<uint32_t>(b.qubit_index()),
                      anc);
    } else if (a.is_quantum() && b.bit_value()) {
        primitive_XOR(ctx, static_cast<uint32_t>(a.qubit_index()), anc);
    } else if (b.is_quantum() && a.bit_value()) {
        primitive_XOR(ctx, static_cast<uint32_t>(b.qubit_index()), anc);
    } else if (a.bit_value() && b.bit_value()) {
        emit_X_lifted(ctx, anc);
    }

    qbool result;
    result.qubits[0]  = anc_idx;
    result.owning_    = true;
    result.super_mask = 1ULL;
    result.uncompute_ = uncompute_op::make_bitwise_qbool(
        a.is_quantum() ? static_cast<uint32_t>(a.qubit_index()) : 0u,
        b.is_quantum() ? static_cast<uint32_t>(b.qubit_index()) : 0u,
        1u);  // 1 = OR
    return result;
}

// Overload accepting an OrExpr<BitProxy> directly for convenience.
inline qbool materialize_or(const OrExpr<BitProxy>& expr) {
    return materialize_or(expr.a, expr.b);
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
