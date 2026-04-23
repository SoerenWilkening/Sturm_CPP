// reversible_synth_runtime.cpp — Phase R / R-5 + Phase T / T-5 transpiler
// INPUT fixture for the straight-line automatic-adjoint-synthesis
// gate-stream equivalence pair.
//
// Fifteenth gate-equivalence pair.  Whereas the S-5 triple
// (reversible_loop_{ripple,bit_reversal,adder}_runtime.cpp) pins down
// Phase S's B11 LOOP-REVERSAL contract, this fixture pins down the
// earlier Phase R (B10) STRAIGHT-LINE adjoint-emission contract: a
// `[[sturm::reversible]]` forward routine whose body is a sequence of
// compound-assignment statements (no for/while loops, no if/WHEN) MUST
// be paired with a reverse-statement-order adjoint that T-1's wiring
// auto-synthesises into a sibling `__demo_adj` + STURM_REGISTER_ADJOINT
// binding.  The one pair in this batch (reversible_synth) mirrors the
// R-6 roundtrip test's `parity_cascade` fixture in
// `tests/test_invert.cpp` — same four-statement XOR cascade shape,
// but operating on `sturm::qbool` references so the M12 gate-stream
// byte-compare can observe the synthesised adjoint end-to-end.
//
// Auto-synthesis status (post T-1 wiring)
// ---------------------------------------
// T-1 (sturm-xrob.2) has wired matcher_reversible_drive into
// TranspileConsumer.  For every `[[clang::annotate("sturm::reversible")]]`
// forward the transpiler now auto-emits:
//
//   * a sibling `__demo_adj` function (inside the same namespace as
//     the forward), whose body is the reverse-statement-order adjoint
//     produced by R-A `adjoint_emitter`,
//
//   * a `STURM_REGISTER_ADJOINT(<qualified fn>, <qualified adj>)`
//     line at END-OF-FILE (global scope), using fully-qualified
//     names so the macro expansion lands in `::sturm::_detail::
//     adjoint_of` — the template the runtime `invert(fn)` helper
//     keys on.
//
// T-5 (sturm-xrob.6) is the m12 gate-equivalence harness for this
// pair.  The demo body here contains ONLY the forward cascade;
// auto-synthesis injects the adjoint as a separate function + global-
// scope registration.  The harness's `run_and_capture_reversible_loop_4`
// captures three independent invocations of `demo()` and compares the
// resulting gate stream against the reference fixture's — byte-
// identical per-invocation streams (4 CX records each) is the test's
// pass/fail signal.
//
// Demo shape (forward only; adjoint auto-synthesised)
// ---------------------------------------------------
// [[clang::annotate("sturm::reversible")]]
// void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
//     qbool* regs[4] = {&q0, &q1, &q2, &q3};
//     // Forward: 4-statement straight-line XOR cascade (NO loops).
//     *regs[1] ^= *regs[0];   // CX(q0, q1)
//     *regs[2] ^= *regs[1];   // CX(q1, q2)
//     *regs[3] ^= *regs[2];   // CX(q2, q3)
//     *regs[3] ^= *regs[0];   // CX(q0, q3)
// }
// // Auto-synthesised by matcher_reversible_drive:
// // void __demo_adj(qbool& q0, qbool& q1, qbool& q2, qbool& q3) { ... }
// // (outside the namespace, at global scope, via end-of-file anchor)
// // STURM_REGISTER_ADJOINT(m12_reversible_synth_transpiled::demo,
// //                        m12_reversible_synth_transpiled::__demo_adj);
//
// Why the `[[clang::annotate("sturm::reversible")]]` attribute
// ------------------------------------------------------------
// The attribute is the user-facing opt-in for Phase R/S automatic
// adjoint synthesis (the `[[sturm::reversible]]` spelling is the
// surface syntax; `[[clang::annotate("sturm::reversible")]]` is the
// low-level portable form the matchers anchor on — see
// transpiler/src/reversible_attribute.cpp).  T-1 wires
// matcher_reversible_drive into the consumer so the attribute now
// drives the R-A / R-B emitters end-of-TU, producing the sibling
// `__demo_adj` body + `STURM_REGISTER_ADJOINT` line described above.
//
// Why the pointer-array indirection is load-bearing
// -------------------------------------------------
// Each qbool `operator^=` call takes `const qbool&` RHS and returns
// `qbool&`, so a bare `q1 ^= q0` is a CXXOperatorCallExpr whose arg0
// is a DeclRefExpr on the parameter `q1`.  PA-3's
// XorAssignCallback (matcher_qbool_assign.cpp:45) anchors on that
// exact shape (`declRefExpr().bind("lhs")` at PH-3 matcher_outer_var_-
// guard.cpp:456 and PA-3 matcher_qbool_assign.cpp:54) — so PA-3 would
// register a QOperation on the enclosing QScope for every one of our
// forward statements and the uncompute pass would plant inverses at
// `demo`'s close brace, inflating the gate stream.  Routing the
// mutation through a local pointer array (`*regs[i] ^= *regs[j]`)
// changes the LHS AST node from DeclRefExpr to UnaryOperator(
// ArraySubscriptExpr), which does NOT match PA-3's DeclRefExpr
// anchor — the transpiler passes the body through unchanged.  This
// is the same portable workaround the S-5 loop fixtures use (see
// reversible_loop_ripple_runtime.cpp's "Why the pointer-array
// indirection is load-bearing" section for the fuller PH-3 story —
// the PA-3 story is structurally identical because the LHS binding
// pattern is shared).  When Phase S's matcher_outer_var_guard edit
// (sturm-ha2k.3) wires the PA-3 handoff to adjoint_emitter under
// `[[sturm::reversible]]` routines (the remaining integration
// detail), the pointer-array indirection can be removed and the
// demo body becomes the more idiomatic `q1 ^= q0;` form.
//
// Gate-stream witness
// -------------------
// With STURM_BACKEND_ENABLED and an APPEND-mode BackendContext installed,
// `qbool::operator^=` emits `emit_CX_lifted(ctx, other.qubits[0],
// qubits[0])` — a single CX record per `^=`.  The demo's forward
// cascade emits four CX records — CX(q0,q1), CX(q1,q2), CX(q2,q3),
// CX(q0,q3) — in that order.  Per `demo()` invocation: 4 gates.
//
// The M12 harness runs `demo()` 3 times per capture (3 independent
// payloads per pair), each against the same caller-supplied qubit
// indices (the harness's `make_non_owning` qbool views hold qubit IDs
// stable across invocations within a capture).  Total captured stream:
// 12 CX records per capture — the reference fixture (which likewise
// contains only the forward cascade in `demo`) produces an identical
// stream.  The byte-compare therefore pins the forward gate sequence
// plus the stable qubit-index invariant across multi-invocation runs.
//
// The auto-synthesised `__demo_adj` body is a sibling function the
// test does NOT invoke — it exists so downstream `invert(demo)` call
// sites (e.g. the T-3 roundtrip test in tests/test_invert.cpp) can
// look it up via the global-scope `STURM_REGISTER_ADJOINT` line.  The
// present M12 test does not exercise that lookup path; it pins only
// the forward gate stream.
//
// ODR note
// --------
// The M12 harness links this TU together with
// `reversible_synth_reference.cpp`.  Both define a
// `demo(qbool&, qbool&, qbool&, qbool&)` function; wrapping each in a
// dedicated namespace avoids ODR collision.  Inside the namespace a
// `using sturm::qbool;` brings the qbool type into local scope so the
// DSL pattern reads as it would to the end user.  The auto-
// synthesised `STURM_REGISTER_ADJOINT` emission lands at END-OF-FILE
// (global scope, outside the namespace) per the T-5 emission split —
// placing it inside the namespace would (a) fail to parse because
// `::demo` and `::__demo_adj` do not exist at the global scope the
// `::` prefix in the macro resolves to, and (b) land the
// specialization in `m12_reversible_synth_transpiled::sturm::_detail::
// adjoint_of` rather than `::sturm::_detail::adjoint_of`, which is
// the template `invert(fn)` consults.
//
// Why the `__has_include` guard
// -----------------------------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths (see transpiler/src/main.cpp).  A bare
// `#include <sturm/sturm.hpp>` would therefore fail to resolve and the
// `qbool` symbol would never appear in the AST.  Gating the include on
// `__has_include` takes the stub branch during the transpile step
// (providing a minimal `sturm::qbool` with `operator^=`) and takes the
// real-header branch during the downstream compile (so the `^=` calls
// emit real CX records).  The stub shape mirrors the one used by the
// other m12 runtime fixtures.
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
} // namespace sturm
#endif

