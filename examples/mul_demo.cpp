#include "sturm.h"
#include "sturm/draw_ascii.h"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// LO-2 single-multiplication end-to-end demo
// ─────────────────────────────────────────────────────────────────────────────
//
// Source contains one statement that the LO-2 transpiler rewrites:
//
//     a *= b;
//
// becomes (in build/sturm_gen/examples/mul_demo.cpp) an allocate-compute-swap
// triplet that invokes mul_oop<W>, with a matching swap + mul_oop_adj<W>
// planted before the inner scope's `}` (LIFO scope-exit cleanup).
//
// To force the GATE path of mul_oop (rather than the classical short-circuit
// for qubits[0] < 0), each qint is auto-promoted to allocated qubits via
// `theta() += 0.0`. That emits W rotation records per operand BEFORE the
// multiplication, so the final ASCII diagram has three visible bands:
//
//   1. Auto-promote setup       (Ry(0) per qubit on a, b, plus X for value&1)
//   2. LO-2 forward             (mul_oop → lib_mul_dsl: Toffoli/CX ladder
//                                writing the 2W-bit product over tmp ‖ ancillas,
//                                followed by SWAPs that move tmp into a)
//   3. LO-2 cleanup at scope }  (SWAPs back, then mul_oop_adj reverses
//                                the lib_mul_dsl network — every gate from
//                                band 2 mirrored)
//
// Width is W=2 so the diagram fits in a terminal: tmp=2 qubits, ancillas=2,
// plus the 2+2 for a,b → 8 qubits total in scope.
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API and the auto-injected lifecycle wraps
// `main` with `sturm_backend_create` / `destroy`. The opt-in
// `sturm/draw_ascii.h` exposes the no-arg renderer entry point.

int main() {
    {
        sturm::qint_t<2> a;
        sturm::qint_t<2> b;
        a.value = 3;   // 0b11
        b.value = 2;   // 0b10  → product = 6 = 0b110, low W=2 bits = 0b10

        // Force gate path: auto-promote both registers to allocated qubits.
        a.theta() += 0.0;
        b.theta() += 0.0;

        // The single multiplication. The LO-2 transpiler rewrites this in
        // build/sturm_gen/examples/mul_demo.cpp into:
        //     sturm::qint_t<2> __sturm_tmp_mul_0;
        //     ::sturm::mul_oop(a, b, __sturm_tmp_mul_0);
        //     sturm::swap(a, __sturm_tmp_mul_0);
        // and plants the matching cleanup before the inner `}` below:
        //     sturm::swap(a, __sturm_tmp_mul_0);
        //     sturm::invert<&::sturm::detail::mul_oop<2>>()(
        //         a, b, __sturm_tmp_mul_0);
        a *= b;
    }

    sturm::print_ascii();
    std::fprintf(stdout, "\n[gate count = %zu]\n", sturm::gate_count());
    return 0;
}
