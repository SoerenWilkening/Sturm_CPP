// qir.hpp — Quantum IR for the STURM transpiler (M6).
//
// Purpose
// -------
// The Quantum IR (QIR) is the data contract between three downstream
// modules of the transpiler:
//
//   - M7 AST matcher fills a QUnit from user source code — one QOperation
//     per qbool binary-op assignment (MVP = OR only).
//   - M8 uncompute pass walks QUnit scopes in reverse, emitting an
//     UncomputeInsertion record for each QOperation.
//   - M9 emitter consumes those insertions to rewrite the source.
//
// Shape
// -----
// The IR is a *flat* list of scopes, and each scope holds a flat list of
// operations in source order (the "default" shape noted in the PRD's Open
// Questions section). The transpiler does not reconstruct a tree of
// expressions — each named intermediate (qbool tmp = a | b) is one op, and
// nesting is expressed transparently by operand names pointing back at
// earlier ops' results.
//
// Dependencies
// ------------
// This header is deliberately kept narrow. It includes only the minimum
// Clang type required to represent source positions (SourceLocation /
// SourceRange) plus <string> and <vector>. No AST types (DeclRefExpr,
// VarDecl, ...) leak in, so:
//
//   - Tests can construct a QUnit by hand, without spinning up a ClangTool.
//   - The M8 uncompute pass and M9 emitter compile in <1 second (they do
//     not drag the entire Clang AST library through their headers).
//
// Stability guarantee
// -------------------
// dump() is the single textual representation of a QUnit. The M7 and M8
// test suites compare against golden strings produced by this function, so
// a change to dump()'s format is a breaking change: every golden must be
// regenerated and every reviewer must approve it. If you need a different
// textual form (e.g. JSON for tooling), add a new function — do not
// repurpose dump().
//
// Golden format summary:
//   "QUnit: N scope(s)\n"
//   for each scope:
//     "  Scope[i] braces=[<open>..<close>]\n"
//     for each op:
//       "    Op[j] <KIND> <result>@<loc> = <op1>@<loc>, <op2>@<loc>, ...  range=[<b>..<e>]\n"
//
// Location fields print the raw SourceLocation encoding (a 32-bit unsigned
// integer). A SourceLocation whose isInvalid() returns true renders as the
// literal string "<invalid>" so hand-built test fixtures produce readable
// output without wiring up a SourceManager.

#ifndef STURM_TRANSPILE_QIR_HPP
#define STURM_TRANSPILE_QIR_HPP

#include "clang/Basic/SourceLocation.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sturm::transpile {


