// matcher_common.hpp — internal helpers shared by every matcher module.
//
// This header is NOT part of the public transpiler API. It lives under
// transpiler/src/ (not include/) because it exposes Clang AST types that
// downstream callers should not transitively pick up through the public
// matcher.hpp. Only the matcher_*.cpp implementation files include this.
//
// Contents:
//   - `enclosing_scope`         : walk ParentMapContext to the nearest
//     enclosing user-visible scope anchor. Returns a {kind, anchor} pair
//     where kind is CompoundStmt (the legacy shape: an enclosing
//     CompoundStmt) or BracelessBody (Phase H PH-1: a single-statement
//     body of a for/while/if/else without braces). Two overloads
//     (Decl, Stmt) funnel through the same DynTypedNode walk.
//   - `find_or_create_scope`    : look up / allocate a QScope. Accepts
//     either the legacy CompoundStmt shape OR the PH-1 EnclosingScope
//     shape, so Phase H matchers can key synthetic QScopes on braceless
//     body stmts while Phase A-G matchers keep their familiar call shape.
//   - `make_ref`                : lift a DeclRefExpr into a QValueRef.
//   - `peel_to_payload`         : strip implicit/paren/temp wrappers (Phase E).
//   - `op_kind_for`             : map a CXXOperatorCallExpr to QOpKind.
//   - `render_decl_line`        : render `qbool x = a <op> b;` text.
//   - `flatten_arg` / `flatten_inner_call` : recursively flatten a compound
//     bitwise sub-expression into a sequence of single-op decls + QOperations.
//
// Every helper is `inline` (header-only), trading a small per-TU code-size
// hit for zero link-time symbols. The MatchFinder callbacks themselves live
// in their respective .cpp files; this header has no callback state.

#ifndef STURM_TRANSPILE_MATCHER_COMMON_HPP
#define STURM_TRANSPILE_MATCHER_COMMON_HPP

#include "sturm/transpile/qir.hpp"
#include "fresh_names.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sturm::transpile::detail {

// ── Phase H PH-1: enclosing-scope finder ────────────────────────────────────
//
// The scope-finder tells callers which source-text region owns a matched
// node. Phase A-G only needed the "braced CompoundStmt" shape, since every
// pre-existing matcher guard was `if (!cs) return;` — a matched op inside
// a braceless `for (...) qop;` body would silently drop out of the IR.
// Phase H widens the contract to the two user-visible scope shapes:
//
//   - CompoundStmt  : the legacy shape. Same as the old
//     `enclosing_compound_stmt` — walk up the parent chain until we hit
//     an enclosing CompoundStmt, then return it.
//   - BracelessBody : the single-statement body of a `for`, `while`, `if`
//     (then or else) that has no surrounding `{ }`. The anchor is the
//     body Stmt itself. The synthetic QScope is keyed on the body Stmt's
//     begin loc; its `close_brace` is the location immediately past the
//     body Stmt's terminating token, computed with
//     `Lexer::getLocForEndOfToken` so insertions there land after the
//     user's trailing `;`.
//
// Macro-expanded body stmts are deliberately NOT treated as BracelessBody:
// the Phase F/G WHEN macro expands to three nested `if`s whose then-arms
// look like "braceless bodies" to a naive parent walk, but they are
// compiler-generated and must not be lifted into user-visible scopes.
// The check is `body->getBeginLoc().isMacroID()` → skip and keep walking up.
enum class QScopeKind { CompoundStmt, BracelessBody };

// Scope-finder result. `kind == CompoundStmt`: `compound` is the enclosing
// CompoundStmt (non-null). `kind == BracelessBody`: `braceless_body` is
// the body Stmt (non-null). Both fields are populated for the matching
// kind and left null for the other so callers can branch cleanly.
// A default-constructed EnclosingScope has `compound == nullptr` and
// `braceless_body == nullptr` — the "no enclosing scope found" signal.
struct EnclosingScope {
    QScopeKind kind = QScopeKind::CompoundStmt;
    const clang::CompoundStmt* compound = nullptr;
    const clang::Stmt*         braceless_body = nullptr;

