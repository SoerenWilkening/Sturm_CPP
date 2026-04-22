// rotations_runtime.cpp — Phase N / PN-8 transpiler INPUT fixture.
//
// Tenth gate-equivalence pair.  Whereas the earlier PG-7 / PH-6a /
// PH-6b / PI-7 / PJ-1i / PJ-4e / PJ-3h / PM5-8 fixtures pin down the
// OR matcher's per-iteration, per-branch, or nested-WHEN scope
// lowering, the user-routine rewrite, the zero-ancilla fusion,
// the dead-ancilla elimination, the uncompute-hoisting, and the
// peephole-reorder no-op, this fixture pins down the Phase N
// CONTINUOUS-PARAMETER ROTATIONS (P5 items 2 and 3).  The PN-2
// matchers anchor on `q.theta() += d;` / `q.theta() -= d;` /
// `q.phi() += d;` / `q.phi() -= d;` (each on a qint_t<W> LHS with
// a double-valued RHS) and stage a QOperation whose kind is one of
// THETA_ADD_ASSIGN_CONST / THETA_SUB_ASSIGN_CONST /
// PHI_ADD_ASSIGN_CONST / PHI_SUB_ASSIGN_CONST on the enclosing
// QScope; the PN-4 uncompute arms emit the sign-flipped inverse
// inline (two-character flip: `+=` ↔ `-=`) before the enclosing
// scope's closing brace.  The runtime's self-dual
// ThetaProxy::operator-= / PhiProxy::operator-= at
// include/sturm/qtypes/qint_core.hpp:305,372 dispatches `-delta`
// through the same emit_RY_lifted / emit_RZ_lifted path, so the
// forward + inverse GateRecord stream cancels to identity.  The
// hand-written companion rotations_reference.cpp spells the same
// sign-flipped LIFO adjoint chain verbatim; the M12 harness
// test_gate_equivalence.cpp byte-compares the two captured gate
// streams.
//
// Demo shape
// ----------
// void demo() {
//     qint_t<1> a;
//     qint_t<1> b;
//     qbool c(0.5);
//     a.theta() += 0.3;      // PN-2a forward — auto-promote a.qubits[0]
//     a.theta() -= 0.1;      // PN-2b forward
//     b.phi()   += 0.7;      // PN-2c forward — auto-promote b.qubits[0]
//     b.phi()   -= 0.2;      // PN-2d forward
//     WHEN(c) {
//         a.theta() += 0.5;  // PN-2a forward (depth-1 WHEN control) → CRy
//         b.phi()   += 0.4;  // PN-2c forward (depth-1 WHEN control) → CRz
//         // transpiler injects (LIFO inside WHEN body):
//         //   b.phi()   -= 0.4;
//         //   a.theta() -= 0.5;
//     }
//     // transpiler injects (LIFO / reverse-source order at demo close):
//     //   b.phi()   += 0.2;   (dual of b.phi()   -= 0.2)
//     //   b.phi()   -= 0.7;   (dual of b.phi()   += 0.7)
//     //   a.theta() += 0.1;   (dual of a.theta() -= 0.1)
//     //   a.theta() -= 0.3;   (dual of a.theta() += 0.3)
// }
//
// Why demo() takes no arguments
// -----------------------------
// Phase N rotations auto-promote fully-classical qint_t<W> registers
// on first rotation via the M15/M16 auto-promote path in
// ThetaProxy::operator+= / PhiProxy::operator+= at
// include/sturm/qtypes/qint_core.hpp:256-265 / 319-332: the first
// rotation on an all-unallocated register calls
// QubitPool::allocate() for each bit, sets super_mask, and falls
// through to emit_RY_lifted / emit_RZ_lifted.  Later rotations on
// the same register find qubits already allocated and skip the
// auto-promote prologue.  The qbool c(0.5) constructor similarly
// calls QubitPool::allocate() (see qbool.hpp line 61) but DOES
// NOT emit a gate to ctx->ir — it calls current_sink()->prepare()
// which writes to the CounterSink, NOT the backend GateIR.  This
// is the designed separation: prep is a preparation primitive
// whose "record" is sink-level metadata, not a quantum gate on
// the hardware timeline.
//
// Because every qubit comes from the QubitPool (which the harness
// resets via ScopedAppendContext before each capture), the index
// assignments are deterministic across runs: c.qubits[0] = 0,
// a.qubits[0] = 1, b.qubits[0] = 2.  Both the runtime and reference
// fixtures observe the SAME pool state on entry, so the captured
// gate streams share identical qubit triples.
//
// Gate-stream witness
// -------------------
// With STURM_BACKEND_ENABLED and a BackendContext installed,
// emit_RY_lifted / emit_RZ_lifted dispatch as follows:
//   depth 0 → RY / RZ (1-qubit, param=delta)
//   depth 1 → CRY / CRZ (2-qubit [ctrl, target], param=delta)
//   depth >= 2 → assertion failure (not yet supported; Phase G AND-
//               fold ensures depth <= 1 at the matcher)
//
// Captured stream (twelve gates total):
//   [0]  RY (q_a=1, 0.3)                // a.theta() += 0.3
//   [1]  RY (q_a=1, -0.1)               // a.theta() -= 0.1
//   [2]  RZ (q_b=2, 0.7)                // b.phi()   += 0.7
//   [3]  RZ (q_b=2, -0.2)               // b.phi()   -= 0.2
//   [4]  CRY(q_c=0, q_a=1, 0.5)         // WHEN(c) { a.theta() += 0.5; }
//   [5]  CRZ(q_c=0, q_b=2, 0.4)         // WHEN(c) { b.phi()   += 0.4; }
//   [6]  CRZ(q_c=0, q_b=2, -0.4)        // PN-4 in-body inverse (LIFO)
//   [7]  CRY(q_c=0, q_a=1, -0.5)        // PN-4 in-body inverse (LIFO)
//   [8]  RZ (q_b=2, 0.2)                // PN-4 LIFO dual of [3]
//   [9]  RZ (q_b=2, -0.7)               // PN-4 LIFO dual of [2]
//   [10] RY (q_a=1, 0.1)                // PN-4 LIFO dual of [1]
//   [11] RY (q_a=1, -0.3)               // PN-4 LIFO dual of [0]
//
// Each forward/inverse Ry(±d) pair cancels to identity on the
// register.  The load-bearing observable is the byte-identical
// GateRecord stream the runtime (transpiled) and reference
// (hand-written) fixtures produce; semantically both realisations
// leave the register in its entry state.
//
// Depth-1 WHEN-guarded rotation (B5 invariant)
// --------------------------------------------
// Principle B5 ("Primitives have uncontrolled and singly-controlled
// forms only.") combined with Phase G's AND-fold (which collapses
// nested WHEN chains to depth <= 1 before the rotation matcher ever
// sees them) means the transpiler only ever emits an uncontrolled
// inverse `a.theta() -= 0.5;` or a depth-1 WHEN-guarded inverse
// `WHEN(c) { a.theta() -= 0.5; }`.  This fixture exercises exactly
// ONE depth-1 WHEN-guarded rotation (on `a.theta()` controlled by
// `c`); a depth-2 nesting would be a test that this fixture is
// wrong — Phase G AND-fold would refuse to fire on a rotation body,
// and the PN-2 matcher would see a depth-1 control at the rotation
// site.  The in-body inverse lands INSIDE the WHEN body because the
// PN-2 matcher anchored the QOperation on the inner WHEN body's
// QScope; the PN-4 uncompute pass emits the inverse when THAT scope
// closes — preserving the B5 single-control-scope invariant for
// controlled rotations.
//
// ODR note
// --------
// The M12 harness links this TU together with rotations_reference.cpp.
// Both define a `demo()` function.  Wrapping each in a dedicated
// namespace avoids ODR collision at link time; inside the namespace a
// `using sturm::qbool; using sturm::qint_t;` brings the types into the
// local scope so the DSL pattern reads as it would to the end user.
//
// Why the `__has_include` guard
// -----------------------------
// sturm-transpile runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and
// the `qbool` / `qint_t` types would never appear in the AST,
// silently disabling the PN-2 rotation matchers.  To make this TU
// parseable by the transpiler AND compilable at runtime, the include
// is gated on `__has_include`: during transpile the preprocessor
// takes the stub branch (which provides minimal `sturm::qbool` +
// `sturm::qint_t<W>` with the four rotation compound-assigns plus
// the exact WHEN macro from include/sturm/control/when.hpp), and
// during the downstream compile the real headers are available so
// both the forward rotations and the transpiler-injected inverses
// resolve to the real gate-emitting implementations.
//
// The stub shape mirrors the one used by the hermetic PN-3 snapshot
// fixtures under tests/transpiler/fixtures/{theta,phi}_{add,sub}_
// const.cpp: same class layout, same nested ThetaProxy / PhiProxy
// with operator+= / operator-=.  The transpiler cares about the
// textual pattern `q.theta() += d;` / `q.phi() += d;` on a qint_t
// LHS — not the semantic behaviour of the proxies.
#if defined(__has_include) && __has_include(<sturm/sturm.hpp>)
#  include <sturm/sturm.hpp>
// The umbrella `sturm/sturm.hpp` pulls in the uncompute free-
// function API.  The real qint_t rotation proxies live in
// qint_core.hpp (via the qint.hpp umbrella) and the WHEN macro
// lives in control/when.hpp, so include them explicitly here.
#  include "sturm/qtypes/qbool.hpp"
#  include "sturm/qtypes/qint.hpp"
#  include "sturm/control/when.hpp"
#else
namespace sturm {
template <int W>
class qint_t {
public:
    struct ThetaProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };
    struct PhiProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };
    ThetaProxy theta() { return ThetaProxy{}; }
    PhiProxy   phi()   { return PhiProxy{}; }
};
class qbool {
public:
    qbool() {}
    explicit qbool(double) {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};
namespace detail {
inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }
struct WhenCapture { WhenCapture() = default; };
inline qbool& make_when_guard(qbool& q) { return q; }
} // namespace detail
} // namespace sturm

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())
#endif

