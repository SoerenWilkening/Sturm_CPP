// transpile_consumer.cpp — PM1-1: implementation of the shared consumer.
//
// The bulk of the matcher registration block and the post-walk backstop
// sequencing were pulled verbatim out of main.cpp so the byte-identical
// gate that fronts this issue survives intact. Only the emit step at the
// tail of HandleTranslationUnit branches on EmissionMode — StandaloneFile
// preserves the pre-PM1 `emit()` call shape; Plugin mode stashes the
// rewritten buffer via emit_to_string for the wrapping PluginASTAction
// to hand to a nested CompilerInvocation (PM1-3 / PM1-4).

#include "transpile_consumer.hpp"

#include "sturm/transpile/emitter.hpp"
#include "sturm/transpile/io.hpp"
#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/skip.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

// sturm-v0ur (LO-2 wiring): consumer-only headers for the lossy
// rewrite pipeline. The matcher (LO-2a) produces hits during
// `matchAST`; the emitters (LO-2b/2c) drain those hits after the
// walk, BEFORE `synthesize()`.
#include "fresh_names.hpp"
#include "lossy_rewrite_emitter.hpp"
#include "lossy_scope_exit_emitter.hpp"

#include "matcher_reversible_drive.hpp"
#include "matcher_user_routine.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace sturm::transpile {

