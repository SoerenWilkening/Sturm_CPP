// fuse_xor_and_reference.cpp — Phase J / PJ-1i hand-written control for
// the zero-ancilla fusion gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the PJ-1d peephole matcher rewrites
// fuse_xor_and_runtime.cpp.  The demo body is deliberately byte-
// similar to the transpiler's expected output: two `ccnot_inplace`
// calls (one forward fused emission + one self-adjoint uncompute),
// but spelled via `primitive_AND(ctx, a_idx, b_idx, x_idx)` directly
// against the 18-gate sink — NOT via `lazy_expr` / `operator&` on
// qbool.  The pre-K `lazy_expr` path materialises `a & b` as a four-
// gate CCX-plus-ancilla sequence (allocate ancilla, CCX(a,b,tmp), ...)
// which would NOT match the SINGLE-CCX fused shape the PJ-1d matcher
// collapses the pair into; using it here would give a trivially-
// mismatched comparison that does not exercise the fusion.  By
// bypassing the lazy wrappers and calling `primitive_AND` (which is
// exactly one `execute_gate(ctx, STURM_GATE_CCX, ...)` call — see
// include/sturm/backend/primitives.hpp:34-37) we emit the EXACT gate
// stream the transpiled fixture produces: two CCX(a, b, x) records on
// the three caller-supplied qubit indices, no ancilla allocation.
//
// Having a hand-written control checked in directly is what turns
// PJ-1i into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same fused circuit, and
// any drift in either the transpiler's PJ-1d rewrite, the PJ-1c
// render case, the `ccnot_inplace` helper body, or the underlying
// `primitive_AND` sink shows up as a mismatch in the captured gate
// stream.
//
// ODR note
// --------
// The harness links this TU together with fuse_xor_and_runtime.cpp.
// Both define `demo(const qbool&, const qbool&, qbool&)` inside their
// own namespace to avoid collisions at the demo level.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` and the real `sturm::primitive_AND` so the two CCX
// records actually land in the captured gate stream.  The umbrella
// header is included directly — no `__has_include` guard is necessary
// here because this file never passes through the transpiler (CMake
// compiles it as-is) and the test target always sees `sturm`'s
// include directory.

#include "sturm/sturm.hpp"
// primitive_AND lives in `sturm/backend/primitives.hpp` — it's a thin
// wrapper over `execute_gate(ctx, STURM_GATE_CCX, ...)` that emits
// exactly one CCX record with the three caller-supplied qubit indices
// as operands.
#include "sturm/backend/primitives.hpp"
#include "sturm/core/context.hpp"
#include "sturm/qtypes/qbool.hpp"

#include <cassert>
#include <cstdint>

namespace m12_fused_reference {

// Hand-written realisation of the PJ-1d / PJ-1c rewrite.  The two gate
// emissions are:
//   forward fused (`ccnot_inplace(x, a, b);` the PJ-1d matcher
//     would emit — spelled as primitive_AND(ctx, a, b, x) here)
//     → one CCX(a, b, x) record on the three caller-supplied qubit
//       indices.
//   self-adjoint uncompute (the PJ-1c render case's second
//     `ccnot_inplace(x, a, b);` call — spelled via a second
//     primitive_AND(ctx, a, b, x) here because CCX is its own inverse)
//     → one more CCX(a, b, x) record on the same three indices.
// Total: two CCX gates — the stream the transpiled companion must
// match byte-for-byte.  No ancilla is allocated on either side; the
// qubit indices come straight from the caller-supplied qbools.
//
// Why we call primitive_AND directly (not via `operator&` on qbool)
// ----------------------------------------------------------------
// The pre-K `lazy_expr` / `qbool_ops.hpp` materialisation path for
// `a & b` allocates a fresh ancilla qubit, emits `CCX(a, b, tmp)`
// into that ancilla, then needs a second CCX at scope close to
// uncompute — that's a four-gate-plus-ancilla shape that the PJ-1d
// peephole explicitly collapses OUT of existence.  The fused
// transpiled output emits ONLY two CCX records on the original three
// qubits (no ancilla).  To match that stream byte-for-byte the
// reference must avoid the lazy materialisation entirely — which is
// exactly what `primitive_AND(ctx, a, b, x)` does: one CCX, three
// explicit qubit operands, no allocation.
//
// `const sturm::qbool&` inputs preserve the caller's qubit indices
// across the demo boundary; `sturm::qbool& x` mirrors the runtime
// fixture's non-const `x` target (the `^=` in the runtime demo
// requires a non-const lvalue, and `ccnot_inplace` takes its target
// by non-const reference).  No qbool is default-constructed here —
// the caller owns all three qubits and the reference body never
// allocates.
void demo(const sturm::qbool& a, const sturm::qbool& b, sturm::qbool& x) {
    // Resolve the active BackendContext (the harness installs an
    // APPEND-mode context in ScopedAppendContext before invoking
    // demo()).  Each primitive_AND call goes through execute_gate
    // against this sink — one STURM_GATE_CCX record with qubits
    // (a, b, x), matching the fused output of PJ-1d.
    sturm_backend_context_t* raw = sturm_get_thread_context();
    assert(raw && "fuse_xor_and_reference: no BackendContext installed");
    sturm::BackendContext& ctx = *raw;

    // Extract the caller-supplied qubit indices.  All three qbools are
    // expected to carry valid indices (the harness allocates them via
    // QubitPool::allocate() before invoking demo() — see the
    // run_and_capture_fused helper in test_gate_equivalence.cpp).
    assert(a.qubits[0] >= 0 && "fuse_xor_and_reference: a must hold a qubit");
    assert(b.qubits[0] >= 0 && "fuse_xor_and_reference: b must hold a qubit");
    assert(x.qubits[0] >= 0 && "fuse_xor_and_reference: x must hold a qubit");
    const uint32_t qa = static_cast<uint32_t>(a.qubits[0]);
    const uint32_t qb = static_cast<uint32_t>(b.qubits[0]);
    const uint32_t qx = static_cast<uint32_t>(x.qubits[0]);

    // Forward fused emission — what the PJ-1d QReplacement writes in
    // place of `qbool __t = a & b; x ^= __t;`.
    sturm::primitive_AND(ctx, qa, qb, qx);

    // Self-adjoint uncompute — what the PJ-1c render case plants
    // before the enclosing scope's close brace.  CCX is its own
    // inverse, so the same primitive is reused for the adjoint.
    sturm::primitive_AND(ctx, qa, qb, qx);
}

} // namespace m12_fused_reference
