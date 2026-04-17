// dead_ancilla_reference.cpp — Phase J / PJ-4e hand-written control for
// the dead-ancilla elimination gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the PJ-4a peephole matcher rewrites
// dead_ancilla_runtime.cpp.  The demo body is deliberately byte-
// similar to the transpiler's expected output: the PJ-4a-eliminated
// `qbool dead = a | b;` line is simply ABSENT here (that is the whole
// point of the elimination — the decl produces no gates, so the
// hand-written reference omits it entirely).  The sentinel
// `target.flip()` call is the same as in the runtime fixture; it
// guarantees the captured gate stream is non-empty on both sides
// (so the M12 harness's `if (ref_stream.empty()) return 1;` guard
// does not false-positive this pair as a trivially-equal comparison).
//
// Having a hand-written control checked in directly is what turns
// PJ-4e into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and any
// drift in either the transpiler's PJ-4a elimination, the
// `eliminated_stmt_ranges` backstop, or the downstream MVP OR
// matcher's early-return path shows up as a mismatch in the captured
// gate stream.
//
// ODR note
// --------
// The harness links this TU together with dead_ancilla_runtime.cpp.
// Both define `demo(const qbool&, const qbool&, qbool&)` inside their
// own namespace to avoid collisions at the demo level.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` and the real `sturm::qbool::flip()` so the one X
// record actually lands in the captured gate stream.  The umbrella
// header is included directly — no `__has_include` guard is necessary
// here because this file never passes through the transpiler (CMake
// compiles it as-is) and the test target always sees `sturm`'s
// include directory.

#include "sturm/sturm.hpp"
// The umbrella `sturm/sturm.hpp` only pulls in the uncompute free-
// function API.  The real `qbool::flip()` lives in qbool_ops.hpp, so
// include it explicitly here — without it the `target.flip()` below
// would fail to compile.
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

namespace m12_dead_ancilla_reference {

// Hand-written realisation of the PJ-4a elimination.  The single gate
// emission is:
//   sentinel (`target.flip();`) — one X(target) record via
//   emit_X_lifted → execute_gate(ctx, STURM_GATE_X, ...).  `a` and
//   `b` are bound to the demo's arguments but never touched at the
//   gate level, because the dead `qbool = a | b` decl the runtime
//   fixture carries is ELIMINATED by the PJ-4a matcher and therefore
//   simply absent here.  Total: one X gate — the stream the transpiled
//   companion must match byte-for-byte.  No ancilla is allocated on
//   either side; the `dead` qbool in the runtime fixture is eliminated
//   before its RHS `a | b` can allocate a fresh ancilla, which is
//   exactly the invariant PJ-4e is checking.
//
// Why we OMIT the `qbool dead = a | b;` line entirely
// ---------------------------------------------------
// The pre-J MVP OR path would allocate a fresh ancilla qubit, emit
// three gates (CX + CX + CCX) into that ancilla, and need a three-gate
// adjoint at scope close — that's a six-gate-plus-ancilla shape that
// the PJ-4a peephole explicitly collapses OUT of existence when the
// decl has zero readers.  The eliminated transpiled output emits
// ZERO gates and allocates ZERO ancillas for the dead decl.  To
// match that slice byte-for-byte the reference must NOT write the
// decl — doing so would emit six gates on this side vs zero on the
// transpiled side, a trivially-mismatched comparison that does not
// exercise the elimination.
//
// `const sturm::qbool&` inputs preserve the caller's qubit indices
// across the demo boundary; `sturm::qbool& target` mirrors the
// runtime fixture's non-const `target` operand (the `flip()` call
// mutates the qbool's state via emit_X_lifted, which requires a
// non-const member invocation).  No qbool is default-constructed
// here — the caller owns all three qubits and the reference body
// never allocates.
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& target) {
    // Silence unused-parameter warnings for `a` and `b`: the reference
    // body deliberately does not touch them (the dead `qbool = a | b`
    // decl the runtime fixture carries is eliminated, so its RHS
    // operands never flow through to any gate emission here).  The
    // runtime fixture keeps them in the signature so the two demos are
    // ABI-compatible at the link level — the harness takes one
    // function pointer shape and dispatches to both.
    (void)a;
    (void)b;

    // Sentinel — guarantees the captured gate stream is non-empty so
    // the harness's empty-reference guard does not false-positive
    // this pair.  Emits exactly one X(target) record via
    // emit_X_lifted.
    target.flip();
}

} // namespace m12_dead_ancilla_reference
