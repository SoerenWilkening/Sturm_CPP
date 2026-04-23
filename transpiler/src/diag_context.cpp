// diag_context.cpp — PM3-0: implementation of the shared DiagContext.
//
// PM3-0 scope: scaffold only. The ctor captures the parent
// CompilerInstance's `DiagnosticsEngine` reference, `getOrRegister`
// lazily registers custom diag-IDs on the engine and caches them on
// the (level, fmt) pair, and every `report_*` member is an empty stub
// that the PM3-2 .. PM3-6 sub-issues will flesh out when they wire the
// matching matcher through this context.
//
// The stubs swallow their arguments via `(void)` casts so the
// -Wunused-parameter warning the transpiler builds with does not fire
// until the bodies are filled in. This keeps the PM3-0 build green
// against both targets (`sturm-transpile` + `sturm-transpile-plugin`)
// without perturbing any matcher-layer call site.

#include "diag_context.hpp"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace sturm::transpile {

DiagContext::DiagContext(clang::DiagnosticsEngine& diag) : diag_(diag) {}

unsigned DiagContext::getOrRegister(int level, std::string_view fmt) {
    // Key on the (level, fmt) pair. The key must own the string to
    // avoid dangling-view hazards — the caller's `string_view` is
    // allowed to go out of scope between this call and the next
    // lookup. The `static_cast<DiagnosticsEngine::Level>` below
    // narrows the `int` back into the enum; callers obtain the int
    // from the same enum so the cast is lossless.
    Key key{level, std::string(fmt)};
    if (auto it = id_cache_.find(key); it != id_cache_.end()) {
        return it->second;
    }

    // `DiagnosticsEngine::getCustomDiagID` is a template that only
    // accepts a `const char (&)[N]` — i.e. a string literal — because
    // its internal helper computes `N - 1` at the call site. That is
    // not compatible with a runtime `std::string_view`. We reach
    // through to the engine's shared `DiagnosticIDs` (obtained via
    // `getDiagnosticIDs()`), whose `getCustomDiagID(Level, StringRef)`
    // overload accepts an arbitrary runtime string. The IDs returned
    // by the two paths are interchangeable — the engine and the
    // DiagnosticIDs share the same DenseMap keyed on the format
    // string, so subsequent `Report(loc, id)` calls on the engine
    // resolve correctly.
    const auto lvl =
        static_cast<clang::DiagnosticIDs::Level>(level);
    // `DiagnosticIDs::getCustomDiagID` takes a `StringRef`;
    // constructing one from a `std::string` binds the StringRef to
    // that string's storage. The engine internally stores the
    // format string inside its own arena keyed on this ID, so the
    // StringRef's transient lifetime is not an issue past the call.
    const unsigned id = diag_.getDiagnosticIDs()->getCustomDiagID(
        lvl,
        llvm::StringRef(key.fmt.data(), key.fmt.size()));
    id_cache_.emplace(std::move(key), id);
    return id;
}

// ── PM3-2 .. PM3-6 empty stubs ──────────────────────────────────────
//
// Each body is intentionally empty in PM3-0 — the struct exists so
// downstream sub-issues can call `diag.report_*(...)` against a
// stable signature while the detection logic is being authored. When
// a sub-issue lands, it replaces the `(void)` swallows below with a
// `getOrRegister` + `Report()` call pair.

void DiagContext::report_when_operand_mutation(
    clang::SourceLocation loc, std::string_view name) {
    // PM3-4 / Class 1: the format string is locked down by the issue
    // description. `%0` is the mutated qbool/qint identifier. Error
    // severity per P4 ("the operands of a `WHEN` control expression
    // must not be modified within the scope; violation is undefined
    // behaviour") — the transpiler cannot honour a source program
    // that relies on UB, so compilation must abort. The
    // `getOrRegister` cache amortises the diag-ID lookup to one call
    // per TU.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: WHEN operand '%0' is mutated inside the WHEN body - "
        "mutation of a control qbool/qint is undefined behaviour (P4).");
    // `DiagnosticsEngine::Report(...) << arg` takes `std::string` for
    // the `%N` slot; constructing one from a string_view is
    // necessary because the builder's operator<< does not have a
    // string_view overload.
    diag_.Report(loc, id) << std::string(name);
}

