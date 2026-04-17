#pragma once
// qbool_logic.hpp — Boolean operators for qbool (non-backend / frontend path).
//
// Defines: operator| operator& operator^ (free functions returning qbool by value)
//          qbool::operator~() body (member, declared in qbool.hpp)
//
// Phase K PK-2: these are the eager, classical-only counterparts to the
// backend qbool_ops.hpp operators.  When STURM_BACKEND_ENABLED is set,
// operator& / operator| / operator^ / operator~ are provided by
// qbool_ops.hpp (allocating a qubit via QubitPool::instance().acquire(),
// emitting the forward gate, and returning an owning qbool by value).
// Lazy AndExpr / OrExpr wrappers were retired in PK-2 — the zero-ancilla
// optimization now lives in the transpiler IR pass (see Phase J PJ-1).
//
// This header is included by when.hpp so that WHEN(c | d) works out of the
// box in frontend builds without requiring additional includes.

#include "sturm/qtypes/qbool.hpp"

#ifndef STURM_BACKEND_ENABLED

namespace sturm {

// ── operator| ─────────────────────────────────────────────────────────────────
// Boolean OR: returns a new qbool whose classical value is a || b.
// If either operand is superposed, the result is marked superposed (no qubit
// allocated — lazy, matching qbool copy-constructor semantics).

inline qbool operator|(const qbool& a, const qbool& b) {
    qbool result;
    result.value      = (a.value | b.value) & 1;
    result.super_mask = (a.super_mask | b.super_mask) & 1;
    return result;
}

// ── operator& ─────────────────────────────────────────────────────────────────
// Boolean AND: returns a new qbool whose classical value is a && b.

inline qbool operator&(const qbool& a, const qbool& b) {
    qbool result;
    result.value      = (a.value & b.value) & 1;
    result.super_mask = (a.super_mask | b.super_mask) & 1;
    return result;
}

// ── operator^ ─────────────────────────────────────────────────────────────────
// Boolean XOR: returns a new qbool whose classical value is a != b.

inline qbool operator^(const qbool& a, const qbool& b) {
    qbool result;
    result.value      = (a.value ^ b.value) & 1;
    result.super_mask = (a.super_mask | b.super_mask) & 1;
    return result;
}

// ── qbool::operator~() ──────────────────────────────────────────────────────
// Boolean NOT: returns a new qbool whose classical value is !a.
// Declared in qbool.hpp; body provided here for non-backend builds.
// (Backend body is in qbool_ops.hpp — allocates ancilla + emits X gate.)

inline qbool qbool::operator~() const {
    qbool result;
    result.value      = (~value) & 1;
    result.super_mask = super_mask;
    return result;
}

} // namespace sturm

#endif  // !STURM_BACKEND_ENABLED
