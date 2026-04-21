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
    (void)loc;
    (void)name;
    // TODO(backend): PM3-5 wires the detection site here.
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

} // namespace sturm::transpile
