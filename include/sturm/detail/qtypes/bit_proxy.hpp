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
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/ops/lifted_primitives.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace sturm {

// ── AndExpr / OrExpr (BitProxy-local, PK-2) ──────────────────────────────────
// Phase K PK-2 retired the qbool-level lazy_expr.hpp wrappers; BitProxy still
// uses these lightweight reference-holders as tag types for the `a & b` /
// `a | b` shapes consumed by `result ^= (a & b);` inside qint_bitwise_*.hpp.
template<typename T>
struct AndExpr {
    const T& a; const T& b;
    AndExpr(const T& a_, const T& b_) noexcept : a(a_), b(b_) {}
    AndExpr(const AndExpr&) = delete;
    AndExpr& operator=(const AndExpr&) = delete;
};
template<typename T>
struct OrExpr {
    const T& a; const T& b;
    OrExpr(const T& a_, const T& b_) noexcept : a(a_), b(b_) {}
    OrExpr(const OrExpr&) = delete;
    OrExpr& operator=(const OrExpr&) = delete;
};

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

    // ── Implicit conversion to qbool ────────────────────────────────────
    // Returns a non-owning qbool view of the bit, matching the semantics
    // of the const operator[] on qint_t<W>.  This allows BitProxy to be
    // used seamlessly wherever qbool was expected.
    // NOLINTNEXTLINE(google-explicit-constructor)
    operator qbool() const {
        qbool out = qbool::make_non_owning(*qubit_ptr);
        out.value      = static_cast<int64_t>(bit_value());
        out.super_mask = (*mask_ptr >> bit_pos) & 1ULL;
        return out;
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

    // ── Per-bit phase / rotation proxies (sturm-51wc) ────────────────────
    // Single-bit slice of qint_t<W>::PhiProxy / ThetaProxy. The full-register
    // proxies fan rotation across every allocated bit of the parent; these
    // emit on exactly one bit. Stored by value: a BitProxy is a 5-field
    // pointer pack into the parent qint_t/qbool, and the parent — not the
    // BitProxy temporary — must outlive the proxy. Forward-declared here +
    // defined out-of-line below because each holds a BitProxy by value, which
    // requires BitProxy to be complete.
    struct PhiProxy;
    struct ThetaProxy;
    PhiProxy   phi();
    ThetaProxy theta();
};

// ── BitProxy::PhiProxy / ThetaProxy out-of-line definitions ─────────────────
struct BitProxy::PhiProxy {
    BitProxy bit;
    // RZ on a classical bit is just an unobservable global phase when no
    // control is active, so skip. Under a WHEN control the phase becomes
    // relative between control branches and IS observable, so promote.
    // Predicate matches BitProxy::flip().
    void operator+=(double delta) {
        if (!bit.is_quantum() && detail::current_control == nullptr) {
            return;
        }
        bit.ensure_quantum();
        emit_RZ_lifted(get_ctx(),
                       static_cast<uint32_t>(bit.qubit_index()),
                       delta);
    }
    void operator-=(double delta) { operator+=(-delta); }
};

struct BitProxy::ThetaProxy {
    BitProxy bit;
    // RY on |0> or |1> creates a superposition — that IS the quantum
    // operation that justifies promoting a classical bit, so unconditional
    // ensure_quantum + emit.
    void operator+=(double delta) {
        bit.ensure_quantum();
        emit_RY_lifted(get_ctx(),
                       static_cast<uint32_t>(bit.qubit_index()),
                       delta);
    }
    void operator-=(double delta) { operator+=(-delta); }
};

inline BitProxy::PhiProxy   BitProxy::phi()   { return PhiProxy{*this}; }
inline BitProxy::ThetaProxy BitProxy::theta() { return ThetaProxy{*this}; }

// ── BitProxy lazy expressions (tag types for operator^= dispatch) ───────────
inline AndExpr<BitProxy> operator&(const BitProxy& a, const BitProxy& b) noexcept {
    return AndExpr<BitProxy>{a, b};
}
inline OrExpr<BitProxy> operator|(const BitProxy& a, const BitProxy& b) noexcept {
    return OrExpr<BitProxy>{a, b};
}

// ── Materialization: BitProxy pair -> owning qbool ───────────────────────────
// Free functions (not operator T()) since they return a new owning qbool
// with its own allocated ancilla.  Usage: qbool tmp = materialize_and(a, b);

inline qbool materialize_and(const BitProxy& a, const BitProxy& b) {
    BackendContext& ctx = get_ctx();
    const int anc_idx = QubitPool::instance().allocate();
    const auto anc = static_cast<uint32_t>(anc_idx);
    if (a.is_quantum() && b.is_quantum()) {
        primitive_AND(ctx, static_cast<uint32_t>(a.qubit_index()),
                      static_cast<uint32_t>(b.qubit_index()), anc);
    } else if (a.is_quantum() && b.bit_value()) {
        primitive_XOR(ctx, static_cast<uint32_t>(a.qubit_index()), anc);
    } else if (b.is_quantum() && a.bit_value()) {
        primitive_XOR(ctx, static_cast<uint32_t>(b.qubit_index()), anc);
    } else if (!a.is_quantum() && !b.is_quantum() && a.bit_value() && b.bit_value()) {
        emit_X_lifted(ctx, anc);
    }
    qbool result;
    result.qubits[0]  = anc_idx;
    result.owning_    = true;
    result.super_mask = 1ULL;
    // Phase K PK-3: inverse emission is now the transpiler's responsibility
    // (see uncompute_api.hpp uncompute_and). Destructor is release-only.
    return result;
}
inline qbool materialize_and(const AndExpr<BitProxy>& expr) {
    return materialize_and(expr.a, expr.b);
}

inline qbool materialize_or(const BitProxy& a, const BitProxy& b) {
    BackendContext& ctx = get_ctx();
    const int anc_idx = QubitPool::instance().allocate();
    const auto anc = static_cast<uint32_t>(anc_idx);
    // CX(a, anc) and CX(b, anc).
    if (a.is_quantum()) primitive_XOR(ctx, static_cast<uint32_t>(a.qubit_index()), anc);
    else if (a.bit_value()) emit_X_lifted(ctx, anc);
    if (b.is_quantum()) primitive_XOR(ctx, static_cast<uint32_t>(b.qubit_index()), anc);
    else if (b.bit_value()) emit_X_lifted(ctx, anc);
    // CCX(a, b, anc) -- AND term.
    if (a.is_quantum() && b.is_quantum()) {
        primitive_AND(ctx, static_cast<uint32_t>(a.qubit_index()),
                      static_cast<uint32_t>(b.qubit_index()), anc);
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
    // Phase K PK-3: inverse emission is now the transpiler's responsibility
    // (see uncompute_api.hpp uncompute_or). Destructor is release-only.
    return result;
}
inline qbool materialize_or(const OrExpr<BitProxy>& expr) {
    return materialize_or(expr.a, expr.b);
}

} // namespace sturm

#endif  // STURM_BACKEND_ENABLED
