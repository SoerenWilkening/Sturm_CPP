// lazy_expr.hpp — M13: Lazy AND/OR expression wrappers for qbool (and qint_t<W>).
//
// AndExpr<T> and OrExpr<T> are lightweight reference-holding structs created by
// operator& and operator| on qbool.  They do NOT emit any gates at construction
// time — consumption via qbool::operator^= is the optimized zero-ancilla path.
//
// Implicit conversion operator T() materializes the result into an owning qbool
// (or qint) using an ancilla qubit, with RAII uncomputation on destruction.
//
// Design notes:
//   - Both operands are stored by const reference.  The caller must ensure the
//     operands outlive the expression (this is natural in DSL use).
//   - Materialization allocates one qubit from QubitPool::instance().
//   - The materialized qbool has owning_ = true (destructor releases qubit and
//     emits uncompute gate).
//
// Target: <150 LoC.

#pragma once

#include "sturm/core/qubit_pool.hpp"

namespace sturm {

// Forward declarations so we can reference qbool without including the full header.
// (qbool.hpp includes lazy_expr.hpp; we break the cycle via forward declarations
// in qbool_ops.hpp which is included after both.)

class qbool;

// ── AndExpr ──────────────────────────────────────────────────────────────────
//
// Represents the lazy expression (a & b) for quantum types.
// Created by operator&(const T&, const T&).

template<typename T>
struct AndExpr {
    const T& a;
    const T& b;

    // Prevent copies — references must stay bound.
    AndExpr(const T& a_, const T& b_) noexcept : a(a_), b(b_) {}
    AndExpr(const AndExpr&) = delete;
    AndExpr& operator=(const AndExpr&) = delete;

    // ── Materialization ───────────────────────────────────────────────────────
    // Implicit conversion to T allocates an ancilla qubit, emits a Toffoli gate
    // to compute AND into the ancilla, and returns an owning T.
    // On the returned T's destruction, the Toffoli is re-emitted (uncompute).
    //
    // Specialised for qbool in qbool_ops.hpp (requires BackendContext access).
    // The declaration here is left for generic use; the body is in qbool_ops.hpp.
    operator T() const;
};

// ── OrExpr ───────────────────────────────────────────────────────────────────
//
// Represents the lazy expression (a | b) for quantum types.
// Created by operator|(const T&, const T&).

template<typename T>
struct OrExpr {
    const T& a;
    const T& b;

    OrExpr(const T& a_, const T& b_) noexcept : a(a_), b(b_) {}
    OrExpr(const OrExpr&) = delete;
    OrExpr& operator=(const OrExpr&) = delete;

    // ── Materialization ───────────────────────────────────────────────────────
    // Implicit conversion to T allocates an ancilla qubit, emits De Morgan
    // decomposition into the ancilla, and returns an owning T.
    //
    // Specialised for qbool in qbool_ops.hpp.
    operator T() const;
};

// ── Free operator& for qbool ─────────────────────────────────────────────────
// Returns a lazy AndExpr<qbool>.  No gate is emitted.

inline AndExpr<qbool> operator&(const qbool& a, const qbool& b) noexcept {
    return AndExpr<qbool>{a, b};
}

// ── Free operator| for qbool ─────────────────────────────────────────────────
// Returns a lazy OrExpr<qbool>.  No gate is emitted.

inline OrExpr<qbool> operator|(const qbool& a, const qbool& b) noexcept {
    return OrExpr<qbool>{a, b};
}

} // namespace sturm