TranspileConsumer::TranspileConsumer(clang::CompilerInstance& ci,
                                     EmissionMode mode,
                                     std::string source_path,
                                     std::string output_dir,
                                     std::string dump_transpiled_path)
    : ci_(ci),
      mode_(mode),
      source_path_(std::move(source_path)),
      output_dir_(std::move(output_dir)),
      dump_transpiled_path_(std::move(dump_transpiled_path)),
      // PM3-0: capture the parent CompilerInstance's DiagnosticsEngine
      // into the shared diag context. Matchers that fire PM3
      // diagnostics receive a reference to this member; the PM3-2
      // sub-issue cashed in the first slot by wiring the PH-3
      // outer-var guard through it.
      diag_(ci.getDiagnostics()) {
    // Phase I PI-1: the routine registry matcher runs first so the
    // map is built before any PI-2+ routine-call matcher consults
    // it. Placing registration at the top of the consumer body
    // documents the ordering invariant. Registration order among
    // MatchFinder callbacks affects callback invocation order only
    // for a single matched node; the routine-registry and Phase
    // A–H matchers match disjoint AST shapes (the former fires on
    // ClassTemplateSpecializationDecl, the latter on expressions /
    // VarDecls), so the registry is naturally populated as soon as
    // the first `adjoint_of<...>` specialization is visited during
    // the AST walk.
    sturm::transpile::register_routine_registry_matcher(finder_, registry_);
    // Phase T T-1 (sturm-xrob.2): the reversible-drive collector
    // matcher records every `[[clang::annotate("sturm::reversible")]]`
    // FunctionDecl into `synth_registry_` so the end-of-TU driver
    // (`drive_reversible_forwards`, invoked from
    // `HandleTranslationUnit`) can iterate the forwards and emit
    // auto-synthesised `__fn_adj` + `STURM_REGISTER_ADJOINT` lines.
    // Registered immediately after the routine-registry matcher
    // because the drive-phase consults the PI-1 `registry_` for the
    // PRD §9 Q2 hand-registration precedence gate — the collector's
    // pattern (FunctionDecl anchor) is structurally disjoint from the
    // routine-registry matcher's anchor
    // (ClassTemplateSpecializationDecl), so callback-invocation order
    // is irrelevant for correctness; the placement is documentation-
    // level grouping with the other "synthesis-registry populating"
    // callbacks.
    sturm::transpile::register_reversible_drive_matcher(
        finder_, synth_registry_, registry_, diag_,
        ci.getSourceManager(), ci.getLangOpts());
    // Phase J PJ-4b: the dead-ancilla elimination matcher runs
    // BEFORE every matcher whose AST anchor could overlap an
    // eliminated qbool VarDecl. The ordering invariant has three
    // load-bearing edges:
    //
    //   1. BEFORE register_or_matcher (MVP) and the Phase A
    //      bitwise VarDecl-init matchers register_not_matcher
    //      (PA-1) and register_xor_matcher (PA-2). All three
    //      anchor on the SAME qbool VarDecl shape PJ-4a may
    //      eliminate — a `qbool t = a | b;` / `~a;` / `a ^ b;`
    //      decl with zero readers. The PJ-4a callback populates
    //      `unit_.eliminated_stmt_ranges` with the decl's full
    //      stmt range; each downstream callback's
    //      `is_range_covered_by_fused` probe against that list
    //      then early-returns on a covered VarDecl so the M8
    //      pass does not render an uncompute against a decl
    //      the Rewriter has already deleted. Registering PJ-4a
    //      first maximises the odds MatchFinder invokes its
    //      callback before the downstream Decl-pool callbacks
    //      for the same VarDecl, letting the fast-path guard
    //      fire. The `apply_eliminated_stmt_guards` post-matcher
    //      cleanup pass (called below, between `matchAST` and
    //      `synthesize`) is the authoritative backstop for the
    //      cases where MatchFinder interleaves Decl callbacks
    //      in the other order.
    //
    //   2. BEFORE register_compound_qbool_matcher (PE-4). A
    //      compound init (`qbool t = (a | b) & c;`) is also
    //      eligible for PJ-4a elimination when `t` has zero
    //      readers, and PE-4's VarDecl-init anchor overlaps
    //      PJ-4a's on that shape. PE-4's callback consults
    //      `eliminated_stmt_ranges` with the same
    //      `is_range_covered_by_fused` probe and bails on a
    //      covered decl.
    //
    //   3. BEFORE the Phase A `a ^= b;` assign matchers
    //      register_xor_assign_matcher (PA-3) and
    //      register_xor_assign_classical_matcher (PA-4). These
    //      are Stmt-anchored, not Decl-anchored, so they do not
    //      overlap PJ-4a's VarDecl anchor on the matched node
    //      itself. The ordering still matters in the
    //      `apply_eliminated_stmt_guards` backstop's favour —
    //      if a user ever writes a `t ^= <expr>;` statement
    //      immediately after an eliminated `qbool t = a | b;`
    //      decl, the PJ-4a guard on the `^=` op's stmt_range
    //      (which would lie OUTSIDE the decl's range, so not
    //      technically covered) is a no-op — but the same
    //      guards on the PA-3 callback sit alongside the PJ-4a
    //      VarDecl guards in matcher_qbool_assign.cpp, and
    //      registering PJ-4a first keeps the two guards
    //      symmetric in source order.
    //
    // None of these edges are hard correctness constraints on
    // their own — the post-matcher cleanup
    // (`apply_eliminated_stmt_guards`, called below) and the
    // per-matcher range guards make the pipeline robust to
    // MatchFinder's Decl/Stmt interleaving — but the ordering
    // documented here is the happy-path schedule, and
    // downstream blocks (sturm-0v9i PJ-3e which stacks the
    // hoist matcher LAST on top of this chain) rely on it
    // staying stable.
    sturm::transpile::register_dead_ancilla_matcher(finder_, unit_);
    sturm::transpile::register_or_matcher(finder_, unit_);
    sturm::transpile::register_not_matcher(finder_, unit_);
    sturm::transpile::register_xor_matcher(finder_, unit_);
    // Phase J PJ-1f: the zero-ancilla fusion peephole matcher runs
    // BEFORE every matcher whose AST anchor could overlap a fused
    // pair's statements. The ordering invariant has three load-
    // bearing edges:
    //
    //   1. BEFORE register_xor_assign_matcher (PA-3). The PJ-1d
    //      callback populates `unit_.fused_stmt_ranges` with the
    //      second statement's range (`x ^= __t;`) when it fuses
    //      a pair; the PA-3 callback's `is_range_covered_by_fused`
    //      early-return guard suppresses its own push when that
    //      range is already listed. Registering PJ-1d first
    //      maximises the odds MatchFinder invokes its callback
    //      before PA-3's for the same enclosing node, letting
    //      the fast-path guard fire. The `apply_fused_stmt_guards`
    //      post-matcher cleanup pass (called below, between
    //      `matchAST` and `synthesize`) is the authoritative
    //      backstop for the cases where MatchFinder interleaves
    //      Decl and Stmt callbacks in the other order.
    //
    //   2. BEFORE register_compound_qbool_matcher (PE-4). The
    //      PE-4 matcher's own AST pattern already requires AT
    //      LEAST ONE nested op-call argument — mutually exclusive
    //      with PJ-1d's bare-DRE-only pattern — so the two
    //      matchers are structurally disjoint on any single
    //      VarDecl. Registering PJ-1d first is defensive: if a
    //      future PJ-1 relaxation admits nested init, PE-4's
    //      `is_range_covered_by_fused` guard catches the overlap
    //      without requiring main.cpp to be re-ordered.
    //
    //   3. BEFORE register_outer_var_guard_matcher (PH-3). PH-3
    //      post-processes `unit_.scopes` to flag compound-assign
    //      ops with `skip_uncompute=true` when their target is
    //      declared in an outer scope. On a fused pair PJ-1d
    //      replaces the `x ^= __t;` stmt's contribution with a
    //      `QOperation{kind=CCNOT_INPLACE}` (NOT an XOR_ASSIGN),
    //      and `apply_fused_stmt_guards` removes any stale
    //      XOR_ASSIGN op from the scope. Registering PJ-1d
    //      before PH-3 ensures the fuse decision is locked in by
    //      the time PH-3 walks the ops — PH-3 would otherwise see
    //      the not-yet-suppressed XOR_ASSIGN and potentially flag
    //      a mutation target that the fused output no longer
    //      compound-assigns to.
    //
    // None of these edges are hard correctness constraints on
    // their own — the post-matcher cleanup and per-matcher
    // range guards make the pipeline robust to MatchFinder's
    // Decl/Stmt interleaving — but the ordering documented here
    // is the happy-path schedule, and downstream blocks
    // (sturm-0v9i, sturm-6a2z) rely on it staying stable.
    sturm::transpile::register_ccnot_fuse_matcher(finder_, unit_);
    sturm::transpile::register_xor_assign_matcher(finder_, unit_);
    sturm::transpile::register_xor_assign_classical_matcher(
        finder_, unit_);
    // PM4-6: the Phase B (PB-1..PB-4) qint_t compound-assign matchers
    // (ADD/SUB/MUL/DIV_ASSIGN_CONST) used to be registered here with
    // four direct `register_*_matcher(finder_, unit_)` calls. They have
    // been dogfood-migrated to the plugin Registry API
    // (`STURM_REGISTER_PLUGIN(PBDogfoodPlugin)` in
    // `matcher_qint_const.cpp`). The drain + invoke_all block below
    // picks them up at link time and registers them against `finder_`
    // in the same relative position (after PA-4 xor-assign-classical,
    // before PC-1 add-assign-qint) they occupied pre-PM4-6. Byte-
    // identical snapshot invariance is preserved — see the
    // `STURM_REGISTER_PLUGIN` comment at the bottom of
    // `matcher_qint_const.cpp` for the full ordering rationale.
    sturm::transpile::register_add_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_sub_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_mul_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_div_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_mod_assign_qint_matcher(finder_, unit_);
    // sturm-v0ur (LO-2 wiring): register the lossy compound-assign
    // matcher (LO-2a). Anchors on `CXXOperatorCallExpr` for `qint *=`,
    // `/=`, `%=`, `&=`, `|=`. Records a `LossyOpHit` per match into
    // `lossy_hits_`; the consumer drains the vector after `matchAST`
    // returns and before `synthesize()` so the LO-2b forward triplet
    // becomes a `QReplacement` in `unit_.replacements` and the LO-2c
    // cleanup becomes one external-cleanup record per enclosing
    // block. Coexistence with the Phase C qint-qint compound-assign
    // matchers above is intentional and structurally safe — Phase C
    // pushes a `QOpKind::*_ASSIGN_QINT` op into `unit_.scopes`,
    // LO-2a records a `LossyOpHit` in a disjoint sink. The post-walk
    // wiring below DELETES the corresponding Phase C op from the
    // QUnit when an LO hit fires on the same call (so the Phase C
    // `uncompute_*_qint` shim isn't double-emitted alongside the LO
    // forward triplet + cleanup pair).
    sturm::transpile::register_lossy_op_matcher(finder_, lossy_hits_);
    sturm::transpile::register_eq_compare_qint_matcher(finder_, unit_);
    sturm::transpile::register_ne_compare_qint_matcher(finder_, unit_);
    sturm::transpile::register_lt_compare_qint_matcher(finder_, unit_);
    sturm::transpile::register_le_compare_qint_matcher(finder_, unit_);
    sturm::transpile::register_gt_compare_qint_matcher(finder_, unit_);
    sturm::transpile::register_ge_compare_qint_matcher(finder_, unit_);
    sturm::transpile::register_compound_qbool_matcher(finder_, unit_);
    sturm::transpile::register_when_lift_matcher(finder_, unit_);
    sturm::transpile::register_when_nested_matcher(finder_, unit_);
    // Phase H PH-2: the brace-wrap matcher appends `{` + `}` raw
    // insertions for braceless for/while/if/else bodies containing
    // quantum ops. Order relative to the per-op matchers does NOT
    // matter: raw insertions are concatenated at the END of the
    // M8 synthesis pass's insertion vector and the M9 emitter's
    // reverse-iteration stacks them correctly against co-located
    // per-op insertions at the same SourceLocation. Registered
    // here, immediately before PH-3, so the brace-wrap anchors
    // land alongside the Phase F / G WHEN matchers' raw insertions
    // for diagnostic clarity.
    sturm::transpile::register_brace_wrap_matcher(finder_, unit_);
    // ── PM4-3 / PM4-6: drain plugin registrars BEFORE PH-3 ─────────────────
    //
    // Drain position is load-bearing. MatchFinder invokes callbacks in
    // registration order on a matched node, and the PH-3 outer-var
    // guard (registered immediately below) consults `unit_.scopes` for
    // the QOperation each Phase A/B/C compound-assign matcher pushed.
    // The Phase B matchers (PB-1..PB-4) now register through the
    // plugin Registry API (PM4-6 dogfood — `STURM_REGISTER_PLUGIN` in
    // `matcher_qint_const.cpp`), so the drain MUST fire before PH-3 or
    // the PB ops will be absent when PH-3's callback runs on the same
    // `a += C;` AST node.
    //
    // Plan §6 ordering within the drain: runtime-dlopen plugins first,
    // then link-time plugins. Two reasons for this direction:
    //
    //   - Runtime plugins, loaded via `plugin.cpp`'s `load=<path>`
    //     branch (PM4-4), may depend on the in-tree matcher set being
    //     fully in place (they run AFTER all pre-drain in-tree
    //     registrations above); they may NOT depend on any other
    //     plugin's matcher being registered first. Draining runtime-
    //     dlopen first gives link-time plugins a stable foundation to
    //     sit on top of.
    //
    //   - Link-time plugins are baked into the binary at static-init
    //     time (via `STURM_REGISTER_PLUGIN`) — typically custom-build
    //     registrars like the PM4-6 PB dogfood and the PM4-10 demo
    //     shim. Running them after runtime-dlopen plugins mirrors the
    //     "most-specific wins" posture used elsewhere in the matcher
    //     registration block (Phase J peepholes registered before
    //     their Phase A/E analogues, etc.).
    //
    // Both drains are idempotent w.r.t. THIS Registry — the collision
    // detection in `register_matcher` / `register_op` guarantees that
    // re-invocation (PM1-4 nested consumer) cannot double-register
    // within a single Registry. The Meyer vectors themselves are not
    // cleared between consumer constructions: each consumer gets a
    // fresh, independent view of the registrar set.
    //
    // `runtime_registrars()` is appended by `plugin.cpp` after a
    // successful `dlopen` + Clang-version check + `dlsym`.
    // `registrars()` (the link-time Meyer's singleton) is appended at
    // static-init time by every `STURM_REGISTER_PLUGIN(TypeName)`
    // declaration in the TUs this binary links.
    //
    // `invoke_all` at the tail of this block fires every registered
    // `MatcherRegisterFn` against the shared `finder_` + `unit_`,
    // installing the plugin matchers on the same MatchFinder the
    // in-tree matchers are bound to. Render functions registered via
    // `register_op` are consulted by the M8 synthesis pass below when
    // it encounters a `QOpKind::PLUGIN` op.
    for (const auto& fn :
         ::sturm::transpile::plugin::runtime_registrars()) {
        fn(plugin_registry_);
    }
    for (const auto& fn : ::sturm::transpile::plugin::registrars()) {
        fn(plugin_registry_);
    }
    plugin_registry_.invoke_all(finder_, unit_);

    // Phase H PH-3: the outer-variable-mutation guard must run AFTER
    // the Phase A / B / C compound-assign matchers have populated
    // `unit_.scopes` — the callback looks up the QOperation each
    // A/B/C matcher pushed by `stmt_range.getBegin()` and flags its
    // `skip_uncompute` field. MatchFinder invokes callbacks in
    // registration order on a given node, so placing this register
    // call LAST among the mutation matchers is the load-bearing
    // ordering invariant for PH-3.
    //
    // PM4-6 adds a new edge: the Phase B matchers are now registered
    // via the plugin Registry drain immediately above, so the drain
    // block must remain above THIS registration point. Moving PH-3
    // earlier would re-introduce the nullity race in
    // `matcher_outer_var_guard.cpp`'s `find_op_for_call` against any
    // PB op.
    sturm::transpile::register_outer_var_guard_matcher(
        finder_, unit_, diag_);
    // Phase N PN-5: the qbool(p) prep diagnostic matcher is a pure
    // diagnostic — it does NOT mutate `unit_` and does not interact
    // with any per-op or post-processor matcher above. Registration
    // order is irrelevant for correctness (the VarDecl anchor is
    // structurally disjoint from every mutation-op anchor above), but
    // parking it RIGHT AFTER `register_outer_var_guard_matcher` groups
    // the "quantum-specific diagnostic matchers that consult the
    // matcher IR" visually — PH-3 flags outer-scoped mutations inside
    // loops/branches, PN-5 flags probabilistic preparation inside
    // WHEN bodies / compound-expression intermediate scopes. Both
    // flag P9-violation shapes; both route through the shared
    // `DiagContext`. Downstream PM3-family diagnostic matchers
    // (PM3-4 WHEN operand mutation, PM3-5 quantum->classical cast,
    // PM3-6 dropped quantum return) anchor on entirely different AST
    // shapes (IfStmt / cast / CallExpr) so their registration is
    // commutative with PN-5.
    sturm::transpile::register_qbool_prep_matcher(
        finder_, unit_, diag_);
    // Phase I PI-2: the user-defined-routine call matcher runs
    // after the Phase H PH-2 brace-wrap matcher, after the Phase
    // A/B/C compound-assign matchers, and after PH-3's outer-var
    // guard. The issue description locks this ordering in so PH-3
    // gets first crack at any qbool/qint mutation shapes, leaving
    // PI-2 to pick up only the clean routine-call anchors that
    // survive. Ordering is not a correctness requirement — PI-2's
    // AST anchor (`callExpr` on a registered FunctionDecl) is
    // structurally disjoint from every Phase A..H matcher anchor —
    // but placing it here keeps diagnostic output grouped by phase.
    sturm::transpile::register_user_routine_matcher(
        finder_, unit_, registry_, diag_);
    // Phase J PJ-3e + Phase M PM5-6: the uncompute-hoisting matcher
    // (PJ-3d) runs LAST among the IR-mutating matchers, immediately
    // followed by the peephole gate-reorder matcher (PM5-5) which
    // runs LAST overall — after every Phase A..I per-op matcher
    // (MVP OR, PA-1/PA-2 bitwise, PA-3/PA-4 xor-assign, PB/PC qint
    // compound-assigns, PD qint compares, PE-4 compound-qbool, PF
    // WHEN-lift, PG WHEN-nested, PH-2 brace-wrap, PH-3 outer-var
    // guard, PI-2 user-routine), after the PJ-1f zero-ancilla fuse
    // peephole (registered above), after the PJ-4b dead-ancilla
    // eliminator (registered above), AND after the PJ-3d uncompute
    // hoist (registered on the next line). The ordering invariant
    // has three load-bearing edges:
    //
    //   1. AFTER every per-op matcher. The PJ-3d callback is
    //      anchored on `translationUnitDecl()` and does its real
    //      work in `onEndOfTranslationUnit()` — after MatchFinder
    //      has finished the entire AST walk. That timing makes
    //      registration order among per-node callbacks irrelevant
    //      for correctness (the hoist callback runs once, after
    //      every per-node callback has fired). Registering LAST
    //      is therefore a diagnostic-grouping convention that
    //      keeps the per-phase matcher callback pool contiguous
    //      above the post-processor, but it is also a forward-
    //      looking guard: if a future PJ-3e+ relaxation swaps the
    //      post-processing idiom for a per-node anchor, the LAST
    //      registration keeps the invariant that the hoist
    //      callback sees fully-populated `unit_.scopes` without
    //      requiring main.cpp to be re-ordered.
    //
    //   2. AFTER register_ccnot_fuse_matcher (PJ-1f) and AFTER
    //      register_dead_ancilla_matcher (PJ-4b). Both of those
    //      peepholes can mutate `unit_.scopes` — PJ-1f REPLACES
    //      a pair of `qbool __t = a & b; x ^= __t;` ops with a
    //      single CCNOT_INPLACE op (not a decl-producing kind,
    //      so `is_decl_producing_kind` rejects it on the hoist
    //      path), and PJ-4b ERASES ops whose VarDecl has zero
    //      readers. Running PJ-3d after both eliminators means
    //      the hoist matcher observes the FINAL op list — it
    //      never tries to hoist a CCNOT_INPLACE fused pair (it
    //      can't — the kind guard filters it) and never tries
    //      to hoist an op that is about to be deleted (it
    //      can't — the `apply_eliminated_stmt_guards` backstop
    //      above has already dropped the op from
    //      `unit_.scopes`). The backstop order discipline in
    //      `HandleTranslationUnit` below (fused guards before
    //      eliminated guards before `synthesize`) is what
    //      actually enforces this sequencing at runtime; the
    //      registration order here is documentation of the
    //      happy-path schedule.
    //
    //   3. LAST — after register_hoist_invariant_matcher. The
    //      reorder matcher observes the post-fuse / post-hoist /
    //      post-dead-ancilla op list and reorders adjacent triples
    //      only when all three of (A, B, C) survive the prior
    //      passes' guards. Bails on any hoisted op, any fused
    //      range, any eliminated range, any plugin-op boundary.
    //
    //      WHY reorder MUST run AFTER hoist (not before):
    //      reorder produces NO new `QOperation`s — it only
    //      shuffles the source-range assignments of statements
    //      that are already in `unit_.scopes`, emitting
    //      `QReplacement` objects that touch DISJOINT text
    //      regions from PJ-1d's fused-pair replacement. If
    //      reorder fired BEFORE hoist, the hoist pass could
    //      then migrate A (the `qbool __t = a & b;` head) out
    //      of the loop while B remains dangling past C with A
    //      gone — the hoist invariant ("forward/uncompute pair
    //      stays paired") would break. Running AFTER hoist
    //      means A is either not hoisted (safe to reorder) or
    //      hoisted (the triple bails at Gate 2's
    //      `hoist_to_override.isInvalid()` check). Either way,
    //      no cross-pass interaction bug.
    //
    //      WHY reorder MUST run AFTER fuse + dead-ancilla (as
    //      well): PJ-1d's fuse emission and PJ-4b's elimination
    //      BOTH mutate the op list reorder reads. A reorder
    //      fired before either pass would see a stale op list
    //      whose triples may already be doomed (fuse has
    //      absorbed A+C into a CCNOT_INPLACE op, or PJ-4b has
    //      marked A for elimination). Gate 2's
    //      `is_range_covered_by_fused` + eliminated-range probe
    //      is the guard, but the guard's argument vectors are
    //      only fully populated after their producing matchers
    //      have run — which (per the ordering block above) is
    //      before the LAST-group matchers. Running reorder LAST
    //      means `fused_stmt_ranges` and `eliminated_stmt_ranges`
    //      are final by the time Gate 2 consults them.
    //
    //      WHY reorder produces no new fusion opportunities
    //      that PJ-1d could absorb: reorder's single-pass
    //      design emits `QReplacement` text in which A and C
    //      are now adjacent, but PJ-1d's AST-anchored callback
    //      has already fired; it does NOT re-run against the
    //      rewritten buffer. The fuse condition reorder checks
    //      at Gate 4 (via `detail::count_readers_in_scope`) is
    //      therefore a PAYOFF probe — "would this pair have
    //      fused if B weren't between them?" — not a re-entrant
    //      call into PJ-1d. The next transpile invocation (the
    //      user's next build) will observe the rewritten source
    //      and let PJ-1d absorb the now-adjacent pair.
    //
    //      WHY running reorder AFTER hoist captures MORE
    //      opportunities, not fewer: hoist migrates loop-
    //      invariant ops out of the loop body, which SHRINKS
    //      the in-body op list. When a hoisted op sat between
    //      a surviving `(A, C)` pair inside the loop body, the
    //      remaining in-body ops close the gap — a new
    //      adjacency the reorder pass can now exploit at Gate
    //      1, that did NOT exist before hoist fired. Running
    //      peephole reorder BEFORE hoist would miss every such
    //      newly-exposed triple, because the pre-hoist in-body
    //      list still contained the invariant op wedged between
    //      A and C. So the LAST ordering is not only safe (the
    //      prior paragraph's hoist-invariant argument) but also
    //      maximally-productive: reorder observes the final,
    //      post-every-other-pass op list, letting it find every
    //      adjacency opportunity hoist exposes.
    //
    // Downstream blocks (sturm-8cwe PJ-3f snapshot fixtures,
    // sturm-u655.8 PM5-8 reorder snapshots) rely on this
    // ordering staying stable.
    sturm::transpile::register_hoist_invariant_matcher(finder_, unit_);
    sturm::transpile::register_peephole_reorder_matcher(finder_, unit_);
    // PM3-4: Class 1 — WHEN operand mutation. Pure diagnostic matcher;
    // advisory only, does NOT mutate `unit_`. Registration order is
    // irrelevant for correctness because the callback anchors on the
    // WHEN macro's middle IfStmt (the same pattern the Phase F / G
    // WHEN matchers use) — structurally disjoint from every per-op
    // callback above. Grouped with the other PM3-family diagnostic
    // matchers at the bottom of the registration block. The
    // DiagContext threaded here is the shared `diag_` member captured
    // from the parent CompilerInstance's DiagnosticsEngine; under
    // StandaloneFile mode it routes through the PM3-1
    // `TextDiagnosticPrinter(llvm::errs(), ...)` so the user sees
    // the Error on stderr.
    sturm::transpile::register_when_operand_mutation_matcher(
        finder_, unit_, diag_);
    // PM3-5: Class 2 — quantum -> classical in branch condition. Pure
    // diagnostic matcher; advisory only, does NOT mutate `unit_`.
    // Anchors on explicit casts (`static_cast<bool>`, C-style, or
    // functional) from a qbool / qint_t; on match the callback walks
    // ASTContext::getParents toward the nearest control stmt and
    // fires iff the cast reached the stmt's cond slot AND the stmt is
    // not WHEN-expanded. Registration order is irrelevant for
    // correctness — the cast AST shape is structurally disjoint from
    // every per-op callback above. Grouped with the other PM3-family
    // diagnostic matchers at the bottom of the registration block.
    sturm::transpile::register_quantum_to_classical_cond_matcher(
        finder_, unit_, diag_);
    // PM3-6: Class 4 — caller drops returned qbool / qint_t. This
    // matcher is a pure diagnostic; it does not mutate `unit_` and
    // does not interact with any per-op or post-processor matcher
    // above. Registration order is therefore irrelevant for
    // correctness — we park it LAST so the PM3-family callbacks stay
    // grouped at the bottom of the registration block. The
    // DiagnosticsEngine threaded here is the shared one attached to
    // the parent CompilerInstance; under StandaloneFile mode it
    // routes through the PM3-1 `TextDiagnosticPrinter(llvm::errs(),
    // ...)` so the user sees the Warning on stderr.
    sturm::transpile::register_dropped_quantum_return_matcher(
        finder_, ci_.getDiagnostics());

    // PM4-6: the PM4-3 plugin-registry drain + `invoke_all` call is
    // hoisted to BEFORE `register_outer_var_guard_matcher` above, so
    // plugin matchers (including the dogfood PB-1..PB-4 family migrated
    // in PM4-6) land in `finder_` before PH-3 and its sibling PM3
    // diagnostics. See the drain block's comment for the full
    // ordering rationale and the nullity-race argument.
}

