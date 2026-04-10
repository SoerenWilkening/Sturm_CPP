#pragma once
// qbool.hpp — Quantum boolean type (M5: qbool inherits qint_t<1>).
// qbool is a subclass of qint_t<1>, inheriting value (int64_t),
// super_mask (uint64_t), qubits[1], owning_, and uncompute_.
// Backward-compatible accessors (get_is_super, get_bool_value) are provided.

#include "sturm/qtypes/qint_core.hpp"   // qint_t<1> base class

#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // brings in current_sink()

// M22: uncompute_op for comparison results stored in qbool.
// Only compiled when STURM_BACKEND_ENABLED is defined (backend builds).
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/uncompute/uncompute_op.hpp"
#  include "sturm/uncompute/qint_base.hpp"
#  include "sturm/core/context.hpp"
// M13: gate emission helpers for qbool uncompute (AND / OR / X).
#  include "sturm/backend/primitives.hpp"
#endif

#include <array>
#include <cstdint>

namespace sturm {

// Forward declarations for lazy expression types (defined in lazy_expr.hpp).
template<typename T> struct AndExpr;
template<typename T> struct OrExpr;

// ── qbool ─────────────────────────────────────────────────────────────────────
// Represents a boolean that may be in a quantum superposition.
// Inherits from qint_t<1>:
//   int64_t  value      — classical integer value (bit 0 = boolean value)
//   uint64_t super_mask — bit 0 set iff qubit is in superposition (was: is_super)
//   std::array<int,1> qubits — single-element array; qubits[0]==-1 means no qubit
//   bool owning_        — destructor releases qubit iff true
//   uncompute_op uncompute_ (ifdef STURM_BACKEND_ENABLED)
class qbool : public qint_t<1> {
public:
    // ── Temporary fields (M7 will migrate these) ──────────────────────────
    // These fields are kept temporarily because qbool_ops.hpp still sets them.
    // M7 will migrate operators to use uncompute_op instead.
    enum class QboolUncompute : uint8_t {
        NONE,        ///< No uncompute (default)
        X,           ///< Re-emit X(qubits[0]) to uncompute operator~
        AND,         ///< Re-emit CCX(a, b, qubits[0]) to uncompute AND materialization
        OR,          ///< Re-emit 2 CX + CCX to uncompute OR materialization
    };

    QboolUncompute qbool_uncompute_  = QboolUncompute::NONE;
    uint32_t       uncompute_a_qubit_ = 0u;
    uint32_t       uncompute_b_qubit_ = 0u;

    // ── Backward-compatible accessors ─────────────────────────────────────
    // These replace direct field access (.is_super, .value as bool).
    bool get_is_super() const noexcept { return (super_mask & 1) != 0; }
    void set_is_super(bool s) noexcept { super_mask = s ? 1ULL : 0ULL; }

    bool get_bool_value() const noexcept { return (value & 1) != 0; }
    void set_bool_value(bool v) noexcept { value = v ? 1 : 0; }

    // ── Default constructor ───────────────────────────────────────────────
    // Produces a classical false with no qubit allocated.
    // value=0, super_mask=0, qubits={-1}, owning_=true
    qbool() : qint_t<1>(static_cast<int64_t>(0)) {}

    // ── Classical bool constructor (implicit) ─────────────────────────────
    // Builds a classical qbool with the given value; no qubit is allocated.
    // Intentionally non-explicit so that `qbool q = true;` works.
    qbool(bool v) noexcept  // NOLINT(google-explicit-constructor)
        : qint_t<1>(v ? static_cast<int64_t>(1) : static_cast<int64_t>(0)) {}

    // ── Probabilistic / superposition constructor ─────────────────────────
    // Creates a superposed qbool with the given Bloch-sphere probability p.
    // Allocates one qubit from QubitPool and calls current_sink()->prepare().
    explicit qbool(double p)
        : qint_t<1>(static_cast<int64_t>(0)) {
        super_mask = 1ULL;
        qubits[0] = QubitPool::instance().allocate();
        current_sink()->prepare(qubits[0], p);
    }

    // ── Non-owning factory ────────────────────────────────────────────────
    // Creates a qbool that references qubit `idx` but does NOT own it.
    // The qubit must outlive this qbool.
    static qbool make_non_owning(int idx) noexcept {
        qbool q;
        q.qubits[0] = idx;
        q.owning_   = false;
        return q;
    }

    // ── as_qint_base (M22) ────────────────────────────────────────────────
    // Builds a width-1 qint_base view of this qbool's qubit register.
#ifdef STURM_BACKEND_ENABLED
    [[nodiscard]] qint_base as_qint_base() const noexcept {
        qint_base b;
        b.value          = value;
        b.super_mask     = super_mask;
        b.promotion_mask = 0u;
        b.width          = 1u;
        b.qubits[0]      = (qubits[0] >= 0)
                           ? static_cast<uint32_t>(qubits[0]) : 0u;
        return b;
    }
#endif

