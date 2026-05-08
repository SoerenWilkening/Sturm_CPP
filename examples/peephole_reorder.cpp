#include "sturm.h"
#include "sturm/draw_ascii.h"
#include "sturm/uncompute/uncompute_api.hpp"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Phase M PM5: peephole gate reordering demo (alias-analysis-backed)
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile has a PM5-5 peephole matcher
// (`register_peephole_reorder_matcher` in
// transpiler/src/matcher_peephole_reorder.cpp, registered LAST after
// every Phase A..I matcher + PJ-1 fuse + PJ-3 hoist + PJ-4 dead-ancilla)
// whose intended trigger is a triple (A, B, C) inside a LoopBody /
// Function scope where:
//
//     A = `QOpKind::AND` with a synthetic `__stu_t*`-prefixed result
//         (produced upstream by Phase E / PE-4 compound-flatten on a
//          nested AND sub-expression, or by any future standalone AND
//          matcher whose decl-sink would emit the synthetic temp name),
//     B = ANY non-PLUGIN / non-USER_ROUTINE op between A and C,
//     C = `QOpKind::XOR_ASSIGN` whose first operand matches A's result
//         (by name AND decl_loc),
//
// and where B's footprint is bit-disjoint from A's result AND from every
// operand / result of C.  On a match, the matcher emits a
// `QReplacement` that rewrites `A; B; C;` into `A; C; B;` — leaving
// (A, C) adjacent in the rewritten buffer so the PJ-1d ccnot-fuse
// peephole can absorb them on a subsequent transpile (PM5 itself does
// NOT invoke the fuse; its contract is "make (A, C) adjacent when safe,
// without altering any gate's semantics").
//
// The schematic before/after shape the PM5 rewrite targets:
//
//     qbool __stu_t0 = a & b;   // A — AND with synthetic result
//     B;                         // B — bit-disjoint footprint
//     x ^= __stu_t0;             // C — XOR_ASSIGN on __stu_t0
//
// after the reorder:
//
//     qbool __stu_t0 = a & b;   // A — unchanged
//     x ^= __stu_t0;             // C — moved adjacent to A
//     B;                         // B — moved past C (disjoint footprint)
//
// and on the next transpile the PJ-1d fuse peephole collapses the (A, C)
// pair into a single `ccnot_inplace(x, a, b);` call with a matching
// self-adjoint uncompute planted before scope close.  Net effect on the
// circuit: the ancilla qubit the unfused path would have allocated for
// `__stu_t0` vanishes, and B stays in its original semantic slot
// (its operands are bit-disjoint from A.result and C's operands, so
// the commutation is a true no-op on the quantum state).
//
// WHY THE REORDER DOES NOT FIRE ON NATURAL USER SOURCE (YET)
// ----------------------------------------------------------
// PM5-5 Gate 1 requires A's result name to start with `__stu_t` — the
// synthetic prefix `FreshNameAllocator` (transpiler/src/fresh_names.hpp)
// produces for compiler-synthesized temporaries.  Real user source
// cannot literally name a variable `__stu_t0` (it would collide with the
// transpiler's allocator on any subsequent compound-flatten pass); the
// intended upstream producer is the Phase E / PE-4 compound-flatten
// matcher on a nested AND sub-expression like `qbool r = (a & b) | c;`.
//
// In that canonical shape PE-4 lifts the inner AND into a synthetic
// `qbool __stu_t0 = a & b;` decl AND emits an outer OR decl
// `qbool r = __stu_t0 | c;` that wedges between the inner AND and any
// subsequent user-written `^=` consumer in `scope.ops`.  Because the
// outer OR is also a QOperation, the PM5 triple lookup sees
// `(AND, OR, XOR_ASSIGN)` where B = the outer OR; Gate 3's footprint
// disjointness check refuses (B reads `__stu_t0`, which is A's result),
// so the reorder does not fire.  This is correct conservative behaviour
// — the outer OR genuinely depends on A's result and cannot be
// commuted past C.
//
// A future standalone AND matcher (not yet implemented — tracked as a
// follow-up in the PM5 design doc) whose decl-sink emits a QOperation
// for a bare `qbool __stu_t = a & b;` decl WITHOUT a wedge OR would
// make PM5 fire through user source.  In that world the shape
//
//     qbool __stu_t0 = a & b;   // A — bare AND, no wedge
//     y ^= z;                   // B — bit-disjoint footprint
//     x ^= __stu_t0;             // C — XOR_ASSIGN consumer
//
// would observe the reorder in the generated sibling.  Until then this
// example captures the CURRENT pipeline's emission for the canonical
// triple-shape source, pinning the pre-reorder layout so any future
// enablement produces a visible delta in the generated file.  This
// mirrors the PM5-8 snapshot fixtures (`tests/transpiler/fixtures/
// reorder_*.expected.cpp`) which take the same "pin the current
// pipeline emission" approach.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_peephole_reorder`, open:
//     build/sturm_gen/examples/peephole_reorder.cpp
// main()'s inner scope has been rewritten through the pipeline:
//     - PE-4 flatten on `qbool r = (a & b) | c;` lifts the inner AND
//       into a synthetic `qbool __stu_t0 = a & b;` decl + an outer
//       `qbool r = __stu_t0 | c;` decl.
//     - The user-written `y ^= z;` and `x ^= r;` stmts are preserved
//       verbatim (PA-3 passes them through as forward XOR_ASSIGNs).
//     - PA-3 / MVP OR / Phase E LIFO uncompute emissions fire at scope
//       close: `x ^= r;` and `y ^= z;` self-adjoints plus
//       `uncompute_or(r, __stu_t0, c);` and
//       `uncompute_and(__stu_t0, a, b);` in LIFO order.
//     - PM5-5 inspects the final op list but Gate 1's synthetic-prefix
//       check vs Gate 3's footprint check conservatively refuses (as
//       described in the prose above); no reorder QReplacement lands.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_peephole_reorder
//
// The program prints an ASCII circuit diagram.  With superposed
// operands the forward OR and AND decompositions each emit real gates;
// the injected LIFO self-adjoints cancel them exactly, so the net
// effect on the live state is identity.  The load-bearing observable
// is the GENERATED FILE layout (checked by
// `check_example_peephole_reorder.cmake`), not the runtime gate
// stream.
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.
//
// Frontend simplification (sturm-yggr / Phase 8): the umbrella `sturm.h`
// brings in the curated public API (including `qbool` at namespace
// scope) and the auto-injected lifecycle wraps `main` with
// `sturm_backend_create` / `destroy`. The opt-in `sturm/draw_ascii.h`
// exposes the no-arg renderer entry point.