    // True when the finder produced a usable scope anchor.
    bool valid() const {
        return compound != nullptr || braceless_body != nullptr;
    }
};

// True when `body` is the branch of a ForStmt/WhileStmt/IfStmt that
// qualifies as a braceless user body — i.e. non-compound, with a
// non-macro spelling. Shared between the parent-walk (both Decl and Stmt
// overloads). Invalid / macro-expanded body stmts are rejected here so
// the walk falls through to the next parent.
inline bool is_user_braceless_body(const clang::Stmt* body) {
    if (!body) return false;
    if (clang::isa<clang::CompoundStmt>(body)) return false;
    const clang::SourceLocation loc = body->getBeginLoc();
    if (!loc.isValid()) return false;
    // WHEN and friends expand through `_when_capture_`/`_when_val_`/
    // `_when_guard_` nested `if`s; their then-branches superficially
    // look like braceless bodies. Skip macro-expanded spellings.
    if (loc.isMacroID()) return false;
    return true;
}

// Walk up the parent chain of `node` toward the enclosing scope. Returns
// {CompoundStmt, cs} if the first enclosing scope is a braced block, or
// {BracelessBody, body_stmt} if the first enclosing scope is a
// non-compound for/while/if/else body position. A default-constructed
// EnclosingScope (`valid() == false`) signals "no enclosing scope"
// (e.g. the node lives at file scope — not possible for the matched ops
// the Phase A-G callbacks care about, but we return cleanly for safety).
template <typename T>
inline EnclosingScope enclosing_scope_impl(const T& n, clang::ASTContext& ctx) {
    clang::DynTypedNode current = clang::DynTypedNode::create(n);
    // `prev_stmt` tracks the most recent Stmt we walked through. When we
    // reach a ForStmt/WhileStmt/IfStmt, we check whether prev_stmt is the
    // body/then/else branch — i.e. whether our original node lives inside
    // the control-flow body (as opposed to its condition / init / inc).
    const clang::Stmt* prev_stmt = nullptr;
    if constexpr (std::is_base_of_v<clang::Stmt, T>) {
        prev_stmt = &n;
    }
    while (true) {
        const auto parents = ctx.getParents(current);
        if (parents.empty()) return {};
        // Follow only the first parent — multi-parent shapes only arise in
        // template instantiations, which the MVP matcher does not enter.
        current = parents[0];
        // CompoundStmt branch — the legacy shape.
        if (const auto* cs = current.get<clang::CompoundStmt>()) {
            return {QScopeKind::CompoundStmt, cs, nullptr};
        }
        // BracelessBody branch — for/while/if with a non-compound body.
        if (const auto* fs = current.get<clang::ForStmt>()) {
            if (fs->getBody() == prev_stmt &&
                is_user_braceless_body(fs->getBody())) {
                return {QScopeKind::BracelessBody, nullptr, fs->getBody()};
            }
        } else if (const auto* ws = current.get<clang::WhileStmt>()) {
            if (ws->getBody() == prev_stmt &&
                is_user_braceless_body(ws->getBody())) {
                return {QScopeKind::BracelessBody, nullptr, ws->getBody()};
            }
        } else if (const auto* is = current.get<clang::IfStmt>()) {
            if ((is->getThen() == prev_stmt &&
                 is_user_braceless_body(is->getThen())) ||
                (is->getElse() == prev_stmt &&
                 is_user_braceless_body(is->getElse()))) {
                return {QScopeKind::BracelessBody,
                        nullptr,
                        (is->getThen() == prev_stmt) ? is->getThen()
                                                     : is->getElse()};
            }
        }
        // Update prev_stmt for the next iteration.
        if (const auto* as_stmt = current.get<clang::Stmt>()) {
            prev_stmt = as_stmt;
        }
    }
}

