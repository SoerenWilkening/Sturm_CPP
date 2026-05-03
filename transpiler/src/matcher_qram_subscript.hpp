// matcher_qram_subscript.hpp — sturm-u9ge.12 (Beat C1) v1 matcher for the
// QRAM-via-array-subscript epic.
//
// Plan §6 / Beat C1; PRD §7 / M3. Recognises EXACTLY the source shape
//
//     qint b = a[i];
//
// where:
//   - `b` is a freshly declared frontend `qint` (a `VarDecl` with the
//     subscript as its initializer; existing-target `b = a[i];` is the
//     E1 OOS shape `qram-oos-existing-target` and MUST NOT produce a
//     hit here).
//   - `i` is a frontend `qint`. The discriminator (PRD §7) is the
//     `ImplicitCastExpr` of `CK_UserDefinedConversion` whose conversion
//     function is `sturm::frontend::qint::operator size_t()`. This is
//     the strong, unambiguous signal — the only surface type in the v1
//     alias model that carries an implicit `qint -> size_t` conversion.
//   - `a` is one of three container shapes (PRD §7):
//       (1) `std::array<qint_t<W>, N>`  → AST: CXXOperatorCallExpr on
//           `array::operator[]`. Surfaces `QramContainerKind::StdArray`.
//       (2) `qint_t<W>[N]`              → AST: ArraySubscriptExpr (built-in
//           subscript). Surfaces `QramContainerKind::CArray`. Distinguished
//           from the pointer arm by the base-expression type being a true
//           array (`isArrayType()`), not a pointer.
//       (3) `qint_t<W>*`                → AST: ArraySubscriptExpr (built-in
//           subscript). Surfaces `QramContainerKind::Pointer`.
//
// Out-of-scope shapes (PRD §9, deliberate non-goals for v1) MUST NOT
// produce a hit here. These are E1's responsibility:
//   - `b = a[i];`        — `qram-oos-existing-target`
//   - `a[i] = b;`        — `qram-oos-write`
//   - `a[i] += b;` …     — `qram-oos-rmw`
//   - `c = a[i] + d;`    — `qram-oos-expression-position`
//
// Coexistence with E1 (matcher_qram_oos.{hpp,cpp}): C1 anchors on a
// VarDecl whose initializer IS the subscript shape; E1's four shapes
// are by construction structurally disjoint from that anchor (they
// either lack a VarDecl, put the subscript in a non-init position, or
// wrap the subscript inside a binary operator). A TU containing both an
// in-scope read and an out-of-scope shape produces exactly one C1 hit
// + one E1 diagnostic.
//
// Per-hit width (`W`) is populated via Beat B1's `infer_width()` (the
// RHS-driven rule fires on the matched container, returning the
// `qint_t<W>` element width). PRD §11.3 / D0c.3 mandates that C1 share
// the width via `QramSubscriptHit::W` rather than re-deriving it — two
// paths to a width decision is an explicitly forbidden second source of
// truth.
//
// D0b note: PRD §11.2 (QROM vs quantum-register semantics for `a`) is
// still OPEN at the time of this beat. The matcher therefore records
// only the container's *kind* (StdArray / CArray / Pointer) and lets
// downstream code (D2 emitter) dispatch on the resolved D0b decision —
// branching on QROM-vs-quantum-register at the matcher layer would
// either pre-commit to a decision or duplicate the logic across beats.
//
// LoC budget: <= 300 (plan §1, §6 / C1).

#ifndef STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_HPP
#define STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <string>
#include <vector>

namespace clang {
class Expr;
class VarDecl;
} // namespace clang

