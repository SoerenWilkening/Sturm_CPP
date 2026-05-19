// matcher_when_lift.cpp — Phase F / PF-3+PF-4 WHEN(expr) lift matcher.
//
// Purpose
// -------
// Detects `WHEN(expr) { body }` macro invocations in user source and
// performs the three-point rewrite that promotes a compound / unary /
// comparator `WHEN` argument into a named qbool temporary:
//
//   qbool __stu_t0 = b | c;              // (1) pre-WHEN decl block
//   WHEN(__stu_t0) { body }              // (2) arg replaced with top temp
//   uncompute_or(__stu_t0, b, c);        // (3) uncompute after WHEN body
//
// The PF-2 slice (sturm-q3uo) landed the detection logic; PF-3 layered on
// the emission logic for single-op and compound bitwise (`|`, `&`, `~`)
// shapes; PF-4 extended `flatten_when_arg` to additionally recognise
// Phase D comparator `CXXOperatorCallExpr` shapes (`==`, `!=`, `<`, `<=`,
// `>`, `>=`) and emit the matching `*_QINT` `QOpKind`. The compound
// recursive shape (`WHEN((b | c) & d)`) works transparently through the
// same `detail::flatten_arg` call path PF-3 established — PF-4's
// additional contribution there is to pair with the `std::stable_sort`
// switch in `uncompute_pass.cpp` so that multi-op lifts from a single
// WHEN-argument expansion emit their inverses in source LIFO order. PF-2's detection counter remains in place — it is
// incremented on every lift for easy unit-test observability. The
// named-passthrough short-circuit (PF-2) is preserved verbatim: when the
// `WHEN` argument (after `peel_to_payload`) is a bare `DeclRefExpr` to a
// qbool, no rewrite is scheduled. The emitter's output for that shape is
// byte-identical to the input (the arg already names a qbool).
//
// Mechanics (Option A from the issue description):
//
//   (1) Flatten the peeled payload via `detail::flatten_arg` against a
//       scratch `QScope` + fresh `FreshNameAllocator`. Unlike the Phase E
//       compound matcher — which gives the outermost node the user's
//       VarDecl name — Phase F names the outermost with a fresh
//       `__stu_t<N>` temp. `flatten_arg` achieves this automatically: when
//       the payload is an op-call it recurses into `flatten_inner_call`
//       which allocates a fresh name.
//
//   (2) Schedule one `QReplacement` over `arg->getSourceRange()` mapped
//       through `SourceManager::getSpellingLoc` + `Lexer::makeFileCharRange`
//       to a pure file-range, with replacement text equal to the top
//       temp name. This substitutes the user's compound expression inside
//       the `WHEN(...)` argument list with a bare identifier.
//
//   (3) Schedule one `UncomputeInsertion` on `QUnit::raw_insertions`. The
//       text is the full decl block (every line from `flat_lines`, joined
//       by newlines, with a trailing newline). The anchor is the WHEN
//       macro's expansion loc — the file location where `WHEN(...)` is
//       spelled — so the new decls land on the line preceding the
//       macro invocation.
//
//   (4) Transfer every op from the scratch scope into the enclosing
//       CompoundStmt's `QScope`, each tagged with a per-op
//       `insert_before_override = post_body_brace`. The post-body loc is
//       computed by descending `when_if->getThen()` twice (first hop:
//       inner `if (auto _when_guard_ = ...; ...)`; second hop: the user's
//       body CompoundStmt), taking `CompoundStmt::getRBracLoc()`, and
//       stepping past it with `Lexer::getLocForEndOfToken`.
//
// Disjointness
// ------------
// From the Phase E compound matcher: that matcher keys on a VarDecl whose
// type is `qbool` and whose initializer is a `CXXOperatorCallExpr`.
// Phase F keys on an `IfStmt` whose init-stmt declares `_when_val_`
// (which binds `decltype(auto)`, not `qbool`, and whose initializer is a
// plain `CallExpr` to `materialize_when`, not a `CXXOperatorCallExpr`).
// No double-bind.
//
// From the Phase F PF-1 per-op override discipline: every op this matcher
// emits carries a valid `insert_before_override`, so the M8 synthesis
// pass plants each uncompute call at the WHEN-body close brace — not at
// the enclosing `CompoundStmt`'s close brace — without affecting any
// pre-Phase-F matcher's output (those matchers leave the override
// default-constructed / invalid).

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
// PM2-4: pulls in `format_line_directive` so each lifted `qbool
// __stu_tN = ...;` decl can be prefixed with a `#line` directive
// anchored at the WHEN replacement's range begin (the user's
// compound expression inside `WHEN(...)`) — source-map emission
// for Phase F WHEN-lift decls.
#include "sturm/transpile/emitter.hpp"
#include "matcher_common.hpp"
#include "fresh_names.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_when_lift_anon_ns {

