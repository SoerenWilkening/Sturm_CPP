// matcher.hpp — Clang AST matcher for the MVP uncompute pattern (M7).
//
// Purpose
// -------
// This module registers a single Clang ASTMatcher whose job is to identify
// every MVP "qbool tmp = a | b;" assignment in a translation unit and record
// it as a QOperation in a caller-supplied QUnit.
//
// The matcher is strictly scoped to the MVP:
//
//   - The declared variable must be a `qbool` (matched by class name only;
//     this covers both the `sturm::qbool` class and its typedef form).
//   - The initializer must be `operator|` with exactly two arguments.
//   - Both arguments must be plain DeclRefExprs (references to named qbool
//     parameters / locals in the enclosing scope).
//
// Anything broader — `a & b`, `qint tmp = ...`, `foo(a, b)`, compound
// initializers, template-dependent contexts — deliberately does NOT match.
// Widening the matcher is a Phase A/B roadmap deliverable, not this module.
//
// On match the callback synthesizes one `QOperation{kind=OR, result, operands,
// stmt_range}` and appends it to the QScope that corresponds to the matched
// VarDecl's enclosing CompoundStmt. Sibling compound statements become sibling
// QScopes; each QScope is created lazily the first time the matcher fires
// inside it, and subsequent matches in the same block reuse it.
//
// Contract
// --------
//   - The caller owns the QUnit and keeps it alive for the duration of
//     the MatchFinder's run. The matcher holds a raw pointer; no copy.
//   - `register_or_matcher` must be called exactly once per QUnit. Calling
//     it twice with the same QUnit would double-count matches.
//
// Dependencies: Clang ASTMatchers only. The header drags in LibTooling's
// matcher-finder types because the caller needs to instantiate a MatchFinder
// before registering. We keep the surface tiny so downstream modules do not
// transitively pick up the full Clang AST library through this header.

#ifndef STURM_TRANSPILE_MATCHER_HPP
#define STURM_TRANSPILE_MATCHER_HPP

#include "sturm/transpile/qir.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"

namespace sturm::transpile {

/// Register the MVP `qbool tmp = a | b;` matcher against `finder`, directing
/// every match into `unit`. `unit` must outlive the MatchFinder's run. Call
/// at most once per QUnit.
void register_or_matcher(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_HPP
