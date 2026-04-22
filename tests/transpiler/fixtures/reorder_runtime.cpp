// reorder_runtime.cpp — Phase M / PM5-8 transpiler INPUT fixture.
//
// Gate-equivalence capstone for the peephole reorder matcher.  The
// fixture exercises the Phase E / PE-4 compound-flatten plus the
// Phase A / PA-3 xor-assign matchers and runs the result through the
// PM5-5 peephole reorder pass as the LAST matcher in the pipeline.
//
// Demo shape
// ----------
//   qbool r = (a | b) | c;    // PE-4: OR(__stu_t0, a, b); OR(r, __stu_t0, c)
//   y ^= x;                    // intermediate XOR_ASSIGN — bit-disjoint
//                              // from {a, b, c, __stu_t0, r}
//   x ^= r;                    // XOR_ASSIGN on the OR's result
//
// The PM5-5 matcher observes scope.ops in source order:
//   ops[0] = OR(__stu_t0, a, b)  — A candidate (wrong kind: OR not AND)
//   ops[1] = OR(r, __stu_t0, c)  — A candidate (wrong synthetic prefix)
//   ops[2] = XOR_ASSIGN(y, x)    — B / C candidate
//   ops[3] = XOR_ASSIGN(x, r)    — C candidate
// No triple satisfies Gate 1 (A must be QOpKind::AND), so the reorder
// does NOT fire.  The fixture therefore proves the stronger property:
// PM5 is a TRUE NO-OP on any program that does not present its trigger
// shape, preserving the gate stream byte-for-byte regardless of
// whether the reorder matcher is registered.  The hand-written
// reorder_reference.cpp counterpart spells the same circuit by
// hand; both emit identical gate streams.
//
// Gate-stream witness
// -------------------
// Three iterations of the OR expansion + their adjoints PLUS the two
// user-written XOR_ASSIGNs + their adjoints:
//   - `(a | b)`: CX(a, q_t0) + CX(b, q_t0) + CCX(a, b, q_t0)
//     (three gates; forward OR)
//   - `r = __stu_t0 | c`: three more gates (CX + CX + CCX)
//   - `y ^= x`: CX(x, y) (self-adjoint, one gate — qbool-qbool)
//   - `x ^= r`: CX(r, x) (self-adjoint, one gate — qbool-qbool)
//   - Adjoint `uncompute_or(r, __stu_t0, c)`: three gates
//   - Adjoint `uncompute_or(__stu_t0, a, b)`: three gates
//   - LIFO adjoint `y ^= x`: one gate
//   - LIFO adjoint `x ^= r`: one gate
//
// Not every backend surface emits each of the listed gates; the
// m12 harness compares the gate stream the runtime AND reference
// fixtures emit against the installed APPEND-mode context.  Both
// fixtures run against the SAME backend, so structural parity of
// the streams is the load-bearing invariant — not a specific gate
// count.
//
// ODR note
// --------
// The harness links this TU together with reorder_reference.cpp.
// Both define `demo(const qbool&, const qbool&, const qbool&,
// qbool&, qbool&)` inside their own namespace to avoid an ODR
// collision.
//
// Why the `__has_include` guard
// -----------------------------
// sturm-transpile runs Clang with a FixedCompilationDatabase that
// carries no include paths.  A bare `#include <sturm/sturm.hpp>`
// would therefore fail to resolve and the `qbool` type would never
// appear in the AST, silently disabling the PE-4 compound-flatten
// + PA-3 xor-assign matchers.  The include is gated on
// `__has_include`: during transpile the preprocessor takes the
// stub branch (which provides a minimal `sturm::qbool` with
// `operator|` and `operator^=`); during the downstream compile the
// real headers are available so the forward `a | b` and the
// injected `uncompute_or(...)` + `^=` self-adjoints resolve to the
// real gate-emitting implementations.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
#  include "sturm/qtypes/qbool.hpp"
#  include "sturm/qtypes/qbool_ops.hpp"
#else
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
#endif

namespace m12_reorder_transpiled {
using sturm::qbool;

// Through the PM5 matcher's Gate 1 filter, no triple in this scope
// satisfies the AND-kind prefix + matching XOR_ASSIGN consumer
// shape, so the peephole reorder does not fire.  The pipeline
// emits the standard PE-4 flatten + MVP OR uncompute + PA-3 LIFO
// self-adjoint decomposition.  The reference fixture reproduces
// the same circuit by hand; both gate streams match.
void demo(const qbool& a,
          const qbool& b,
          const qbool& c,
          qbool& x,
          qbool& y) {
    qbool r = (a | b) | c;
    y ^= x;
    x ^= r;
    (void)r;
}

} // namespace m12_reorder_transpiled
