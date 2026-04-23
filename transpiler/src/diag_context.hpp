// diag_context.hpp — PM3-0: shared DiagContext for the quantum-specific
// diagnostic pool (PM3-2 .. PM3-6).
//
// Purpose
// -------
// PM3 routes five classes of quantum-specific compile-time diagnostics
// through `clang::DiagnosticsEngine` (plan at
// /home/agent/.claude/plans/okay-lets-plan-the-fizzy-simon.md):
//
//   1. WHEN operand mutation                       (Error)
//   2. Quantum -> classical in branch cond         (Error)
//   3. Missing adjoint registration                (Error)
//   4. Caller drops returned qbool                 (Warning)
//   5. PH-3 outer-variable reverse-loop upgrade    (Warning)
//
// Every detection site needs to (a) resolve a stable `unsigned` custom
// diag-ID and (b) call `DiagnosticsEngine::Report(loc, id) << args...`.
// Resolving the ID is non-trivial — `getCustomDiagID` walks a DenseMap
// keyed on the full format string, so re-resolving on every callback
// fire is measurable overhead on a TU with many diagnostics and also
// loses the opportunity to document the per-class message strings in
// one place.
//
// `DiagContext` is the shared struct that:
//
//   - Holds a reference to `clang::DiagnosticsEngine` (obtained from
//     the parent `CompilerInstance` via `ci.getDiagnostics()`).
//   - Provides a lazy `getOrRegister(level, fmt)` helper that caches
//     the returned ID per format string so each `report_*` member
//     registers its ID exactly once per translation unit.
//   - Declares five empty-stubbed `report_*` members — one per
//     diagnostic class the PM3 epic lands. PM3-0 leaves the bodies
//     empty; PM3-2 .. PM3-6 fill each in when they wire the matching
//     matcher through this context.
//
// Construction happens in `TranspileConsumer` so the engine reference
// threads consistently through the plugin and standalone flows: the
// same `CompilerInstance` already owns the shared DiagnosticsEngine
// the PM3-1 `TextDiagnosticPrinter` is attached to (see
// `transpiler/src/main.cpp` / `transpiler/src/plugin.cpp`).
//
// PM3-0 scope
// -----------
// This header declares the struct and the five stub members; it does
// NOT wire any matcher into the context. PM3-2 is the first issue that
// cashes in on the scaffold — it extends `register_outer_var_guard_
// matcher` with a `DiagContext&` parameter and replaces the existing
// `std::fprintf(stderr, ...)` call at `matcher_outer_var_guard.cpp:
// 280-286` with `diag.report_outer_var_mutation(...)`.

#ifndef STURM_TRANSPILE_DIAG_CONTEXT_HPP
#define STURM_TRANSPILE_DIAG_CONTEXT_HPP

#include <string>
#include <string_view>
#include <unordered_map>

namespace clang {
class DiagnosticsEngine;
class SourceLocation;
} // namespace clang

namespace sturm::transpile {

/// PM3-0 — shared diagnostic context for the PM3 family of quantum-
/// specific matchers. Owns a reference to the parent
/// `CompilerInstance`'s `DiagnosticsEngine` (so reports land on the
/// PM3-1 `TextDiagnosticPrinter` that fronts both the standalone
/// driver and the in-process plugin) plus a lazy cache mapping format
/// strings to their registered custom diag-IDs.
///
/// Lifetime: constructed once per translation unit inside
/// `TranspileConsumer`'s ctor; destroyed with the consumer at end of
/// `HandleTranslationUnit`. Passed by reference to the `register_*_
/// matcher` helpers that fire PM3 diagnostics. The underlying
/// `DiagnosticsEngine` is owned by `CompilerInstance` and outlives
/// the consumer.
///
/// Thread-safety: not required. `MatchFinder::matchAST` runs the
/// matcher pool single-threaded; every `report_*` call fires on the
/// same thread that constructed the context.
struct DiagContext {
    /// Captures a reference to the parent `CompilerInstance`'s
    /// diagnostic engine. The engine's lifetime dominates this
    /// context's lifetime — the caller is responsible for not
    /// out-living the engine.
    explicit DiagContext(clang::DiagnosticsEngine& diag);

    DiagContext(const DiagContext&) = delete;
    DiagContext& operator=(const DiagContext&) = delete;