inline EnclosingScope
enclosing_scope(const clang::Decl& decl, clang::ASTContext& ctx) {
    return enclosing_scope_impl(decl, ctx);
}

inline EnclosingScope
enclosing_scope(const clang::Stmt& stmt, clang::ASTContext& ctx) {
    return enclosing_scope_impl(stmt, ctx);
}

// Locate the QScope in `unit` whose open_brace matches `cs`, creating one at
// the end of `unit.scopes` if none exists. The raw encoding of the opening
// brace is a stable scope identity within a single translation unit.
//
// Legacy Phase A-G overload: called by matchers that have already narrowed
// to a CompoundStmt (e.g. the WHEN matchers, which inspect macro-generated
// AST shapes and must remain CompoundStmt-only).
inline QScope& find_or_create_scope(QUnit& unit, const clang::CompoundStmt& cs) {
    const auto key = cs.getLBracLoc().getRawEncoding();
    for (auto& scope : unit.scopes) {
        if (scope.open_brace.getRawEncoding() == key) return scope;
    }
    QScope fresh;
    fresh.open_brace  = cs.getLBracLoc();
    fresh.close_brace = cs.getRBracLoc();
    unit.scopes.push_back(std::move(fresh));
    return unit.scopes.back();
}

// Phase H PH-1 overload: locate-or-create a QScope from an EnclosingScope
// result. CompoundStmt-kind scopes delegate to the legacy overload above;
// BracelessBody-kind scopes synthesize a QScope keyed on the body Stmt's
// begin loc, with `close_brace` set just past the body's terminating
// token (one-past-the-`;`).
inline QScope& find_or_create_scope(QUnit& unit,
                                    const EnclosingScope& es,
                                    const clang::SourceManager& sm,
                                    const clang::LangOptions& lang) {
    if (es.kind == QScopeKind::CompoundStmt) {
        // Compound branch never touches sm/lang — the braces come straight
        // off the CompoundStmt node. Delegating keeps identity behaviour
        // byte-identical to Phase A-G.
        return find_or_create_scope(unit, *es.compound);
    }
    // BracelessBody branch. Key on the body Stmt's begin loc so repeated
    // matches inside the same braceless body coalesce into one QScope.
    const clang::Stmt* body = es.braceless_body;
    const clang::SourceLocation begin = body->getBeginLoc();
    const auto key = begin.getRawEncoding();
    for (auto& scope : unit.scopes) {
        if (scope.open_brace.getRawEncoding() == key) return scope;
    }
    QScope fresh;
    fresh.open_brace = begin;
    // `Lexer::getLocForEndOfToken` on the body's last token returns the
    // loc immediately past that token. For a `qop;` body the end token
    // is the `;`, so `close_brace` lands exactly where a synthesized
    // `}` would go in the Phase H brace-wrap pass.
    fresh.close_brace = clang::Lexer::getLocForEndOfToken(
        body->getEndLoc(), /*Offset=*/0, sm, lang);
    unit.scopes.push_back(std::move(fresh));
    return unit.scopes.back();
}

// Walk up the macro-expansion chain from `loc` and look for an immediate
// caller whose spelled macro name is `needle` (e.g. "WHEN"). Returns true
// if any step on the caller chain spells that macro. A pure textual
// comparison against `Lexer::getImmediateMacroName` is intentional: the
// `WHEN` macro is defined in a specific header today, but we do not want
// to pin the lookup to a particular FileID / expansion depth — a user
// wrapper `#define MY_WHEN(x) WHEN(x)` should still be recognized at the
// one-level-up step where `WHEN` is spelled.
//
// Promoted from `matcher_when_lift.cpp` into `matcher_common.hpp` by Phase G
// PG-1 so the new `matcher_when_nested.cpp` TU can reuse it verbatim. Body
// is byte-identical to the original; only the surrounding namespace + the
// `inline` keyword change (header-only linkage).
inline bool is_expansion_of_macro(clang::SourceLocation loc,
                                  const clang::SourceManager& sm,
                                  const clang::LangOptions& lang,
                                  llvm::StringRef needle) {
    // Only valid for locations inside some macro body expansion.
    if (!loc.isMacroID()) return false;
    // Cap the walk — pathological circular expansions are impossible in
    // well-formed source but a belt-and-braces bound costs nothing.
    clang::SourceLocation cur = loc;
    for (int hops = 0; hops < 64 && cur.isMacroID(); ++hops) {
        llvm::StringRef name =
            clang::Lexer::getImmediateMacroName(cur, sm, lang);
        if (name == needle) return true;
        clang::SourceLocation next = sm.getImmediateMacroCallerLoc(cur);
        if (next == cur) break; // fixed point — no more caller info.
        cur = next;
    }
    return false;
}

