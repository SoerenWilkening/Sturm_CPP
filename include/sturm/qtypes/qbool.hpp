#pragma once
// qbool.hpp — Quantum boolean type (Step 3, spec §2)
// Holds a classical value, a superposition flag, and a single qubit index.
// The qubit index is lazily allocated from QubitPool on construction with a
// probability (double ctor) or on explicit ensure_qubit() calls.

#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // brings in current_sink()

// M22: uncompute_op for comparison results stored in qbool.
// Only compiled when STURM_BACKEND_ENABLED is defined (backend builds).
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/uncompute/uncompute_op.hpp"
#  include "sturm/uncompute/qint_base.hpp"
#  include "sturm/core/context.hpp"
// M13: gate emission helpers for qbool uncompute (AND / OR / X).
#  include "sturm/backend/primitives_v3.hpp"
#endif

#include <array>
#include <cstdint>

namespace sturm {

// Forward declarations for lazy expression types (defined in lazy_expr.hpp).
template<typename T> struct AndExpr;
template<typename T> struct OrExpr;

// ── qbool ─────────────────────────────────────────────────────────────────────
// Represents a boolean that may be in a quantum superposition.
//
// Fields (public for struct-style access in tests and dispatch helpers):
//   value      — classical boolean value (stub for measurement result).
//   is_super   — true when the qubit is in a genuine superposition.
//   qubits     — single-element array; qubits[0] == -1 means no qubit yet.
class qbool {
public:
    bool              value    = false;
    bool              is_super = false;
    std::array<int,1> qubits{-1};

    // ── Ownership flag (M13) ──────────────────────────────────────────────
    // true  (default): destructor releases the qubit to QubitPool.
    // false           : destructor does NOT release — this qbool is an alias
    //                   for a qubit owned elsewhere (e.g. a[i] bit slice).
    bool owning_ = true;

    // ── M13 qbool uncompute tag ────────────────────────────────────────────
    // Tracks which uncompute action the destructor should perform for qbools
    // produced by operator~, AndExpr::operator qbool(), or OrExpr::operator qbool().
    // Defined always (not guarded) so that qbool objects work without STURM_BACKEND_ENABLED
    // (the destructor simply skips emission when the tag is NONE, which it always is
    // without STURM_BACKEND_ENABLED since no operators set a non-NONE tag).
    enum class QboolUncompute : uint8_t {
        NONE,        ///< No uncompute (default)
        X,           ///< Re-emit X(qubits[0]) to uncompute operator~
        AND,         ///< Re-emit CCX(a, b, qubits[0]) to uncompute AND materialization
        OR,          ///< Re-emit 2 CX + CCX to uncompute OR materialization
    };

    QboolUncompute qbool_uncompute_  = QboolUncompute::NONE;

    // Qubit indices needed for AND / OR uncomputation (stored as uint32_t).
    uint32_t uncompute_a_qubit_ = 0u;
    uint32_t uncompute_b_qubit_ = 0u;

    // ── Uncompute op (M22/Strategy B) ─────────────────────────────────────
    // Carries the semantic inverse of the comparison that produced this qbool
    // (e.g. COMPARE tag from operator==, operator<, etc.).
    // Only present when STURM_BACKEND_ENABLED is compiled in.
#ifdef STURM_BACKEND_ENABLED
    uncompute_op uncompute_{};
#endif

    // ── Default constructor ───────────────────────────────────────────────
    // Produces a classical false with no qubit allocated.
    qbool() = default;

    // ── Classical bool constructor (implicit) ─────────────────────────────
    // Builds a classical qbool with the given value; no qubit is allocated.
    // Intentionally non-explicit so that `qbool q = true;` works.
    qbool(bool v) noexcept  // NOLINT(google-explicit-constructor)
        : value(v), is_super(false), qubits{-1} {}

    // ── Probabilistic / superposition constructor ─────────────────────────
    // Creates a superposed qbool with the given Bloch-sphere probability p.
    // Allocates one qubit from QubitPool and calls current_sink()->prepare().
    explicit qbool(double p)
        : value(false), is_super(true) {
        qubits[0] = QubitPool::instance().allocate();
        current_sink()->prepare(qubits[0], p);
    }

    // ── Non-owning factory (M13) ──────────────────────────────────────────
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
        b.value          = value ? 1 : 0;
        b.super_mask     = is_super ? 1u : 0u;
        b.promotion_mask = 0u;
        b.width          = 1u;
        b.qubits[0]      = (qubits[0] >= 0)
                           ? static_cast<uint32_t>(qubits[0]) : 0u;
        return b;
    }
#endif

    // ── Destructor ────────────────────────────────────────────────────────
    ~qbool() {
#ifdef STURM_BACKEND_ENABLED
        // M22: comparison uncompute.
        if (uncompute_.tag != uncompute_op::kind::NONE) {
            if (sturm_backend_context_t* ctx_raw = sturm_get_thread_context()) {
                qint_base view = as_qint_base();
                uncompute_.apply(*ctx_raw, view);
            }
        }

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
        // Qubit release (always guarded by owning_ flag).
        if (owning_ && qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
            qubits[0] = -1;
        }
    }

    // ── Copy constructor & assignment ─────────────────────────────────────
    qbool(const qbool& other)
        : value(other.value), is_super(other.is_super), qubits{-1} {}

    qbool& operator=(const qbool& other) {
        if (this == &other) return *this;
        if (owning_ && qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
            qubits[0] = -1;
        }
        value            = other.value;
        is_super         = other.is_super;
        owning_          = true;
        qbool_uncompute_ = QboolUncompute::NONE;
        return *this;
    }

    // ── Move constructor & assignment ─────────────────────────────────────
    qbool(qbool&& other) noexcept
        : value(other.value)
        , is_super(other.is_super)
        , qubits{other.qubits[0]}
        , owning_(other.owning_)
        , qbool_uncompute_(other.qbool_uncompute_)
        , uncompute_a_qubit_(other.uncompute_a_qubit_)
        , uncompute_b_qubit_(other.uncompute_b_qubit_)
#ifdef STURM_BACKEND_ENABLED
        , uncompute_(other.uncompute_)
#endif
    {
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
        is_super             = other.is_super;
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
    explicit operator bool() const noexcept { return value; }

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
