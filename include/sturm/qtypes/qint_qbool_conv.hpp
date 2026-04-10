#pragma once
// qint_qbool_conv.hpp — Out-of-class bodies for qint_t<W> ↔ qbool conversions.
//
// These definitions need both qint_core.hpp (class template) and qbool.hpp
// (full qbool definition).  They live here — rather than in qint_core.hpp —
// to break the include cycle that would arise once qbool.hpp includes
// qint_core.hpp for inheritance (M5).
//
// Include order enforced by qint.hpp:
//   qint_core.hpp          (qint_t<W> class, forward-declares qbool)
//   qbool.hpp              (qbool full definition, inherits qint_t<1>)
//   qint_qbool_conv.hpp    ← this file (sees both)

#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qbool.hpp"

namespace sturm {

// ── qint_t<W>(const qbool&) ───────────────────────────────────────────────────
// Implicit zero-extension of bit 0: value and super_mask set from the qbool's
// int64_t/uint64_t fields (M5: qbool inherits qint_t<1>, so fields are already
// int64_t value and uint64_t super_mask).
// The qbool's qubit index is shared in slot 0.
// All other qubit slots stay at -1 (unallocated / classical 0).
template <std::size_t W>
qint_t<W>::qint_t(const qbool& b) noexcept
    : value(b.value),
      super_mask(b.super_mask) {
    qubits.fill(-1);
    qubits[0] = b.qubits[0];
}

// ── qint_t<W>::operator qbool() ───────────────────────────────────────────────
// Explicit narrowing to bit 0: extracts the lsb of value and super_mask.
// The returned qbool is a non-owning view (owning_ = false) so it does not
// release the qubit index on destruction.
template <std::size_t W>
qint_t<W>::operator qbool() const noexcept {
    qbool out;
    out.value      = (value & 1);         // int64_t: extract bit 0
    out.super_mask = (super_mask & 1);    // uint64_t: extract bit 0
    out.qubits[0]  = qubits[0];
    out.owning_    = false;  // view only — qubit is owned by the source qint_t
    return out;
}

} // namespace sturm