void DiagContext::report_quantum_to_classical_cond(
    clang::SourceLocation loc, std::string_view name) {
    // PM3-5 / Class 2: Quantum -> classical in a branch condition. Fires
    // when an explicit cast (`static_cast<bool>(q)`, `(bool)q`, or
    // `bool(q)`) from a qbool / qint_t appears in the condition slot of
    // an IfStmt / WhileStmt / DoStmt / ConditionalOperator. `%0` is the
    // source qbool / qint_t identifier being cast. Error severity —
    // collapsing a quantum value to classical at branch time cannot be
    // honoured by the transpiler (contradicts the WHEN primitive's
    // lexical-scope control semantics under P4), so compilation must
    // abort. The `getOrRegister` cache amortises the diag-ID lookup to
    // one call per TU. The fix-hint suggests `WHEN(q) { ... }` —
    // scheduling the body lexically on the quantum state rather than
    // collapsing it.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: branch condition derives from quantum value via "
        "explicit cast; use WHEN(%0) { ... } to schedule the body "
        "conditionally on the quantum state.");
    // `DiagnosticsEngine::Report(...) << arg` takes `std::string` for
    // the `%N` slot; constructing one from a string_view is necessary
    // because the builder's operator<< does not have a string_view
    // overload.
    diag_.Report(loc, id) << std::string(name);
}

void DiagContext::report_missing_adjoint(
    clang::SourceLocation loc, std::string_view fn) {
    // PM3-3: Class 3 — missing adjoint registration. Fires when a
    // CallExpr targets a FunctionDecl that is NOT in the PI-1
    // RoutineRegistry AND has at least one quantum output parameter
    // (non-const `qbool&` / `qint&` — i.e. `outputs_mask != 0` on the
    // callsite's would-be QOperation). Error severity — compilation
    // must abort because the transpiler cannot emit an
    // `invert(<fn>)(...)` call without a registered adjoint, and a
    // silent miss would leak qubits at run time (contradicts P9).
    //
    // The `%0` slot is the callee's qualified name. The PI-1 macro
    // `STURM_REGISTER_ADJOINT(fn, adj)` takes the same name verbatim,
    // so the suggested fix text substitutes both occurrences with the
    // user's callsite identifier.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: call to '%0' requires uncomputation (has quantum "
        "output parameter), but no adjoint is registered. Use "
        "STURM_REGISTER_ADJOINT(%0, <adjoint_fn>) at TU scope.");
    diag_.Report(loc, id) << std::string(fn);
}

void DiagContext::report_dropped_quantum_return(
    clang::SourceLocation loc, std::string_view fn) {
    (void)loc;
    (void)fn;
    // TODO(backend): the PM3-6 matcher already routes its report
    // directly through `DiagnosticsEngine::Report` in
    // `matcher_dropped_quantum_return.cpp`; a follow-up tidies the
    // call site to use this context for consistency with PM3-2..5.
}

void DiagContext::report_outer_var_mutation(
    clang::SourceLocation loc,
    std::string_view kind,
    std::string_view name,
    unsigned decl_line) {
    // PM3-2: the format string is locked down in the issue
    // description. `%0` is the type qualifier ("qbool/qint" per the
    // matcher's current non-discriminating classification), `%1` is
    // the mutated identifier, `%2` is the 1-based user-source line
    // the variable was declared on. Warning severity — NOT Error —
    // because compilation must continue so the user sees every
    // outer-var mutation in one pass instead of stopping at the
    // first. The `getOrRegister` cache amortises the diag-ID lookup
    // to a single call per TU.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Warning,
        "STURM: %0 '%1' (declared at line %2) is modified inside a "
        "for/while/if/WHEN body — automatic uncomputation would "
        "require reverse-loop synthesis. Provide a manual adjoint "
        "(P9) or restructure.");
    // `DiagnosticsEngine::Report(...) << arg` takes std::string by
    // value for the `%N` slot; constructing one from a string_view
    // is necessary because the builder's operator<< does not have a
    // string_view overload.
    diag_.Report(loc, id) << std::string(kind)
                          << std::string(name)
                          << decl_line;
}