/// Kinds of quantum operations representable in the IR.
///
/// MVP covers OR (`qbool tmp = a | b;`). Phase A adds the self-inverse
/// family — NOT is the first of these. Phase B adds the non-self-inverse
/// constant compound-assign family (`a += k;`, `a -= k;`, `a *= k;`,
/// `a /= k;` where k is classical). Each new kind must be handled by
/// every switch in the uncompute pass and emitter; absence of a `default:`
/// in those switches makes a missing case a build failure, which is the
/// intended contract.
enum class QOpKind {
    OR,
    // Phase E — second qbool bitwise kind. Same shape as OR (one result,
    // two named operands); enters the IR via the compound-expression
    // matcher in PE-4. Kept immediately after OR so switch cases that
    // group "qbool bitwise" kinds stay contiguous. The inverse is a
    // free-function call `uncompute_and(r, a, b);` declared in
    // include/sturm/uncompute/uncompute_api.hpp.
    AND,
    NOT,
    XOR,
    XOR_ASSIGN,
    // Phase B — constant compound-assigns. Each op still carries one
    // result QValueRef plus one operand QValueRef whose .name is the
    // verbatim RHS source text (classical integer literal / expression),
    // mirroring the PA-4 shape used for `a ^= c;` with classical c.
    ADD_ASSIGN_CONST,
    SUB_ASSIGN_CONST,
    MUL_ASSIGN_CONST,
    DIV_ASSIGN_CONST,
    // Phase C — qint-qint compound-assigns. Each op carries one result
    // QValueRef (the LHS qint) plus one operand QValueRef naming the RHS
    // qint identifier. The inverse is a free-function call of the form
    // `uncompute_{add,sub,mul,div,mod}_qint(lhs, rhs);` — see the PC-ir
    // section of docs/implementation_plan_transpiler_phase_c.md.
    ADD_ASSIGN_QINT,
    SUB_ASSIGN_QINT,
    MUL_ASSIGN_QINT,
    DIV_ASSIGN_QINT,
    MOD_ASSIGN_QINT,
    // Phase N — amplitude / phase rotation compound-assigns. Each op carries
    // one result QValueRef (the qint LHS that `theta()`/`phi()` dispatched
    // from) plus one operand QValueRef whose `.name` is the verbatim RHS
    // source text captured via `Lexer::getSourceText` (same PN pattern used
    // by Phase B for integer compound-assigns; the RHS is a double-valued
    // expression at the source level, treated opaquely by the matcher /
    // emitter). Inverses emit inline in `uncompute_pass.cpp` (Phase B style,
    // no free-function helper) because the runtime `ThetaProxy` /
    // `PhiProxy` `operator-=` already exists at
    // `include/sturm/qtypes/qint_core.hpp:305,372` and is self-dual.
    //
    // No `QOpKind::PREP` — P5 primitive 1 (`qbool(p)` preparation) is
    // handled via a Warning-severity diagnostic on `DiagContext` rather
    // than a first-class IR op (see
    // `docs/implementation_plan_transpiler_phase_n.md` §2, §5).
    //
    // See `docs/implementation_plan_transpiler_phase_n.md` §2-4.
    THETA_ADD_ASSIGN_CONST,
    THETA_SUB_ASSIGN_CONST,
    PHI_ADD_ASSIGN_CONST,
    PHI_SUB_ASSIGN_CONST,
    // Phase D — qint-qint comparisons producing a named qbool result
    // (`qbool c = a == b;`, etc.). Each op carries one result QValueRef
    // (the produced qbool) plus two operand QValueRefs naming the LHS/RHS
    // qint identifiers. The inverse is a free-function call of the form
    // `uncompute_{eq,ne,lt,le,gt,ge}_qint(c, a, b);` declared in
    // include/sturm/uncompute/uncompute_api.hpp, which re-dispatches to
    // the self-adjoint DSL comparators in include/sturm/lib/compare_dsl.hpp.
    // See docs/roadmap_transpiler_post_mvp.md Phase D.
    EQ_QINT,
    NE_QINT,
    LT_QINT,
    LE_QINT,
    GT_QINT,
    GE_QINT,
    // Phase I — user-defined routine call. Introduced by PI-2 to back the
    // `matcher_user_routine` module. A QOperation with this kind does NOT
    // have a single named `result`; instead every argument to the call is
    // listed in `operands` in source order, and `outputs_mask` flags which
    // of those operands are writable (i.e. non-const qbool&/qint& in the
    // callee's parameter list). `routine_name` holds the source-level
    // identifier of the callee FunctionDecl so the M8 uncompute pass (PI-4)
    // can look up the registered adjoint and emit
    // `invert(routine_name)(op0, op1, ...);`.
    //
    // Classical scalar arguments (int, double, verbatim expressions) are
    // still listed in `operands` — their QValueRef's `name` carries the
    // Lexer-extracted source text and `decl_loc` is invalid. This keeps
    // the operand list positional so the adjoint dispatch does not have
    // to reason about which slots were elided.
    //
    // Until PI-4 lands there is no render case for USER_ROUTINE in the
    // uncompute pass; the op is recorded in the IR (so dump() shows it)
    // but no inverse is emitted. PI-4 is tracked separately.
    USER_ROUTINE,
    // Phase J PJ-1c — zero-ancilla fusion. Seeded by the PJ-1d peephole
    // matcher (`matcher_ccnot_fuse`) from the adjacent pair
    //     qbool __t = a & b;
    //     x ^= __t;
    // when `__t` has exactly one reader. The fusion collapses the forward
    // CCX + uncompute CCX into a single CCX acting on `(a, b, x)` with no
    // intermediate qubit. Operand shape matches the existing binary qbool
    // kinds (OR / AND): one result QValueRef (the `x` target of the
    // in-place flip) plus two named operand QValueRefs (the two qbool
    // controls `a`, `b`). Self-adjoint — CCX is its own inverse — so the
    // M8 render case (PJ-1c) emits `ccnot_inplace(x, a, b);` verbatim at
    // the uncompute point, the same text the matcher inserts as the
    // forward QReplacement. The forward helper lives at
    // `include/sturm/uncompute/uncompute_api.hpp:118` (sturm-8cxd).
    CCNOT_INPLACE,
    // Phase M PM4-3 — plugin-registered op. Seeded by a third-party AST
    // matcher registered via `Registry::register_op(kind_id, matcher_fn,
    // render_fn)` (see `sturm/transpile/plugin_api.hpp`). The plugin's
    // matcher callback is responsible for populating the QOperation fields
    // (result, operands, stmt_range, etc.) appropriate to the plugin's
    // semantics; the only new invariant at the IR boundary is that
    // `plugin_kind_id` MUST be set to the same string the plugin passed
    // to `register_op`. The M8 uncompute pass's `case QOpKind::PLUGIN:`
    // arm consults the per-consumer Registry via
    // `Registry::find_render_fn(plugin_kind_id)` and invokes the returned
    // `UncomputeRenderFn` to produce the inverse source text. No in-tree
    // matcher ever constructs a `QOpKind::PLUGIN` op, so every existing
    // snapshot fixture stays byte-identical (the default string-empty
    // `plugin_kind_id` on non-plugin ops keeps the dump() format
    // unchanged — PLUGIN ops are printed with an explicit `<plugin
    // kind_id>` suffix so hand-built fixtures remain diagnosable).
    PLUGIN,
    // ... — added per post-MVP phases.
};

