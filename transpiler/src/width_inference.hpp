// width_inference.hpp — sturm-u9ge.11 (Beat B1) width-inference module
// for the QRAM-via-array-subscript epic.
//
// Plan §5 / Beat B1; PRD §6 + §11.3 (D0c). Given a `VarDecl` of frontend
// `qint`, decide the backend width `W` for the substituted `qint_t<W>`
// the C1 matcher (sturm-u9ge.12) will emit. The rule order is the one
// fixed by D0c (PRD §11.3) — three rules, evaluated in strict order, the
// first whose precondition holds short-circuits the rest. The rule list
// is the ONLY width-decision surface in the transpiler — no rule may be
// re-ordered, skipped, or overridden by a later pass (PRD §11.3 / D0c.3).
//
// Rule order (PRD §11.3 / D0c.1):
//
//     1. Annotation (reserved): `qint<W> b = …;`
//          - Gated behind `kAllowAnnotation = false` in v1.
//          - When detected, fires `qram-width-annotation-reserved` Error
//            and falls through to rule 3.
//     2. RHS-driven: initializer is a subscript on a container of
//        `qint_t<W_e>` for a unique `W_e`.
//          - Returns `W_e`.
//     3. Global default: `kDefaultWidth = 32` (PRD §11.3 v1 choice).
//
// Ambiguity (PRD §11.3 / D0c.2): rule 2 with two or more distinct
// candidate `W_e` values fires `qram-width-mismatch` (Error severity)
// at the VarDecl's source range, with one Note per candidate, and falls
// through to rule 3 (default).
//
// Diagnostic ids (PRD §11.3 / D0c.2):
//   - `qram-width-mismatch`              — rule 2 ambiguity
//   - `qram-width-annotation-reserved`   — rule 1 v1 fallthrough
//
// LoC budget: <= 300 (plan §1, §5 / B1).

#ifndef STURM_TRANSPILE_WIDTH_INFERENCE_HPP
#define STURM_TRANSPILE_WIDTH_INFERENCE_HPP

namespace clang {
class VarDecl;
class DiagnosticsEngine;
} // namespace clang

namespace sturm::transpile {

// ── Tunable constants (PRD §11.3 / D0c.1) ───────────────────────────────────
//
// Both constants live in the header as `inline constexpr` so a future
// bump (e.g. `kDefaultWidth` to 64, or `kAllowAnnotation` to true once
// v2 unlocks the `qint<W>` annotation form) is a one-line edit at the
// declaration site, with no need to touch the consumer or the test.
// PRD §11.3 explicitly mandates "the value lives in a single named
// constant in `width_inference.hpp` so a future bump (e.g. to 64) is a
// one-line change".

/// v1 global default width (PRD §11.3 / D0c.1 row 3). 32 wins for v1
/// because the orkan simulator's qubit budget already pushes against
/// multi-`qint` algorithms at that width; 64 would double the per-qint
/// ancilla footprint for no v1 benefit (PRD §11.3 rationale).
inline constexpr unsigned kDefaultWidth = 32;

/// v1 gate for rule 1 (annotation form `qint<W>`). PRD §11.3 / D0c.1
/// row 1: "Reserved as future syntax … not required for v1". When
/// `false`, rule 1 detection still fires but emits the
/// `qram-width-annotation-reserved` diagnostic and falls through to
/// rule 3. Flipping this to `true` is the v2 lift point.
inline constexpr bool kAllowAnnotation = false;

// ── Diagnostic id constants (PRD §11.3 / D0c.2) ─────────────────────────────
//
// Stable string ids, embedded verbatim into the diagnostic format
// strings so test code can substring-match rather than depending on
// Clang's per-engine custom-diag-ID integers. Naming follows the
// established `qram-<area>-<specific>` kebab-case convention used by
// the four `qram-oos-*` ids in `matcher_qram_oos.hpp:62-75`.

/// Fired by rule 2 when the initializer subscript admits two or more
/// distinct candidate element widths. Severity: Error. Source range:
/// the `VarDecl` of the LHS. Notes: one Note per candidate.
inline constexpr const char* kQramWidthMismatchId =
    "qram-width-mismatch";

/// Fired by rule 1 when a `qint<W>` annotation is detected but
/// `kAllowAnnotation == false`. Severity: Error. Source range: the
/// `VarDecl` of the LHS. After firing, rule 1 falls through to rule
/// 3 (default).
inline constexpr const char* kQramWidthAnnotationReservedId =
    "qram-width-annotation-reserved";

// ── InferContext ────────────────────────────────────────────────────────────
//
// Caller-owned context bundle threaded into every `infer_width` call.
// The fields are:
//
//   - `diag`: optional diagnostics sink. When non-null, ambiguity
//     (rule 2) and annotation-reserved (rule 1) diagnostics are
//     reported. When null, both rules silently fall through (the test
//     harness uses this null variant for the rule-3-fallback positive
//     case where no diagnostics should fire).
//   - `default_width`: the v1 global default (PRD §11.3 / D0c.1 row 3).
//     Defaults to `kDefaultWidth`. Tests override this slot to verify
//     the constant flows through.
//   - `allow_annotation`: the v1 gate for rule 1 (PRD §11.3 / D0c.1
//     row 1). Defaults to `kAllowAnnotation`. Tests override this slot
//     to verify the v2 lift point works without code churn.
//
// The struct is value-typed and trivially copyable; pass by const-ref.
struct InferContext {
    clang::DiagnosticsEngine* diag = nullptr;
    unsigned default_width        = kDefaultWidth;
    bool allow_annotation         = kAllowAnnotation;
};

// ── Public API ──────────────────────────────────────────────────────────────
//
// Decide the backend width `W` for the substituted `qint_t<W>` of
// `vd`. Implements the three rules in PRD §11.3 / D0c.1 in strict
// order; the first whose precondition holds returns its width.
//
// Returns: a positive unsigned width. Always defined — the rule-3
// fallback (`ctx.default_width`) is the universal terminator.
//
// Side effects (via `ctx.diag` if non-null):
//   - Rule 1 with `allow_annotation == false`: emits
//     `qram-width-annotation-reserved` Error at `vd`'s source range.
//   - Rule 2 with two or more distinct candidate widths: emits
//     `qram-width-mismatch` Error at `vd`'s source range plus one
//     Note per candidate.
//
// Both rules' fallthrough lands on rule 3, so the return value is
// well-defined even after a diagnostic fires. PRD §11.3 / D0c.2
// rationale: "The fall-through is deliberate: it lets the rest of the
// translation unit keep parsing so the user sees ALL related
// diagnostics in one build, instead of stopping at the first
// ambiguity."
unsigned infer_width(const clang::VarDecl& vd, const InferContext& ctx);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_WIDTH_INFERENCE_HPP