void DiagContext::report_prep_in_uncompute_scope(
    clang::SourceLocation loc, std::string_view name) {
    // PN-5 / Class 6: the format string is locked down in the Phase N
    // §5 plan. `%0` is the qbool VarDecl identifier. Warning severity
    // — NOT Error — because compilation must continue so the user
    // sees every preparation that would bump against the P9 "no
    // implicit adjoint" invariant in a single pass. The user is
    // expected to restructure (hoist the prep out of the
    // uncompute-eligible scope, or supply a manual adjoint) without
    // the transpiler having to halt the pipeline.
    //
    // Why Warning (not Error): the forward emission of a prep inside
    // a WHEN body still produces a physically-meaningful gate stream
    // (a conditional Ry preparation). Only the uncompute half is
    // broken — no adjoint exists. Treating this as Error would abort
    // compilation at the first prep the user writes inside a WHEN;
    // treating it as Warning lets the full translation unit be
    // surfaced so the user fixes every site at once.
    //
    // The `getOrRegister` cache amortises the diag-ID lookup to a
    // single call per TU, mirroring the other PM3 report_* members.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Warning,
        "qbool %0 preparation in uncompute-eligible scope has no "
        "adjoint (P9)");
    // `DiagnosticsEngine::Report(...) << arg` takes std::string by
    // value for the `%N` slot; constructing one from a string_view
    // is necessary because the builder's operator<< does not have a
    // string_view overload.
    diag_.Report(loc, id) << std::string(name);
}

// ── P-D (Phase P / automatic adjoint synthesis) report_* members ────
//
// All five mirror the existing report_* pattern:
//   - Resolve a custom diag-ID via `getOrRegister(Error, fmt)` so the
//     DenseMap lookup amortises to a single cache miss per TU.
//   - Stream the `std::string(name|fn)` argument into the builder —
//     the builder's operator<< has no string_view overload.
// Every body is locked at Error severity per P9d — a reversible
// routine whose body cannot be inverted is a hard compile error at
// the forward-function definition site (NOT at the `invert(fn)` use
// site, so the diagnostic points at the code the user would fix).

void DiagContext::report_reversible_measurement(
    clang::SourceLocation loc, std::string_view name) {
    // P-D / P9d item (i): measurement inside a reversible body.
    // Format's `%0` is the reversible routine's name. The user's fix
    // is to remove the measurement (move it outside the routine) or
    // drop the `[[sturm::reversible]]` attribute so the routine is
    // not subjected to synthesis.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' contains a measurement "
        "(quantum -> classical conversion); measurement is not "
        "invertible (P9d).");
    diag_.Report(loc, id) << std::string(name);
}

void DiagContext::report_reversible_io(
    clang::SourceLocation loc, std::string_view name) {
    // P-D / P9d item (ii): classical I/O inside a reversible body.
    // Format's `%0` is the reversible routine's name. The user's fix
    // is to lift the I/O call outside the reversible routine or drop
    // `[[sturm::reversible]]` — I/O is an observable classical side
    // effect and cannot be undone by gate reversal.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' contains classical I/O or an "
        "observable classical side effect; I/O is not invertible "
        "(P9d).");
    diag_.Report(loc, id) << std::string(name);
}

void DiagContext::report_reversible_unregistered_callee(
    clang::SourceLocation loc, std::string_view fn) {
    // P-D / P9d item (iii): unregistered callee inside a reversible
    // body. Format's `%0` is the callee's qualified name — NOT the
    // enclosing reversible routine — because the user's fix targets
    // the callee (register an adjoint for it, or mark it
    // `[[sturm::reversible]]` so P-C validates it transitively). The
    // suggested fix text mirrors `report_missing_adjoint` so the two
    // surfaces are user-consistent.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine body calls '%0', which has no "
        "registered or synthesized adjoint. Use "
        "STURM_REGISTER_ADJOINT(%0, <adjoint_fn>) at TU scope, or "
        "annotate '%0' with [[sturm::reversible]] so the transpiler "
        "synthesizes one (P9d).");
    diag_.Report(loc, id) << std::string(fn);
}

void DiagContext::report_reversible_while_loop(
    clang::SourceLocation loc, std::string_view name) {
    // P-D / P9d: while-loop inside a reversible body. Format's `%0`
    // is the reversible routine's name. The user's fix is to rewrite
    // the while-loop as a bounded `for`-loop with a compile-time
    // trip count (Phase S B11 reverses the iteration order), or to
    // hand-register an adjoint and drop `[[sturm::reversible]]`.
    // Pinned by PRD §5.2 ("while(cond) body — rejected — unbounded
    // trip count is not invertible without a manual adjoint").
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' contains a while-loop; "
        "unbounded trip counts are not invertible by B11 loop "
        "reversal — rewrite as a bounded for-loop or supply a manual "
        "adjoint (P9d).");
    diag_.Report(loc, id) << std::string(name);
}