namespace sturm::transpile {

/// Discriminated container kind for a matched subscript site (PRD §7).
/// Mirrors the three rows of the v1 container shape table; D2 (the
/// emitter) dispatches on this enum to pick the right `QRAM_read`
/// overload (D1's runtime header — see `sturm-u9ge.13` / D0a).
///
/// The kind is determined at match time from the AST node class plus
/// the base-expression type:
///   - `CXXOperatorCallExpr` on `operator[]` → `StdArray` (the only
///     v1-supported class type with an `operator[]` overload).
///   - `ArraySubscriptExpr` whose base is `T[N]` (true array type) →
///     `CArray`.
///   - `ArraySubscriptExpr` whose base is `T*` (pointer type) →
///     `Pointer`.
///
/// PRD §11.2 (D0b) is still OPEN at the time of this beat; the kind
/// records only the syntactic container shape, NOT the QROM-vs-quantum
/// register semantics. D2 will dispatch on the resolved D0b decision
/// using `kind` as the per-row index.
enum class QramContainerKind {
    StdArray,
    CArray,
    Pointer,
};

/// One matched QRAM-subscript site. Non-owning pointers reference AST
/// nodes valid only for the MatchFinder's ASTContext lifetime. Field
/// shape mirrors `ModularOpHit` (per the bd issue's "mirrors
/// matcher_modular_op.cpp style" directive) — flat struct, no variant,
/// no optional — so the D2 emitter can drain hits with a single switch
/// on `kind`.
struct QramSubscriptHit {
    /// Which container shape fired (PRD §7). Used by the D2 emitter to
    /// pick the right `QRAM_read` overload (D0a).
    QramContainerKind kind = QramContainerKind::StdArray;

    /// VarDecl for the LHS `b` of `qint b = a[i];`. The D2 emitter
    /// rewrites the entire VarDecl source range to a two-line sequence
    /// `qint_t<W> b; QRAM_read(a, i, b);` (PRD §8). Null on a
    /// dropped / unsuccessfully matched hit (the matcher never
    /// publishes such hits to the vector).
    const clang::VarDecl* target_var = nullptr;

    /// Container expression `a` from `a[i]`. For `StdArray` this is
    /// `arg(0)` of the `CXXOperatorCallExpr`; for the C-array and
    /// pointer arms this is the base of the `ArraySubscriptExpr`. The
    /// D2 emitter uses this expression's source range to render the
    /// container argument verbatim (e.g. preserving the user's spelling
    /// of a member access like `obj.tbl[i]`).
    const clang::Expr* container_expr = nullptr;

    /// Index expression `i` from `a[i]`. Carries the
    /// `UserDefinedConversion` ImplicitCastExpr the matcher pivots on;
    /// the D2 emitter strips that cast and emits the bare `qint`
    /// expression as the runtime call's index argument.
    const clang::Expr* index_expr = nullptr;

    /// Inferred width `W` for the substituted `qint_t<W>` of the
    /// target. Populated via B1's `infer_width()` running rule 2
    /// (RHS-driven from the container's element type). When rule 2
    /// fails (ambiguity, non-qint_t element) the field falls back to
    /// rule 3 (`kDefaultWidth = 32`) per PRD §11.3 / D0c.1. Always
    /// positive — never zero — by B1's contract.
    unsigned W = 0;

    /// Length expression `n` source text for the pointer arm (PRD
    /// §11.1.6 — D2's gating extension). Populated only when
    /// `kind == Pointer` and a recoverable length source exists in
    /// the container's enclosing scope; the heuristic matches the
    /// container `ParmVarDecl` to a sibling integral-typed
    /// `ParmVarDecl` immediately following it and records that
    /// parameter's identifier. Empty for `StdArray` / `CArray`
    /// (length encoded in the type) and for `Pointer` hits whose
    /// length the matcher could not recover — the emitter handles
    /// the latter by emitting a `qram-pointer-length-missing`
    /// placeholder per §11.1.6. Stored as a string (rather than an
    /// `Expr*`) so the emitter does not need to keep the matcher's
    /// ASTContext alive across drains.
    std::string length_text;
};

/// Register the v1 QRAM-subscript matcher against `finder`, directing
/// every matched site into `hits`. One callback fires per row of the
/// PRD §7 container shape table; the bound names + kind discriminant
/// let the consumer drain in `transpile_consumer.cpp` dispatch with
/// O(1) cost per hit.
///
/// `hits` must outlive the finder's run. Call at most once per hits
/// vector — the per-callback unique_ptr pool is owned via a function-
/// local static so the finder's raw-pointer storage stays valid for
/// the whole run.
///
/// PRD §11.3 / D0c.3 contract: B1's `infer_width()` is invoked inside
/// the callback to populate `QramSubscriptHit::W`, sharing the same
/// AST node C1 inspects. Two paths to a width decision would be a
/// second source of truth and is explicitly forbidden.
void register_qram_subscript_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QramSubscriptHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_HPP
