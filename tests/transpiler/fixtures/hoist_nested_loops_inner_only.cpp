// Phase J / PJ-3f input for the sturm-transpile snapshot test.
//
// Nested-loop coverage: `qbool t = a | b;` sits inside an INNER `for`
// whose body is the immediate enclosing scope. `classify_scope_kind`
// returns `LoopBody` for the inner body, and both operands (`a`, `b`)
// are outer-outer-scoped function parameters that the whole nested
// loop nest never writes — the PJ-3d hoist-invariance probe admits
// the op.
//
// The hoist lands ONE LEVEL OUT — at the outer loop body's close
// brace, not the function body's close brace. `compute_loop_enclosing_
// close_brace` walks the parent chain from the INNER loop stmt until
// it hits the OUTER loop body's CompoundStmt, and returns its
// RBracLoc. That is the uncompute anchor.
//
// Expected transform (see .expected.cpp for the byte-exact golden):
//
//     for (int i = 0; i < 3; ++i) {
//         for (int j = 0; j < 3; ++j) {
//             qbool t = a | b;
//             (void)t;
//         }
//         uncompute_or(t, a, b);    // hoisted to outer loop body, NOT
//     }                              //  to the function body
//
// Net effect: the inner loop body's per-iteration uncompute is
// amortized across `inner_iterations` executions (runs once per
// outer iteration, not once per inner iteration). The outer loop
// still runs N times, so `uncompute_or(t, a, b);` fires N times
// total — the optimization saved `(inner_iterations - 1) * N` calls.
//
// The `(void)t;` reader keeps the decl alive past PJ-4a dead-ancilla
// elimination.
//
// The stub is hermetic; only `operator|` need resolve for the MVP OR
// matcher to fire.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            qbool t = a | b;
            (void)t;
        }
    }
}