    /// Lazily register-or-lookup a custom diag-ID for `fmt` at
    /// `level`. The first call with a given `(level, fmt)` pair
    /// resolves a fresh ID via `DiagnosticsEngine::getCustomDiagID`
    /// and caches the result; every subsequent call with the same
    /// format string returns the cached ID without re-hashing the
    /// engine's internal DenseMap.
    ///
    /// `level` is the raw `DiagnosticsEngine::Level` enumerator
    /// (value-equal to `DiagnosticIDs::Level`); we take it as `int`
    /// in the header to avoid dragging the full Clang `Diagnostic.h`
    /// into every translation unit that pulls in
    /// `transpile_consumer.hpp`. The `.cpp` re-interprets the int as
    /// the engine's `Level` enum via a narrow cast.
    ///
    /// Keyed on `(level, fmt)` so two classes can share a format
    /// string at different severities — not actually used today, but
    /// future-proofs against a PM3-7+ upgrade that wants to demote an
    /// Error to a Warning behind a flag without re-typing the
    /// message.
    unsigned getOrRegister(int level, std::string_view fmt);

    /// Direct access to the engine. Exposed so matchers that need
    /// low-level control (e.g. `Report()` with a custom fix-it hint)
    /// can bypass the `report_*` wrappers without having to re-plumb
    /// the reference through their call sites.
    clang::DiagnosticsEngine& engine() { return diag_; }

    // ── PM3 per-class report_* members ──────────────────────────────
    //
    // Each member maps 1:1 to a `report_*` call the corresponding
    // sub-issue (PM3-2 .. PM3-6) wires into its matcher. PM3-0 leaves
    // every body empty — the member exists so the diagnostic call
    // sites can be written against a stable signature while the
    // downstream sub-issues land the detection logic.
    //
    // The arguments match what each matcher needs to emit a well-
    // formed, user-facing diagnostic:
    //
    //   - `loc`           : the precise source location to cite. The
    //                       caller is responsible for funnelling
    //                       macro-expansion locations through
    //                       `SourceManager::getFileLoc(...)` so the
    //                       diagnostic points at user source, not
    //                       memory buffers.
    //   - `name` / `fn`   : the identifier / qualified function name
    //                       embedded in the `%0` slot of the format.
    //   - `decl_line`     : PH-3-specific — the user-facing line the
    //                       outer-variable was declared on.

    /// PM3-4 / Class 1: WHEN operand mutation. Fires when a qbool /
    /// qint `DeclRefExpr` appearing in the WHEN control expression is
    /// mutated inside the WHEN body. Error severity.
    void report_when_operand_mutation(clang::SourceLocation loc,
                                      std::string_view name);

    /// PM3-5 / Class 2: Quantum -> classical conversion in the
    /// condition slot of an `IfStmt` / `WhileStmt` / `DoStmt` /
    /// `ConditionalOperator`. Error severity. `name` is the qbool /
    /// qint_t identifier being cast.
    void report_quantum_to_classical_cond(clang::SourceLocation loc,
                                          std::string_view name);

    /// PM3-3 / Class 3: Missing adjoint registration on a user
    /// routine that has at least one quantum output parameter. Error
    /// severity. `fn` is the callee's qualified name.
    void report_missing_adjoint(clang::SourceLocation loc,
                                std::string_view fn);

    /// PM3-6 / Class 4: Caller drops the returned qbool / qint_t of
    /// a CallExpr at statement scope. Warning severity (not Error —
    /// compilation must continue so the user sees every leak in one
    /// pass). `fn` is the callee's qualified name. Already wired via
    /// the in-matcher report path (sturm-5btt.7); the stub here lets
    /// PM3-2..PM3-6 converge on the same report_* surface.
    void report_dropped_quantum_return(clang::SourceLocation loc,
                                       std::string_view fn);

    /// PM3-2 / Class 5: Outer-variable reverse-loop upgrade. Fires
    /// when a qbool / qint declared in an outer scope is mutated
    /// inside a for / while / if / WHEN body. Warning severity. Cites
    /// the original decl line so the user can find both halves of the
    /// mutation.
    ///
    /// `kind` is the human-readable type qualifier embedded in the
    /// `%0` slot of the format (e.g. `"qbool/qint"` — the matcher does
    /// not currently discriminate between the two element types).
    /// `name` is the mutated identifier (`%1`); `decl_line` is the
    /// 1-based user-source line at which `name` was declared (`%2`).
    /// Caller funnels macro-expansion locations through
    /// `SourceManager::getFileLoc(...)` before handing `loc` over so
    /// the diagnostic cites user source, not the memory buffer.
    void report_outer_var_mutation(clang::SourceLocation loc,
                                   std::string_view kind,
                                   std::string_view name,
                                   unsigned decl_line);

