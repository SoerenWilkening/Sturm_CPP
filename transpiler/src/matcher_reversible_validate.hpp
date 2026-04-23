// matcher_reversible_validate.hpp — Phase P P-C (sturm-z2e8.5): the
// P9d validation pass for `[[sturm::reversible]]` user routines.
//
// Purpose
// -------
// Phase P of the automatic-adjoint-synthesis roadmap (see
// `docs/implementation_plan_automatic_adjoint_synthesis.md` §2.1 P-C
// and `docs/prd_automatic_adjoint_synthesis.md` §5.0 / §9 item P9d)
// walks the body of every reversible forward and rejects any
// construct that cannot be honoured by the downstream straight-line
// adjoint emitter (R-A) or the loop-reversal pass (S-A). Per PRD §9
// P9d the five reject reasons are:
//
//   1. measurement                       — quantum → classical
//                                          conversion has no inverse.
//   2. classical I/O / observable side-  — `printf`, `std::cout`, etc.
//      effect                              can't be reversed.
//   3. unregistered callee               — a body-level CallExpr whose
//                                          callee is neither
//                                          `[[sturm::reversible]]` nor
//                                          bound in the PI-1
//                                          `RoutineRegistry` can't be
//                                          uncomputed.
//   4. while-loop (including `do-while`) — unbounded trip counts are
//                                          not invertible by B11 loop
//                                          reversal (PRD §5.2).
//   5. quantum-dependent classical       — a branch whose condition
//      condition                           collapses a quantum value
//                                          into a classical bit
//                                          breaks the WHEN primitive's
//                                          lexical-scope control
//                                          semantics (P4).
//
// Each rejection fires through the Phase P-D (`DiagContext`)
// `report_reversible_*` family added by sturm-z2e8.4 at Error
// severity. Diagnostics anchor on the offending statement inside the
// reversible routine so the user can find the exact construct to fix.
//
// Body-only scope
// ---------------
// The validator only walks the forward routine's own body (every
// `Stmt` node reachable from `fd->getBody()`). It does NOT walk into
// nested function bodies, lambdas, or template instantiations — those
// are separate forwards which would each be validated independently
// when the MatchFinder surfaces them as `[[sturm::reversible]]`
// candidates in their own right. This matches R-A's and Q-A's opt-in
// posture.
//
// Swappable single entry point
// ----------------------------
// The module exposes a single free function:
//
//     ReversibleValidationResult validate_reversible_body(
//         const clang::FunctionDecl* fd,
//         clang::ASTContext& ctx,
//         DiagContext& diag,
//         const RoutineRegistry& routine_reg);
//
// Callers (the R-3 driver's `DriveOptions::body_validator` hook, and
// the tests) receive a `valid` bool + a machine-readable first-reject
// reason. The validator reports every reject reason it encounters
// via `DiagContext` (so the user sees all five P9d diagnostics in
// one compile pass) but returns only the first reason on the
// `.reason` field — matching the "one reason code per validation
// call" convention used by `TwinRejectReason` / `DriveRejectReason`.
//
// Reject-with-diagnostic contract
// -------------------------------
// Every P9d class the validator detects fires one diagnostic via
// `DiagContext`. A null FD or a non-reversible FD is a silent
// caller-side skip (the validator returns `valid=true` or a
// dedicated reject reason without firing any diagnostic) — the
// opt-in contract from `is_reversible` propagates.
//
// Stub-driven heuristics
// ----------------------
// Because the sturm-z2e8.5 issue description calls out that no prior-
// art measurement matcher or I/O matcher exists, the validator makes
// the following conservative choices (documented in-code):
//
//   - "Measurement" = any CallExpr whose callee's qualified name
//     matches a known measurement spelling (`sturm::measure_qubit`,
//     `sturm::sturm_measure`, `measure`, `measure_qubit`); OR an
//     explicit cast / user-defined conversion whose source is a
//     quantum type (`qbool` / `qint` / `qint_t`) and whose
//     destination is a non-quantum builtin (`bool` / integral type).
//     The cast shape mirrors the PM3-5
//     `matcher_quantum_to_classical_cond` detection surface but
//     without the "in branch condition only" constraint — inside a
//     reversible body, any quantum-to-classical collapse is a
//     measurement.
//
//   - "Classical I/O" = any CallExpr whose callee's qualified name
//     is in the known I/O set (`printf`, `fprintf`, `scanf`,
//     `std::cout`-style operator calls, `std::cerr`, `std::cin`,
//     `putchar`, `getchar`, `puts`, `gets`, `putc`, `getc`,
//     `std::printf`, etc.); OR an insertion/extraction operator
//     (`operator<<`, `operator>>`) on a stream type. The set is
//     conservatively small today; a follow-up roadmap phase can
//     expand it as new I/O shapes surface in user code.
//
//   - "Unregistered callee" = any CallExpr whose callee FunctionDecl
//     is NOT `[[sturm::reversible]]`, NOT in the PI-1
//     `RoutineRegistry`, AND NOT the enclosing forward itself (a
//     recursive call to the forward being validated — out of scope
//     per PRD §9 Q4, but we short-circuit the reject cleanly). The
//     callee's builtin-ness is probed via
//     `FunctionDecl::getBuiltinID`: builtins (e.g. `__builtin_ia32_*`,
//     `__assert_fail`) are treated as unregistered because the
//     transpiler cannot synthesise an adjoint for them. Calls
//     already classified as measurement / I/O above are NOT double-
//     reported here — each node fires at most one reject per
//     category.
//
//   - "Quantum-dependent classical condition" = the condition slot
//     of an `IfStmt` / `ConditionalOperator` / `WhileStmt` /
//     `DoStmt` contains an expression whose type (after stripping
//     top-level references + implicit casts) is a quantum type
//     (`qbool` / `qint` / `qint_t`). This includes the explicit-cast
//     shape PM3-5 fires on, but also bare uses like
//     `if (measure(q))` where the inner CallExpr returns `bool`.
//     Because `operator bool()` on `qbool` is `explicit`, a bare
//     `if (q)` is already a C++ error at parse time; the
//     measurement-inside-branch shape is what this reject targets.
//
// LOC budget
// ----------
// CLAUDE.md caps source modules at 300 LOC for headers. Plan §2.1
// P-C budgets 360 LOC for the implementation file; the header stays
// small.