namespace m12_reversible_synth_transpiled {
using sturm::qbool;

// Phase R straight-line adjoint-synthesis input.  Four qbool references
// + a local pointer-array alias so each `*regs[i] ^= *regs[j]` lands as
// a UnaryOperator(ArraySubscriptExpr) on the LHS — see top-of-file
// prose on why this matters for PA-3 pass-through.  The
// `[[clang::annotate("sturm::reversible")]]` attribute is the user-
// facing opt-in for automatic adjoint synthesis; T-1's wiring drives
// R-A + R-B off this attribute to emit the sibling `__demo_adj` +
// global-scope `STURM_REGISTER_ADJOINT` registration.
//
// Per-invocation gate stream (four CX records):
//   Forward:  CX(q0,q1), CX(q1,q2), CX(q2,q3), CX(q0,q3)
//
// The reverse-statement-order adjoint is emitted as a separate
// `__demo_adj` function by matcher_reversible_drive; the present M12
// test does not invoke it.  See the "Gate-stream witness" section in
// the top-of-file prose for the full accounting.
[[clang::annotate("sturm::reversible")]]
void demo(qbool& q0, qbool& q1, qbool& q2, qbool& q3) {
    qbool* regs[4] = {&q0, &q1, &q2, &q3};

    // Forward: 4-statement straight-line XOR cascade (NO loops).  The
    // fourth statement re-touches regs[3] with regs[0] so the forward
    // gate stream is distinguishable from the S-5 ripple triple's
    // 3-statement left-to-right sweep.  Each statement is a single
    // CXXOperatorCallExpr whose LHS is `*regs[i]` — a UnaryOperator
    // dereference of an ArraySubscriptExpr — which keeps PA-3 off
    // (see top-of-file prose for the full rationale).
    *regs[1] ^= *regs[0];
    *regs[2] ^= *regs[1];
    *regs[3] ^= *regs[2];
    *regs[3] ^= *regs[0];
}

} // namespace m12_reversible_synth_transpiled
