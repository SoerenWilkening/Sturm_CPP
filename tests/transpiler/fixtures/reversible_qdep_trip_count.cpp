// reversible_qdep_trip_count.cpp — Phase S / S-4 (sturm-ha2k.5)
// negative loop fixture pinning the P9d quantum-dependent trip-count
// diagnostic contract from `matcher_reversible_validate.cpp` (P-C,
// sturm-z2e8.5).
//
// Canonical shape (PRD §5.2, plan §2.4 test surface)
// --------------------------------------------------
// A `[[clang::annotate("sturm::reversible")]]` routine whose for-loop
// trip count is derived from a quantum value — the classic example is
// `for (int i = 0; i < static_cast<int>(q_bound); ++i) body` where
// `q_bound` is a `qint_t`. PRD §5.2 rejects quantum-dependent trip
// counts for the same reason it rejects `while`-loops: the reversed
// iteration order has no well-defined starting `i` when the upper
// bound is only known at quantum-measurement time.
//
// Validator classification
// ------------------------
// The static_cast from qint_t -> int inside the for-cond slot is a
// quantum -> classical conversion. The current P-C validator
// classifies this via `ReversibleBodyValidator::check_quantum_to_
// classical_cast` (matcher_reversible_validate.cpp around line 304),
// which fires `DiagContext::report_reversible_measurement` at Error
// severity. The result carries `reason=Measurement` rather than a
// dedicated `ClassicalCond` / `WhileLoop` reject — the P-C module
// does not today special-case "cast in for-cond" as its own reject
// category. That's the honest state of the contract:
//
//   * The cast IS detected today (via the measurement classifier).
//   * The P9d diagnostic fires through `report_reversible_
//     measurement` — the closest pre-existing `report_reversible_*`
//     method for a qdep trip count. A follow-up validator widening
//     may introduce a dedicated `report_reversible_qdep_trip_count`
//     or route the for-cond case through `report_reversible_
//     classical_cond`; this fixture pins the current contract and
//     the accompanying CTest TODO notes the flip.
//
// Driver wiring — PASS-THROUGH
// ----------------------------
// Same story as the sibling `reversible_while_loop.cpp` fixture: as
// of sturm-ha2k.5, `validate_reversible_body` is wired into
// `matcher_reversible_drive.cpp`'s `DriveOptions::body_validator`
// hook, but `transpile_consumer.cpp` does NOT yet populate that
// hook. So `sturm-transpile` runs this fixture as a pass-through —
// the transpiler prepends the `AUTO-GENERATED` header, copies the
// body verbatim, and exits zero. No diagnostic fires on stderr.
//
// The validator-side behaviour is pinned at unit-test level by
// `transpiler/tests/test_matcher_reversible_validate.cpp`'s
// `test_reject_measurement_cast_qbool_to_bool` (analogous shape on a
// `qbool -> bool` cast, not in a loop header). A dedicated
// for-header unit test lands together with the driver glue flip, at
// which point the companion CTest below switches from snapshot mode
// to diagnostic mode and asserts the Error text on stderr + non-zero
// exit.
//
// Why pointer-array indirection in the body
// -----------------------------------------
// The body uses `*regs[0] ^= *regs[1];` — the same UnaryOperator
// (ArraySubscriptExpr) LHS shape the S-3 positive loop fixtures
// (`reversible_loop_ripple.cpp` et al.) use, so Phase H's PH-3
// outer-var-guard and PE / PM3-6 dropped-quantum-return matchers do
// NOT anchor on it (they only match DeclRefExpr LHS). That keeps
// the transpile a clean byte-for-byte pass-through today — the only
// delta against the source is the two-line `AUTO-GENERATED` /
// `Source:` header the emitter prepends. Under the current
// transpiler the entry-level matchers are dormant and the fixture
// reliably reproduces the same output across runs.
//
// Why a qint_t<4> stub and not a bare qbool
// -----------------------------------------
// The transpiler recognises three quantum record spellings:
// `qbool`, `qint`, and `qint_t` (see `is_quantum_record` in
// matcher_reversible_validate.cpp). An `int`-returning
// `explicit operator int()` on a `qint_t<N>` instantiation is the
// natural way to spell "measure this bound". A `qbool` would work
// too (via `bool -> int` widening), but `qint_t<4>` is closer to
// real user code that hits this trap — "I have a superposed upper
// bound; let me loop up to it".
//
// Stub qint_t / qbool
// -------------------
// Minimal hermetic stubs — `sturm-transpile` uses a
// FixedCompilationDatabase without include paths. `qint_t<N>` is a
// ClassTemplateSpecialization so `is_quantum_record` matches it via
// `getNameAsString() == "qint_t"`. The `explicit operator int()`
// overload is load-bearing: without it the static_cast<int> would
// not compile, and the validator would never see the cast expr.
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t(int) {}  // P4a classical -> quantum constructor.
    explicit operator int() const { return 0; }
};
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
using sturm::qbool;
using qint = sturm::qint_t<4>;

[[clang::annotate("sturm::reversible")]]
void bad_qdep_trip(qbool& q0, qbool& q1, qint q_bound) {
    qbool* regs[2] = {&q0, &q1};
    // The P9d validator MUST cite the line carrying the
    // `static_cast<int>(q_bound)` expression — the `CXXStaticCastExpr`
    // node from `qint_t<4> -> int` is the measurement anchor. Today
    // the validator fires `report_reversible_measurement` on it; a
    // future widening may reclassify it under a dedicated
    // `report_reversible_qdep_trip_count` surface, at which point
    // the CMakeLists MUST_CONTAIN string flips.
    for (int i = 0; i < static_cast<int>(q_bound); ++i) {
        *regs[0] ^= *regs[1];
    }
}
