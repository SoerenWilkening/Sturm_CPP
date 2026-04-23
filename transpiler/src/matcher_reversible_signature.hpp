// matcher_reversible_signature.hpp — Phase Q Q-B (sturm-5kgu.3):
// parameter-list signature enforcement for `[[sturm::reversible]]`
// user routines.
//
// Purpose
// -------
// Phase Q of the automatic-adjoint-synthesis roadmap (see
// `docs/implementation_plan_automatic_adjoint_synthesis.md` §2.2 Q-B
// and `docs/prd_automatic_adjoint_synthesis.md` §5.3 + §9 P9b) walks
// the parameter list of every `[[sturm::reversible]]` forward and
// rejects three classes of signature that the downstream straight-
// line / loop-reversal adjoint emitter (R-A / S-A) cannot honour:
//
//   1. Pass-by-pointer of a quantum type
//        `void fn(qbool* x) { ... }`
//      Rejected because the transpiler models the adjoint's parameter
//      binding through references (P9b: "a parameter passed by
//      non-const reference may be mutated, and the synthesized adjoint
//      un-mutates it"). Pointer parameters would require the transpiler
//      to reason about aliasing / nullness, which is out of scope for
//      the MVP reversible surface. The user's fix is to switch to the
//      canonical `qbool&` spelling.
//
//   2. Non-const by-value quantum parameter mutated in the body
//        `void fn(qbool x) { x ^= a; }`
//      A by-value quantum parameter is a local copy per P9b; mutating
//      it cannot propagate back to the caller and, more importantly,
//      the copy's gate stream is emitted to the same backend as the
//      caller's qubits — leaking compute the adjoint pass has no
//      plausible way to undo (there is no "out" slot to un-XOR). The
//      user's fix is either to declare the parameter `const qbool x`
//      (read-only copy, no mutation allowed), to pass by `qbool&`
//      (mutable, adjoint tracks it), or to remove the mutation.
//
//   3. `const`-qualified reference parameter mutated in the body
//        `void fn(const qbool& x) { x ^= a; }`
//      Normally this is a C++ compile-time error because `const qbool&`
//      cannot bind to a non-const `operator^=`. We still defend against
//      it because (a) user-defined conversion operators or overloads
//      could make the mutation shape syntactically valid on a custom
//      quantum type, and (b) a clear Sturm-branded diagnostic at the
//      reversible routine definition site is strictly more useful than
//      a generic Clang "qualifier discarded" message buried under a
//      template instantiation stack. The user's fix is to drop the
//      `const` qualifier or remove the mutation.
//
// Each rejection fires through the Phase P-D (`DiagContext`)
// `report_reversible_*_param_*` family. All three methods fire at
// Error severity per P9d — a reversible routine whose signature
// cannot be honoured is a hard compile error at the forward-function
// definition site so the user can find and fix it at the canonical
// audit anchor.
//
// Body-only scope for mutation detection
// --------------------------------------
// The mutation detector only walks the forward routine's own body
// (`fd->getBody()`), not nested lambdas / class templates. This
// matches the P-C validator's posture: each reversible forward is
// validated in isolation. The detector's set of "assignment-shape"
// operators mirrors `matcher_when_operand_mutation.cpp`'s (xor-assign,
// plain copy-assign, compound `+=`/`-=`/`*=`/`/=`/`%=`, bitwise
// `|=`/`&=`) plus pre/post `++`/`--` so every parameter-mutating
// shape the P9b contract cares about is covered.
//
// Single entry point
// ------------------
// The module exposes a single free function:
//
//     ReversibleSignatureResult validate_reversible_signature(
//         const clang::FunctionDecl* fd,
//         clang::ASTContext& ctx,
//         DiagContext& diag);
//
// Callers (the R-C driver's pipeline, the Q-3 fixtures, and the
// unit tests) receive a `valid` bool + a first-reject reason. Every
// offending parameter fires one diagnostic through `DiagContext` so
// the user sees every violation in one compile pass. `reason` holds
// the first reject encountered.
//
// Reject-with-diagnostic contract
// -------------------------------
// Every P9b class the validator detects fires one diagnostic via
// `DiagContext`. A null FD or a non-reversible FD is a silent
// caller-side skip (returns a dedicated reject reason without firing
// a diagnostic) — mirrors the P-C validator's opt-in posture.
//
// Interaction with P-C
// --------------------
// Q-B is orthogonal to P-C: this module checks the *parameter list
// shape*, not the body's P9d constructs (measurement, I/O,
// while-loop, etc.). The R-C driver runs P-C first (reject body
// constructs) then Q-B (reject signature shapes); the two
// validators have disjoint report families so diagnostic text does
// not collide across them.
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers. Plan §2.2
// Q-B budgets 240 LOC for the implementation file; the header stays
// small.

