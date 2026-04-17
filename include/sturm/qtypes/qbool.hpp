#pragma once
// qbool.hpp — Quantum boolean type (M5: qbool inherits qint_t<1>).
// qbool is a subclass of qint_t<1>, inheriting value (int64_t),
// super_mask (uint64_t), qubits[1], and owning_.
// Backward-compatible accessors (get_is_super, get_bool_value) are provided.
//
// Phase K PK-3 (sturm-pzye): the uncompute_op tagged union was retired.
// qbool no longer carries an `uncompute_` member — inverse gate streams
// are emitted by transpiler-synthesised `uncompute_*` free-function
// calls from include/sturm/uncompute/uncompute_api.hpp.

#include "sturm/qtypes/qint_core.hpp"   // qint_t<1> base class

#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // brings in current_sink()

#ifdef STURM_BACKEND_ENABLED
// M13: gate emission helpers for qbool (AND / OR / X).
#  include "sturm/backend/primitives.hpp"
#endif

#include <array>
#include <cstdint>

namespace sturm {

// ── qbool ─────────────────────────────────────────────────────────────────────
// Represents a boolean that may be in a quantum superposition.
// Inherits from qint_t<1>:
//   int64_t  value      — classical integer value (bit 0 = boolean value)
//   uint64_t super_mask — bit 0 set iff qubit is in superposition (was: is_super)
//   std::array<int,1> qubits — single-element array; qubits[0]==-1 means no qubit
//   bool owning_        — destructor releases qubit iff true
class qbool : public qint_t<1> {
public:
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

    // 3-arg overload: propagates value and super_mask from the source operand.
    static qbool make_non_owning(int idx, int64_t val, uint64_t mask) noexcept {
        qbool q;
        q.qubits[0]  = idx;
        q.value       = val;
        q.super_mask  = mask;
        q.owning_     = false;
        return q;
    }

    // ── Destructor ────────────────────────────────────────────────────────
    // M7: trivial destructor — base class qint_t<1> handles qubit release
    // (Phase K PK-3: release-only, no inverse gate emission).
    ~qbool() = default;

    // ── Copy constructor & assignment ─────────────────────────────────────
    // Copies value/super_mask but does NOT share qubit indices.
    qbool(const qbool& other)
        : qint_t<1>(static_cast<int64_t>(0)) {
        value      = other.value;
        super_mask = other.super_mask;
        qubits[0]  = -1;
        owning_    = true;
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
        return *this;
    }

    // ── Move constructor & assignment ─────────────────────────────────────
    qbool(qbool&& other) noexcept
        : qint_t<1>(static_cast<int64_t>(0)) {
        value      = other.value;
        super_mask = other.super_mask;
        qubits[0]  = other.qubits[0];
        owning_    = other.owning_;
        other.qubits[0]  = -1;
        other.owning_    = false;
        other.super_mask = 0;
    }

    qbool& operator=(qbool&& other) noexcept {
        if (this == &other) return *this;
        if (owning_ && qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
        }
        value      = other.value;
        super_mask = other.super_mask;
        qubits[0]  = other.qubits[0];
        owning_    = other.owning_;
        other.qubits[0]  = -1;
        other.owning_    = false;
        other.super_mask = 0;
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

    // ── qbool operator declarations ───────────────────────────────────────
    // Bodies are provided in qbool_ops.hpp (backend) / qbool_logic.hpp
    // (non-backend).  Only declared here.  Phase K PK-2 retired the
    // AndExpr / OrExpr overloads — the zero-ancilla fusion for
    // `qbool __t = a & b; x ^= __t;` now lives in the transpiler IR pass
    // (see include/sturm/uncompute/uncompute_api.hpp sturm::ccnot_inplace).

    qbool& operator^=(const qbool& other);
    qbool& flip();
    qbool  operator~() const;
};

} // namespace sturm