// Resolve the post-body close-brace anchor for a `WHEN(expr) { body }`
// invocation. The WHEN macro expands to three nested `if`s:
//
//   if (WhenCapture _when_capture_{}; true)       // outer
//       if (decltype(auto) _when_val_ = ...; true) // middle (bound as when_if)
//           if (auto _when_guard_ = ...; ...)      // inner
//               { body }                           // CompoundStmt
//
// Starting from the middle `if` (our match anchor), descend `getThen()`
// twice to reach the user's body CompoundStmt. The first descent lands
// on the innermost `if` (the `_when_guard_` gate); the second descent
// lands on the CompoundStmt body. We then return the location immediately
// past the closing `}` so insertions anchored here land after the user's
// body, outside the WHEN's scope.
//
// Returns an invalid SourceLocation on any structural mismatch (e.g.
// WHEN macro wrapped in an unexpected statement shape) — the caller
// treats that as a hard bail and skips the whole lift.
//
// Promoted from `matcher_when_lift.cpp` into `matcher_common.hpp` by Phase G
// PG-1 so the new `matcher_when_nested.cpp` TU can reuse it verbatim. Body
// is byte-identical to the original; only the surrounding namespace + the
// `inline` keyword change (header-only linkage).
inline clang::SourceLocation
compute_post_body_brace(const clang::IfStmt* when_if,
                        const clang::SourceManager& sm,
                        const clang::LangOptions& lang) {
    if (!when_if) return {};
    const clang::Stmt* first = when_if->getThen();
    const auto* inner_if = clang::dyn_cast_or_null<clang::IfStmt>(first);
    if (!inner_if) return {};
    const clang::Stmt* second = inner_if->getThen();
    const auto* body = clang::dyn_cast_or_null<clang::CompoundStmt>(second);
    if (!body) return {};
    const clang::SourceLocation rbrac = body->getRBracLoc();
    if (rbrac.isInvalid()) return {};
    // `getLocForEndOfToken` on the `}` token returns the location
    // immediately past the closing brace. Passing 0 for the `Offset`
    // parameter is the standard convention (we want end-of-token, not
    // end-of-token+N).
    return clang::Lexer::getLocForEndOfToken(rbrac, /*Offset=*/0, sm, lang);
}

// Extract a QValueRef from a DeclRefExpr. The decl_loc is the referenced
// declaration's location (NOT the call-site DeclRefExpr's location), which
// is what QValueRef equality uses to discriminate shadowed locals.
inline QValueRef make_ref(const clang::DeclRefExpr& dre) {
    QValueRef ref;
    const clang::NamedDecl* nd = dre.getDecl();
    if (nd) {
        ref.name     = nd->getNameAsString();
        ref.decl_loc = nd->getLocation();
    }
    return ref;
}

// ---- Compound-expression flattening helpers -----------------------------
//
// Promoted from `matcher_qbool_compound.cpp` (Phase E) by Phase F PF-0 so
// a second matcher TU (the WHEN-lift matcher) can reuse them without
// copy-paste. Behaviour is unchanged. These helpers have no dependency on
// MatchFinder or matcher state; they consume Clang AST nodes and write
// into a QScope plus a flat std::vector<std::string> of decl-line text.
// The FreshNameAllocator is owned by the caller (per-callback / per-QUnit).