// Test-only detection counter. Incremented by `WhenLiftCallback::run` on
// every run that passes every guard and is NOT short-circuited by the
// named-passthrough path. Phase F PF-2 added this counter for its
// detection-only phase; PF-3 continues to bump it on every successful
// lift so the unit tests (PF-2 + PF-3) share one observable. A plain
// non-atomic int is fine — the transpiler is single-threaded by
// construction (LibTooling drives one AST at a time).
static int g_when_lift_detection_count = 0;

using namespace clang;
using namespace clang::ast_matchers;

// Phase G PG-1 promoted `is_expansion_of_macro` and `compute_post_body_brace`
// into `matcher_common.hpp` so the new `matcher_when_nested.cpp` TU can reuse
// them verbatim. The implementations now live under
// `sturm::transpile::detail::` — this TU calls them qualified at each use
// site.

// Materialize the full decl block (newline-terminated). Each entry in
// `flat_lines` is a single `qbool __stu_tN = ... ;` statement (with
// trailing semicolon). We append a newline after every line so the
// inserted block sits on its own line(s) immediately above the WHEN
// macro invocation.
//
// PM2-4: when `line_directive` is non-empty, it is emitted immediately
// BEFORE each flat decl line so that Clang diagnostics landing inside
// any synthesized decl cite the user's originating WHEN-argument
// expression. All flattened decls share the same anchor (the
// replacement's `range.getBegin()` is the spelling begin of the user's
// compound expression), so every decl's directive is identical — the
// text is appended once per line, not deduped, because repeated
// `#line` directives are idempotent for Clang's line counter and
// keep the block's semantics obvious in a diff review.
//
// When `line_directive` is empty (degenerate loc — invalid / not in
// main file), the emission falls back to the pre-PM2-4 layout
// byte-for-byte so any snapshot that happens to synthesize outside
// the main file stays stable.
static std::string render_decl_block(
    const std::vector<std::string>& flat_lines,
    const std::string& line_directive) {
    std::ostringstream os;
    for (const auto& line : flat_lines) {
        if (!line_directive.empty()) {
            os << line_directive;
        }
        os << line << "\n";
    }
    return os.str();
}

// Phase F PF-4: map a Phase D comparator OverloadedOperatorKind to the
// matching `*_QINT` QOpKind, plus the source-level operator spelling used
// when rendering the decl line. The spelling is the two- or one-character
// operator text as it appears in user source (`==`, `!=`, `<`, `<=`, `>`,
// `>=`). Returns std::nullopt for any non-comparator operator.
static std::optional<std::pair<QOpKind, const char*>>
comparator_kind_for(clang::OverloadedOperatorKind op) {
    switch (op) {
    case clang::OO_EqualEqual:       return {{QOpKind::EQ_QINT, "=="}};
    case clang::OO_ExclaimEqual:     return {{QOpKind::NE_QINT, "!="}};
    case clang::OO_Less:             return {{QOpKind::LT_QINT, "<"}};
    case clang::OO_LessEqual:        return {{QOpKind::LE_QINT, "<="}};
    case clang::OO_Greater:          return {{QOpKind::GT_QINT, ">"}};
    case clang::OO_GreaterEqual:     return {{QOpKind::GE_QINT, ">="}};
    default:                          return std::nullopt;
    }
}

