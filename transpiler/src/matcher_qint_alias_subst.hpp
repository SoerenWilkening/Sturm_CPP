// matcher_qint_alias_subst.hpp — sturm-65rs.8 (Beat C1) public surface.
//
// Plan §9, PRD §4.3. The matcher recognises every site at which a
// `sturm::frontend::qint` is *spelled in source*: a VarDecl, a
// ParmVarDecl, a FieldDecl, a function return type, or a
// CXXFunctionalCastExpr target type. The C2 emitter (sturm-65rs.9)
// consumes the typed `Match` struct and rewrites each TypeLoc range
// to `sturm::qint_t<W>` where `W` comes from the existing
// `infer_width()` / falls through to `kDefaultWidth = 32`.
//
// Matcher-only contract at this beat:
//   - Discriminator: `cxxRecordDecl(hasName("qint"),
//     hasParent(namespaceDecl(hasName("frontend"))))`. This rejects
//     the backend `qint_t<W>` (different class name) and the namespace
//     alias `using qint = sturm::qint_t<W>;` (a TypeAliasDecl, not a
//     CXXRecordDecl) by construction.
//   - Per-anchor bind names: `vd`, `pmd`, `fd`, `fn`, `cast` —
//     stable across beats so the C2 emitter and the C3 consumer
//     wiring can rely on them.
//   - Each `Match` records the captured `TypeLoc` SourceRange so the
//     emitter can rewrite the type-spelling without re-walking the
//     AST. The matcher does NOT filter on macro-expansion / isInSystemHeader;
//     that policy is the emitter's concern (deliberate — keeps this
//     beat anchor-only).
//
// PRD §4.3 close: the discriminator naturally rejects the namespace
// alias because the alias resolves to `qint_t<W>` whose record name
// is `qint_t`, not `qint`. No additional gating needed here.
//
// LoC budget: <= 300 (plan §1, §9 / C1).

#ifndef STURM_TRANSPILE_MATCHER_QINT_ALIAS_SUBST_HPP
#define STURM_TRANSPILE_MATCHER_QINT_ALIAS_SUBST_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceLocation.h"

#include <vector>

namespace clang {
class CXXFunctionalCastExpr;
class FieldDecl;
class FunctionDecl;
class ParmVarDecl;
class VarDecl;
} // namespace clang

namespace sturm::transpile {

/// Discriminated kind for one matched alias-spelling site. The C2
/// emitter dispatches on this enum to pick the rewrite shape (the
/// VarDecl arm consults `infer_width()` for rule 2; the others fall
/// through to `kDefaultWidth = 32` per PRD §3 non-goal).
enum class QintAliasSubstKind {
    VarDecl,        // `sturm::frontend::qint x;` — bound name "vd"
    ParmVarDecl,    // `void f(sturm::frontend::qint p)` — bound name "pmd"
    FieldDecl,      // `struct S { sturm::frontend::qint f; };` — "fd"
    FunctionDecl,   // `sturm::frontend::qint f();` (return type) — "fn"
    FunctionalCast, // `sturm::frontend::qint(0)` — bound name "cast"
};

/// One matched alias-spelling site. Non-owning pointers reference AST
/// nodes valid only for the MatchFinder's ASTContext lifetime. Shape
/// mirrors `QramSubscriptHit` in `matcher_qram_subscript.hpp` — flat
/// struct, no variant — so the C2 emitter can drain matches with a
/// single switch on `kind`.
struct QintAliasSubstMatch {
    /// Which anchor fired. Used by the C2 emitter to dispatch on
    /// rewrite shape.
    QintAliasSubstKind kind = QintAliasSubstKind::VarDecl;

    /// The TypeLoc source range to rewrite. Spans the type-spelling
    /// only (e.g. `sturm::frontend::qint` from `sturm::frontend::qint
    /// x;` — NOT including the variable name, the `=`, nor the
    /// trailing `;`). Captured at match time so the emitter can rewrite
    /// in a single pass without re-walking the AST. Always valid on a
    /// successful match (an invalid range would mean the AST node
    /// lacked TypeSourceInfo, which the matcher gates against — see
    /// `gate_match_has_typeloc` in the .cpp).
    clang::SourceRange type_range{};

    /// Source-file pointer for the bound AST node. Exactly one of the
    /// five pointers below is non-null per match; the rest are null.
    /// The C2 emitter narrows on `kind` and then reads the
    /// corresponding pointer.
    const clang::VarDecl*               vd   = nullptr;
    const clang::ParmVarDecl*           pmd  = nullptr;
    const clang::FieldDecl*             fd   = nullptr;
    const clang::FunctionDecl*          fn   = nullptr;
    const clang::CXXFunctionalCastExpr* cast = nullptr;
};

/// Register the C1 alias-substitution matcher against `finder`,
/// directing every matched site into `matches`. One callback fires
/// per anchor row of the §4.3 match-anchors table.
///
/// `matches` must outlive the finder's run. Call at most once per
/// matches vector — the per-callback unique_ptr pool is owned via a
/// function-local static so the finder's raw-pointer storage stays
/// valid for the whole run (mirrors `register_qram_subscript_matcher`
/// in `matcher_qram_subscript.hpp`).
void register_qint_alias_subst_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QintAliasSubstMatch>& matches);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_QINT_ALIAS_SUBST_HPP