    /// PN-5 / Class 6: qbool(p) preparation inside an uncompute-eligible
    /// scope. Warning severity. Format:
    ///   "qbool %0 preparation in uncompute-eligible scope has no adjoint (P9)"
    /// Fires when a `qbool x(p);` VarDecl whose initializer is a
    /// probabilistic `double` (not a `bool` literal / `bool`-typed
    /// expression) appears inside a WHEN body or a compound-expression
    /// intermediate scope. `%0` is the VarDecl identifier; `loc` is the
    /// VarDecl's file location. The matcher funnels the reported loc
    /// through `SourceManager::getFileLoc(...)` before handing it over
    /// so the diagnostic cites user source rather than the memory
    /// buffer.
    ///
    /// Prep at top-level function-body scope is silent by design — the
    /// warning only fires inside uncompute-eligible scopes where the
    /// transpiler would otherwise try to synthesize an inverse for an
    /// operation (the CP map `prepare`) that has no adjoint.
    void report_prep_in_uncompute_scope(clang::SourceLocation loc,
                                        std::string_view name);

    // ── P-D / Phase P automatic adjoint synthesis diagnostics ──────
    //
    // All five mirror the existing report_* family for the P9d "body
    // cannot be inverted" rejection set (plan §2.1 row P-D; validated
    // by §2.1 row P-C). Every method fires at Error severity — P9d
    // treats a non-invertible reversible body as a hard compile error
    // at the forward-function definition site (not at the
    // `invert(fn)` call site). The `%0` slot is the offending
    // identifier (enclosing routine name, or callee name for the
    // unregistered-callee case); macro-expansion `loc` values must be
    // funneled through `SourceManager::getFileLoc(...)` first.

    /// P-D / P9d (i): measurement inside a reversible routine.
    /// `name` is the reversible routine's name.
    void report_reversible_measurement(clang::SourceLocation loc,
                                       std::string_view name);

    /// P-D / P9d (ii): classical I/O or observable side-effect inside
    /// a reversible routine. `name` is the reversible routine's name.
    void report_reversible_io(clang::SourceLocation loc,
                              std::string_view name);

    /// P-D / P9d (iii): call to an unregistered routine inside a
    /// reversible body. `fn` is the callee's qualified name (NOT the
    /// enclosing routine — the fix targets the callee).
    void report_reversible_unregistered_callee(clang::SourceLocation loc,
                                               std::string_view fn);

    /// P-D / P9d: `while`-loop inside a reversible routine. Rejected
    /// because unbounded trip counts are not invertible by B11 loop
    /// reversal (PRD §5.2). `name` is the reversible routine's name.
    void report_reversible_while_loop(clang::SourceLocation loc,
                                      std::string_view name);

    /// P-D / P9d: classical branch condition derived from a quantum
    /// value inside a reversible routine. User should rewrite as
    /// `WHEN(q) { ... }`. `name` is the reversible routine's name.
    void report_reversible_classical_cond(clang::SourceLocation loc,
                                          std::string_view name);

private:
    clang::DiagnosticsEngine& diag_;

    // Lazy ID cache. Keyed on the format string so lookups are O(1)
    // amortised across a TU. The `int` level is folded into the key
    // so two classes can share a format at different severities —
    // see the `getOrRegister` docstring for the rationale.
    //
    // We key on `std::string` (owned) rather than `std::string_view`
    // because the `fmt` literal handed in at call sites is a string
    // literal with static duration — but future callers may hand in
    // a runtime-computed message string (e.g. built from a template
    // argument). Owning the key side-steps the dangling-view hazard.
    struct Key {
        int level;
        std::string fmt;
        bool operator==(const Key& other) const {
            return level == other.level && fmt == other.fmt;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            // Combine the level into the string hash without extra
            // allocation — the FNV-style mix is cheap and the key
            // space is tiny (one entry per diagnostic class).
            return std::hash<std::string>{}(k.fmt) ^
                   (static_cast<std::size_t>(k.level) * 0x9e3779b97f4a7c15ULL);
        }
    };
    std::unordered_map<Key, unsigned, KeyHash> id_cache_;
};

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_DIAG_CONTEXT_HPP
