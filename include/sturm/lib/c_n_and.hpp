// c_n_and.hpp — M3 (PRD v2): C^n-X (n-controlled NOT) via recursive
// Nielsen-Chuang sandwich cascade.
//
// lib_c_n_AND(s, mgr, ctrls, n_ctrls, tgt):
//   Flip tgt iff ALL n_ctrls control qubits are 1.
//
// Algorithm (recursive Nielsen-Chuang sandwich):
//   n=0:  flip unconditionally (X on tgt).
//   n=1:  XOR(ctrls[0], tgt).
//   n=2:  AND(ctrls[0], ctrls[1], tgt)  [direct Toffoli].
//   n≥3:
//     1. anc = allocate_ancilla()
//     2. AND(ctrls[0], ctrls[1], anc)            [forward: anc = c0 AND c1]
//     3. lib_c_n_AND(s, mgr, {anc, c2..c_{n-1}}, n-1, tgt)  [recurse]
//     4. AND(ctrls[0], ctrls[1], anc)            [uncompute anc → |0⟩]
//     5. free_ancilla(anc)
//
// Space: O(n) ancilla qubits borrowed, all returned clean.
// Depth: O(n) recursive calls, each emitting ≤2 CCX gates.
//
// The maximum number of controls is bounded by kMaxCtrls (set to 20 here,
// matching the limit in when_lift_dispatch.hpp).
//
// Target: <200 LoC (impl plan M3).

#pragma once

#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cstdint>
#include <stdexcept>

namespace sturm {
namespace v2 {

// Maximum number of controls supported by lib_c_n_AND.
static constexpr uint32_t kLibCnAndMaxCtrls = 20u;

// ── lib_c_n_AND ───────────────────────────────────────────────────────────────
//
// C^n-X gate implemented via recursive Nielsen-Chuang sandwich cascade.
// Flips tgt iff all qubits in ctrls[0..n_ctrls-1] are 1.
inline void lib_c_n_AND(SimState& s, AncillaManager& mgr,
                        const uint32_t* ctrls, uint32_t n_ctrls,
                        uint32_t tgt) {
    if (n_ctrls == 0u) {
        primitive_X(s, tgt);
        return;
    }
    if (n_ctrls == 1u) {
        primitive_XOR(s, ctrls[0], tgt);
        return;
    }
    if (n_ctrls == 2u) {
        primitive_AND(s, ctrls[0], ctrls[1], tgt);
        return;
    }

    // n_ctrls >= 3: borrow ancilla, AND-fold first two controls, recurse.
    if (n_ctrls > kLibCnAndMaxCtrls) {
        throw std::runtime_error("lib_c_n_AND: too many controls (max 20)");
    }

    uint32_t anc = mgr.allocate_ancilla();

    // Forward: anc = ctrls[0] AND ctrls[1]
    primitive_AND(s, ctrls[0], ctrls[1], anc);

    // Build reduced control list: [anc, ctrls[2], ..., ctrls[n_ctrls-1]]
    uint32_t reduced[kLibCnAndMaxCtrls];
    reduced[0] = anc;
    for (uint32_t i = 2u; i < n_ctrls; ++i) {
        reduced[i - 1u] = ctrls[i];
    }
    uint32_t reduced_n = n_ctrls - 1u;

    // Recurse.
    lib_c_n_AND(s, mgr, reduced, reduced_n, tgt);

    // Uncompute: AND(ctrls[0], ctrls[1], anc) again (self-inverse Toffoli)
    primitive_AND(s, ctrls[0], ctrls[1], anc);

    mgr.free_ancilla(anc);
}

} // namespace v2
} // namespace sturm
