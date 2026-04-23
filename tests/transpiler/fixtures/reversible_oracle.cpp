// reversible_oracle.cpp — Phase P / P-5 (sturm-z2e8.6) positive
// fixture pinning the happy-path P-C (`validate_reversible_body`)
// contract.
//
// Canonical PRD §5.1 / P9c reversible-body shape
// ----------------------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body is a
// straight-line XOR cascade against `qbool` parameters is the textbook
// Phase P oracle shape. It clears every P9d reject class:
//
//   - no measurement call     (no `sturm::measure_qubit` / cast to
//                              classical),
//   - no classical I/O call   (no `printf` / stream op / `abort`),
//   - no unregistered callee  (the body only dispatches via qbool's
//                              `operator^=`, which is a primitive op-
//                              call that the validator's
//                              `OO_CaretEqual` guard skips),
//   - no while-loop           (straight-line, no `while` / `do-while`),
//   - no quantum-dependent    (no `if` / `?:` whose cond reads a
//     classical condition       quantum value, and the static type of
//                              `qbool` is NOT the cond).
//
// Running this fixture through `validate_reversible_body` therefore
// returns `{valid=true, reason=None, diagnostics_fired=0}` (pinned
// unit-test side by `test_matcher_reversible_validate.cpp`'s
// `test_happy_path_clean_body_passes`). The accompanying CTest drives
// the fixture through `sturm-transpile` in MUST_NOT_CONTAIN mode —
// stderr MUST NOT emit any of the five P9d diagnostic fragments, and
// the driver MUST exit zero.
//
// Driver wiring — PASS-THROUGH
// ----------------------------
// As of sturm-z2e8.6, `validate_reversible_body` is NOT yet wired into
// `transpile_consumer.cpp` (the R-C `DriveOptions::body_validator`
// hook is populated by a null caller — see the TODO(backend) around
// `matcher_reversible_drive.cpp`). Running `sturm-transpile` on this
// fixture is a pure pass-through: the driver prepends the two-line
// `AUTO-GENERATED` / `Source:` header, copies the body verbatim, exits
// zero, emits nothing on stderr. Independent of that wiring, the
// harness's MUST_NOT_CONTAIN invariant is stable — a clean positive
// body never trips the P-C validator even once it wires in.
//
// Pointer-array indirection on the LHS
// ------------------------------------
// `qbool::operator^=` takes `const qbool&` RHS and returns `qbool&`, so
// a bare `r ^= a` would be a `CXXOperatorCallExpr` whose arg0 is a
// `DeclRefExpr` on the parameter `r`. Phase A's PA-3 XorAssignCallback
// (`matcher_qbool_assign.cpp`) anchors on exactly that shape and would
// fire, landing an uncompute line in the body. Routing the LHS through
// `*regs[i] ^= *regs[j]` gives it a `UnaryOperator(ArraySubscriptExpr)`
// shape that PA-3 does not match, so the transpiler pass-through stays
// byte-clean. This is the same portable workaround the S-3 loop
// fixtures use (see `reversible_loop_ripple.cpp`).
//
// Stub qbool
// ----------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths, so we inline a minimal qbool stub whose
// `operator^=` overload is sufficient for the parser to accept the
// compound assignment. `operator^=` returns `void` — the cascade at
// the bottom of the fixture drops the `^=` return value, and the
// diagnostic CTest registered against this file asserts `stderr` is
// EMPTY after transpile (Phase T / T-4 sturm-xrob.5). Using the
// production `qbool&` return spelling would trip PM3-6's
// `report_dropped_quantum_return` warning and defeat the EMPTY_STDERR
// contract. The production Phase R / Phase S snapshot fixtures spell
// `qbool& operator^=(...)` because they are byte-compared on stdout
// and ignore stderr; this diagnostic-only fixture needs the tighter
// stderr hygiene, hence the `void` return spelling here.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    void operator^=(const qbool&) {}
};
} // namespace sturm
using sturm::qbool;

// Clean XOR oracle: three `qbool ^= qbool` gates in a straight line.
// The routine carries `[[sturm::reversible]]` and every statement
// clears all five P9d classes. `validate_reversible_body` returns
// `valid=true` and fires zero diagnostics.
[[clang::annotate("sturm::reversible")]]
void oracle(qbool& q0, qbool& q1, qbool& q2) {
    qbool* regs[3] = {&q0, &q1, &q2};
    // Straight-line XOR cascade — no loops, no branches, no calls
    // other than the primitive `operator^=` the validator's op-call
    // skip-list explicitly ignores.
    *regs[1] ^= *regs[0];
    *regs[2] ^= *regs[1];
    *regs[2] ^= *regs[0];
}
