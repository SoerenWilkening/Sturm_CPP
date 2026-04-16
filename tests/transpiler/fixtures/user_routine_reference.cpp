// user_routine_reference.cpp — Phase I / PI-7 hand-written control for
// the user-routine gate-stream equivalence test.
//
// This file is the manually-maintained counterpart to what the M12
// harness links against after the transpiler emits the PI-2 / PI-3 /
// PI-4 rewrite into user_routine_runtime.cpp.  The demo body is
// deliberately byte-similar to the transpiler's expected output: a
// forward call to the registered routine followed by its adjoint,
// spelled verbatim in LIFO order:
//
//     ur_rotate_fwd_reference(tmp, in, 3);
//     ur_rotate_adj_reference(tmp, in, 3);
//
// Having a hand-written control checked in directly is what turns
// PI-7 into a meaningful capstone — the test is then a cross-check
// between two independent realisations of the same circuit, and any
// drift in either the transpiler's output, the PI-0 invert()
// dispatch, or the runtime flip() lowering shows up as a mismatch in
// the captured gate stream.
//
// ODR note
// --------
// The harness links this TU together with user_routine_runtime.cpp.
// Both define `demo(const qbool&)` inside their own namespace to
// avoid collisions at the demo level.  The forward / adjoint free
// functions live at global scope (per the nested-name-specifier
// constraint of `STURM_REGISTER_ADJOINT`), so the names include a
// per-TU suffix (`_reference` here vs `_runtime` in the companion)
// to keep the two TUs linkable in the same binary.
//
// Preprocessor contract: we want this TU to compile against the real
// `sturm::qbool` / `sturm::invert` so the forward + adjoint each emit
// three X gates under an empty control stack.  The umbrella header is
// included directly — no `__has_include` guard is necessary here
// because this file never passes through the transpiler (CMake
// compiles it as-is) and the test target always sees `sturm`'s
// include directory.

#include "sturm/sturm.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/routines/invert.hpp"

// ── Forward user routine (global scope) ─────────────────────────────────────
// Identical body to the companion runtime fixture — just a renamed
// symbol so both TUs can coexist in the same binary.  Three flips on
// `out` emit three X gates on `out`'s qubit index against an empty
// control stack.
void ur_rotate_fwd_reference(sturm::qbool& out,
                             const sturm::qbool& in,
                             int k) {
    (void)in;
    for (int i = 0; i < k; ++i) {
        out.flip();
    }
}

// ── Hand-written adjoint (global scope) ─────────────────────────────────────
// Same shape as the forward — a self-inverse X-chain is its own
// adjoint.  Same three X gates as the forward, which is the precise
// LIFO counterpart the transpiler plants via PI-4.
void ur_rotate_adj_reference(sturm::qbool& out,
                             const sturm::qbool& in,
                             int k) {
    (void)in;
    for (int i = 0; i < k; ++i) {
        out.flip();
    }
}

// ── Register the (forward, adjoint) pair ────────────────────────────────────
// Registering this pair is not strictly required for the reference
// fixture — the demo below calls the adjoint directly, never through
// `sturm::invert(...)`.  We include the registration anyway so the
// symmetry with the companion runtime fixture is explicit, and so any
// future harness extension that uses `sturm::invert(&ur_rotate_fwd_reference)`
// resolves through the real PI-0 trait table.
STURM_REGISTER_ADJOINT(ur_rotate_fwd_reference, ur_rotate_adj_reference)

namespace m12_user_routine_reference {

// Hand-written realisation of the PI-2 / PI-3 / PI-4 rewrite.  The
// six gate emissions are:
//   forward (`ur_rotate_fwd_reference(tmp, in, 3);`)
//     → three X(tmp) gates on `tmp`'s qubit index.
//   adjoint (`ur_rotate_adj_reference(tmp, in, 3);` — the LIFO
//     counterpart the transpiler plants via
//     `invert(ur_rotate_fwd_runtime)(tmp, in, 3);`)
//     → three more X(tmp) gates on the same qubit index.
// Total: six X gates — the stream the transpiled companion must
// match byte-for-byte.
//
// `const sturm::qbool&` input preserves the caller's qubit index
// across the demo boundary; `tmp` is a fresh default-constructed
// qbool whose `ensure_qubit()` allocates from the harness-reset
// QubitPool, so the allocated index matches between the reference
// and runtime captures.  The destructor at the demo's `}` is
// release-only (flip() does not set `uncompute_`) and contributes
// no additional gates.
void demo(const sturm::qbool& in) {
    sturm::qbool tmp;
    tmp.ensure_qubit();
    ur_rotate_fwd_reference(tmp, in, 3);
    ur_rotate_adj_reference(tmp, in, 3);
}

} // namespace m12_user_routine_reference