// Render a single flat decl line for a Phase D comparator (Phase F PF-4).
// Shape is `qbool <name> = <lhs> <op> <rhs>;` with a terminating semicolon
// — comparators always go into the flat decl block, never the user's
// VarDecl source range, so the semicolon is always emitted.
static std::string render_compare_decl_line(const std::string& name,
                                            const std::string& lhs,
                                            const char* op_spelling,
                                            const std::string& rhs) {
    std::ostringstream os;
    os << "qbool " << name << " = " << lhs << " " << op_spelling << " "
       << rhs << ";";
    return os.str();
}

// Phase F PF-4: extract the identifier for an argument of a comparator
// CXXOperatorCallExpr. The Phase D matcher binds these as bare
// DeclRefExprs (see matcher_qint_compare.cpp:154-161) — we apply the same
// expectation here, peeling implicit / paren / bind-temporary wrappers
// with `peel_to_payload` before reading the identifier. Returns an empty
// string on any structural mismatch (non-DRE leaf) so the caller can bail
// the whole lift cleanly.
static std::string leaf_name_for_compare_arg(const clang::Expr* arg) {
    const clang::Expr* inner = detail::peel_to_payload(arg);
    if (!inner) return {};
    if (const auto* dre = clang::dyn_cast<clang::DeclRefExpr>(inner)) {
        if (const clang::NamedDecl* nd = dre->getDecl()) {
            return nd->getNameAsString();
        }
    }
    return {};
}

// Phase F PF-4: WHEN-specific argument flattener. Top-level routing for a
// peeled `WHEN(...)` payload:
//
//   - Phase D comparator shape (CXXOperatorCallExpr on `==`, `!=`, `<`,
//     `<=`, `>`, `>=` with exactly two DeclRefExpr args): allocate a
//     fresh temp, record one QOperation with the corresponding `*_QINT`
//     kind, append one `qbool __stu_tN = a == b;` decl line, return the
//     temp name. The render switch in `uncompute_pass.cpp` (`EQ_QINT`
//     case and siblings) already emits the matching
//     `uncompute_{eq,ne,lt,le,gt,ge}_qint` inverse — no emitter change.
//
//   - Anything else: delegate to the Phase E `detail::flatten_arg`, which
//     handles bitwise `|`/`&`/`~` (including recursive compounds like
//     `(b | c) & d`) and bare DeclRefExpr leaves (though a bare leaf is
//     already short-circuited via the named-passthrough path earlier).
//
// Returns an empty string on structural failure; the caller bails without
// scheduling any edit.
static std::string flatten_when_arg(const clang::Expr* peeled,
                                    QScope& scope,
                                    FreshNameAllocator& alloc,
                                    clang::SourceRange stmt_range,
                                    std::vector<std::string>& flat_lines) {
    if (!peeled) return {};

    // Comparator branch: CXXOperatorCallExpr with a relational operator.
    if (const auto* op_call =
            clang::dyn_cast<clang::CXXOperatorCallExpr>(peeled)) {
        if (auto mapped = comparator_kind_for(op_call->getOperator())) {
            // Comparators in Phase D are binary: exactly two operands in
            // the CXXOperatorCallExpr arg list (member form: arg0 = object,
            // arg1 = rhs; free form: arg0 = lhs, arg1 = rhs — both present
            // as a 2-arg CXXOperatorCallExpr by the time the AST is
            // populated). Any other arity means we matched something we do
            // not understand; bail.
            if (op_call->getNumArgs() != 2) return {};

            std::string lhs_name = leaf_name_for_compare_arg(op_call->getArg(0));
            if (lhs_name.empty()) return {};
            std::string rhs_name = leaf_name_for_compare_arg(op_call->getArg(1));
            if (rhs_name.empty()) return {};

            std::string temp = alloc.next();

            QOperation op;
            op.kind = mapped->first;
            op.result.name = temp;
            op.result.decl_loc = {}; // synthetic WHEN-lift temp

            // Resolve each operand's decl_loc so downstream consumers
            // (QValueRef equality, diagnostics) stay consistent with the
            // Phase D matcher's shape. Missing decl_loc is tolerated by
            // the emitter's text rendering, but we fill it in when the
            // peeled leaf is a DeclRefExpr (it always is for the shapes
            // the matcher accepts).
            auto make_ref = [&](const clang::Expr* a, const std::string& name) {
                QValueRef ref;
                ref.name = name;
                const clang::Expr* inner = detail::peel_to_payload(a);
                if (const auto* dre =
                        clang::dyn_cast_or_null<clang::DeclRefExpr>(inner)) {
                    if (const clang::NamedDecl* nd = dre->getDecl()) {
                        ref.decl_loc = nd->getLocation();
                    }
                }
                return ref;
            };
            op.operands.push_back(make_ref(op_call->getArg(0), lhs_name));
            op.operands.push_back(make_ref(op_call->getArg(1), rhs_name));
            op.stmt_range = stmt_range;
            scope.ops.push_back(std::move(op));

            flat_lines.push_back(render_compare_decl_line(
                temp, lhs_name, mapped->second, rhs_name));
            return temp;
        }
    }

    // Non-comparator: delegate to the Phase E bitwise / unary-NOT
    // flattener. `detail::flatten_arg` handles recursive compounds
    // (`(b | c) & d`) and bare leaves.
    return detail::flatten_arg(peeled, scope, alloc, stmt_range, flat_lines);
}