namespace m12_rotations_transpiled {
using sturm::qbool;
using sturm::qint_t;

// Phase N rotation input: four rotations spanning all four directions
// (theta add, theta sub, phi add, phi sub) plus one depth-1 WHEN-
// guarded rotation.  The PN-2 matchers stage four QOperations on
// the demo's body QScope and one more on the inner WHEN body's
// QScope; the PN-4 uncompute pass emits five sign-flipped inverses
// in LIFO order — four at the demo body's close brace (dual of the
// four outer rotations in reverse source order), one INSIDE the
// WHEN body (dual of the depth-1 rotation).  The WHEN body's inverse
// is emitted before the WHEN body closes so the controlled-rotation
// scope covers both directions (B5 invariant: depth-1 controlled
// rotations have their inverse in the same controlled scope).
void demo() {
    qint_t<1> a;
    qint_t<1> b;
    qbool c(0.5);

    // Four outer-scope rotations.  Each stages a THETA_ADD_ASSIGN_CONST /
    // THETA_SUB_ASSIGN_CONST / PHI_ADD_ASSIGN_CONST / PHI_SUB_ASSIGN_CONST
    // QOperation on the demo body's QScope; the verbatim RHS source text
    // is captured via Lexer::getSourceText.  The PN-4 uncompute arms
    // emit the sign-flipped duals in LIFO order before the demo's close
    // brace — see the top-of-file "Captured stream" prose.
    a.theta() += 0.3;
    a.theta() -= 0.1;
    b.phi()   += 0.7;
    b.phi()   -= 0.2;

    // Depth-1 WHEN-guarded rotations — two forward rotations under
    // the same WHEN guard so the captured stream exercises BOTH the
    // CRy (theta) and CRz (phi) controlled-rotation gate kinds.  The
    // PN-2a / PN-2c matchers anchor their QOperations on the inner
    // WHEN body's QScope (not the enclosing demo body's scope), so
    // the PN-4 uncompute pass emits the sign-flipped duals INSIDE the
    // WHEN body in LIFO order — preserving the B5 single-control-
    // scope invariant for controlled rotations.  A second nested
    // WHEN here would be a test that this fixture is wrong: Phase G
    // AND-fold would refuse to fire on a rotation body, and the PN-2
    // matchers would see a depth-1 control at the rotation sites.
    WHEN(c) {
        a.theta() += 0.5;
        b.phi()   += 0.4;
    }
}

} // namespace m12_rotations_transpiled