/// Symbolic reference to a named qbool / qint in user code.
///
/// Two QValueRefs compare equal only if both the name AND the declaration
/// location match. Name-only equality would alias shadowed locals across
/// nested scopes, which would silently corrupt the uncompute schedule.
struct QValueRef {
    std::string name;                  // e.g. "a", "tmp"
    clang::SourceLocation decl_loc;    // diagnostics + emitter insertion points
};

/// A single operation in the IR. One QOperation corresponds to one user
/// statement that the M7 matcher recognized (e.g. `qbool tmp = a | b;`).
///
/// - `kind`       : which quantum primitive was used.
/// - `result`     : the produced intermediate (a named qbool in user code).
/// - `operands`   : inputs in source order; references resolve by name + loc.
/// - `stmt_range` : full range of the originating statement, used by the
///                  M9 emitter to decide where to inject the uncompute code.
/// - `insert_before_override` : optional per-op anchor for the M8 synthesis
///                  pass (Phase F PF-1). Default-constructed (invalid)
///                  means: use the enclosing scope's `close_brace` as the
///                  insertion anchor (the legacy behaviour every Phase
///                  A..E matcher relies on). When valid (any non-zero
///                  raw encoding), `synthesize()` honours this location
///                  instead — used by Phase F's `WHEN(expr)` lift to plant
///                  the uncompute calls immediately past the WHEN body's
///                  closing brace, NOT at the enclosing CompoundStmt's
///                  close brace. Existing matchers leave it default, so
///                  every prior snapshot stays byte-identical.
/// - `hoist_to_override` : optional per-op uncompute anchor for Phase J
///                  PJ-3 (uncompute hoisting). Parallel to
///                  `insert_before_override` but dedicated to the hoisted
///                  case: when valid, the M8 synthesis pass plants the
///                  uncompute AFTER the loop end (at this location),
///                  while the FORWARD computation is moved BEFORE the
///                  loop begin by a matcher-owned QReplacement /
///                  raw_insertion pair (see PJ-3d). The PJ-3d matcher
///                  sets BOTH overrides on a hoisted op:
///                  `insert_before_override` = loop-begin location (for
///                  the forward compute anchor, the matcher's concern)
///                  and `hoist_to_override` = loop-enclosing scope's
///                  `close_brace` (for the post-loop uncompute anchor,
///                  `synthesize()`'s concern). `hoist_to_override` takes
///                  precedence over `insert_before_override` in
///                  `synthesize()`: if both are valid, the uncompute
///                  lands at `hoist_to_override` — if only
///                  `insert_before_override` is valid, the Phase F
///                  WHEN-lift behaviour is preserved unchanged.
///                  Default-constructed (invalid) means "not hoisted",
///                  and every Phase A..I snapshot fixture stays
///                  byte-identical.
/// - `skip_uncompute` : Phase H PH-3 flag. When true, the M8 synthesis
///                  pass emits NO UncomputeInsertion for this op — the
///                  op stays in the QIR (so `dump()` still shows it, and
///                  downstream consumers remain aware it was recognised),
///                  but no inverse is planted. The PH-3 matcher
///                  (`matcher_outer_var_guard.cpp`) sets this to true on
///                  compound-assign ops that mutate an outer-scoped
///                  qbool / qint from inside a for/while/if/WHEN body,
///                  where automatic uncomputation would require reverse-
///                  loop synthesis (contradicts P9). The matcher also
///                  emits a stderr diagnostic so the user knows a manual
///                  adjoint is required. Per-op flag, not per-scope — a
///                  scope can contain both "intermediate, uncompute
///                  normally" and "outer mutation, skipped" ops. Pre-
///                  Phase-H matchers leave the flag false, so every prior
///                  snapshot stays byte-identical.
struct QOperation {
    QOpKind kind;
    QValueRef result;
    std::vector<QValueRef> operands;
    clang::SourceRange stmt_range;
    clang::SourceLocation insert_before_override{};
    // Phase J PJ-3c: parallel uncompute-anchor override used by the PJ-3d
    // hoisting matcher. When valid, `synthesize()` plants the uncompute
    // at this location (AFTER the loop end), overriding both the legacy
    // `scope.close_brace` and the Phase F `insert_before_override`
    // (which carries the loop-BEGIN anchor for the matcher-owned forward
    // QReplacement). Default-constructed (invalid) preserves all prior
    // snapshots byte-identical — no pre-Phase-J matcher sets this.
    clang::SourceLocation hoist_to_override{};
    bool skip_uncompute = false;
    // Phase S S-B (sturm-ha2k.3): Phase S marker the Phase H PH-3
    // outer-var-guard matcher sets when the enclosing `FunctionDecl`
    // carries `[[sturm::reversible]]` AND the outer-scoped mutation
    // sits inside a `for` body. Unlike `skip_uncompute`, this flag
    // does NOT suppress emission — it hands the op off to Phase S's
    // `loop_reversal` module (sturm-ha2k.2) so the driver (R-C) can
    // emit a reversed-iteration adjoint loop around the op's
    // reverse-render. The PH-3 matcher never sets both flags on the
    // same op: in a reversible context it sets `needs_loop_reversal`
    // instead of `skip_uncompute`, preserving the PH-3 diagnostic
    // path bit-for-bit outside synthesis. Pre-Phase-S matchers leave
    // the flag false, so every prior snapshot fixture stays byte-
    // identical.
    bool needs_loop_reversal = false;
    // Phase I PI-2: source-level identifier of the callee FunctionDecl when
    // `kind == USER_ROUTINE`. Empty string for every other kind so existing
    // snapshot fixtures stay byte-identical (the dump() renderer omits the
    // suffix unless `kind == USER_ROUTINE`).
    std::string routine_name{};
    // Phase I PI-2: bitmask indicating which entries of `operands` are
    // output parameters (non-const qbool&/qint& in the callee signature).
    // Bit i corresponds to operands[i]. Zero for every non-USER_ROUTINE
    // op. Classical scalar and const-reference arguments remain 0 (input).
    // PI-4 pins the width at uint32_t — routines with >32 params are out
    // of scope. The M8 render case does not read individual bits; it
    // just iterates operands in source order.
    std::uint32_t outputs_mask = 0;
    // Phase M PM4-3: string key identifying which plugin-registered op
    // this is. Consulted by the M8 uncompute pass's
    // `case QOpKind::PLUGIN:` arm via
    // `Registry::find_render_fn(plugin_kind_id)`. Empty string for every
    // non-PLUGIN op so every existing snapshot fixture stays
    // byte-identical (the dump() renderer omits the suffix unless
    // `kind == PLUGIN`). Pre-PM4 matchers leave this default-empty; the
    // field is written only by plugin matchers that seed
    // `kind = QOpKind::PLUGIN`.
    std::string plugin_kind_id{};
};

