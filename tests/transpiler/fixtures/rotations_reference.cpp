// rotations_reference.cpp — Phase N / PN-8 hand-written control for
// the rotation gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness will link against after the transpiler processes
// rotations_runtime.cpp.  The demo body is deliberately byte-similar
// to the transpiler's expected output: four forward rotations on
// `a.theta()` / `a.theta()` / `b.phi()` / `b.phi()`, one depth-1
// WHEN-guarded forward rotation on `a.theta()` paired with an
// explicit in-body inverse `a.theta() -= 0.5;`, and four LIFO
// sign-flipped dual rotations at the demo body's close brace.  The
// PN-4 uncompute pass would emit these same five inverses (one
// in-body, four at scope close) against rotations_runtime.cpp; the
// reference spells them verbatim.
//
// Having a hand-written control checked in directly is what turns
// PN-8 into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and
// any drift in either the transpiler's PN-2 rotation matcher, the
// PN-4 uncompute emission, or the runtime ThetaProxy / PhiProxy /
// emit_RY_lifted / emit_RZ_lifted decomposition shows up as a
// mismatch in the captured gate stream.
//
// Gate-stream witness
// -------------------
// With STURM_BACKEND_ENABLED and an APPEND-mode BackendContext
// installed, emit_RY_lifted / emit_RZ_lifted dispatch as follows:
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
// The runtime's self-dual ThetaProxy::operator-= / PhiProxy::operator-=
// at include/sturm/qtypes/qint_core.hpp:305,372 dispatches `-delta`
// through the same emit path, so writing `a.theta() -= 0.3` and
// `a.theta() += -0.3` would emit identical gate records (sign
// carried in the `param` field).  The reference therefore spells
// `a.theta() -= 0.3` (the sign-flipped DSL adjoint) for the dual of
// `a.theta() += 0.3`, matching the transpiler's injection verbatim.
//
// Qubit-index determinism
// -----------------------
// The M12 harness resets QubitPool via ScopedAppendContext before
// each capture, so allocations start from 0 on every invocation.
// With the demo declaring `qbool c(0.5)` first (qbool.hpp's
// probabilistic constructor calls QubitPool::allocate() at line 61
// — c.qubits[0] = 0), then `a.theta() += 0.3` auto-promoting `a`
// (a.qubits[0] = 1), then `b.phi() += 0.7` auto-promoting `b`
// (b.qubits[0] = 2), the captured stream's qubit triples are
// deterministic across runs.  Both the runtime (transpiled) and
// reference fixtures observe the SAME pool state on entry, so the
// streams share identical qubit indices.
//
// Note that `qbool c(0.5)` writes a `prepare` record to the
// CounterSink (via current_sink()->prepare at qbool.hpp line 62) —
// NOT to the backend GateIR.  The captured GateRecord stream
// therefore omits any "prepare" entry.  This is the designed
// separation: prep is a preparation primitive whose record is
// sink-level metadata, not a quantum gate on the hardware timeline.
//
// ODR note
// --------
// The harness links this TU together with rotations_runtime.cpp.
// Both define a `demo()` function inside their own namespace to
// avoid an ODR collision.
//
// Preprocessor contract: we want this TU to compile against the
// real `sturm::qbool` / `sturm::qint_t` / `WHEN` so the forward
// rotations and the hand-written inverses actually land as
// RY / RZ / CRY records in the captured gate stream.  The umbrella
// header is included directly — no `__has_include` guard is
// necessary here because this file never passes through the
// transpiler (CMake compiles it as-is) and the test target always
// sees `sturm`'s include directory.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"

