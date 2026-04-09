// c_and.hpp — M3 (PRD v2): C³-X (controlled-controlled-controlled-NOT) via the
// Nielsen-Chuang relative-phase Toffoli sandwich pattern.
//
// lib_c_AND(s, mgr, c0, c1, tgt):
//   Flip tgt iff c0=1 AND c1=1.  Equivalent to a Toffoli (CCX) gate but
//   implemented using the Nielsen-Chuang decomposition:
//
//     1. Borrow one ancilla qubit (guaranteed |0⟩).
//     2. AND(c0, c1, anc)   [forward: anc = c0 AND c1]
//     3. XOR(anc, tgt)      [conditionally flip tgt]
//     4. AND(c0, c1, anc)   [uncompute: anc → |0⟩]
//     5. Free ancilla.
//
//   For the 2-control case this is identical to a direct CCX.  The ancilla
//   round-trip is the "sandwich" — it computes and immediately uncomputes anc,
//   leaving it clean.
//
// NOTE: lib_c_AND with exactly 2 controls *is* a Toffoli.  It is provided as a
// named library entry point so higher-level code can call it by role (C^3-X
// via sandwich) rather than calling primitive_AND directly.  The PRD v2 §4
// "c_AND" refers to this function.
//
// Contrast with c_n_and.hpp which handles n ≥ 3 controls recursively.
//
// Target: <150 LoC (impl plan M3).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cstdint>

namespace sturm {
namespace v2 {

// ── lib_c_AND ─────────────────────────────────────────────────────────────────
//
// C³-X (3-qubit controlled NOT, 2 control qubits) via Nielsen-Chuang sandwich.
//
//   tgt ^= (c0 AND c1)
//
// One ancilla qubit is borrowed from mgr and returned clean.
inline void lib_c_AND(SimState& s, AncillaManager& mgr,
                      uint32_t c0, uint32_t c1, uint32_t tgt) {
    uint32_t anc = mgr.allocate_ancilla();

    // Forward half: compute anc = c0 AND c1.
    primitive_AND(s, c0, c1, anc);

    // Apply the conditional NOT on tgt.
    primitive_XOR(s, anc, tgt);

    // Mirror half: uncompute anc back to |0⟩.
    primitive_AND(s, c0, c1, anc);

    mgr.free_ancilla(anc);
}

} // namespace v2
} // namespace sturm