#ifndef STURM_TRANSPILE_MATCHER_REVERSIBLE_VALIDATE_HPP
#define STURM_TRANSPILE_MATCHER_REVERSIBLE_VALIDATE_HPP

#include <string_view>

namespace clang {
class ASTContext;
class FunctionDecl;
} // namespace clang

namespace sturm::transpile {

class DiagContext;
class RoutineRegistry;

/// Why `validate_reversible_body` returned `valid=false`. The first
/// reject encountered during the walk wins — downstream rejects are
/// still reported to `DiagContext` so the user sees every diagnostic
/// in one compile pass, but only the first feeds this field. Tests
/// and the R-3 driver compare against the exact spellings returned
/// by `to_string(ReversibleRejectReason)`.
enum class ReversibleRejectReason {
    /// Body passes every P9d class — safe for R-A / S-A to consume.
    None,
    /// `fd == nullptr`. Silent; no diagnostic fires.
    NullDecl,
    /// `fd` is not carrying `[[clang::annotate("sturm::reversible")]]`.
    /// Silent; no diagnostic fires — the validator is opt-in.
    NotReversible,
    /// `fd` has no body (forward declaration only). Silent reject.
    NoBody,
    /// P9d (i): measurement (quantum → classical conversion) inside
    /// the body. Fired via `report_reversible_measurement`.
    Measurement,
    /// P9d (ii): classical I/O / observable side effect inside the
    /// body. Fired via `report_reversible_io`.
    ClassicalIO,
    /// P9d (iii): call to a function that is neither
    /// `[[sturm::reversible]]` nor bound in the PI-1 RoutineRegistry.
    /// Fired via `report_reversible_unregistered_callee`.
    UnregisteredCallee,
    /// P9d: `while`-loop or `do-while`-loop inside the body. Fired
    /// via `report_reversible_while_loop`.
    WhileLoop,
    /// P9d: classical branch (`if` / `?:` / etc.) whose condition
    /// derives from a quantum value (measurement). Fired via
    /// `report_reversible_classical_cond`.
    ClassicalCond,
};

/// Human-readable spelling of a `ReversibleRejectReason`. Stable
/// across runs — tests compare against these exact strings.
std::string_view to_string(ReversibleRejectReason reason);

/// Result of a single `validate_reversible_body` invocation.
struct ReversibleValidationResult {
    /// `true` iff the forward's body cleared every P9d class. When
    /// false, `reason` carries the first reject encountered; every
    /// reject (first and subsequent) is also fired through
    /// `DiagContext` so the user sees every offending site in one
    /// compile pass.
    bool valid = false;