void DiagContext::report_reversible_classical_cond(
    clang::SourceLocation loc, std::string_view name) {
    // P-D / P9d: quantum-dependent classical branch condition inside
    // a reversible body. Format's `%0` is the reversible routine's
    // name. The user's fix is to rewrite the branch as a `WHEN(q) {
    // ... }` block so the body is scheduled conditionally on the
    // quantum state rather than collapsing it. Mirrors
    // `report_quantum_to_classical_cond` but is emitted by the P-C
    // validation pass (not the PM3-5 matcher), so the diagnostic
    // text cites the reversible routine the offending branch is
    // embedded in.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' branches on a collapsed "
        "quantum value; use WHEN(q) { ... } to schedule the body "
        "conditionally on the quantum state without measurement "
        "(P9d).");
    diag_.Report(loc, id) << std::string(name);
}

// ── Q-B (Phase Q / signature normalization) report_* members ───────
//
// All three mirror the existing P-D report_* pattern:
//   - Resolve a custom diag-ID via `getOrRegister(Error, fmt)` so the
//     DenseMap lookup amortises to a single cache miss per TU.
//   - Stream the `std::string(fn)` + `std::string(param)` arguments
//     into the builder — the builder's operator<< has no string_view
//     overload.
// Every body is locked at Error severity per P9b / P9d — a reversible
// signature the transpiler cannot rewrite is a hard compile error at
// the forward-function definition site so the user can find the
// offending parameter at the canonical audit anchor.

void DiagContext::report_reversible_pointer_param(
    clang::SourceLocation loc,
    std::string_view fn,
    std::string_view param) {
    // Q-B (i): quantum parameter declared with pointer type. `%0` is
    // the reversible routine's name; `%1` is the offending parameter
    // identifier. The user's fix is to switch to the canonical
    // reference spelling (`qbool&` / `qint&`). Pointers would force
    // the transpiler to reason about aliasing / nullness — out of
    // scope for the MVP reversible surface.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' parameter '%1' uses pointer-"
        "to-quantum type; declare it by reference (e.g. 'qbool&') so "
        "the synthesized adjoint can track mutation (P9b).");
    diag_.Report(loc, id) << std::string(fn) << std::string(param);
}

void DiagContext::report_reversible_value_param_mutated(
    clang::SourceLocation loc,
    std::string_view fn,
    std::string_view param) {
    // Q-B (ii): non-const by-value quantum parameter mutated in the
    // body. `%0` is the reversible routine's name; `%1` is the
    // offending parameter identifier. The local copy's gate stream
    // cannot be undone by the synthesized adjoint (there is no out
    // slot). The user's fix is to declare the parameter `const`,
    // pass by reference (`qbool&`), or remove the mutation.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' mutates by-value quantum "
        "parameter '%1'; the local copy has no adjoint slot. Declare "
        "'%1' as 'const', pass by reference, or remove the mutation "
        "(P9b).");
    diag_.Report(loc, id) << std::string(fn) << std::string(param);
}

void DiagContext::report_reversible_const_ref_mutated(
    clang::SourceLocation loc,
    std::string_view fn,
    std::string_view param) {
    // Q-B (iii): const-qualified reference-to-quantum parameter
    // mutated in the body. `%0` is the reversible routine's name;
    // `%1` is the offending parameter identifier. Normally a C++
    // error, but we defend against user-defined conversion /
    // overload shapes that make the mutation syntactically valid.
    // The user's fix is to drop the `const` qualifier or remove the
    // mutation.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' mutates const-qualified "
        "reference parameter '%1'; drop the 'const' qualifier or "
        "remove the mutation so the adjoint can un-mutate it (P9b).");
    diag_.Report(loc, id) << std::string(fn) << std::string(param);
}

void DiagContext::report_reversible_sig_multi_return(
    clang::SourceLocation loc, std::string_view fn) {
    // Q-A multi-statement body reject surfaced to the user. Fires at
    // Error severity per PRD §9 / §4.1 — return-style normalization
    // requires a single `return <expr>;` body so the Q-A emitter can
    // lift the return expression into an `^=` out-param assignment.
    // `%0` is the reversible routine's name.
    const unsigned id = getOrRegister(
        clang::DiagnosticsEngine::Error,
        "STURM: reversible routine '%0' has a multi-statement body; "
        "return-style normalization requires a single 'return "
        "<expr>;' body (P9a + PRD §4.1 multi-return reject).");
    diag_.Report(loc, id) << std::string(fn);
}

} // namespace sturm::transpile