void TranspileConsumer::HandleTranslationUnit(clang::ASTContext& ctx) {
    // M7: populate the QUnit via the match finder.
    finder_.matchAST(ctx);

    // Phase J PJ-1e: backstop cleanup for the ccnot-fuse peephole.
    // `MatchFinder::matchAST` does not strictly pre-order callbacks
    // across Decl and Stmt matcher pools, so the Stmt-anchored PA-3
    // / PA-4 / PE-4 callbacks can fire BEFORE the Decl-anchored
    // PJ-1d callback that populates `unit_.fused_stmt_ranges`.
    // Their in-callback early-return only fires when the fused
    // entry is already present, so we do one final pass over
    // `unit_.scopes` here to remove any op whose stmt_range is
    // covered but whose matcher ran before PJ-1d. No-op when
    // `fused_stmt_ranges` is empty (pre-Phase-J shapes).
    sturm::transpile::apply_fused_stmt_guards(
        unit_, ctx.getSourceManager());

    // Phase J PJ-4a: backstop cleanup for the dead-ancilla
    // eliminator. Same rationale as `apply_fused_stmt_guards` —
    // MatchFinder's Decl/Stmt visit-pool interleaving means a
    // downstream Decl-anchored callback (MVP OR, PA-1 NOT,
    // PA-2 XOR, PE-4 compound) can fire BEFORE the PJ-4a
    // callback populates `eliminated_stmt_ranges`. Their
    // in-callback early-return only fires when the eliminated
    // entry is already present, so we do one final pass over
    // `unit_.scopes` here to remove any op whose stmt_range is
    // covered by an eliminated range but whose matcher ran
    // before PJ-4a. No-op when `eliminated_stmt_ranges` is
    // empty (pre-Phase-J shapes). Called AFTER
    // `apply_fused_stmt_guards` so the fuse-aware whitelisting
    // (which retains CCNOT_INPLACE ops inside fused pair
    // ranges) happens before the unconditional elimination
    // filter — an op that survives the fuse cleanup is still
    // subject to the elimination cleanup, which is the
    // correct ordering when both peepholes target the same
    // VarDecl.
    sturm::transpile::apply_eliminated_stmt_guards(
        unit_, ctx.getSourceManager());

    // sturm-v0ur (LO-2 wiring): drain `lossy_hits_` produced by the
    // LO-2a matcher. Per hit:
    //
    //   1. Compute the LO-2b forward triplet via `emit_lossy_forward`
    //      (using a per-TU `FreshNameAllocator` so suffixes are
    //      monotonic across hits).
    //   2. Replace the user's `*=`/`/=`/`%=`/`&=`/`|=` source range
    //      with the triplet text via a `QReplacement` pushed onto
    //      `unit_.replacements`.
    //   3. Suppress any Phase C `*_ASSIGN_QINT` op the qint-qint
    //      matchers may have pushed for the same call so the LO
    //      forward triplet + cleanup pair is the SOLE emission for
    //      that compound-assign — no `uncompute_*_qint` shim from
    //      the Phase C path on top.
    //
    // Then group hits by enclosing `CompoundStmt` (LO-2c), apply the
    // PRD §4.3 main-outer suppression (LO-2d), and feed each
    // surviving block's cleanup text through `register_external_
    // cleanup` so `synthesize()` plants it before the close brace.
    if (!lossy_hits_.empty()) {
        const clang::SourceManager& sm = ctx.getSourceManager();
        const clang::LangOptions& lang = ctx.getLangOpts();

        FreshNameAllocator alloc;
        std::vector<sturm::transpile::LossyEmission> forwards;
        forwards.reserve(lossy_hits_.size());
        for (const auto& hit : lossy_hits_) {
            auto em = sturm::transpile::emit_lossy_forward(hit, alloc);
            forwards.push_back(em);

            // Skip degenerate hits (empty operand names) — the
            // emitter returned an empty triplet, no rewrite to plant.
            if (em.text.empty() || hit.call == nullptr) continue;

            // Replace the user's compound-assign call with the
            // forward triplet text. The call's `getSourceRange()`
            // covers the full `lhs <op>= rhs` expression; the
            // statement's terminating `;` lands AFTER that range,
            // so a `QReplacement` over the call range produces
            // `<triplet>;` — the trailing semicolon already in the
            // user's source becomes a stray no-op statement after
            // the last `swap(...)` line. To fold it cleanly we
            // strip the trailing `\n` from the triplet text and
            // let the user's `;` close the final swap.
            //
            // Indentation: the `lossy_rewrite_emitter` emits each
            // triplet line at column 0. The user's `*=` may be
            // anywhere on the line; the simplest "good enough"
            // posture (matching the LO-0.x expected fixtures) is
            // to leave column-0 lines and let the `#line`
            // directive prefix carry source-map fidelity. Each
            // line of the triplet gets its own `#line` directive
            // anchored at the call's begin loc.
            const clang::SourceLocation call_begin =
                hit.call->getBeginLoc();
            const std::string line_directive =
                sturm::transpile::format_line_directive(sm, call_begin);

            // Split triplet into lines (each ending in '\n') and
            // re-emit with `#line` per line. The triplet has 3
            // lines (single-ancilla) or 3 lines (divide-kernel,
            // pre-split combined decl). We do not over-engineer:
            // simple manual scan is enough.
            //
            // Leading `\n` before each `#line`: per the C/C++
            // standard, `#line` must be the FIRST non-whitespace
            // token on its own line. The user's source at the
            // replacement begin loc may carry whatever was on the
            // same line as the matched call (e.g. `void demo(...)
            // { a *= b; }` collapses everything onto one line, so
            // the source position immediately before the call has
            // a non-newline `{ ` prefix). A leading `\n` guarantees
            // the directive lands at column 0 of a fresh line.
            // Mirrors the PM2-3 prefix logic in
            // `uncompute_pass.cpp`.
            std::string body;
            body.reserve(em.text.size() + 4 * line_directive.size());
            std::size_t pos = 0;
            while (pos < em.text.size()) {
                const std::size_t nl = em.text.find('\n', pos);
                if (nl == std::string::npos) break;
                if (!line_directive.empty()) {
                    body.push_back('\n');
                    body.append(line_directive);
                }
                body.append(em.text, pos, nl - pos);
                // Drop the trailing `\n` on the LAST emitted line —
                // the user's `;` will replace it. For non-last
                // lines, keep the `\n`.
                const bool is_last = (nl + 1 == em.text.size());
                if (!is_last) body.push_back('\n');
                pos = nl + 1;
            }

            sturm::transpile::QReplacement rep;
            const auto char_range =
                clang::CharSourceRange::getTokenRange(
                    hit.call->getSourceRange());
            const auto end_loc = clang::Lexer::getLocForEndOfToken(
                char_range.getEnd(), 0, sm, lang);
            if (end_loc.isInvalid()) continue;
            rep.range = clang::SourceRange(call_begin, end_loc);
            // CharSourceRange-style end is exclusive; ReplaceText
            // takes a [begin, end) range, so subtract one token.
            // Clang's Rewriter::ReplaceText with `range` uses the
            // token-end semantics already.
            rep.replacement = std::move(body);
            unit_.replacements.push_back(std::move(rep));

            // Suppress any QOperation in `unit_.scopes` whose
            // `stmt_range.getBegin()` matches this call's begin —
            // that is the Phase C `*_ASSIGN_QINT` op the
            // pre-existing matchers pushed for this same call.
            // Without this suppression the M8 synthesis pass would
            // emit a `uncompute_*_qint(lhs, rhs);` shim AFTER the
            // LO forward triplet, and the resulting source would
            // contain BOTH the LO cleanup pair AND the Phase C
            // shim — semantically wrong and snapshot-breaking.
            const unsigned key =
                hit.call->getBeginLoc().getRawEncoding();
            for (auto& scope : unit_.scopes) {
                for (auto& op : scope.ops) {
                    if (op.stmt_range.getBegin().getRawEncoding() == key) {
                        op.skip_uncompute = true;
                    }
                }
            }
        }

        // LO-2c: group cleanups by enclosing block (LIFO within each
        // block) and apply LO-2d main-outer suppression. Pass `&ctx`
        // so the suppression predicate can resolve the enclosing
        // FunctionDecl and match `main`'s outermost body.
        const auto blocks =
            sturm::transpile::group_cleanups_by_block(
                lossy_hits_, forwards, &ctx);

        for (const auto& bc : blocks) {
            if (bc.text.empty() || bc.enclosing_block == nullptr) continue;

            // Anchor the cleanup `#line` directives at the FIRST
            // hit's call begin loc — every hit in the same block
            // matches a forward statement on the user's same line
            // (the LO fixtures only ever pin one or two ops per
            // block), so a single anchor is enough. If the block
            // contains hits from multiple lines, the first-hit
            // anchor is still semantically correct: each cleanup
            // line maps back to ITS forward op's line, but the
            // simple shared anchor matches the snapshot fixture
            // shape.
            clang::SourceLocation anchor;
            for (const auto& hit : lossy_hits_) {
                if (hit.enclosing_block == bc.enclosing_block &&
                    hit.call != nullptr) {
                    anchor = hit.call->getBeginLoc();
                    break;
                }
            }
            const std::string line_directive =
                anchor.isValid()
                    ? sturm::transpile::format_line_directive(sm, anchor)
                    : std::string();

            // Format the cleanup text. Each `\n`-terminated line in
            // `bc.text` becomes `\n#line ...\n    <line>\n` so the
            // expected snapshot byte-shape is reproduced (PM2-3
            // pattern, four-space indent leading each statement).
            std::string formatted;
            formatted.reserve(bc.text.size() * 2 + 64);
            std::size_t pos = 0;
            while (pos < bc.text.size()) {
                const std::size_t nl = bc.text.find('\n', pos);
                if (nl == std::string::npos) break;
                formatted.push_back('\n');
                if (!line_directive.empty()) {
                    formatted.append(line_directive);
                }
                formatted.append("    ");
                formatted.append(bc.text, pos, nl - pos);
                formatted.push_back('\n');
                pos = nl + 1;
            }

            const clang::SourceLocation close_brace =
                bc.enclosing_block->getRBracLoc();
            sturm::transpile::register_external_cleanup(
                external_cleanups_, close_brace, std::move(formatted));
        }
    }

    // Phase T T-1 (sturm-xrob.2): drive the reversible-adjoint
    // synthesis pipeline for every `[[sturm::reversible]]` forward
    // collected during `matchAST`. For each forward that passes P-C
    // validation + Q-B constness, this helper appends one
    // `UncomputeInsertion` to `unit_.raw_insertions` carrying the
    // emitted `__fn_adj` body + `STURM_REGISTER_ADJOINT` line anchored
    // just after the forward's body close brace. The M8 synthesis
    // pass below then folds the raw insertion into the rewritten
    // buffer so PI-1's matcher picks up the registration on the
    // second PM3 transpile pass.
    //
    // Runs AFTER the fuse / eliminated-range backstops so the scope's
    // op list is stable before we read it, and BEFORE the emit step
    // below so the raw insertion is visible to `synthesize()` /
    // `emit_to_string`.
    sturm::transpile::drive_reversible_forwards(
        unit_, synth_registry_, registry_, diag_, ctx);

    // Mode-specific emit step. StandaloneFile preserves the pre-PM1
    // `emit()` call shape for byte-identical output; Plugin stashes
    // the rewritten buffer via emit_to_string for the wrapping
    // PluginASTAction (PM1-3 / PM1-4) to thread into a nested
    // CompilerInvocation.
    switch (mode_) {
        case EmissionMode::StandaloneFile: {
            // PM1-6: when `dump_transpiled_path_` is non-empty the
            // standalone driver wants the emitted bytes landing at that
            // exact path, bypassing `resolve_output_path`. The header +
            // rewritten buffer content is IDENTICAL to the legacy
            // output-dir path (the dump flag's gate is "output matches
            // the legacy standalone invocation byte-for-byte"), so we
            // reuse `emit_to_string` for the rewrite step and prepend
            // the same idempotency header the M9 emit() path would.
            if (!dump_transpiled_path_.empty()) {
                std::string body =
                    sturm::transpile::emit_to_string(
                        unit_, external_cleanups_, ctx,
                        &plugin_registry_);
                std::string out;
                out.reserve(body.size() + 128);
                out.append(
                    sturm::transpile::idempotency_header(source_path_));
                out.append(body);
                if (!sturm::transpile::write_file(
                        std::filesystem::path(dump_transpiled_path_),
                        out)) {
                    std::fprintf(stderr,
                                 "sturm-transpile: error: could not "
                                 "write --dump-transpiled output %s\n",
                                 dump_transpiled_path_.c_str());
                }
                break;
            }

            // M8: synthesize uncompute insertions + replacements from
            // the QUnit. PE-2: the return type is QSynthesisResult — a
            // struct of two vectors. Pre-Phase-E the `replacements`
            // field is empty, so this call produces byte-identical
            // output to the pre-PE-2 pipeline (the "existing snapshot
            // fixtures byte-identical" acceptance criterion is enforced
            // by the snapshot tests downstream).
            //
            // PM2-3: pass the active SourceManager so `synthesize()`
            // prefixes each rendered uncompute call with a `#line`
            // directive pointing at the forward op's begin loc, and
            // appends a restoring `#line` at every close-brace that
            // carries at least one uncompute insertion.
            //
            // PM4-3: also pass this consumer's plugin Registry so the
            // M8 render pass's `case QOpKind::PLUGIN:` arm can dispatch
            // to any plugin-registered render function. Pre-PM4
            // fixtures contain no PLUGIN ops, so the Registry lookup
            // never fires and the output is byte-identical to the
            // pre-PM4 shape.
            // sturm-v0ur (LO-2 wiring): pass `external_cleanups_`
            // through the new `synthesize()` overload so the LO-2c
            // cleanups assembled in the LO drain block above land at
            // their close-brace anchors interleaved with the per-op
            // LIFO insertions.
            auto synth = sturm::transpile::synthesize(
                unit_, external_cleanups_,
                &ctx.getSourceManager(), &plugin_registry_);

            // M9: build a Rewriter over the same
            // SourceManager/LangOptions and let emit() apply
            // replacements, insertions, prepend the header, and write
            // the output file.
            clang::Rewriter rw(ctx.getSourceManager(), ctx.getLangOpts());
            (void)sturm::transpile::emit(
                ctx.getSourceManager(), rw,
                synth.insertions, synth.replacements,
                source_path_, output_dir_);
            break;
        }
        case EmissionMode::Plugin: {
            // PM1-2 emit_to_string handles the matcher-free rewrite
            // step without prepending the idempotency header — the
            // buffer this stores is fed straight into a nested
            // CompilerInvocation by the wrapping PluginASTAction.
            //
            // TODO(backend): PM1-3 / PM1-4 will read
            // `rewritten_buffer_` via `rewritten_buffer()` and hand it
            // to an OverlayFileSystem backing a nested
            // CompilerInvocation + EmitObjAction. The ci_ reference
            // stashed on this consumer is the handle those steps need
            // to clone the parent invocation.
            rewritten_buffer_ =
                sturm::transpile::emit_to_string(
                    unit_, external_cleanups_, ctx,
                    &plugin_registry_);
            (void)ci_;
            break;
        }
    }
}

} // namespace sturm::transpile
