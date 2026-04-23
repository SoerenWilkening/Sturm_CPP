// reversible_sig_const_ref_mutated.cpp — Phase Q / Q-3 (sturm-5kgu.4)
// negative signature fixture pinning the Q-B (iii) reject class:
// `const`-qualified reference parameter mutated inside a
// `[[sturm::reversible]]` routine body.
//
// Canonical Q-B reject shape (PRD §5.3 / §9 P9b)
// ----------------------------------------------
// A reversible routine that declares one of its parameters as a
// `const`-qualified reference to a quantum record (`const qbool&`,
// `const qint&`) AND whose body contains an assignment-shape op
// against that parameter MUST be rejected at the forward-function
// definition site. The Q-B matcher
// (`transpiler/src/matcher_reversible_signature.cpp`,
// `validate_reversible_signature`) fires
// `DiagContext::report_reversible_const_ref_mutated` at Error severity
// through the Phase P-D diagnostic family; the text's format fragment
// is:
//
//     reversible routine 'bad_const_ref' mutates const-qualified
//     reference parameter 'x'
//
// (pinned verbatim as the MUST_CONTAIN substring in the companion
// CTest that drives this fixture through
// `check_reversible_diagnostic.cmake`).
//
// Why the reject is mandatory
// ---------------------------
// P9b locks constness-declares-input-mutability: `const qbool&` / `const
// qint&` is the read-only-reference spelling. If the body mutates the
// referent the synthesized adjoint (R-A) has no way to un-mutate the
// caller's original — the reversible contract breaks at the parameter
// level. Normally a mutation of a const-qualified reference is a C++
// compile-time error, but user-defined conversion operators or overload
// sets can make the mutation shape syntactically valid on a custom
// quantum type (that is why the minimal stub below declares the
// `qbool& operator^=(const qbool&)` overload on a non-const receiver
// and the fixture body uses `const_cast` to route a mutation through
// the const reference). Q-B defends against that shape explicitly so
// the user gets a crisp Sturm-branded diagnostic instead of a generic
// template-instantiation traceback.
//
// Q-B wiring status — PASS-THROUGH
// --------------------------------
// As of Phase Q the Q-B matcher is NOT yet wired into the production
// `transpile_consumer.cpp` driver (the R-C `DriveOptions::
// signature_validator` hook is still null — see the TODO(backend)
// around `matcher_reversible_drive.cpp`). Running `sturm-transpile`
// on this fixture therefore PASSES TODAY as a pure pass-through: the
// driver prepends the `AUTO-GENERATED` header, copies the body
// verbatim, exits zero, emits nothing on stderr. No Q-B diagnostic
// fires because the matcher is never invoked.
//
// The Q-B validator's behaviour on this shape is already pinned at
// unit-test level by
// `transpiler/tests/test_matcher_reversible_signature.cpp`'s
// const-ref-mutation case, which calls `validate_reversible_signature`
// directly and asserts `reason=ConstRefParamMutated` +
// `diagnostics_fired=1`. This fixture serves as the END-TO-END
// safety net: once the Q-B validator wires into the consumer, the
// companion CTest MUST flip from MUST_NOT_CONTAIN (today — no
// diagnostic is allowed to fire because the matcher is not wired) to
// MUST_CONTAIN with the `.expected.diag` substring below. The
// TODO(backend) in the CMakeLists wiring captures this flip.
//
// Expected diagnostic format (for the future MUST_CONTAIN flip)
// ------------------------------------------------------------
// See `reversible_sig_const_ref_mutated.expected.diag` for the golden
// substring the harness will require on stderr once Q-B wires in.
// The diagnostic funnels through `SourceManager::getFileLoc(...)`, so
// stderr cites `reversible_sig_const_ref_mutated.cpp:<line>:` where
// `<line>` is the parameter declaration line (the Q-B matcher reports
// at the `ParmVarDecl` location, not the mutation site). A
// preamble-edit that shifts the parameter's declaration line MUST bump
// the CMakeLists `EXPECTED_LINE` constant to match.
//
// Stub qbool
// ----------
// Minimal `sturm::qbool` stub whose `operator^=` is declared on a
// non-const receiver — the mutation at line 78 below routes through
// a `const_cast` so the receiver ends up non-const at dispatch.
// P9b's rule still stands (the PARAMETER declaration is `const
// qbool&`, and Q-B reports against parameter declarations, not
// dispatch-site const-ness).
namespace sturm {
class qbool {
public:
    qbool() = default;
    qbool(const qbool&) = default;
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm
using sturm::qbool;

// Q-B (iii) reject: `const qbool& x` parameter mutated in body.
//
// The parameter is declared `const qbool&` — Q-B MUST reject at this
// line number. The `const_cast` at the mutation site is what lets the
// C++ front-end accept the code (`operator^=` is declared on a
// non-const receiver on the stub); without it the fixture would fail
// to parse and this file would never reach the Q-B matcher. Q-B does
// NOT look at the cast itself — it walks the parameter list and
// classifies `const qbool&` parameters whose `DeclRefExpr` appears as
// the peeled LHS of an assignment-shape op anywhere in the body.
[[clang::annotate("sturm::reversible")]]
void bad_const_ref(const qbool& x, const qbool& y) {
    // `const_cast` peels the `const`-qualification from the receiver
    // so the C++ front-end accepts the compound assignment. The Q-B
    // matcher's `ParameterMutationCollector::peel` unwraps the
    // `CXXConstCastExpr` before binding to the underlying
    // `DeclRefExpr(x)`, so this shape registers as a mutation of the
    // const-qualified reference parameter `x` (exactly what the
    // `report_reversible_const_ref_mutated` diagnostic reports).
    const_cast<qbool&>(x) ^= y;
}

// Phase T T-2 (sturm-xrob.3): the PRD §9 Q2 error-emission gate fires
// only when the TU contains at least one `sturm::invert(&fd)` call
// site targeting this forward. We add the canonical `sturm::invert`
// stub + a call site below so condition (3) holds and the Q-B
// const-ref-mutated diagnostic is not swallowed by the silence guard.
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
} // namespace sturm

void invoke_bad_const_ref_adjoint() {
    auto p = sturm::invert(&bad_const_ref);
    (void)p;
}
