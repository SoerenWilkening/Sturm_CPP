// reversible_reject_while_loop.cpp — Phase P / P-5 (sturm-z2e8.6)
// negative fixture pinning the P9d (iv) while-loop reject class.
//
// Canonical PRD §5.2 / P9d (iv) shape
// -----------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose body
// contains a `while (...)` (or `do { ... } while (...)`) is rejected
// at the forward-function definition site. Unbounded trip counts are
// not invertible by B11 loop reversal — the reversed iteration order
// has no well-defined starting `i` without a compile-time trip count.
//
// Under the P-C validator (`matcher_reversible_validate.cpp` line ~164
// `VisitWhileStmt`) the node fires one
// `DiagContext::report_reversible_while_loop` at Error severity. The
// validator's `ReversibleValidationResult` reports
// `reason=WhileLoop` with `diagnostics_fired=1` (pinned unit-test side
// by `test_matcher_reversible_validate.cpp`'s
// `test_reject_while_loop` / `test_reject_do_while_loop`).
//
// Expected diagnostic format
// --------------------------
// See the sibling `.expected.diag` golden for the locked-down
// substring the harness forbids today (MUST_NOT_CONTAIN — driver
// pass-through) and will require once P-C wires into the consumer
// (MUST_CONTAIN + EXPECTED_LINE at the `while (...)` line).
//
// Relationship to `reversible_while_loop.cpp`
// -------------------------------------------
// The sibling fixture `reversible_while_loop.cpp` was added by S-4
// (sturm-ha2k.5) as a Phase-S snapshot probe — it exercises the same
// P9d (iv) reject reason but is registered as a snapshot CTest
// (pass-through byte-compare today). This fixture is the P-5 PAIR:
// registered via `check_reversible_diagnostic.cmake` with the
// MUST_NOT_CONTAIN/MUST_CONTAIN contract flip sequence. Keeping both
// lets us retire the MUST_NOT_CONTAIN invariant (flip to
// MUST_CONTAIN) independently of the snapshot's byte-compare.
//
// Driver wiring — PASS-THROUGH
// ----------------------------
// As of sturm-z2e8.6, `validate_reversible_body` is not wired into
// `transpile_consumer.cpp`; the R-C `DriveOptions::body_validator`
// hook is null. Running `sturm-transpile` on this fixture therefore
// PASSES TODAY as a pure pass-through — the driver prepends the
// `AUTO-GENERATED` / `Source:` header, copies the body verbatim,
// exits zero, emits nothing on stderr. The companion CTest asserts
// MUST_NOT_CONTAIN against the `.expected.diag` substring; once the
// validator wires in, that CTest flips to MUST_CONTAIN.
//
// Stub qbool
// ----------
// Minimal stub with the operator^= overload the while-body
// statement needs to parse. The same shape the Phase R/S fixtures
// use.
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
void bad_while_reject(qbool& r, qbool a) {
    int i = 0;
    // P-C MUST cite this line — the `WhileStmt` node is the match
    // anchor. A preamble edit that shifts this line number MUST bump
    // the CMakeLists `EXPECTED_LINE` constant once the
    // MUST_NOT_CONTAIN CTest flips to MUST_CONTAIN mode.
    while (i < 3) {
        r ^= a;
        ++i;
    }
}