namespace m12_rotations_reference {

// Hand-written realisation of the Phase N rotation lowering.  The
// shape mirrors the expected transpiler output verbatim:
//
//   Four outer-scope forward rotations   →
//       `a.theta() += 0.3;` `a.theta() -= 0.1;`
//       `b.phi()   += 0.7;` `b.phi()   -= 0.2;`
//   Depth-1 WHEN-guarded two forward rotations + two inverses in LIFO
//   (all inside the same WHEN body) →
//       `WHEN(c) {
//            a.theta() += 0.5;  b.phi() += 0.4;
//            b.phi()   -= 0.4;  a.theta() -= 0.5;
//        }`
//   LIFO sign-flipped duals at scope close →
//       `b.phi()   += 0.2;`  (dual of b.phi()   -= 0.2)
//       `b.phi()   -= 0.7;`  (dual of b.phi()   += 0.7)
//       `a.theta() += 0.1;`  (dual of a.theta() -= 0.1)
//       `a.theta() -= 0.3;`  (dual of a.theta() += 0.3)
//
// Each dual flips the `+=` ↔ `-=` sign; the runtime's self-dual
// ThetaProxy::operator-= / PhiProxy::operator-= at
// include/sturm/qtypes/qint_core.hpp:305,372 carries the sign into
// the `param` field of the emitted Ry/Rz record, so both DSL
// spellings (`a.theta() -= 0.3` and `a.theta() += -0.3`) produce
// byte-identical GateRecord entries.
//
// Auto-promotion of fully-classical qint_t<1> registers happens on
// the FIRST rotation to each register — the M15/M16 path in
// ThetaProxy::operator+= / PhiProxy::operator+= allocates
// a.qubits[0] / b.qubits[0] and emits the Ry/Rz record.  Because
// both `a` and `b` enter the demo with `value=0`, no X gate is
// emitted during auto-promote (the auto-promote loop only emits an
// X for bits whose classical value is 1).
//
// `qbool c(0.5)` allocates c.qubits[0] via QubitPool but does NOT
// emit a gate to ctx->ir (the probabilistic constructor calls
// current_sink()->prepare at qbool.hpp line 62, which writes to
// the CounterSink only).  The captured GateRecord stream therefore
// omits any "prepare" entry — see the top-of-file prose.
void demo() {
    sturm::qint_t<1> a;
    sturm::qint_t<1> b;
    sturm::qbool c(0.5);

    // Four outer-scope forward rotations.  Each emits exactly one
    // Ry / Rz record; the first rotation on each register also
    // auto-promotes via the M15/M16 path (allocating a.qubits[0] /
    // b.qubits[0]) without emitting any X gate because the
    // classical `value` is 0 on entry.
    a.theta() += 0.3;
    a.theta() -= 0.1;
    b.phi()   += 0.7;
    b.phi()   -= 0.2;

    // Depth-1 WHEN-guarded forward rotations + in-body inverses.
    // The WHEN(c) guard pushes c.qubits[0] onto
    // ctx->control_stack, so the forward `a.theta() += 0.5;` emits
    // a CRy(q_c, q_a, 0.5) record and the forward `b.phi() += 0.4;`
    // emits a CRz(q_c, q_b, 0.4) record.  The hand-written LIFO
    // inverses `b.phi() -= 0.4;` and `a.theta() -= 0.5;` inside the
    // SAME WHEN body emit matching CRz(q_c, q_b, -0.4) and
    // CRy(q_c, q_a, -0.5) records — preserving the B5 single-
    // control-scope invariant for controlled rotations and
    // exercising BOTH the CRy and CRz controlled-rotation gate
    // kinds in a single captured stream.  The WHEN body's close
    // brace then pops the control stack.
    WHEN(c) {
        a.theta() += 0.5;
        b.phi()   += 0.4;
        b.phi()   -= 0.4;
        a.theta() -= 0.5;
    }

    // Four sign-flipped duals at the demo body's close brace, in
    // LIFO (reverse-source) order.  Each pair cancels to identity
    // on its target register.
    b.phi()   += 0.2;
    b.phi()   -= 0.7;
    a.theta() += 0.1;
    a.theta() -= 0.3;
}

} // namespace m12_rotations_reference
