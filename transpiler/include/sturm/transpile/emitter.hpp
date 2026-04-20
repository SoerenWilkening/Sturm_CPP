// emitter.hpp — M9 C++ emitter (clang::Rewriter based).
//
// Purpose
// -------
// The emitter closes the transpiler pipeline:
//
//   M7 matcher  →  M8 uncompute_pass  →  M9 emitter  →  disk
//
// Given the SourceManager + Rewriter bound to the user's source file and
// the flat list of UncomputeInsertion records produced by M8, the emitter
//
//   1. Calls clang::Rewriter::InsertTextBefore at each insertion's anchor
//      SourceLocation (the originating scope's `close_brace`), dropping
//      the exact source text M8 prepared.
//   2. Serializes the rewritten buffer into a std::string.
//   3. Prepends the idempotency header (from M5's skip.hpp) so the
//      transpiler can recognize its own output on a second run.
//   4. Writes the final bytes to `<output_dir>/<relpath-of-source>` via
//      the M4 io.cpp helpers (write_file / resolve_output_path).
//
// Contract
// --------
// - The Rewriter's lifetime must be at least the duration of this call.
//   The emitter does not take ownership, does not reset the Rewriter, and
//   leaves the in-memory edit state in place so callers can inspect it.
// - Insertions are applied in the order they appear in `insertions`. M8's
//   reverse-iteration (LIFO) ordering is preserved here — Rewriter merges
//   multiple insertions at the same location in call order, so the last
//   call's text ends up closest to the anchor location. This is exactly
//   what the uncompute pass wants: the most recently computed
//   intermediate's uncompute call lands first, closest to `}`.
// - `source_path` is embedded in the idempotency header verbatim. Callers
//   pass whatever path they want the header to carry (typically the
//   user-supplied input path, relative or absolute, however it appeared
//   on the command line). No normalization happens here.
// - On I/O failure (`write_file` returns false) the emitter returns
//   false. A best-effort diagnostic is written to stderr.
//
// LOC budget: this header stays small; the .cpp carries the implementation.

#ifndef STURM_TRANSPILE_EMITTER_HPP
#define STURM_TRANSPILE_EMITTER_HPP

#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

/// PM1-2 — pure rewrite step.
///
/// Apply the synthesized uncompute insertions and VarDecl replacements for
/// `unit` to a fresh `clang::Rewriter` bound to `ctx`'s SourceManager, then
/// serialize the main-file rewrite buffer into a std::string and return it.
///
/// The returned string is EXACTLY the rewritten main-file contents — no
/// idempotency header is prepended, nothing is written to disk. This is the
/// entry point the PM1-4 in-memory Clang plugin will call: the plugin hands
/// the returned buffer to a nested CompilerInvocation which compiles it as
/// if it were the original translation unit, so any sentinel header would
/// be a syntax error (a comment is fine, but prepending it here would make
/// the emit_to_file path double-prepend; we avoid both problems by keeping
/// the header strictly inside emit_to_file).
///
/// Internally this calls `synthesize(unit)` to build the insertion and
/// replacement lists (the same call the standalone driver makes), applies
/// the replacements first and the insertions second (same order as emit()),
/// and serializes the resulting buffer via `raw_string_ostream`.
///
/// If the Rewriter recorded no edits (empty insertions AND empty
/// replacements, or every edit had an invalid range/location) the returned
/// string is a verbatim copy of the main file's original buffer, so
/// callers can unconditionally treat the return value as "the TU source
/// to feed downstream" without branching on whether a rewrite occurred.
std::string emit_to_string(const QUnit& unit, clang::ASTContext& ctx);

/// PM1-2 — file emission step.
///
/// Call `emit_to_string(unit, ctx)`, prepend `idempotency_header(
/// source_path)` to the returned buffer, then write the combined bytes to
/// `resolve_output_path(source_path, output_dir)` via `write_file`.
///
/// Returns true on success, false on any I/O failure (best-effort
/// diagnostic written to stderr). Header-prepend logic lives here and
/// nowhere else: the plugin (PM1-4) uses `emit_to_string` directly.
bool emit_to_file(const QUnit& unit,
                  clang::ASTContext& ctx,
                  std::string_view source_path,
                  std::string_view output_dir);

/// Full MVP emit step.
///
/// Inputs:
///   - `sm`            : the SourceManager bound to the parsed user source.
///   - `rw`            : a Rewriter constructed against `sm`.
///   - `insertions`    : M8 output, in LIFO-within-scope order.
///   - `replacements`  : M8 output, zero or more source-range text
///                       replacements to apply before the insertion pass.
///                       (PE-2: pre-Phase-E this is always empty; Phase E
///                       compound matcher is the first producer.)
///   - `source_path`   : path to embed in the idempotency header.
///   - `output_dir`    : directory under which the output is placed; the
///                       relative path of `source_path` is mirrored under
///                       it (via `resolve_output_path` from M4).
///
/// Behavior:
///   1. Apply every `rw.ReplaceText(rep.range, rep.replacement)` in
///      the order supplied. Replacements and insertion anchors are
///      disjoint by construction so no overlap handling is required.
///   2. Apply every `rw.InsertTextBefore(ins.insert_before, ins.code)`.
///   3. Fetch the rewrite buffer for the main file; serialize to string.
///   4. Prepend `idempotency_header(source_path)`.
///   5. Write the result to `resolve_output_path(source_path, output_dir)`.
///
/// Returns true on success, false on any I/O or Rewriter failure.
bool emit(const clang::SourceManager& sm,
          clang::Rewriter& rw,
          const std::vector<UncomputeInsertion>& insertions,
          const std::vector<QReplacement>& replacements,
          std::string_view source_path,
          std::string_view output_dir);

/// Back-compat overload used by call sites that have no replacements. PE-2
/// keeps the MVP-era two-vector shape (`insertions` only) working so the
/// snapshot fixtures and existing tests stay byte-identical without
/// churning every caller. New callers (Phase E and later) should use the
/// five-argument form above.
bool emit(const clang::SourceManager& sm,
          clang::Rewriter& rw,
          const std::vector<UncomputeInsertion>& insertions,
          std::string_view source_path,
          std::string_view output_dir);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_EMITTER_HPP
