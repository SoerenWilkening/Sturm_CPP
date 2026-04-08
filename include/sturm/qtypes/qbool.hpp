#pragma once
// qbool.hpp — Quantum boolean type (Step 3, spec §2)
// Holds a classical value, a superposition flag, and a single qubit index.
// The qubit index is lazily allocated from QubitPool on construction with a
// probability (double ctor) or on explicit ensure_qubit() calls.

#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/counter_sink.hpp"  // brings in current_sink()

#include <array>

namespace sturm {

// ── qbool ─────────────────────────────────────────────────────────────────────
// Represents a boolean that may be in a quantum superposition.
//
// Fields (public for struct-style access in tests and dispatch helpers):
//   value      — classical boolean value (stub for measurement result).
//   is_super   — true when the qubit is in a genuine superposition.
//   qubits     — single-element array; qubits[0] == -1 means no qubit yet.
class qbool {
public:
    bool           value    = false;
    bool           is_super = false;
    std::array<int,1> qubits{-1};

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

    // ── Destructor ────────────────────────────────────────────────────────
    // Returns the owned qubit (if any) back to the pool.
    ~qbool() {
        if (qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
            qubits[0] = -1;
        }
    }

    // ── Copy constructor & assignment ─────────────────────────────────────
    // Copies value and is_super but does NOT share the qubit — the copy gets
    // its own allocation when/if it is needed.  For now the qubit is simply
    // reset to -1 on the copy (lazy allocation on first use via ensure_qubit).
    qbool(const qbool& other)
        : value(other.value), is_super(other.is_super), qubits{-1} {}

    qbool& operator=(const qbool& other) {
        if (this == &other) return *this;
        // Release current qubit before overwriting.
        if (qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
            qubits[0] = -1;
        }
        value    = other.value;
        is_super = other.is_super;
        // Do not copy the qubit index — caller must call ensure_qubit() if needed.
        return *this;
    }

    // ── Move constructor & assignment ─────────────────────────────────────
    qbool(qbool&& other) noexcept
        : value(other.value), is_super(other.is_super), qubits{other.qubits[0]} {
        other.qubits[0] = -1;  // prevent double-release
    }

    qbool& operator=(qbool&& other) noexcept {
        if (this == &other) return *this;
        if (qubits[0] >= 0) {
            QubitPool::instance().release(qubits[0]);
        }
        value           = other.value;
        is_super        = other.is_super;
        qubits[0]       = other.qubits[0];
        other.qubits[0] = -1;
        return *this;
    }

    // ── Explicit bool conversion ──────────────────────────────────────────
    // Returns the classical value.
    // TODO(backend): perform a real quantum measurement and collapse the state.
    explicit operator bool() const noexcept { return value; }

    // ── ensure_qubit ──────────────────────────────────────────────────────
    // Lazily allocates a qubit if none is assigned yet.
    // Used by the WHEN guard and comparison operators to materialise a qubit
    // for a classically-known qbool before passing it to the control path.
    void ensure_qubit() {
        if (qubits[0] < 0) {
            qubits[0] = QubitPool::instance().allocate();
        }
    }
};

} // namespace sturm