    // ── Destructor ────────────────────────────────────────────────────────
    // Handles QboolUncompute (X/AND/OR) and then delegates qubit release
    // and uncompute_op to the base class destructor.
    ~qbool() {
#ifdef STURM_BACKEND_ENABLED
        // M13: X / AND / OR uncompute.
        if (qbool_uncompute_ != QboolUncompute::NONE && qubits[0] >= 0) {
            if (sturm_backend_context_t* ctx_raw = sturm_get_thread_context()) {
                BackendContext& ctx = *ctx_raw;
                const uint32_t tgt  = static_cast<uint32_t>(qubits[0]);
                switch (qbool_uncompute_) {
                case QboolUncompute::X:
                    primitive_X(ctx, tgt);
                    break;
                case QboolUncompute::AND:
                    primitive_AND(ctx, uncompute_a_qubit_, uncompute_b_qubit_, tgt);
                    break;
                case QboolUncompute::OR:
                    primitive_XOR(ctx, uncompute_a_qubit_, tgt);
                    primitive_XOR(ctx, uncompute_b_qubit_, tgt);
                    primitive_AND(ctx, uncompute_a_qubit_, uncompute_b_qubit_, tgt);
                    break;
                default:
                    break;
                }
            }
        }
#endif
        // Base destructor handles: uncompute_.apply() and qubit release.
        // We must clear the base uncompute_ tag here before base dtor runs
        // if we want the base to still do qubit release — actually the base
        // dtor runs automatically after this body. We do NOT need to call
        // it explicitly. The base will release qubits if owning_.
    }

    // ── Copy constructor & assignment ─────────────────────────────────────
    // Copies value/super_mask but does NOT share qubit indices.
    qbool(const qbool& other)
        : qint_t<1>(static_cast<int64_t>(0)) {
        value      = other.value;
        super_mask = other.super_mask;
        qubits[0]  = -1;
        owning_    = true;
        qbool_uncompute_  = QboolUncompute::NONE;
        uncompute_a_qubit_ = 0u;
        uncompute_b_qubit_ = 0u;
#ifdef STURM_BACKEND_ENABLED
        uncompute_ = uncompute_op{};
#endif
    }

    qbool& operator=(const qbool& other) {
        if (this == &other) return *this;
        // Release current qubit if owning
        if (owning_ && qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
            qubits[0] = -1;
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits[0]  = -1;
        owning_    = true;
        qbool_uncompute_   = QboolUncompute::NONE;
        uncompute_a_qubit_ = 0u;
        uncompute_b_qubit_ = 0u;
#ifdef STURM_BACKEND_ENABLED
        uncompute_ = uncompute_op{};
#endif
        return *this;
    }

    // ── Move constructor & assignment ─────────────────────────────────────
    qbool(qbool&& other) noexcept
        : qint_t<1>(static_cast<int64_t>(0)) {
        value              = other.value;
        super_mask         = other.super_mask;
        qubits[0]          = other.qubits[0];
        owning_            = other.owning_;
        qbool_uncompute_   = other.qbool_uncompute_;
        uncompute_a_qubit_ = other.uncompute_a_qubit_;
        uncompute_b_qubit_ = other.uncompute_b_qubit_;
#ifdef STURM_BACKEND_ENABLED
        uncompute_         = other.uncompute_;
#endif
        other.qubits[0]        = -1;
        other.owning_          = false;
        other.qbool_uncompute_ = QboolUncompute::NONE;
#ifdef STURM_BACKEND_ENABLED
        other.uncompute_ = uncompute_op{};
#endif
    }

    qbool& operator=(qbool&& other) noexcept {
        if (this == &other) return *this;
        if (owning_ && qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
        }
        value                = other.value;
        super_mask           = other.super_mask;
        qubits[0]            = other.qubits[0];
        owning_              = other.owning_;
        qbool_uncompute_     = other.qbool_uncompute_;
        uncompute_a_qubit_   = other.uncompute_a_qubit_;
        uncompute_b_qubit_   = other.uncompute_b_qubit_;
        other.qubits[0]      = -1;
        other.owning_        = false;
        other.qbool_uncompute_ = QboolUncompute::NONE;
#ifdef STURM_BACKEND_ENABLED
        uncompute_       = other.uncompute_;
        other.uncompute_ = uncompute_op{};
#endif
        return *this;
    }

    // ── Explicit bool conversion ──────────────────────────────────────────
    explicit operator bool() const noexcept { return (value & 1) != 0; }

    // ── ensure_qubit ──────────────────────────────────────────────────────
    void ensure_qubit() {
        if (qubits[0] < 0) {
            qubits[0] = QubitPool::instance().allocate();
        }
    }

    // ── M13 qbool operator declarations ───────────────────────────────────
    // Bodies are provided in qbool_ops.hpp.  Only declared here.

    qbool& operator^=(const qbool& other);
    qbool& operator^=(const AndExpr<qbool>& expr);
    qbool& operator^=(const OrExpr<qbool>& expr);
    qbool& flip();
    qbool  operator~() const;
};

} // namespace sturm