/// One compound statement (curly-brace block) in the user's source.
///
/// `open_brace` and `close_brace` identify the scope uniquely — the M7
/// matcher uses them to locate-or-create a QScope when it sees a new op,
/// and the M8 pass uses them to anchor the uncompute-insertion point at
/// `close_brace`.
///
/// Ops are stored in source order. The uncompute pass iterates in reverse
/// to realize LIFO uncomputation.
struct QScope {
    clang::SourceLocation open_brace;
    clang::SourceLocation close_brace;
    std::vector<QOperation> ops;
};

/// A source-text replacement the matcher schedules for the M9 emitter to
/// apply ahead of its insertion pass. Introduced in Phase E (PE-2) to back
/// the compound-expression matcher, which decomposes a nested VarDecl
/// initializer into a flat decl sequence — the original VarDecl's source
/// range is replaced verbatim with the flattened text.
///
/// - `range` is the source range of the AST node being replaced
///   (typically the full range of the original compound VarDecl).
/// - `replacement` is the new source text that will be written in place
///   of that range. Callers are responsible for any trailing newline or
///   indentation — the emitter injects it byte-for-byte.
///
/// Replacements and insertions are disjoint by construction (a VarDecl
/// body vs. the scope's `close_brace`), so the emitter applies all
/// replacements first and then lays down the insertion pass without any
/// overlap-merging logic.
///
/// Defined in qir.hpp (and re-exported from uncompute_pass.hpp for
/// readability) because `QUnit` stores a vector of these, so the
/// type must be complete at the IR boundary.
struct QReplacement {
    clang::SourceRange range;
    std::string replacement;
};

