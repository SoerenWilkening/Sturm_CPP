#include "sturm.h"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Phase C: qint-qint compound-assign demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognizes each of the five compound-assign operators on
// qint_t<W> when the RHS is a bare DeclRefExpr to another qint_t (no
// converting constructor fires), and injects a free-function inverse call
// before the enclosing scope closes. LIFO order:
//
//   Source pattern   | Phase | Injected inverse (called with a, b)
//   -----------------|-------|-------------------------------------
//   a += b;          | PC-1  | uncompute_add_qint
//   a -= b;          | PC-2  | uncompute_sub_qint
//   a *= b;          | PC-3  | uncompute_mul_qint
//   a /= b;          | PC-4  | uncompute_div_qint
//   a %= b;          | PC-5  | uncompute_mod_qint
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_qint_arith`, open:
//     build/sturm_gen/examples/qint_arith.cpp
// The five injected inverse lines appear just before the inner-block
// closing brace inside main(), in LIFO (reverse source) order — so
// uncompute_mod_qint is first, uncompute_add_qint is last.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_qint_arith
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API and the auto-injected lifecycle wraps
// `main` with `sturm_backend_create` / `destroy`.

int main() {
    // Inner scope so the transpiler-injected inverses fire BEFORE the
    // qint destructor runs. Both qints are left on the classical short-
    // circuit path (qubits[0] < 0 for both throughout), so the forward
    // chain evaluates purely classically:
    //   a = 6, b = 2 → a += b → 8 → -= b → 6 → *= b → 12 → /= b → 6
    //                 → %= b → 0. b is non-zero throughout, so the
    //   classical div/mod fallbacks never hit their divide-by-zero
    //   guard. Matches the Phase C plan's value choice.
    {
        sturm::qint_t<8> a;
        sturm::qint_t<8> b;
        a.value = 6;
        b.value = 2;
        a += b;
        a -= b;
        a *= b;
        a /= b;
        a %= b;

        // ↳ Transpiler injects five LIFO inverses just before the `}`
        //    below: uncompute_mod_qint, uncompute_div_qint,
        //    uncompute_mul_qint, uncompute_sub_qint, uncompute_add_qint
        //    (each on `(a, b)`). The exact emitted text is visible in
        //    build/sturm_gen/examples/qint_arith.cpp.
    }

    return 0;
}
