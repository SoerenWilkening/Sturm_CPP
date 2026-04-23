// reversible_sig_multi_return.cpp — Phase Q / Q-3 (sturm-5kgu.4)
// negative signature fixture pinning the multi-return reject class
// for return-style `[[sturm::reversible]]` routines.
//
// Canonical Q-A reject shape (PRD §9 open question 3 + §4.1)
// ----------------------------------------------------------
// Q-A (`transpiler/src/return_to_out_param.cpp`,
// `synthesize_out_param_twin`) only normalizes reversible forwards
// whose body is a SINGLE `return <expr>;` statement. A body with two
// or more `return` statements — even if each is a simple return of a
// quantum expression — is rejected because the out-param twin's
// target depends on which return fires, and PRD §9's pointwise-
// semantics contract requires every mutation to have a single
// statement-level target.
//
// The Q-A classifier returns `synthesized=false` with reason
// `TwinRejectReason::MultiStatementBody` (when the body has more
// than one top-level statement) or `NotReturnStatement` (when the
// sole body statement is something other than a ReturnStmt) — see
// the enum in `return_to_out_param.hpp`. In either case, the
// downstream driver (R-C) is expected to surface the reject to the
// user as a hard compile-time diagnostic at the forward-function
// definition site, mirroring the posture of P-C's body-level rejects
// and Q-B's signature-level rejects.
//
// Q-A wiring status — PASS-THROUGH
// --------------------------------
// As of Phase Q the Q-A classifier's diagnostic surface is NOT yet
// wired into the production `transpile_consumer.cpp` driver (the Q-A
// module's reject reasons today bubble out to unit tests only —
// see `test_return_to_out_param.cpp`'s `test_multi_statement_body_
// rejects` case). Running `sturm-transpile` on this fixture therefore
// PASSES TODAY as a pure pass-through: the driver prepends the
// `AUTO-GENERATED` header, copies the body verbatim, exits zero,
// emits nothing on stderr. No reject diagnostic fires because the
// Q-A classifier is never consulted at the TU level.
//
// The Q-A classifier's behaviour on this shape is already pinned at
// unit-test level by `test_return_to_out_param.cpp`'s multi-statement
// reject cases. This fixture serves as the END-TO-END safety net:
// once the Q-A reject path wires into the consumer (follow-up R-C
// driver item), the companion CTest MUST flip from MUST_NOT_CONTAIN
// (today — no diagnostic is allowed to fire because the classifier
// is not wired) to MUST_CONTAIN with the `.expected.diag` substring
// below. The TODO(backend) in the CMakeLists wiring captures this
// flip.
//
// Expected diagnostic format (for the future MUST_CONTAIN flip)
// ------------------------------------------------------------
// See `reversible_sig_multi_return.expected.diag` for the golden
// substring the harness will require on stderr once the Q-A reject
// surface wires in. Per PRD §5.3 the diagnostic funnels through
// `SourceManager::getFileLoc(...)` so stderr cites
// `reversible_sig_multi_return.cpp:<line>:` where `<line>` is the
// forward-function definition line. A preamble-edit that shifts the
// definition line MUST bump the CMakeLists `EXPECTED_LINE` constant
// to match.
//
// Why the body has TWO returns
// ----------------------------
// The simplest shape that tests the multi-return reject is a body
// like `if (cond) return a; return b;`. The Q-A classifier walks the
// top-level `CompoundStmt`; two sibling `ReturnStmt` nodes yield
// `TwinRejectReason::MultiStatementBody`. An `if (cond) return a; else
// return b;` shape has a SINGLE `IfStmt` top-level statement, so Q-A
// reports `NotReturnStatement` instead — we want the `MultiStatement
// Body` path so the body explicitly contains two siblings. The first
// return is conditional on `c` (a classical `int` passed by value,
// Q-B-clean) so the C++ flow analysis accepts it as
// potentially-non-taken; the second is the unconditional terminator.
//
// Stub qbool
// ----------
// Minimal `sturm::qbool` stub that supports default-construct,
// copy-construct, and `bool`-conversion (the body does not actually
// use the bool-conversion — the stub is kept minimal to avoid
// dragging the `operator==` surface in).
namespace sturm {
class qbool {
public:
    qbool() = default;
    qbool(const qbool&) = default;
    qbool& operator=(const qbool&) = default;
};
} // namespace sturm
using sturm::qbool;

// Q-A multi-return reject: body contains two sibling `ReturnStmt`
// nodes at the top of the `CompoundStmt`. The single-return-only
// contract (PRD §4.1 / §9) is broken.
//
// The Q-A module's classifier returns
// `TwinRejectReason::MultiStatementBody` on this shape; the
// downstream diagnostic will cite this line (the FunctionDecl
// definition anchor).
[[clang::annotate("sturm::reversible")]]
qbool two_returns(int c) {
    qbool a;
    qbool b;
    if (c) return a;
    return b;
}

// Phase T T-2 (sturm-xrob.3): the PRD §9 Q2 error-emission gate fires
// only when the TU contains at least one `sturm::invert(&fd)` call
// site targeting this forward. We add the canonical `sturm::invert`
// stub + a call site below so condition (3) holds and the Q-A
// multi-return diagnostic is not swallowed by the silence guard.
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
} // namespace sturm

void invoke_two_returns_adjoint() {
    auto p = sturm::invert(&two_returns);
    (void)p;
}