// Peel a sub-expression through any number of ParenExpr / ImplicitCastExpr
// / CXXConstructExpr / MaterializeTemporaryExpr / CXXBindTemporaryExpr /
// user-defined-conversion CXXMemberCallExpr wrappers to reach the "shape"
// the matcher is interested in. The MaterializeTemporaryExpr /
// CXXBindTemporaryExpr / CXXMemberCallExpr (zero-arg conversion) descents
// are needed to traverse the `OrExpr<qbool>::operator qbool()` /
// `AndExpr<qbool>::operator qbool()` chains that lazy_expr.hpp produces
// under STURM_BACKEND_ENABLED. The CXXMemberCallExpr peel is restricted
// to zero-arg member calls whose callee is a CXXConversionDecl so that
// regular member calls (e.g. `b.flip()`) are not silently descended into.
inline const clang::Expr* peel_to_payload(const clang::Expr* e) {
    if (!e) return nullptr;
    const clang::Expr* cur = e->IgnoreParenImpCasts();
    while (true) {
        if (const auto* mte = clang::dyn_cast<clang::MaterializeTemporaryExpr>(cur)) {
            if (const clang::Expr* sub = mte->getSubExpr()) {
                cur = sub->IgnoreParenImpCasts();
                continue;
            }
        }
        if (const auto* bte = clang::dyn_cast<clang::CXXBindTemporaryExpr>(cur)) {
            if (const clang::Expr* sub = bte->getSubExpr()) {
                cur = sub->IgnoreParenImpCasts();
                continue;
            }
        }
        if (const auto* ctor = clang::dyn_cast<clang::CXXConstructExpr>(cur)) {
            // Degenerate ctors (zero-arg, or the implicit copy-from-temp
            // wrapping the real call) have exactly one argument we should
            // descend into. If the ctor has a different arity, bail — it
            // is not the copy-elision wrapper pattern we know how to peel.
            if (ctor->getNumArgs() == 1 && ctor->getArg(0)) {
                cur = ctor->getArg(0)->IgnoreParenImpCasts();
                continue;
            }
        }
        if (const auto* mce = clang::dyn_cast<clang::CXXMemberCallExpr>(cur)) {
            // User-defined conversion: zero-arg member call whose target
            // is a CXXConversionDecl (e.g. `OrExpr<qbool>::operator
            // qbool()` in lazy_expr.hpp). Descend into the implicit object
            // argument so we can keep walking toward the wrapped `|`/`&`
            // CXXOperatorCallExpr.
            if (mce->getNumArgs() == 0) {
                if (const auto* method = mce->getMethodDecl()) {
                    if (clang::isa<clang::CXXConversionDecl>(method)) {
                        if (const clang::Expr* obj = mce->getImplicitObjectArgument()) {
                            cur = obj->IgnoreParenImpCasts();
                            continue;
                        }
                    }
                }
            }
        }
        break;
    }
    return cur;
}

// Return the QOpKind corresponding to a CXXOperatorCallExpr's overloaded
// operator, or std::nullopt if the operator is not one of the qbool
// kinds the compound / WHEN-lift matchers handle. OR and AND are the two
// binary bitwise kinds (Phase E compound matcher); NOT is the unary
// bitwise kind added in Phase F PF-3 for the `WHEN(~a) { body }` lift.
// The caller treats nullopt as a "leaf" signal (the sub-expression is
// not a recognized bitwise op, so it must reduce to a bare DeclRefExpr).
inline std::optional<QOpKind>
op_kind_for(const clang::CXXOperatorCallExpr* call) {
    if (!call) return std::nullopt;
    switch (call->getOperator()) {
    case clang::OO_Pipe:   return QOpKind::OR;
    case clang::OO_Amp:    return QOpKind::AND;
    case clang::OO_Tilde:  return QOpKind::NOT;
    default:               return std::nullopt;
    }
}

