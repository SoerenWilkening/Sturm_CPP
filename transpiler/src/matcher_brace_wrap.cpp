// matcher_brace_wrap.cpp — Phase H PH-2 auto-brace wrap for braceless
// for / while / if / else bodies containing quantum ops.
//
// Problem
// -------
// Users who write `for (...) qop;` / `if (cond) qop;` without braces would
// otherwise surface a matcher-side silent reject (pre-PH-1) or a synthetic
// BracelessBody QScope (post-PH-1) whose uncompute insertions land after
// the body's terminating `;` — semantically correct, but the emitted text
// has no `}` to delimit the synthetic scope from the surrounding context.
// Without a trailing `}`, the `uncompute_*` call that the M8 pass plants at
// `close_brace` is a sibling of the original op rather than being enclosed,
// which re-runs across iterations / branches as if it were part of the
// user's body. That is wrong on two counts:
//
//   (a) The inserted `uncompute_*` call runs on every iteration / re-enter,
//       instead of once at scope exit; and
//   (b) If the body only executes conditionally, the uncompute is NOT
//       guarded by the same condition — introducing an imbalance that the
//       resource accounting catches but the user cannot diagnose.
//
// PH-2 auto-synthesises the braces so the transpiled output looks as if
// the user had written `for (...) { qop; uncompute_qop(...); }` all along.
//
// Shapes covered
// --------------
// - `forStmt(hasBody(<non-compound stmt>))`       → wrap body
// - `whileStmt(hasBody(<non-compound stmt>))`     → wrap body
// - `ifStmt(hasThen(<non-compound stmt>))`        → wrap then-arm
// - `ifStmt(hasElse(<non-compound stmt>))`        → wrap else-arm
//
// The callback validates that the body stmt BOTH (a) is not a compound
// statement (already braced — nothing to do) and (b) lives at a file
// spelling (NOT a macro-expanded inner `if`, so WHEN's three-`if` tower
// never triggers this matcher).
//
// Quantum-op heuristic
// --------------------
// The wrap only fires when the body stmt transitively contains a quantum
// operation — otherwise we would wrap purely-classical bodies like
// `if (cond) ++counter;` for no reason, producing a gratuitous churn
// against the byte-identical-output invariant. The heuristic is:
//
//   - Any CXXOperatorCallExpr on `|`, `&`, `~`, `^`, `^=`, `+=`, `-=`,
//     `*=`, `/=`, `%=`, `==`, `!=`, `<`, `<=`, `>`, `>=` whose arguments'
//     canonical types include `qbool` or `qint_t` (name check, matching
//     the other matchers' class-name guards); OR
//   - Any VarDecl of `qbool` / `qint_t` type; OR
//   - Any macro expansion of `WHEN` in the sub-tree (detected via
//     `detail::is_expansion_of_macro`).
//
// This catches every op shape that Phases A..G recognise. New op kinds
// added in future phases will need their operator-name list updated here;
// the failure mode is conservative — a brand-new op shape in a braceless
// body would produce an un-wrapped body and the downstream matcher would
// plant its uncompute call in the wrong place. Tests will catch this.
//
// Emission
// --------
// The matcher schedules two `UncomputeInsertion` records via
// `QUnit::raw_insertions`:
//
//   - `{` at `body->getBeginLoc()`
//   - `}` at `Lexer::getLocForEndOfToken(body->getEndLoc(), 0, sm, lang)`
//
// The ordering invariant (documented in `docs/implementation_plan_*_h.md`
// PH-2 "Sharp edges") is:
//
//   1. M8 synthesis emits per-op uncompute records first, then appends
//      `raw_insertions` at the end of the result.
//   2. M9 emitter iterates insertions in reverse, so the `raw_insertions`
//      (including PH-2's `}`) are applied FIRST to the Rewriter.
//   3. `Rewriter::InsertTextBefore` at the same SourceLocation PREPENDS
//      each subsequent insert to the accumulated buffer there.
//
// So the final source order at the post-body-`;` location is:
//   `... uncompute_or(tmp, a, b); }` — exactly the shape PH-1's synthetic
// BracelessBody QScope needs to produce correct output.
//
// Per-match dedup
// ---------------
// The callback keys on `body->getBeginLoc()` so if multiple overlapping
// patterns (e.g. an if/else whose else branch happens to be another
// IfStmt with a then-arm that ALSO matches) the same body does not get
// wrapped twice. Two separate bodies in the same function — e.g. both
// arms of an if/else — each key on their own begin loc and are wrapped
// independently.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/DeclarationName.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_brace_wrap_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// Name check on the canonical type of `t`. Returns true iff the
// outermost class name (after stripping refs / sugar) is `qbool` or
// `qint_t` — same heuristic the other matchers' LHS type guards use.
static bool is_quantum_typed(QualType t) {
    if (t.isNull()) return false;
    QualType c = t.getCanonicalType().getNonReferenceType();
    // Strip const/volatile. getUnqualifiedType loses decorators but keeps
    // the underlying class reachable.
    c = c.getUnqualifiedType();
    const Type* tp = c.getTypePtrOrNull();
    if (!tp) return false;
    if (const auto* rd = tp->getAsCXXRecordDecl()) {
        // `CXXRecordDecl::getName()` asserts on records whose name is
        // not a simple identifier (anonymous / template specialisation
        // sugar layers). Guard via `getIdentifier()` so we skip those
        // without tripping the assertion — the real `qbool` / `qint_t`
        // classes are always plain identifiers.
        const IdentifierInfo* id = rd->getIdentifier();
        if (!id) return false;
        const auto name = id->getName();
        if (name == "qbool" || name == "qint_t") return true;
    }
    return false;
}

