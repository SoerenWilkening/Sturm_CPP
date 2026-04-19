// main.cpp — sturm-transpile driver.
//
// At M9 this module composes the full MVP pipeline:
//
//   skip detection  →  LibTooling parse  →  MatchFinder (M7)  →  synthesize
//   (M8)            →  emit (M9)         →  file on disk
//
// Skip detection (the M5 idempotency / opt-out contract) is implemented
// via should_skip() from the skip module. Two cases:
//   1. "// sturm-transpile: skip" magic comment → pass through unchanged.
//   2. Our own AUTO-GENERATED header already present → pass through
//      unchanged (enforces PRD AC #5: re-running on emitted output yields
//      a byte-identical file).
//
// For non-skipped inputs we run Clang's ClangTool with a custom
// FrontendAction whose ASTConsumer drives the M7 matcher, invokes M8, and
// then calls the M9 emitter to land the rewritten source on disk.

#include "sturm/transpile/emitter.hpp"
#include "sturm/transpile/io.hpp"
#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/skip.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

// Phase I PI-1: context-wide forward/adjoint registry, populated by its
// own matcher before any PI-2+ routine-call matcher runs. The registry
// lives alongside QUnit on the TranspileConsumer below.
#include "routine_registry.hpp"
// Phase I PI-2: user-defined-routine call matcher. Consumes the registry
// populated by PI-1 and records one QOperation{kind=USER_ROUTINE} per
// call to a registered forward routine.
#include "matcher_user_routine.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace cl = llvm::cl;

// ── Version string ────────────────────────────────────────────────────────────
// Bump when the transpiler's contract changes. Kept in one place so the
// tests can regex it and CMake can pass it as a compile definition later.
static const char kSturmTranspileVersion[] =
    "sturm-transpile 0.2.0 (MVP pipeline; M9: C++ emitter)";

// ── Command-line options ──────────────────────────────────────────────────────
static cl::OptionCategory kToolCategory("sturm-transpile options");

static cl::opt<std::string> kOutputDir(
    "output-dir",
    cl::desc("Destination directory for transpiled output files"),
    cl::value_desc("dir"),
    cl::Required,
    cl::cat(kToolCategory));

// Note: the input source file is supplied as a positional argument handled
// by CommonOptionsParser. No additional cl::opt is needed for it.

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool handle_version_flag(int argc, const char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 ||
            std::strcmp(argv[i], "-v") == 0) {
            std::puts(kSturmTranspileVersion);
            return true;
        }
    }
    return false;
}

// ── FrontendAction composing the MVP pipeline ────────────────────────────────
//
// The action holds no state of its own; the ASTConsumer below does the
// real work. We thread the source path + output dir through the action
// constructor so the consumer can hand them to emit().

namespace {

class TranspileConsumer : public clang::ASTConsumer {
public:
    TranspileConsumer(std::string source_path, std::string output_dir)
        : source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)) {
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
        sturm::transpile::register_outer_var_guard_matcher(finder_, unit_);
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
            finder_, unit_, registry_);
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
    }

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
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

        // M8: synthesize uncompute insertions + replacements from the QUnit.
        // PE-2: the return type is QSynthesisResult — a struct of two
        // vectors. Pre-Phase-E the `replacements` field is empty, so this
        // call produces byte-identical output to the pre-PE-2 pipeline
        // (the "existing snapshot fixtures byte-identical" acceptance
        // criterion is enforced by the snapshot tests downstream).
        auto synth = sturm::transpile::synthesize(unit_);

        // M9: build a Rewriter over the same SourceManager/LangOptions and
        // let emit() apply replacements, insertions, prepend the header,
        // and write the output file.
        clang::Rewriter rw(ctx.getSourceManager(), ctx.getLangOpts());
        (void)sturm::transpile::emit(ctx.getSourceManager(), rw,
                                     synth.insertions, synth.replacements,
                                     source_path_, output_dir_);
    }

private:
    sturm::transpile::QUnit unit_;
    // Phase I PI-1: context-wide forward/adjoint map populated by the
    // routine-registry matcher. Lives here — alongside `unit_` — so both
    // are destroyed together with the ASTContext the matcher ran under
    // (the registry stores raw FunctionDecl pointers with
    // ASTContext-bound lifetime). Pre-Phase-I consumers leave this
    // empty; downstream PI-2..PI-7 matchers will consult it.
    sturm::transpile::RoutineRegistry registry_;
    clang::ast_matchers::MatchFinder finder_;
    std::string source_path_;
    std::string output_dir_;
};