#ifndef STURM_TRANSPILE_MATCHER_REVERSIBLE_SIGNATURE_HPP
#define STURM_TRANSPILE_MATCHER_REVERSIBLE_SIGNATURE_HPP

#include <string_view>

namespace clang {
class ASTContext;
class FunctionDecl;
} // namespace clang

namespace sturm::transpile {

class DiagContext;

/// Why `validate_reversible_signature` returned `valid=false`. The
/// first reject encountered during the parameter walk wins —
/// downstream rejects are still reported to `DiagContext` so the user
/// sees every diagnostic in one compile pass, but only the first
/// feeds this field. Tests and the R-C driver compare against the
/// exact spellings returned by `to_string(ReversibleSigRejectReason)`.
enum class ReversibleSigRejectReason {
    /// Signature passes every Q-B class — safe for R-A / S-A to consume.
    None,
    /// `fd == nullptr`. Silent; no diagnostic fires.
    NullDecl,
    /// `fd` is not carrying `[[clang::annotate("sturm::reversible")]]`.
    /// Silent; no diagnostic fires — the validator is opt-in.
    NotReversible,
    /// Q-B (i): a parameter has pointer type to a quantum record
    /// (e.g. `qbool*`, `sturm::qint*`). Fired via
    /// `report_reversible_pointer_param`.
    PointerParam,
    /// Q-B (ii): a non-const by-value quantum parameter is mutated
    /// inside the body. Fired via
    /// `report_reversible_value_param_mutated`.
    ValueParamMutated,
    /// Q-B (iii): a `const`-qualified reference-to-quantum parameter
    /// is mutated inside the body. Fired via
    /// `report_reversible_const_ref_mutated`.
    ConstRefParamMutated,
};

/// Human-readable spelling of a `ReversibleSigRejectReason`. Stable
/// across runs — tests compare against these exact strings.
std::string_view to_string(ReversibleSigRejectReason reason);

/// Result of a single `validate_reversible_signature` invocation.
struct ReversibleSignatureResult {
    /// `true` iff the forward's parameter list cleared every Q-B
    /// class. When false, `reason` carries the first reject
    /// encountered; every reject (first and subsequent) is also
    /// fired through `DiagContext` so the user sees every offending
    /// parameter in one compile pass.
    bool valid = false;

    /// First reject reason encountered during the parameter walk.
    /// `None` when `valid == true`. Additional rejects do NOT
    /// overwrite this field — they are surfaced exclusively through
    /// `DiagContext`.
    ReversibleSigRejectReason reason = ReversibleSigRejectReason::NullDecl;

    /// Number of diagnostics the validator fired through
    /// `DiagContext` during this call. Tests use this to assert
    /// that the validator does not double-report the same parameter
    /// across multiple Q-B classes (each parameter votes for at most
    /// one reject reason).
    unsigned diagnostics_fired = 0;
};

/// Walk the parameter list of a `[[sturm::reversible]]` forward
/// routine and reject any signature that fails the Q-B contract.
/// For each offending parameter, report the matching diagnostic
/// through `DiagContext`'s `report_reversible_*_param*` family.
///
/// Pipeline:
///
///   1. Null / attribute guard. A null FD or a non-reversible FD is
///      a silent reject (no diagnostic fires).
///
///   2. Parameter walk. For each `ParmVarDecl p`:
///
///        - `p` has pointer type to a quantum record
///                                                → PointerParam reject.
///        - `p` has non-const by-value quantum
///          type AND the body mutates `p`         → ValueParamMutated reject.
///        - `p` has `const`-qualified reference
///          type to a quantum record AND the
///          body mutates `p`                      → ConstRefParamMutated reject.
///
///      Each parameter votes for at most one reject reason. The walk
///      visits every parameter regardless of the first hit so every
///      offending site fires its diagnostic in one pass.
///
///   3. After the walk, `valid = (reason == None)`.
///
/// Contract:
///   - `fd == nullptr` ⇒ `valid=false`, `reason=NullDecl`, no
///     diagnostic fired.
///   - `fd` missing `[[sturm::reversible]]` ⇒ `valid=false`,
///     `reason=NotReversible`, no diagnostic fired.
///   - Otherwise: the walk visits every parameter; every offending
///     parameter fires one diagnostic via `DiagContext`; `reason`
///     carries the first reject encountered.
///
/// The function never mutates AST state — it only reads the parameter
/// list and the body subtree. Safe to call speculatively from the R-C
/// driver (via its signature-validator hook) as well as from tests.
ReversibleSignatureResult validate_reversible_signature(
    const clang::FunctionDecl* fd,
    clang::ASTContext& ctx,
    DiagContext& diag);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_REVERSIBLE_SIGNATURE_HPP