class WhenLiftCallback : public MatchFinder::MatchCallback {
public:
    explicit WhenLiftCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* when_if = r.Nodes.getNodeAs<IfStmt>("when_if");
        const auto* when_val =
            r.Nodes.getNodeAs<VarDecl>("when_val");
        const auto* mat_call =
            r.Nodes.getNodeAs<CallExpr>("materialize_call");
        if (!when_if || !when_val || !mat_call || !r.Context) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // PF-2 guard #1: the IfStmt must originate inside a macro *body*
        // expansion. A direct user `if (auto _when_val_ = ...)` is spelled
        // in file, not in a macro body, and must not match.
        const SourceLocation if_loc = when_if->getIfLoc();
        if (!if_loc.isValid() || !if_loc.isMacroID()) return;
        if (!sm.isMacroBodyExpansion(if_loc)) return;

        // PF-2 guard #2: the immediate-expansion macro must spell `WHEN`.
        // This rejects users calling `sturm::detail::materialize_when(x)`
        // from inside some *other* macro (e.g. a test harness) that
        // happens to emit an `if (auto _when_val_ = ...)` init-stmt.
        if (!detail::is_expansion_of_macro(if_loc, sm, lang, "WHEN")) return;

        // PF-2 guard #3: the materialize call must have exactly one
        // argument (the user's WHEN expression). Any other arity is a
        // sign we've matched an unrelated overload shape.
        if (mat_call->getNumArgs() != 1) return;
        const Expr* arg = mat_call->getArg(0);
        if (!arg) return;

        // Named-passthrough short-circuit: when the user wrote
        // `WHEN(named_qbool)` the peeled argument is a bare DeclRefExpr
        // to a qbool and no rewrite is needed — the existing named temp
        // already satisfies the WHEN contract. This matches PF-2's
        // behaviour verbatim and keeps the generated output for that
        // shape byte-identical to the input.
        const Expr* peeled = detail::peel_to_payload(arg);
        if (clang::isa_and_nonnull<DeclRefExpr>(peeled)) {
            return;
        }

        // Enclosing scope — required for scope / close_brace lookup.
        // The enclosing stmt is where the `WHEN(...)` call site sits
        // (one level up from the outer `if` of the macro body — we need
        // the lexical scope the *user* wrote in, not the one the macro
        // synthesized). Phase H PH-1: `enclosing_scope` transparently
        // supports both braced CompoundStmt and braceless for/while/if/else
        // body positions; ops stage with `insert_before_override`
        // regardless of the enclosing scope shape.
        const auto enc = detail::enclosing_scope(*when_if, *r.Context);
        if (!enc.valid()) return;