class TranspileAction : public clang::ASTFrontendAction {
public:
    TranspileAction(std::string source_path, std::string output_dir)
        : source_path_(std::move(source_path)),
          output_dir_(std::move(output_dir)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<TranspileConsumer>(source_path_, output_dir_);
    }
private:
    std::string source_path_;
    std::string output_dir_;
};

class TranspileFactory : public clang::tooling::FrontendActionFactory {
public:
    TranspileFactory(std::string src, std::string out)
        : src_(std::move(src)), out_(std::move(out)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<TranspileAction>(src_, out_);
    }
private:
    std::string src_;
    std::string out_;
};

} // namespace

// Read up to 1 KiB from the head of `path` — enough to cover any credible
// leading-blank + sentinel line count — so we can detect skip / already-
// generated files without loading the whole file.
static std::string read_head(const fs::path& path, std::size_t n = 1024) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string buf;
    buf.resize(n);
    in.read(buf.data(), static_cast<std::streamsize>(n));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

// Copy `path` verbatim to `<output_dir>/<resolved relpath>`.
// Returns true on success.
static bool verbatim_copy(const fs::path& path, const fs::path& output_dir) {
    std::string bytes;
    if (!sturm::transpile::read_file(path, bytes)) return false;
    fs::path dst = sturm::transpile::resolve_output_path(path, output_dir);
    return sturm::transpile::write_file(dst, bytes);
}

// ── Driver ────────────────────────────────────────────────────────────────────

