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

#include "matcher_user_routine.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <cstdio>
#include <filesystem>
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
    sturm::transpile::register_add_assign_const_matcher(finder_, unit_);
    sturm::transpile::register_sub_assign_const_matcher(finder_, unit_);
    sturm::transpile::register_mul_assign_const_matcher(finder_, unit_);
    sturm::transpile::register_div_assign_const_matcher(finder_, unit_);
    sturm::transpile::register_add_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_sub_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_mul_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_div_assign_qint_matcher(finder_, unit_);
    sturm::transpile::register_mod_assign_qint_matcher(finder_, unit_);
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
    // Phase H PH-3: the outer-variable-mutation guard must run AFTER
    // the Phase A / B / C compound-assign matchers have populated
    // `unit_.scopes` — the callback looks up the QOperation each
    // A/B/C matcher pushed by `stmt_range.getBegin()` and flags its
    // `skip_uncompute` field. MatchFinder invokes callbacks in
    // registration order on a given node, so placing this register
    // call LAST among the mutation matchers is the load-bearing
    // ordering invariant for PH-3.
    sturm::transpile::register_outer_var_guard_matcher(
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
    // Phase J PJ-3e: the uncompute-hoisting matcher runs LAST —
    // after every Phase A..I per-op matcher (MVP OR, PA-1/PA-2
    // bitwise, PA-3/PA-4 xor-assign, PB/PC qint compound-assigns,
    // PD qint compares, PE-4 compound-qbool, PF WHEN-lift, PG
    // WHEN-nested, PH-2 brace-wrap, PH-3 outer-var guard, PI-2
    // user-routine), after the PJ-1f zero-ancilla fuse peephole
    // (registered above), and after the PJ-4b dead-ancilla
    // eliminator (registered above). The ordering invariant has
    // two load-bearing edges:
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
    // Downstream blocks (sturm-8cwe PJ-3f snapshot fixtures) rely
    // on this ordering staying stable.
    sturm::transpile::register_hoist_invariant_matcher(finder_, unit_);
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

    // ── PM4-3: drain plugin registrars into the per-consumer Registry ───
    //
    // Plan §6 ordering: in-tree (above) → runtime-dlopen → link-time.
    //
    //   - `runtime_registrars()` is appended by `plugin.cpp`'s `load=<path>`
    //     branch (PM4-4) after a successful dlopen + Clang-version check +
    //     dlsym. Each entry wraps the resolved `sturm_register_plugin_v1`
    //     pointer; invoking it against this consumer's Registry registers
    //     the plugin's matchers + render functions.
    //
    //   - `registrars()` (the Meyer's singleton) is appended at static-init
    //     time by every TU that uses `STURM_REGISTER_PLUGIN(TypeName)`.
    //     This is the link-time fallback path (PM4-10's
    //     `STURM_PM4_LINK_DEMO=ON` switch bakes the demo plugin in this
    //     way).
    //
    // Matchers these plugin entries register are installed on the SAME
    // `finder_` the in-tree matchers are bound to via
    // `plugin_registry_.invoke_all`. Render functions registered via
    // `register_op` are consulted by the M8 synthesis pass below when it
    // encounters a `QOpKind::PLUGIN` op.
    //
    // Both drains are idempotent with respect to THIS Registry — the
    // collision-detection in `register_matcher` / `register_op` guarantees
    // that re-invocation (e.g. the PM1-4 nested consumer firing the same
    // Meyer vector again against a fresh Registry) cannot double-register
    // within a single Registry. The vectors themselves are not cleared
    // between consumer constructions, which is the intended shape: each
    // consumer gets a fresh, independent view of the registrar set.
    for (const auto& fn :
         ::sturm::transpile::plugin::runtime_registrars()) {
        fn(plugin_registry_);
    }
    for (const auto& fn : ::sturm::transpile::plugin::registrars()) {
        fn(plugin_registry_);
    }

    // Install every matcher the plugins registered onto `finder_` + let
    // them observe the shared QUnit. Called once, AFTER the
    // registrar-drain above (so every registered matcher is present).
    plugin_registry_.invoke_all(finder_, unit_);
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
                    sturm::transpile::emit_to_string(unit_, ctx,
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
            auto synth = sturm::transpile::synthesize(
                unit_, &ctx.getSourceManager(), &plugin_registry_);

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
                sturm::transpile::emit_to_string(unit_, ctx,
                                                  &plugin_registry_);
            (void)ci_;
            break;
        }
    }
}

} // namespace sturm::transpile
