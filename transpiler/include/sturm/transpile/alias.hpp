// alias.hpp — PM5-1 alias-analysis API (QubitFootprint + footprint + may_overlap).
//
// Internal matcher helper header. Lives under
// `transpiler/include/sturm/transpile/` because the peephole reorder matcher
// (`matcher_peephole_reorder.cpp`, PM5-5) is in a separate TU from the
// alias-extractor implementation (`alias.cpp`, PM5-2 / PM5-3) and the two
// share this tiny surface. The `sturm::transpile::detail` namespace wrap
// matches the `matcher_common.hpp` convention — this is NOT part of the
// public plugin ABI. Plugin authors have no business calling `footprint()`:
// the Registry does not ship a footprint-hook in v1 (see
// `docs/implementation_plan_transpiler_phase_m_pm5.md` §12 Sharp edge 3).
// The `detail` namespace signals "internal to the matcher-ordering machinery,
// not user-facing."
//
// Contents
// --------
// - `struct QubitFootprint` : a bit-range footprint on a named qubit
//   container (`qbool` or `qint_t<W>`). A zero-initialized
//   `QubitFootprint{}` — empty name + invalid decl_loc + lo == hi == 0 —
//   is the "universal sentinel" that `may_overlap()` returns true against
//   every other footprint (including other universal sentinels). Callers
//   emit this sentinel whenever extraction fails (dependent type,
//   plugin-op operand, unresolvable expression shape).
// - `QubitFootprint footprint(const QValueRef&, const clang::ASTContext&)`
//   : peel the underlying expression and produce a `QubitFootprint`.
//   Returns the universal sentinel on any extraction failure; see the
//   plan's §2 / §4 for the exact fallback ladder.
// - `bool may_overlap(const QubitFootprint&, const QubitFootprint&)`
//   : conservative disjointness test. Returns false IFF the two
//   footprints provably refer to disjoint bits (different names, or
//   same name + same decl_loc with disjoint `[lo, hi)` ranges). Returns
//   true in all other cases (including both universal sentinels).
//
// Header-only declarations; implementations land in `transpiler/src/alias.cpp`
// (PM5-2 / PM5-3). This file has no state, no class, no inline bodies — the
// two free functions are the entire public surface of the alias-analysis
// subsystem. See the plan's §3 for the design rationale behind
// "free-function + namespace, not class" and "return by value, not output
// parameter."

#ifndef STURM_TRANSPILE_ALIAS_HPP
#define STURM_TRANSPILE_ALIAS_HPP

#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceLocation.h"

#include <string>

namespace sturm::transpile::detail {

/// A bit-range footprint on a named qubit container.
///
/// `name` + `decl_loc` together identify the qubit container (a `qbool` or
/// a `qint_t<W>`) this footprint points at. `bit_range` is the half-open
/// interval `[lo, hi)` of bits within that container covered by this
/// footprint.
///
/// Universal sentinel. A zero-initialized `QubitFootprint{}` (empty `name`,
/// invalid `decl_loc`, `bit_range.lo == bit_range.hi == 0`) is the
/// "universal" sentinel. `may_overlap()` returns true when either operand
/// is the universal sentinel, including the case where both are universal.
/// Callers emit this sentinel whenever extraction fails — dependent types,
/// `QOpKind::PLUGIN` operands, and unresolvable expression shapes all
/// collapse to this value so the peephole reorder matcher conservatively
/// refuses to commute through them.
///
/// Range semantics are half-open: `bit_range = {0, W}` covers bits
/// `0, 1, ..., W-1` and excludes bit `W`. Two ranges `a` and `b` are
/// disjoint iff `a.hi <= b.lo OR b.hi <= a.lo` — see `may_overlap()` for
/// the full short-circuit ladder.
struct QubitFootprint {
    /// Spelled name of the qubit container's VarDecl (e.g. "a", "tmp").
    /// Empty string IFF this footprint is the universal sentinel.
    std::string name;

    /// Source location of the qubit container's VarDecl, mirroring the
    /// `QValueRef::decl_loc` convention so two `QubitFootprint`s that
    /// refer to the same decl compare equal on both `name` and
    /// `decl_loc`. Invalid IFF this footprint is the universal sentinel.
    clang::SourceLocation decl_loc;

    /// Half-open bit range `[lo, hi)` within the qubit container named by
    /// `{name, decl_loc}`. For a bare `qbool` operand this is `{0, 1}`;
    /// for a bare `qint_t<W>` operand this is `{0, W}`; for a `q[k]`
    /// BitProxy with a compile-time integer `k < W` this is `{k, k+1}`.
    /// `lo == hi == 0` IFF this footprint is the universal sentinel.
    struct BitRange {
        int lo;
        int hi;
    } bit_range{0, 0};
};

/// Extract a `QubitFootprint` from a `QValueRef` by peeling the underlying
/// expression via `ctx`. Returns a universal sentinel on any extraction
/// failure.
///
/// Extraction ladder (see the plan's §2 / §4 for the full specification):
///   1. Bare `qbool` DeclRefExpr  → `{name, decl_loc, {0, 1}}`.
///   2. Bare `qint_t<W>` DRE     → `{name, decl_loc, {0, W}}` (width
///      extracted from the `ClassTemplateSpecializationDecl`'s template
///      args).
///   3. `q[k]` BitProxy with a compile-time integer `k` in `[0, W)` →
///      `{name, decl_loc, {k, k+1}}`.
///   4. `q[k]` with non-constant `k` (or `k` outside `[0, W)`) → fall
///      back to case 2's full-width footprint.
///   5. Dependent type, unrecognised shape, plugin-op operand → universal
///      sentinel.
///
/// Cost: O(1) after the Clang AST peels are cached by `ctx`. Never
/// mutates `op` or `ctx`.
QubitFootprint footprint(const QValueRef& op, const clang::ASTContext& ctx);

/// Return true IFF `a` and `b` MAY refer to overlapping bits.
///
/// Disjointness criteria (short-circuited in order):
///   1. Either footprint is the universal sentinel (empty `name`) →
///      return true. The universal sentinel is the "any-decl, any-bits"
///      wildcard; it overlaps every other footprint including other
///      universal sentinels.
///   2. Different `name` or different `decl_loc` → return false. Two
///      distinct VarDecls cannot alias in single-TU mode; the runtime
///      qubit-index pool never hands the same index to two live
///      declarations (B7, unchanged by PM5).
///   3. Same `name` AND same `decl_loc` → compare bit ranges. Returns
///      false IFF the ranges are disjoint (`a.hi <= b.lo` OR
///      `b.hi <= a.lo`); returns true otherwise (any overlap, including
///      identical ranges).
///
/// Cost: O(1) — three branches and two integer comparisons at most.
/// Never mutates either operand; both are passed by const reference so
/// the peephole reorder matcher can compare the same footprint against
/// many candidate operands without re-extracting.
bool may_overlap(const QubitFootprint& a, const QubitFootprint& b);

} // namespace sturm::transpile::detail

#endif // STURM_TRANSPILE_ALIAS_HPP