    /// First reject reason encountered during the body walk. `None`
    /// when `valid == true`. Additional rejects do NOT overwrite
    /// this field — they are surfaced exclusively through
    /// `DiagContext`.
    ReversibleRejectReason reason = ReversibleRejectReason::NullDecl;

    /// Number of diagnostics the validator fired through
    /// `DiagContext` during this call. Tests use this to assert
    /// that the validator does not double-report the same node
    /// across multiple P9d classes (each node votes for at most
    /// one reject reason).
    unsigned diagnostics_fired = 0;
};

/// Walk the body of a `[[sturm::reversible]]` forward routine and
/// reject any construct that fails the P9d contract. For each
/// offending node, report the matching diagnostic through
/// `DiagContext`'s `report_reversible_*` family.
///
/// Pipeline:
///
///   1. Null / attribute / body guard. A null FD, a non-reversible
///      FD, or an FD without a body is a silent reject (no
///      diagnostic fires).
///
///   2. Body walk via `RecursiveASTVisitor`. For each `Stmt` in
///      the body:
///
///        - `WhileStmt` / `DoStmt`              → WhileLoop reject.
///        - `IfStmt` / `ConditionalOperator` /
///          `WhileStmt` / `DoStmt` cond slot
///          referencing a quantum value         → ClassicalCond reject.
///        - `CallExpr` whose callee is a
///          known measurement spelling          → Measurement reject.
///        - `CallExpr` whose callee is a
///          known I/O spelling                  → ClassicalIO reject.
///        - Any other `CallExpr` whose callee
///          is NOT reversible AND NOT in the
///          `RoutineRegistry`                   → UnregisteredCallee
///                                                reject.
///        - Explicit cast / user-conversion
///          whose source is quantum and
///          destination is a non-quantum
///          builtin                             → Measurement reject
///                                                (a conservative
///                                                conversion-site
///                                                heuristic).
///
///   3. After the walk, `valid = (reason == None)`.
///
/// Contract:
///   - `fd == nullptr` ⇒ `valid=false`, `reason=NullDecl`, no
///     diagnostic fired.
///   - `fd` missing `[[sturm::reversible]]` ⇒ `valid=false`,
///     `reason=NotReversible`, no diagnostic fired.
///   - `fd` without a body ⇒ `valid=false`, `reason=NoBody`, no
///     diagnostic fired.
///   - Otherwise: the walk visits every Stmt; every offending node
///     fires one diagnostic via `DiagContext`; `reason` carries
///     the first reject encountered.
///
/// The function never mutates AST state — it only reads the body.
/// Safe to call speculatively from the R-C driver (via the
/// `body_validator` hook in `DriveOptions`) as well as from tests.
ReversibleValidationResult validate_reversible_body(
    const clang::FunctionDecl* fd,
    clang::ASTContext& ctx,
    DiagContext& diag,
    const RoutineRegistry& routine_reg);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_REVERSIBLE_VALIDATE_HPP