int main() {
    // The canonical PM5 peephole-reorder triple pattern through the
    // existing pipeline.  The inline comments inside `main()`
    // deliberately avoid spelling any of the transpiler-injected
    // fragments verbatim so the PM5-9 byte-identity check and the
    // "did-it-land-in-the-right-place?" search in
    // check_example_peephole_reorder.cmake can both anchor on
    // `int main()` without false-positives.  The detailed rewrite
    // shape is described at the top of the file.
    //
    // All five qbools are superposed (allocated + prepared at p=0.5)
    // so the PE-4 flattened AND + outer OR emit real decomposition
    // gates and the PA-3 forward + self-adjoint `^=` pairs emit real
    // X / CX / CCX records.  The inner scope ensures every injected
    // LIFO inverse fires BEFORE the qbool destructors run.
    {
        qbool a(0.5);
        qbool b(0.5);
        qbool c(0.5);
        qbool x(0.5);
        qbool y(0.5);
        qbool z(0.5);

        // PE-4 compound-flatten fires on the compound below: the
        // inner AND is lifted into a synthetic `__stu_t` temp decl,
        // and the outer OR is rewritten to read that temp.  The
        // exact emitted text is visible in
        //   build/sturm_gen/examples/peephole_reorder.cpp
        // (and is spelled in the top-of-file schematic prose — we
        // omit it verbatim here so the PM5-9 no-leak check does not
        // false-positive on this comment).
        qbool r = (a & b) | c;

        // B — a bit-disjoint XOR_ASSIGN.  Operands `y`, `z` share no
        // footprint with `r`, the synthetic flatten temp, `a`, `b`,
        // or `c`, so PM5's Gate 3 footprint disjointness check would
        // accept commuting B past C — if Gate 1 / Gate 3 accepted
        // the triple shape in the first place.  See top-of-file
        // prose for why the current pipeline does not fire the
        // reorder on this source.
        y ^= z;

        // C — XOR_ASSIGN consuming the flattened outer OR's result.
        // In the intended future where PM5's Gate 1 accepts this
        // triple, PJ-1d's ccnot-fuse would collapse the (A, C) pair
        // into a single fused call with a self-adjoint uncompute
        // before scope close.  See the top-of-file prose for the
        // exact shape (we omit it verbatim here so the PM5-9 check
        // script's "current-pipeline pin" anchor does not
        // false-positive on this comment).
        x ^= r;

        // Reader — keeps PJ-4a dead-ancilla elimination off the `r`
        // decl.  Without at least one reader the PJ-4a matcher would
        // strip the decl entirely and the entire downstream pipeline
        // (including any PM5 observation) would short-circuit.
        (void)r;
    }

    // Emit the ASCII circuit diagram AFTER the Phase M PM5 scope
    // closes, so every injected forward decomposition and LIFO
    // uncompute has fired before the renderer walks the GateIR.  The
    // diagram shows the paired forward + adjoint gates; the load-
    // bearing observable is the GENERATED FILE layout, not the
    // runtime stream.
    sturm::print_ascii();
    return 0;
}