// True when `call` overloads one of the operators the matchers in Phases
// A..G bind to. This is a liberal superset of the list the individual
// registration functions use — PH-2 only needs to decide "does the body
// contain a quantum op" and then let the downstream matchers do the
// heavy lifting. A false positive here is harmless: it just means we
// wrap a body that happens to reference a quantum operand but has no
// op the downstream matchers care about, in which case no uncompute
// insertion is produced and the wrapped braces add at most a stylistic
// rewrap. A false negative, by contrast, leaves the body un-braced and
// the synthesised uncompute call dangles — so the list leans wide on
// purpose.
static bool is_quantum_operator_name(OverloadedOperatorKind op) {
    switch (op) {
    // Bitwise / unary for qbool shapes.
    case OO_Pipe:
    case OO_Amp:
    case OO_Tilde:
    case OO_Caret:
    // Self-inverse and compound-assign mutations.
    case OO_CaretEqual:
    case OO_PlusEqual:
    case OO_MinusEqual:
    case OO_StarEqual:
    case OO_SlashEqual:
    case OO_PercentEqual:
    // qint comparisons.
    case OO_EqualEqual:
    case OO_ExclaimEqual:
    case OO_Less:
    case OO_LessEqual:
    case OO_Greater:
    case OO_GreaterEqual:
        return true;
    default:
        return false;
    }
}

// Walk the subtree rooted at `s` and report whether any descendent looks
// like a quantum op under the heuristic described at the top of the
// file. Stops at the first hit (the result is boolean, not a count).
class QuantumOpProbe : public RecursiveASTVisitor<QuantumOpProbe> {
public:
    QuantumOpProbe(const SourceManager& sm, const LangOptions& lang)
        : sm_(sm), lang_(lang) {}