int main(int argc, const char** argv) {
    if (handle_version_flag(argc, argv)) return 0;

    auto expected_parser =
        clang::tooling::CommonOptionsParser::create(
            argc, argv, kToolCategory,
            /*OccurrencesFlag=*/cl::OneOrMore,
            /*Overview=*/
            "sturm-transpile: STURM's Clang LibTooling-based uncomputation\n"
            "transpiler.\n\n"
            "USAGE:\n"
            "  sturm-transpile <input.cpp> --output-dir <dir>\n"
            "  sturm-transpile --version\n\n"
            "For each input source file, sturm-transpile parses it with\n"
            "Clang and rewrites quantum intermediates with explicit\n"
            "uncompute_* calls. Output is written to\n"
            "<output-dir>/<relpath-of-input> with an AUTO-GENERATED header.\n"
            "Files whose first non-blank line is either the magic comment\n"
            "`// sturm-transpile: skip` or the AUTO-GENERATED sentinel\n"
            "already emitted by a prior run are copied through verbatim.\n");
    if (!expected_parser) {
        llvm::errs() << toString(expected_parser.takeError());
        return 2;
    }
    auto& parser = *expected_parser;

    const auto& inputs = parser.getSourcePathList();
    if (inputs.empty()) {
        std::fprintf(stderr,
                     "sturm-transpile: error: no input file provided\n");
        return 2;
    }
    if (inputs.size() > 1) {
        std::fprintf(stderr,
                     "sturm-transpile: error: multiple inputs not yet "
                     "supported\n");
        return 2;
    }
    const std::string& input_path = inputs.front();

    // Fail-fast on missing input.
    std::error_code ec;
    if (!fs::exists(fs::path(input_path), ec) || ec) {
        std::fprintf(stderr,
                     "sturm-transpile: error: input file not found: %s\n",
                     input_path.c_str());
        return 1;
    }

    // Skip detection: read the first 1 KiB and check for either sentinel.
    // On a hit we short-circuit to a verbatim byte-copy, enforcing PRD AC
    // #5 (idempotency) and AC #6 (skip marker).
    std::string head = read_head(fs::path(input_path));
    if (sturm::transpile::should_skip(head)) {
        if (!verbatim_copy(fs::path(input_path),
                           fs::path(kOutputDir.getValue()))) {
            std::fprintf(stderr,
                         "sturm-transpile: error: verbatim copy failed for "
                         "%s\n", input_path.c_str());
            return 1;
        }
        return 0;
    }

    // Full pipeline. Run the tool with the compilation database resolved
    // by CommonOptionsParser and our custom factory; the factory's consumer
    // drives M7 → M8 → M9.
    //
    // We deliberately use parser.getCompilations() rather than constructing
    // our own empty FixedCompilationDatabase: doing so lets sturm-transpile
    // honor `--extra-arg=-I...`, `--extra-arg=-D...`, `--extra-arg=-std=...`,
    // and any compile_commands.json that lives alongside the input. This is
    // what lets the build-system glue (cmake/SturmTranspile.cmake) propagate
    // the include paths and feature defines a real example like
    // examples/or_circuit.cpp needs in order for `sturm::qbool` to resolve
    // — without those, the matcher's `cxxRecordDecl(hasName("qbool"))`
    // would never fire on the real header chain (LP4 risk R1).
    std::vector<std::string> source_paths{input_path};
    clang::tooling::ClangTool tool(parser.getCompilations(), source_paths);
    // Suppress diagnostics: the MVP transpiler does not need to surface
    // parse errors (the user will re-see them in the downstream compile).
    tool.setDiagnosticConsumer(new clang::IgnoringDiagConsumer());

    // Inject -resource-dir so libTooling can find its builtin headers
    // (stdarg.h, stddef.h, etc.). Without this, the Clang driver fails to
    // resolve macOS libc++ typedefs like __uint32_t / __darwin_wint_t and
    // the parser abandons main-file translation before the user's body is
    // built into the AST — every Phase A-I matcher then sees no ops from
    // the user's code and the transpile produces a byte-identical pass-
    // through.
    //
    // Lookup order:
    //   1. `<bindir>/../lib/clang/<LLVM_VERSION_MAJOR>` next to the running
    //      executable. This is the only path that survives a relocatable
    //      tarball install (the build job ships the Clang builtin-headers
    //      dir alongside `bin/sturm-transpile`), so it must come first.
    //   2. `STURM_CLANG_RESOURCE_DIR` baked in at CMake-configure time from
    //      the LLVM package the transpiler was linked against. This keeps
    //      local in-tree builds green even when the install rule does not
    //      run (e.g. `cmake --build build` + running the binary directly).
    //
    // The adjuster prepends the arg at BEGIN, so a user-supplied
    // `--extra-arg=-resource-dir=...` that appears later in argv wins.
    {
        std::string rd;
        const std::string exe_path = llvm::sys::fs::getMainExecutable(
            argv[0], reinterpret_cast<void*>(&main));
        if (!exe_path.empty()) {
            llvm::SmallString<256> candidate(exe_path);
            llvm::sys::path::remove_filename(candidate);                 // strip exe
            llvm::sys::path::remove_filename(candidate);                 // strip bin/
            llvm::sys::path::append(candidate, "lib", "clang",
                                    STURM_LLVM_VERSION_MAJOR_STR);
            if (llvm::sys::fs::is_directory(candidate)) {
                rd = std::string(candidate.str());
            }
        }
#ifdef STURM_CLANG_RESOURCE_DIR
        if (rd.empty()) rd = STURM_CLANG_RESOURCE_DIR;
#endif
        if (!rd.empty()) {
            const std::string rd_arg = std::string("-resource-dir=") + rd;
            tool.appendArgumentsAdjuster(
                clang::tooling::getInsertArgumentAdjuster(
                    rd_arg.c_str(),
                    clang::tooling::ArgumentInsertPosition::BEGIN));
        }
    }

    TranspileFactory factory(input_path, kOutputDir.getValue());
    int tool_rc = tool.run(&factory);
    // `tool.run` returns non-zero on hard parse failures. Treat them as
    // soft: if the emitter managed to write an output (because the AST
    // was recoverable), we still prefer returning 0 so downstream CMake
    // builds see the generated file. If NO output was produced we return
    // the tool's error code so the caller notices.
    fs::path expected_out = sturm::transpile::resolve_output_path(
        fs::path(input_path), fs::path(kOutputDir.getValue()));
    if (!fs::exists(expected_out)) {
        // Nothing landed on disk — treat that as a hard failure.
        if (tool_rc == 0) tool_rc = 1;
        std::fprintf(stderr,
                     "sturm-transpile: error: no output produced for %s\n",
                     input_path.c_str());
        return tool_rc;
    }
    return 0;
}