        // Resolve the post-body close-brace anchor — fails fast if the
        // WHEN macro body shape is not the three-`if` tower we expect.
        const SourceLocation post_body_brace =
            detail::compute_post_body_brace(when_if, sm, lang);
        if (post_body_brace.isInvalid()) return;

        // Step (1): flatten the peeled payload against a scratch scope
        // with a fresh allocator. Using a scratch scope (rather than the
        // enclosing scope directly) lets us roll back atomically on any
        // structural failure mid-flatten, and gives us a stable vector
        // of ops that we can post-tag with `insert_before_override`
        // before transferring them to the real scope.
        //
        // The `stmt_range` we pass down is the arg's source range — used
        // only for IR-level metadata (the M8 pass sorts ops by
        // stmt_range.getBegin() before reversing, so ordering among
        // multiple WHEN invocations in one scope stays source-LIFO).
        QScope scratch;
        FreshNameAllocator alloc;
        std::vector<std::string> flat_lines;
        const SourceRange stmt_range = arg->getSourceRange();

        std::string top_name =
            flatten_when_arg(peeled, scratch, alloc, stmt_range, flat_lines);
        if (top_name.empty() || flat_lines.empty()) {
            // Not a supported shape (e.g. `WHEN(foo(a))` or a literal).
            // Scratch scope is local so there's nothing to roll back.
            return;
        }

        // Step (2): schedule a QReplacement over the arg's spelling-loc
        // file range. `arg->getSourceRange()` may carry macro-body
        // encodings (the arg was reached through the WHEN macro's
        // materialize_when(expr) call); the Rewriter can only edit file
        // locations, so we normalise both endpoints to their spelling
        // locations via `SourceManager::getSpellingLoc`. The result is a
        // pure-file SourceRange spanning the user-written expression
        // inside `WHEN(...)` — e.g. `b | c` with begin at `b`'s first
        // char and end at `c`'s first char (SourceRange semantics: the
        // end location is the START of the last token, and the Rewriter
        // extends through that token via `Lexer::MeasureTokenLength`).
        //
        // We additionally validate the range by round-tripping it
        // through `Lexer::makeFileCharRange` — if that yields an
        // invalid CharSourceRange, the spelling locs sit in a
        // non-representable part of the file (scratch buffer / macro
        // expansion without a spelling) and we bail without scheduling
        // an edit. The CharSourceRange itself is not what we store; the
        // SourceRange form is what `QReplacement` carries today and
        // what the M9 emitter's `Rewriter::ReplaceText(SourceRange, ...)`
        // call expects.
        const SourceLocation spelling_begin =
            sm.getSpellingLoc(arg->getBeginLoc());
        const SourceLocation spelling_end =
            sm.getSpellingLoc(arg->getEndLoc());
        const CharSourceRange file_char_range =
            Lexer::makeFileCharRange(
                CharSourceRange::getTokenRange(spelling_begin, spelling_end),
                sm, lang);
        if (file_char_range.isInvalid()) return;

        QReplacement rep;
        rep.range = SourceRange(spelling_begin, spelling_end);
        rep.replacement = top_name;
        unit_->replacements.push_back(std::move(rep));