    bool found() const { return found_; }

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* call) {
        if (!call) return true;
        if (!is_quantum_operator_name(call->getOperator())) return true;
        // At least one argument must be quantum-typed for the op to be
        // of interest. Constants / classical operands alone never
        // produce a quantum op.
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            const Expr* a = call->getArg(i);
            if (!a) continue;
            if (is_quantum_typed(a->getType())) {
                found_ = true;
                return false;
            }
        }
        return true;
    }

    bool VisitVarDecl(VarDecl* vd) {
        if (!vd) return true;
        if (is_quantum_typed(vd->getType())) {
            found_ = true;
            return false;
        }
        return true;
    }

    // WHEN-macro expansion inside the body is a quantum op marker. The
    // macro itself produces nested `if`s + a `materialize_when` call, so
    // `VisitCallExpr` picks up the `materialize_when` call. We key on
    // macro spelling to match even when the user has wrapped WHEN in a
    // personal macro.
    bool VisitIfStmt(IfStmt* is) {
        if (!is) return true;
        if (detail::is_expansion_of_macro(is->getIfLoc(), sm_, lang_,
                                           "WHEN")) {
            found_ = true;
            return false;
        }
        return true;
    }

    // If the body calls materialize_when directly (e.g. a user who
    // forgot to use the WHEN macro), we still want to count that as a
    // quantum op marker for the purpose of brace-wrapping.
    //
    // `FunctionDecl::getName()` asserts when the decl's name is not a
    // simple identifier (e.g. constructor / destructor / operator-
    // name). Guard the call with `getDeclName().isIdentifier()` so we
    // never trip the assertion on call exprs to special members — the
    // `materialize_when` free function is always a plain identifier,
    // so any non-identifier callee is trivially rejected.
    bool VisitCallExpr(CallExpr* call) {
        if (!call) return true;
        const FunctionDecl* fd = call->getDirectCallee();
        if (!fd) return true;
        const DeclarationName dn = fd->getDeclName();
        if (!dn.isIdentifier()) return true;
        const IdentifierInfo* id = dn.getAsIdentifierInfo();
        if (!id) return true;
        if (id->getName() == "materialize_when") {
            found_ = true;
            return false;
        }
        return true;
    }

private:
    const SourceManager& sm_;
    const LangOptions& lang_;
    bool found_ = false;
};

static bool body_contains_quantum_op(const Stmt* body, ASTContext& ctx) {
    if (!body) return false;
    QuantumOpProbe probe(ctx.getSourceManager(), ctx.getLangOpts());
    // TraverseStmt accepts a non-const Stmt* — we legitimately hold a
    // non-const handle to the body through the AST, so the const_cast
    // here is safe. RecursiveASTVisitor does not mutate the tree.
    probe.TraverseStmt(const_cast<Stmt*>(body));
    return probe.found();
}

// Shared body for every control-flow kind: validate the body stmt, check
// the quantum-op guard, then schedule the two raw insertions. Dedup on
// body begin-loc so a body that matches via two patterns (which does
// not happen in practice, but the guard costs nothing) is wrapped once.
class BraceWrapCallback : public MatchFinder::MatchCallback {
public:
    explicit BraceWrapCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* body = r.Nodes.getNodeAs<Stmt>("body");
        if (!body || !r.Context) return;

        // Guard (a): already-braced bodies are handled by the legacy
        // CompoundStmt path — we must not wrap them again. The matcher
        // pattern already filters CompoundStmt, but defensive check.
        if (isa<CompoundStmt>(body)) return;

        const SourceLocation begin = body->getBeginLoc();
        const SourceLocation end   = body->getEndLoc();
        if (!begin.isValid() || !end.isValid()) return;

        // Guard (b): a macro-expanded body stmt is compiler-synthesised
        // (e.g. WHEN's three-`if` tower has then-arms that look like
        // braceless bodies). Never wrap those — they are not user-
        // visible scopes.
        if (begin.isMacroID()) return;

        // Quantum-op guard: only wrap when the body actually contains
        // an op one of the downstream matchers will recognise.
        if (!body_contains_quantum_op(body, *r.Context)) return;