// Render a single flat decl line for a binary bitwise op (OR or AND).
// Intermediate temps include the `;`; the outer VarDecl in the Phase E
// compound matcher omits it (the original source's terminating `;` lies
// just past the VarDecl range and the Rewriter preserves it). Phase F
// PF-3 always passes `include_semicolon=true` because the original
// WHEN-argument source range does not include the user's trailing `;`.
inline std::string render_decl_line(const std::string& name,
                                    QOpKind kind,
                                    const std::string& lhs,
                                    const std::string& rhs,
                                    bool include_semicolon) {
    std::ostringstream os;
    const char* op = (kind == QOpKind::OR) ? "|" : "&";
    os << "qbool " << name << " = " << lhs << " " << op << " " << rhs;
    if (include_semicolon) os << ";";
    return os.str();
}

// Render a single flat decl line for a unary NOT (Phase F PF-3). Shape is
// `qbool <name> = ~<operand>` with an optional trailing semicolon.
inline std::string render_not_decl_line(const std::string& name,
                                        const std::string& operand,
                                        bool include_semicolon) {
    std::ostringstream os;
    os << "qbool " << name << " = ~" << operand;
    if (include_semicolon) os << ";";
    return os.str();
}

// Forward decl — flatten_arg, flatten_inner_call, and flatten_unary_not_call
// are mutually recursive.
inline std::string flatten_arg(const clang::Expr* arg,
                               QScope& scope,
                               FreshNameAllocator& alloc,
                               clang::SourceRange stmt_range,
                               std::vector<std::string>& flat_lines);

inline std::string flatten_unary_not_call(const clang::CXXOperatorCallExpr* call,
                                          QScope& scope,
                                          FreshNameAllocator& alloc,
                                          clang::SourceRange stmt_range,
                                          std::vector<std::string>& flat_lines);

// Flatten an inner op-call node. Allocates a fresh temp name, flattens
// both of the call's arguments recursively, records one QOperation into
// `scope.ops`, and appends one decl line to `flat_lines`. Returns the
// allocated temp name.
inline std::string flatten_inner_call(const clang::CXXOperatorCallExpr* call,
                                      QOpKind kind,
                                      QScope& scope,
                                      FreshNameAllocator& alloc,
                                      clang::SourceRange stmt_range,
                                      std::vector<std::string>& flat_lines) {
    const clang::Expr* arg0 = call->getArg(0);
    const clang::Expr* arg1 = call->getArg(1);
    std::string lhs_name = flatten_arg(arg0, scope, alloc, stmt_range,
                                       flat_lines);
    if (lhs_name.empty()) return {};
    std::string rhs_name = flatten_arg(arg1, scope, alloc, stmt_range,
                                       flat_lines);
    if (rhs_name.empty()) return {};

    std::string temp = alloc.next();

    // Resolve each operand's decl_loc by peeling back to the leaf
    // DeclRefExpr when the operand is a bare identifier. For an
    // intermediate temp (result of a nested op-call), we leave decl_loc
    // invalid — the temp is not a user-written decl, and the uncompute
    // pass does not need decl_loc for its text rendering.
    QOperation op;
    op.kind = kind;
    op.result.name     = temp;
    op.result.decl_loc = {}; // synthetic intermediate
    auto build_ref = [&](const clang::Expr* a, const std::string& name) {
        QValueRef ref;
        ref.name = name;
        const clang::Expr* inner = peel_to_payload(a);
        if (const auto* dre = clang::dyn_cast_or_null<clang::DeclRefExpr>(inner)) {
            if (const clang::NamedDecl* nd = dre->getDecl()) {
                ref.decl_loc = nd->getLocation();
            }
        }
        return ref;
    };
    op.operands.push_back(build_ref(arg0, lhs_name));
    op.operands.push_back(build_ref(arg1, rhs_name));
    op.stmt_range = stmt_range;
    scope.ops.push_back(std::move(op));

    flat_lines.push_back(render_decl_line(
        temp, kind, lhs_name, rhs_name, /*include_semicolon=*/true));
    return temp;
}