        // Step (3): stage a raw insertion for the pre-WHEN decl block.
        // Anchor at the WHEN macro's expansion loc — the file location
        // where `WHEN(...)` is spelled — so the decls land immediately
        // before the user's `WHEN(...)` call site.
        //
        // PM2-4: prefix each lifted decl with a `#line <N> "<basename>"`
        // directive anchored at the replacement's
        // `range.getBegin()` (the spelling begin of the user's
        // compound expression inside `WHEN(...)`). The WHEN-lift
        // decls conceptually "come from" the user's compound
        // expression — a compile error in any decl should cite the
        // user's WHEN-argument line, NOT the synthesized block's
        // physical position above the WHEN call site. All flattened
        // decls share the same anchor because they all originate
        // from the same user expression; the helper returns empty
        // on degenerate inputs (invalid loc, non-main-file) in which
        // case the block falls back to the pre-PM2-4 layout
        // byte-for-byte.
        const std::string line_directive = format_line_directive(
            sm, spelling_begin);
        UncomputeInsertion decl_block;
        decl_block.insert_before = sm.getExpansionLoc(if_loc);
        decl_block.code = render_decl_block(flat_lines, line_directive);
        unit_->raw_insertions.push_back(std::move(decl_block));

        // Step (4): transfer scratch ops into the enclosing scope, each
        // tagged with `insert_before_override = post_body_brace`. The
        // M8 synthesis pass then plants the `uncompute_*` calls directly
        // after the WHEN body's closing brace — not at the enclosing
        // CompoundStmt's close brace — preserving the user's quantum
        // control scope as the lifetime boundary for the lifted temps.
        QScope& out_scope =
            detail::find_or_create_scope(*unit_, enc, sm, lang);
        for (auto& op : scratch.ops) {
            op.insert_before_override = post_body_brace;
            out_scope.ops.push_back(std::move(op));
        }

        // Bump the detection counter (shared with PF-2 tests). Reaching
        // this point means we performed a full three-point rewrite.
        ++g_when_lift_detection_count;
        (void)when_val;
    }

private:
    QUnit* unit_;
};

// Callback pool — same pattern as the other matcher_*.cpp TUs. The
// callback is owned by the pool so its lifetime ties to the shared-library
// instance, matching the MatchFinder's non-owning `add_matcher` API.
std::vector<std::unique_ptr<WhenLiftCallback>>& when_callback_pool() {
    static std::vector<std::unique_ptr<WhenLiftCallback>> pool;
    return pool;
}

} // namespace sturm_matcher_when_lift_anon_ns
using namespace sturm_matcher_when_lift_anon_ns;

void register_when_lift_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    // PF-2 pattern — anchor on the middle IfStmt in the three-`if`
    // `WHEN(expr)` tower. The init-stmt binds a VarDecl named exactly
    // `_when_val_` whose initializer is a CallExpr to
    // `::sturm::detail::materialize_when(...)`. The macro-body guard is
    // enforced at callback time (AST matchers do not have a direct
    // "inside macro X" predicate; the SourceManager API is the path).
    //
    // Why match the IfStmt rather than the VarDecl directly? Two reasons:
    //   (a) The PF-3 rewrite needs the IfStmt to walk down to the inner
    //       body's closing brace. Binding the IfStmt here lets PF-3
    //       reuse the same node without re-walking the AST in a second
    //       matcher.
    //   (b) Matching on the IfStmt naturally filters out user code that
    //       happens to name a local `_when_val_` — a VarDecl-only match
    //       would fire on any such decl regardless of enclosing shape.
    //
    // `hasName("_when_val_")` is a string compare on the declaration
    // identifier. Clang's AST matchers treat this as an exact-spelling
    // match (no namespace qualification is needed — the VarDecl's name
    // is `_when_val_` verbatim). The macro-body expansion ensures this
    // spelling is the one the WHEN macro emitted.
    auto materialize_call = callExpr(
        callee(functionDecl(hasName("materialize_when"))),
        argumentCountIs(1)
    ).bind("materialize_call");

    auto pattern = ifStmt(
        hasInitStatement(declStmt(hasSingleDecl(
            varDecl(
                hasName("_when_val_"),
                hasInitializer(ignoringImplicit(materialize_call))
            ).bind("when_val")
        )))
    ).bind("when_if");

    auto& pool = when_callback_pool();
    pool.push_back(std::make_unique<WhenLiftCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

int when_lift_detection_count_for_test() {
    return g_when_lift_detection_count;
}

void reset_when_lift_detection_count_for_test() {
    g_when_lift_detection_count = 0;
}

} // namespace sturm::transpile