        // Dedup on begin-loc raw encoding.
        const unsigned key = begin.getRawEncoding();
        if (!seen_begins_.insert(key).second) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // Opening `{` at the body's begin loc — this is exactly where
        // PH-1's synthetic BracelessBody QScope's `open_brace` points,
        // so the insertion anchor semantics line up perfectly.
        UncomputeInsertion open_rec;
        open_rec.insert_before = begin;
        open_rec.code = "{ ";
        unit_->raw_insertions.push_back(std::move(open_rec));

        // Closing `}` at the loc immediately past the body's terminating
        // token (typically the `;` of a single statement). This is the
        // exact same loc PH-1's synthetic QScope uses as its
        // `close_brace`, so the M8 uncompute insertion at `close_brace`
        // and the PH-2 `}` insertion at the same loc stack correctly
        // under the emitter's reverse-InsertTextBefore ordering.
        const SourceLocation close_loc =
            Lexer::getLocForEndOfToken(end, /*Offset=*/0, sm, lang);
        if (!close_loc.isValid()) {
            // Best-effort: invalid close loc means we cannot place the
            // `}` — leave the open `{` on the insertion list rather
            // than rolling back (the downstream uncompute insertion
            // would still land at the correct place; unbalanced braces
            // are a lesser evil than dropping the whole transform).
            return;
        }
        UncomputeInsertion close_rec;
        close_rec.insert_before = close_loc;
        close_rec.code = " }";
        unit_->raw_insertions.push_back(std::move(close_rec));

        ++g_brace_wrap_detection_count_;
    }

    // Test-only counter accessors — the matcher does not mutate a
    // QUnit scope, so tests need a standalone observable.
    static int detection_count() { return g_brace_wrap_detection_count_; }
    static void reset_detection_count() { g_brace_wrap_detection_count_ = 0; }

private:
    QUnit* unit_;
    // Per-callback dedup set. The same physical callback instance is
    // reused across multiple pattern registrations (for/while/ifThen/
    // ifElse) so the set coalesces duplicate hits from e.g. the then-
    // arm of an if/else binding via the ifThen pattern while the else-
    // arm body binds via the ifElse pattern.
    std::unordered_set<unsigned> seen_begins_;
    static int g_brace_wrap_detection_count_;
};

int BraceWrapCallback::g_brace_wrap_detection_count_ = 0;

std::vector<std::unique_ptr<BraceWrapCallback>>& brace_wrap_callback_pool() {
    static std::vector<std::unique_ptr<BraceWrapCallback>> pool;
    return pool;
}

} // namespace sturm_matcher_brace_wrap_anon_ns
using namespace sturm_matcher_brace_wrap_anon_ns;

void register_brace_wrap_matcher(clang::ast_matchers::MatchFinder& finder,
                                 QUnit& unit) {
    auto& pool = brace_wrap_callback_pool();
    pool.push_back(std::make_unique<BraceWrapCallback>(&unit));
    BraceWrapCallback* cb = pool.back().get();

    // Four patterns — one per control-flow shape. Each binds the body
    // stmt as "body" so the callback can read it uniformly. The
    // `unless(compoundStmt())` filter drops already-braced bodies at
    // the matcher level; the callback's defensive `isa<CompoundStmt>`
    // guard is belt-and-braces.
    auto non_compound = stmt(unless(compoundStmt())).bind("body");

    finder.addMatcher(forStmt(hasBody(non_compound)), cb);
    finder.addMatcher(whileStmt(hasBody(non_compound)), cb);
    finder.addMatcher(ifStmt(hasThen(non_compound)), cb);
    finder.addMatcher(ifStmt(hasElse(non_compound)), cb);
}

int brace_wrap_detection_count_for_test() {
    return BraceWrapCallback::detection_count();
}

void reset_brace_wrap_detection_count_for_test() {
    BraceWrapCallback::reset_detection_count();
}

} // namespace sturm::transpile
