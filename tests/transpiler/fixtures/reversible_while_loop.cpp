// reversible_while_loop.cpp — Phase S / S-4 (sturm-ha2k.5) negative loop
// fixture pinning the P9d `report_reversible_while_loop` diagnostic
// from `matcher_reversible_validate.cpp` (P-C, sturm-z2e8.5).
//
// Canonical shape (PRD §5.2)
// --------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body
// contains a `while (cond) body`. PRD §5.2 row 3 locks the contract:
// a while-loop inside a reversible routine MUST be rejected because
// unbounded trip counts are not invertible by B11 loop reversal —
// the reversed iteration order has no well-defined starting `i`
// without a compile-time trip count.
//
// Under the P-C validator (matcher_reversible_validate.cpp::
// VisitWhileStmt / VisitDoStmt) the node fires one
// `report_reversible_while_loop(loc, forward_name)` at Error severity
// through `DiagContext` (diag_context.cpp line 274 onward). The
// validator's `ReversibleValidationResult` reports
// `reason=WhileLoop` with `diagnostics_fired=1`.
//
// Current driver wiring — PASS-THROUGH
// ------------------------------------
// As of sturm-ha2k.5, R-3's `DriveOptions::body_validator` hook in
// `matcher_reversible_drive.cpp` is still populated by a null caller
// (see the TODO(backend) around line 140 of that file); the
// production driver entry point `transpile_consumer.cpp` does not yet
// instantiate `validate_reversible_body`. Running `sturm-transpile`
// on this fixture therefore PASSES TODAY — the transpile is a
// pass-through that prepends the `AUTO-GENERATED` header and copies
// the body verbatim (no diagnostic fires, exit code zero).
//
// The validator-side behaviour is already pinned at unit-test level
// by `transpiler/tests/test_matcher_reversible_validate.cpp`'s
// `test_reject_while_loop` / `test_reject_do_while_loop` cases, which
// call `validate_reversible_body` directly against a while-loop AST
// and check the `WhileLoop` reject reason + the Error count. This
// fixture serves as the END-TO-END safety net: once the driver glue
// lands (follow-up sub-item of Phase S or a later phase), the
// companion CTest MUST flip to assert the Error on stderr + non-zero
// exit. The TODO in the CMakeLists wiring captures this flip.
//
// Why the body reads `r ^= a;` inside the loop
// --------------------------------------------
// The while-loop reject fires on the `WhileStmt` node itself — the
// body content is only used to make the routine non-trivial so the
// P9d walker cannot short-circuit. `r ^= a;` is the simplest
// quantum-mutation statement; nested body parsing is exercised by
// the positive S-3 fixtures. The statement uses qbool's `operator^=`
// (declared below on the minimal stub) to avoid pulling the full
// sturm runtime.
//
// Stub qbool
// ----------
// `sturm-transpile` runs Clang with a FixedCompilationDatabase that
// carries no include paths, so we inline a minimal qbool stub whose
// `operator^=` overload is sufficient for the parser to accept the
// compound assignment inside the while-body. Same stub shape as the
// S-3 positive loop fixtures.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_while(qbool& r, qbool a) {
    int i = 0;
    // The P9d validator MUST cite this line — the WhileStmt node is
    // the match anchor. Harness keeps the line number stable by
    // pinning it via the CMake EXPECTED_LINE argument (not yet live
    // — pass-through today, see file preamble). A reshuffle of the
    // preamble must bump the CMakeLists `EXPECTED_LINE` constant
    // accordingly.
    while (i < 3) {
        r ^= a;
        ++i;
    }
}