/// One pre-staged insertion record describing a single uncompute call to
/// be applied verbatim by the M9 emitter. Mirrors the shape of the
/// `UncomputeInsertion` records the M8 synthesis pass produces from
/// QOperations — see uncompute_pass.hpp for the original definition and
/// the M9 emitter contract. The struct is duplicated here (not just the
/// type alias) because `QUnit::raw_insertions` is a vector of complete
/// objects, so the type must be complete at the IR boundary, the same
/// rationale as `QReplacement`.
///
/// Phase F PF-1 introduces this type to back the WHEN-lift matcher's
/// "decl block" injection: the matcher assembles the flattened qbool
/// declaration block as a single insertion record and the M8 pass
/// concatenates it into its `QSynthesisResult.insertions` verbatim,
/// without rendering it from a synthetic `QOpKind`. Pre-Phase-F
/// matchers leave `QUnit::raw_insertions` empty.
struct UncomputeInsertion {
    clang::SourceLocation insert_before;
    std::string code;
};

/// Top-level container: one per translation unit. Scopes are stored in
/// source order (by `open_brace` location). The matcher appends as it
/// walks the AST, so no post-hoc sort is required.
///
/// `replacements` carries source-text replacements the matcher schedules
/// for the M9 emitter to apply before its insertion pass. Pre-Phase-E
/// (MVP + Phases A..D) this vector is always empty; Phase E's compound
/// matcher is the first producer.
///
/// `raw_insertions` carries pre-staged insertion records the matcher
/// assembles directly (Phase F PF-1). The M8 synthesis pass concatenates
/// these into its `QSynthesisResult.insertions` verbatim, after the
/// per-op renderings produced from QOperation entries. Pre-Phase-F
/// (MVP + Phases A..E) this vector is always empty, so existing snapshot
/// fixtures stay byte-identical.
///
/// `fused_stmt_ranges` carries source ranges of statements the Phase J
/// PJ-1d ccnot-fuse peephole has absorbed into a single `ccnot_inplace`
/// call. Each entry is the `clang::SourceRange` of the SECOND statement
/// of a fused pair (the `x ^= __t;` stmt in the canonical
/// `qbool __t = a & b; x ^= __t;` shape). Downstream matchers — the
/// Phase A PA-3/PA-4 xor-assign matchers in
/// `matcher_qbool_assign.cpp` and the Phase E compound matcher in
/// `matcher_qbool_compound.cpp` — consult this list and early-return
/// on any match whose own `stmt_range` lies inside one of these
/// entries, preventing double-emission on the fused pair. Pre-Phase-J
/// (MVP + Phases A..I) this vector is always empty, so every existing
/// snapshot fixture stays byte-identical.
///
/// `eliminated_stmt_ranges` carries source ranges of statements the
/// Phase J PJ-4a dead-ancilla eliminator has deleted verbatim from the
/// output. Each entry is the `clang::SourceRange` of the dead `qbool`
/// VarDecl statement whose reader-count within its enclosing scope is
/// zero (e.g. `qbool __t = a | b;` with no subsequent `__t` reference
/// in the same scope). The matcher emits one `QReplacement{range=decl
/// stmt range, replacement=""}` per elimination AND pushes the same
/// range here so downstream matchers — the Phase A PA-3/PA-4
/// xor-assign matchers in `matcher_qbool_assign.cpp` and the Phase E
/// compound matcher in `matcher_qbool_compound.cpp` — can early-return
/// on any match whose own `stmt_range` lies inside one of these
/// entries. The containment probe is the same `is_range_covered_by_*`
/// helper shape PJ-1e uses; the two vectors are kept separate so the
/// PJ-1d / PJ-4a matchers can be reasoned about independently and so
/// `dump()`-adjacent tooling can report which optimization absorbed
/// which statement. Pre-Phase-J (MVP + Phases A..I) this vector is
/// always empty, so every existing snapshot fixture stays byte-
/// identical.
struct QUnit {
    std::vector<QScope>              scopes;
    std::vector<QReplacement>        replacements;
    std::vector<UncomputeInsertion>  raw_insertions;
    std::vector<clang::SourceRange>  fused_stmt_ranges;
    std::vector<clang::SourceRange>  eliminated_stmt_ranges;
};

/// Equality on QValueRef: both the name and the decl_loc must match.
/// See the QValueRef struct docstring for why.
bool operator==(const QValueRef& lhs, const QValueRef& rhs);

inline bool operator!=(const QValueRef& lhs, const QValueRef& rhs) {
    return !(lhs == rhs);
}

/// Produce a stable textual representation of `unit`. See the file-level
/// comment for the exact golden format. Output is deterministic: calling
/// dump() twice on the same QUnit yields byte-identical strings.
std::string dump(const QUnit& unit);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QIR_HPP