// Flatten a unary NOT op-call (Phase F PF-3). Allocates a fresh temp
// name, flattens the single argument recursively, records one
// QOperation{kind=NOT} into `scope.ops`, and appends one decl line to
// `flat_lines`. Returns the allocated temp name.
inline std::string flatten_unary_not_call(const clang::CXXOperatorCallExpr* call,
                                          QScope& scope,
                                          FreshNameAllocator& alloc,
                                          clang::SourceRange stmt_range,
                                          std::vector<std::string>& flat_lines) {
    const clang::Expr* arg0 = call->getArg(0);
    std::string operand_name = flatten_arg(arg0, scope, alloc, stmt_range,
                                           flat_lines);
    if (operand_name.empty()) return {};

    std::string temp = alloc.next();

    QOperation op;
    op.kind = QOpKind::NOT;
    op.result.name     = temp;
    op.result.decl_loc = {}; // synthetic intermediate
    QValueRef operand_ref;
    operand_ref.name = operand_name;
    const clang::Expr* inner = peel_to_payload(arg0);
    if (const auto* dre = clang::dyn_cast_or_null<clang::DeclRefExpr>(inner)) {
        if (const clang::NamedDecl* nd = dre->getDecl()) {
            operand_ref.decl_loc = nd->getLocation();
        }
    }
    op.operands.push_back(std::move(operand_ref));
    op.stmt_range = stmt_range;
    scope.ops.push_back(std::move(op));

    flat_lines.push_back(render_not_decl_line(
        temp, operand_name, /*include_semicolon=*/true));
    return temp;
}

// Walk an argument of a compound qbool operator-call. If the argument is
// itself a recognized `|`/`&`/`~` op-call, recurse to flatten the subtree:
// emit a QOperation for every interior node with a fresh `__stu_t<N>`
// result name, append each decl line to `flat_lines`, and return the
// allocated temp name so the caller can cite it as an operand. If the
// argument is a bare DeclRefExpr, return its identifier unchanged (no
// new op is emitted).
//
// Returns an empty string on a structural match failure (non-DRE leaf
// that is not a recognized op-call) — the caller interprets that as
// "reject the whole compound match" and no ops are appended.
inline std::string flatten_arg(const clang::Expr* arg,
                               QScope& scope,
                               FreshNameAllocator& alloc,
                               clang::SourceRange stmt_range,
                               std::vector<std::string>& flat_lines) {
    const clang::Expr* inner = peel_to_payload(arg);
    if (!inner) return {};

    // Leaf: bare DeclRefExpr to a named qbool.
    if (const auto* dre = clang::dyn_cast<clang::DeclRefExpr>(inner)) {
        if (const clang::NamedDecl* nd = dre->getDecl()) {
            return nd->getNameAsString();
        }
        return {};
    }

    // Interior: CXXOperatorCallExpr on `|`, `&`, or `~`.
    if (const auto* call = clang::dyn_cast<clang::CXXOperatorCallExpr>(inner)) {
        if (auto kind = op_kind_for(call)) {
            if (*kind == QOpKind::NOT) {
                if (call->getNumArgs() != 1) return {};
                return flatten_unary_not_call(call, scope, alloc,
                                              stmt_range, flat_lines);
            }
            if (call->getNumArgs() != 2) return {};
            return flatten_inner_call(call, *kind, scope, alloc,
                                      stmt_range, flat_lines);
        }
    }
    // Anything else (member call, non-bitwise op, literal, ...) is not a
    // supported compound-expression shape. Reject.
    return {};
}

} // namespace sturm::transpile::detail

#endif // STURM_TRANSPILE_MATCHER_COMMON_HPP
